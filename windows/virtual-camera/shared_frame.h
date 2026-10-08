// Shared-memory frame channel:   service (writer)  ->  Frame Server media source (reader)
//
// Why: the media source DLL runs inside the Windows Frame Server process, NOT in
// phonebridge_service.exe, so it cannot touch the service's variables. NV12 frames
// travel through this named file mapping instead.
//
// Layout: [Header 4096 B][Slot0][Slot1][Slot2]; each slot = 64 B slot header + NV12 1920x1080.
// Lock-free: the writer fills the *next* slot (slot seq odd while writing, even when done),
// then publishes it in Header::latestSlot. The reader copies the latest slot and re-checks seq.
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace phonebridge::shared {

inline constexpr wchar_t kMappingName[] = L"Global\\PhoneBridgeCameraFrame";
inline constexpr uint32_t kMagic = 0x4D465250u;
inline constexpr uint32_t kVersion = 1;
inline constexpr uint32_t kWidth = 1920;
inline constexpr uint32_t kHeight = 1080;
inline constexpr uint32_t kFrameBytes = kWidth * kHeight * 3 / 2;  // NV12
inline constexpr uint32_t kSlotCount = 3;
inline constexpr uint32_t kHeaderBytes = 4096;
inline constexpr uint32_t kSlotHeaderBytes = 64;
inline constexpr uint32_t kSlotBytes = kSlotHeaderBytes + kFrameBytes;
inline constexpr size_t kTotalBytes = kHeaderBytes + static_cast<size_t>(kSlotCount) * kSlotBytes;
inline constexpr ULONGLONG kStaleAfterMs = 1000;  // no new frame for 1 s => "no signal"

struct Header {
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    volatile LONG latestSlot;  // -1 = nothing yet
    uint32_t reserved0;
    volatile LONG64 frameCounter;
    volatile LONG64 lastWriteTickMs;  // GetTickCount64() of the last published frame
};

struct SlotHeader {
    volatile LONG64 seq;  // odd = being written
    int64_t timestampUs;
};

inline Header* headerOf(void* base) { return static_cast<Header*>(base); }
inline SlotHeader* slotHeaderOf(void* base, uint32_t i) {
    return reinterpret_cast<SlotHeader*>(static_cast<uint8_t*>(base) + kHeaderBytes + size_t(i) * kSlotBytes);
}
inline uint8_t* slotDataOf(void* base, uint32_t i) {
    return static_cast<uint8_t*>(base) + kHeaderBytes + size_t(i) * kSlotBytes + kSlotHeaderBytes;
}

// Nearest-neighbour NV12 scaler (used when the phone sends something other than 1920x1080).
inline void scaleNv12(const uint8_t* src, uint32_t sw, uint32_t sh, uint8_t* dst, uint32_t dw, uint32_t dh) {
    for (uint32_t y = 0; y < dh; ++y) {
        const uint8_t* srow = src + size_t(y * sh / dh) * sw;
        uint8_t* drow = dst + size_t(y) * dw;
        for (uint32_t x = 0; x < dw; ++x) drow[x] = srow[size_t(x) * sw / dw];
    }
    const uint8_t* suv = src + size_t(sw) * sh;
    uint8_t* duv = dst + size_t(dw) * dh;
    const uint32_t shh = sh / 2, dhh = dh / 2, sww = sw / 2, dww = dw / 2;
    for (uint32_t y = 0; y < dhh; ++y) {
        const uint8_t* srow = suv + size_t(y * shh / dhh) * sw;
        uint8_t* drow = duv + size_t(y) * dw;
        for (uint32_t x = 0; x < dww; ++x) {
            const uint32_t sx = x * sww / dww;
            drow[x * 2] = srow[sx * 2];
            drow[x * 2 + 1] = srow[sx * 2 + 1];
        }
    }
}

// ---------------- Writer (service side, needs admin / SeCreateGlobalPrivilege) ----------------
class Writer {
public:
    ~Writer() { close(); }

    bool open() {
        if (m_view) return true;
        PSECURITY_DESCRIPTOR sd = nullptr;
        // SYSTEM + Administrators: full. LOCAL SERVICE (Frame Server): read only.
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GR;;;LS)", SDDL_REVISION_1, &sd, nullptr)) {
            m_lastError = GetLastError();
            return false;
        }
        SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
        const ULONGLONG total = kTotalBytes;
        m_map = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, static_cast<DWORD>(total >> 32),
                                   static_cast<DWORD>(total & 0xFFFFFFFFu), kMappingName);
        m_lastError = GetLastError();
        LocalFree(sd);
        if (!m_map) return false;
        m_view = MapViewOfFile(m_map, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (!m_view) {
            m_lastError = GetLastError();
            CloseHandle(m_map);
            m_map = nullptr;
            return false;
        }
        Header* h = headerOf(m_view);
        std::memset(m_view, 0, kHeaderBytes);
        h->magic = kMagic;
        h->version = kVersion;
        h->width = kWidth;
        h->height = kHeight;
        h->latestSlot = -1;
        return true;
    }

    // `nv12` must be tightly packed (stride == width). Returns false if not open / bad size.
    bool write(const uint8_t* nv12, uint32_t w, uint32_t h, int64_t timestampUs) {
        if (!m_view || !nv12 || w < 2 || h < 2 || (w & 1) || (h & 1)) return false;
        const uint8_t* src = nv12;
        if (w != kWidth || h != kHeight) {
            m_scaled.resize(kFrameBytes);
            scaleNv12(nv12, w, h, m_scaled.data(), kWidth, kHeight);
            src = m_scaled.data();
        }
        Header* hdr = headerOf(m_view);
        const LONG cur = hdr->latestSlot;
        const uint32_t next = static_cast<uint32_t>((cur + 1 + static_cast<LONG>(kSlotCount)) % static_cast<LONG>(kSlotCount));
        SlotHeader* sh = slotHeaderOf(m_view, next);

        const LONG64 s = sh->seq;  // always even here
        InterlockedExchange64(&sh->seq, s + 1);  // odd: writing
        MemoryBarrier();
        std::memcpy(slotDataOf(m_view, next), src, kFrameBytes);
        sh->timestampUs = timestampUs;
        MemoryBarrier();
        InterlockedExchange64(&sh->seq, s + 2);  // even: complete
        InterlockedExchange(&hdr->latestSlot, static_cast<LONG>(next));
        InterlockedIncrement64(&hdr->frameCounter);
        InterlockedExchange64(&hdr->lastWriteTickMs, static_cast<LONG64>(GetTickCount64()));
        return true;
    }

    void close() {
        if (m_view) { UnmapViewOfFile(m_view); m_view = nullptr; }
        if (m_map) { CloseHandle(m_map); m_map = nullptr; }
    }
    DWORD lastError() const { return m_lastError; }
    bool isOpen() const { return m_view != nullptr; }

