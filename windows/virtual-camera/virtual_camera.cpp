#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "virtual_camera.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <mferror.h>
#include "decoded_frame_buffer.h"  // service/video (already in the include path)
#include "pb_log.h"

namespace phonebridge {

namespace {
const wchar_t kClsidString[] = L"{E6B65C58-4D2A-4C20-9F16-368798135CC4}";
const wchar_t kFriendlyName[] = L"C\u00e2mera (PhoneBridge)";
// KSCATEGORY_VIDEO_CAMERA {E5323777-F976-4f5b-9B55-B94699C46E44}
const GUID kCategoryVideoCamera = {0xe5323777, 0xf976, 0x4f5b, {0x9b, 0x55, 0xb9, 0x46, 0x99, 0xc4, 0x6e, 0x44}};

std::string hrText(HRESULT hr) {
    char buf[256] = {};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, static_cast<DWORD>(hr), 0,
                   buf, sizeof(buf), nullptr);
    char out[320];
    std::snprintf(out, sizeof(out), "0x%08lX %s", static_cast<unsigned long>(hr), buf);
    return out;
}

bool envFlag(const wchar_t* name) {
    wchar_t v[8] = {};
    return GetEnvironmentVariableW(name, v, 8) > 0 && v[0] == L'1';
}
}  // namespace

VirtualCameraDevice::VirtualCameraDevice() = default;
VirtualCameraDevice::~VirtualCameraDevice() { shutdown(); }

bool VirtualCameraDevice::initialize() {
    if (m_initialized) return true;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) m_comInitialized = true;
    else if (hr != RPC_E_CHANGED_MODE) { PB_LOG("VCam: CoInitializeEx failed %s", hrText(hr).c_str()); return false; }

    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        PB_LOG("VCam: MFStartup failed %s", hrText(hr).c_str());
        if (m_comInitialized) { CoUninitialize(); m_comInitialized = false; }
        return false;
    }

    // 1) shared memory for frames (needs elevation: Global\ namespace)
    if (!m_writer.open()) {
        PB_LOG("VCam: cannot create shared memory (error %lu). Run as Administrator.",
               static_cast<unsigned long>(m_writer.lastError()));
        std::cerr << "[VirtualCamera] Shared memory failed - run as Administrator.\n";
        MFShutdown();
        if (m_comInitialized) { CoUninitialize(); m_comInitialized = false; }
        return false;
    }
    PB_LOG("VCam: shared memory ready");

    // 2) the virtual camera. Session lifetime = removed automatically when this process exits
    //    (best while debugging). The media source DLL must already be registered (install_camera.ps1).
    GUID categories[] = {kCategoryVideoCamera};
    hr = MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource, MFVirtualCameraLifetime_Session,
                               MFVirtualCameraAccess_CurrentUser, kFriendlyName, kClsidString, categories, 1,
                               &m_virtualCamera);
    PB_LOG("VCam: MFCreateVirtualCamera -> %s", hrText(hr).c_str());
    if (FAILED(hr)) {
        m_writer.close();
        MFShutdown();
        if (m_comInitialized) { CoUninitialize(); m_comInitialized = false; }
        return false;
    }

    hr = m_virtualCamera->Start(nullptr);
    PB_LOG("VCam: IMFVirtualCamera::Start -> %s", hrText(hr).c_str());
    if (FAILED(hr)) {
        std::cerr << "[VirtualCamera] Start failed. Open C:\\ProgramData\\PhoneBridge\\logs\\phonebridge_camera.log\n";
        m_virtualCamera->Release();
        m_virtualCamera = nullptr;
        m_writer.close();
        MFShutdown();
        if (m_comInitialized) { CoUninitialize(); m_comInitialized = false; }
        return false;
    }
    std::cout << "[VirtualCamera] Camera (PhoneBridge) started.\n";

    enumerateMediaFoundationCameras();

    // Keep the synthetic source available for isolating Frame Server issues,
    // but do not hide a broken phone/decoder pipeline during normal use.
    m_testPattern = envFlag(L"PHONEBRIDGE_TEST_PATTERN");
    PB_LOG("VCam: test pattern %s (set PHONEBRIDGE_TEST_PATTERN=1 to enable)",
           m_testPattern ? "enabled" : "disabled");
    m_stop = false;
    m_feeder = std::thread([this] { feederLoop(); });

    m_initialized = true;
    return true;
}

