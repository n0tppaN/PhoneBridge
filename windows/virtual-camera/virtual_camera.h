#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#pragma once
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfvirtualcamera.h>
#include <atomic>
#include <cstdint>
#include <thread>
#include "shared_frame.h"

namespace phonebridge {

// Service-side half of the virtual camera:
//   1. asks Windows to create "Camera (PhoneBridge)"  (MFCreateVirtualCamera)
//   2. publishes NV12 frames to shared memory; the media source DLL inside Frame Server reads them.
// The media source DLL is NOT loaded here - it is registered once by scripts\install_camera.ps1.
class VirtualCameraDevice {
public:
    VirtualCameraDevice();
    ~VirtualCameraDevice();

    bool initialize();
    // NV12, tightly packed (stride == width). Scaled to 1920x1080 if needed.
    bool pushFrame(const uint8_t* nv12Data, uint32_t width, uint32_t height, int64_t timestampUs);
    void enumerateMediaFoundationCameras();
    void shutdown();

private:
    void feederLoop();
    void fillTestPattern(uint8_t* nv12, uint32_t frameIndex) const;

    bool m_initialized{false};
    IMFVirtualCamera* m_virtualCamera{nullptr};
    shared::Writer m_writer;
    std::thread m_feeder;
    std::atomic<bool> m_stop{false};
    bool m_testPattern{false};
    bool m_comInitialized{false};
};

}  // namespace phonebridge