private:
    HANDLE m_map = nullptr;
    void* m_view = nullptr;
    DWORD m_lastError = 0;
    std::vector<uint8_t> m_scaled;
};

// ---------------- Reader (media source inside Frame Server; read-only, no Interlocked) ----------------
class Reader {
public:
    ~Reader() { close(); }

    // Copies the newest complete frame (kFrameBytes) into dst. false => no valid/fresh frame.
    bool readLatest(uint8_t* dst, int64_t* timestampUs = nullptr) {
        if (!ensureOpen()) return false;
        const Header* hdr = headerOf(m_view);
        if (hdr->magic != kMagic || hdr->version != kVersion) return false;
        const ULONGLONG last = static_cast<ULONGLONG>(hdr->lastWriteTickMs);
        if (last == 0 || GetTickCount64() - last > kStaleAfterMs) return false;  // writer stopped

        for (int attempt = 0; attempt < 3; ++attempt) {
            const LONG slot = hdr->latestSlot;
            if (slot < 0 || slot >= static_cast<LONG>(kSlotCount)) return false;
            SlotHeader* sh = slotHeaderOf(m_view, static_cast<uint32_t>(slot));
            const LONG64 s1 = sh->seq;
            if (s1 & 1) continue;  // being written right now
            std::memcpy(dst, slotDataOf(m_view, static_cast<uint32_t>(slot)), kFrameBytes);
            if (timestampUs) *timestampUs = sh->timestampUs;
            MemoryBarrier();
            if (sh->seq == s1) return true;  // not overwritten while copying
        }
        return false;
    }

    void close() {
        if (m_view) { UnmapViewOfFile(m_view); m_view = nullptr; }
        if (m_map) { CloseHandle(m_map); m_map = nullptr; }
    }
    DWORD lastOpenError() const { return m_lastError; }

private:
    bool ensureOpen() {
        if (m_view) return true;
        const ULONGLONG now = GetTickCount64();
        if (m_lastTry != 0 && now - m_lastTry < 500) return false;  // don't hammer the kernel
        m_lastTry = now;
        m_map = OpenFileMappingW(FILE_MAP_READ, FALSE, kMappingName);
        if (!m_map) { m_lastError = GetLastError(); return false; }
        m_view = MapViewOfFile(m_map, FILE_MAP_READ, 0, 0, 0);
        if (!m_view) {
            m_lastError = GetLastError();
            CloseHandle(m_map);
            m_map = nullptr;
            return false;
        }
        return true;
    }

    HANDLE m_map = nullptr;
    void* m_view = nullptr;
    ULONGLONG m_lastTry = 0;
    DWORD m_lastError = 0;
};

}  // namespace phonebridge::shared