void VirtualCameraDevice::enumerateMediaFoundationCameras() {
    IMFAttributes* attrs = nullptr;
    if (FAILED(MFCreateAttributes(&attrs, 1))) return;
    attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    HRESULT hr = MFEnumDeviceSources(attrs, &devices, &count);
    attrs->Release();
    if (FAILED(hr)) { PB_LOG("VCam: MFEnumDeviceSources failed %s", hrText(hr).c_str()); return; }
    PB_LOG("VCam: %u video capture device(s) visible to Media Foundation", count);
    for (UINT32 i = 0; i < count; ++i) {
        WCHAR name[256] = {};
        UINT32 len = 0;
        devices[i]->GetString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, name, 256, &len);
        char utf8[512] = {};
        WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, sizeof(utf8), nullptr, nullptr);
        PB_LOG("VCam:   device #%u: %s", i, utf8);
        devices[i]->Release();
    }
    CoTaskMemFree(devices);
}

bool VirtualCameraDevice::pushFrame(const uint8_t* nv12Data, uint32_t width, uint32_t height, int64_t timestampUs) {
    if (!m_initialized) return false;
    return m_writer.write(nv12Data, width, height, timestampUs);
}

// Moves real decoded frames (from the H.264 decoder) into shared memory.
// With PHONEBRIDGE_TEST_PATTERN=1 and no real frames, publishes a moving test pattern so the whole
// chain (service -> shared memory -> Frame Server -> Windows Camera) can be verified without a phone.
void VirtualCameraDevice::feederLoop() {
    using clock = std::chrono::steady_clock;
    std::vector<uint8_t> pattern(shared::kFrameBytes);
    auto nextPattern = clock::now();
    uint32_t frameIndex = 0;
    auto lastReal = clock::now() - std::chrono::seconds(10);

    while (!m_stop) {
        video::DecodedFrame frame;
        if (video::getDecodedFrameBuffer().pop(frame)) {
            if (!m_writer.write(frame.data.data(), frame.width, frame.height, frame.timestampUs)) {
                PB_LOG("VCam: failed to publish decoded frame (%ux%u)", frame.width, frame.height);
            } else if ((frameIndex++ % 300) == 0) {
                PB_LOG("VCam: published real decoded frame #%u (%ux%u)", frameIndex, frame.width, frame.height);
            }
            lastReal = clock::now();
            continue;
        }
        const auto now = clock::now();
        if (m_testPattern && now - lastReal > std::chrono::milliseconds(500) && now >= nextPattern) {
            fillTestPattern(pattern.data(), frameIndex++);
            if (!m_writer.write(pattern.data(), shared::kWidth, shared::kHeight,
                                static_cast<int64_t>(frameIndex) * 33333)) {
                PB_LOG("VCam: failed to publish synthetic frame");
            }
            nextPattern = now + std::chrono::microseconds(33333);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
}

// Dark blue background, a bright red bar sweeping left->right. NV12 1920x1080.
void VirtualCameraDevice::fillTestPattern(uint8_t* nv12, uint32_t frameIndex) const {
    const uint32_t w = shared::kWidth, h = shared::kHeight;
    const uint32_t barW = 160, barX = (frameIndex * 16) % (w - barW);
    uint8_t* y = nv12;
    for (uint32_t row = 0; row < h; ++row) {
        std::memset(y + size_t(row) * w, 60, w);
        std::memset(y + size_t(row) * w + barX, 120, barW);
    }
    uint8_t* uv = nv12 + size_t(w) * h;
    for (uint32_t row = 0; row < h / 2; ++row) {
        uint8_t* p = uv + size_t(row) * w;
        for (uint32_t x = 0; x < w / 2; ++x) {
            const bool inBar = (x * 2 >= barX && x * 2 < barX + barW);
            p[x * 2] = inBar ? 90 : 170;       // U
            p[x * 2 + 1] = inBar ? 240 : 110;  // V
        }
    }
}

void VirtualCameraDevice::shutdown() {
    m_stop = true;
    if (m_feeder.joinable()) m_feeder.join();
    if (m_virtualCamera) {
        m_virtualCamera->Stop();
        m_virtualCamera->Shutdown();
        m_virtualCamera->Release();
        m_virtualCamera = nullptr;
    }
    m_writer.close();
    if (m_initialized) {
        MFShutdown();
        m_initialized = false;
        PB_LOG("VCam: shut down");
    }
    if (m_comInitialized) { CoUninitialize(); m_comInitialized = false; }
}

}  // namespace phonebridge
