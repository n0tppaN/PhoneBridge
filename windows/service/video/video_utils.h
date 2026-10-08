// Pure helpers (no Windows dependencies) so they can be unit-tested anywhere.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace phonebridge::video {

// Copies the visible dispW x dispH area of an NV12 surface into a tightly packed buffer
// (stride == dispW). Surface layout: Y plane = codedH rows of `stride` bytes, followed by the
// interleaved UV plane (dispH/2 rows of `stride` bytes). Returns false if the source is too small.
inline bool copyNv12Cropped(const uint8_t* src, size_t srcLen, uint32_t stride, uint32_t codedH,
                            uint32_t dispW, uint32_t dispH, uint8_t* dst) {
    if (!src || !dst || dispW == 0 || dispH == 0 || (dispW & 1) || (dispH & 1)) return false;
    if (stride < dispW || codedH < dispH) return false;
    const size_t yBytes = size_t(stride) * codedH;
    const size_t need = yBytes + size_t(stride) * (dispH / 2 - 1) + dispW;
    if (srcLen < need) return false;

    for (uint32_t y = 0; y < dispH; ++y)
        std::memcpy(dst + size_t(y) * dispW, src + size_t(y) * stride, dispW);
    uint8_t* dstUv = dst + size_t(dispW) * dispH;
    const uint8_t* srcUv = src + yBytes;
    for (uint32_t y = 0; y < dispH / 2; ++y)
        std::memcpy(dstUv + size_t(y) * dispW, srcUv + size_t(y) * stride, dispW);
    return true;
}

// Minimal "key": <integer> lookup for the tiny JSON the phone sends. Returns `def` if absent.
inline int jsonInt(const char* data, size_t size, const char* key, int def) {
    const std::string_view text(data ? data : "", data ? size : 0);
    std::string needle = std::string("\"") + key + "\"";
    const size_t k = text.find(needle);
    if (k == std::string_view::npos) return def;
    size_t i = text.find(':', k + needle.size());
    if (i == std::string_view::npos) return def;
    ++i;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
    bool neg = false;
    if (i < text.size() && text[i] == '-') { neg = true; ++i; }
    if (i >= text.size() || text[i] < '0' || text[i] > '9') return def;
    long v = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9' && v < 100000000) v = v * 10 + (text[i++] - '0');
    return static_cast<int>(neg ? -v : v);
}

}  // namespace phonebridge::video
