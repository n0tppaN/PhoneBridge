# PhoneBridge - Master Technical Reference & Codebase Brief

This document serves as an exhaustive, master-level engineering reference for **PhoneBridge**. It includes system architecture, IPC models, COM Media Source details, and core code snippets for both Android and Windows subsystems. It is designed to provide any advanced AI or developer with 100% of the context required to debug or extend the codebase.

---

## 1. System Architecture & Component Interaction

```text
+------------------------------------+                +--------------------------------------+
| Android App (Kotlin)               |                | Windows PC (C++20)                   |
|                                    |                |                                      |
|  Camera2 -> MediaCodec (H.264)     | -- TCP over -->| AdbTransport (adb forward)           |
|  AudioRecord -> PCM 48kHz          |    ADB socket  | ConnectionManager (Handshake/Frames) |
|  BridgeServer (Socket Server)      |                |   |                                  |
+------------------------------------+                |   +--> VideoReceiver -> H264Decoder  |
                                                      |   +--> AudioReceiver -> ring buffer  |
                                                      |   +--> Shared Memory Writer (Writer) |
                                                      +--------------------------------------+
                                                                         |
                                                            (Named File Mapping IPC)
                                                                         v
                                                      +--------------------------------------+
                                                      | Windows Camera Frame Server (svchost)|
                                                      |                                      |
                                                      |  phonebridge_mediasource.dll (COM)   |
                                                      |  - IMFMediaSourceEx                  |
                                                      |  - IMFMediaStream2 (Reader NV12)     |
                                                      +--------------------------------------+
                                                                         |
                                                            (Media Foundation Pipeline)
                                                                         v
                                                      +--------------------------------------+
                                                      | Windows Camera App / OBS Studio      |
                                                      +--------------------------------------+
```

---

## 2. Core Code Snippets

### A. Android: CameraEncoder (H.264 Hardware Encoding)
*Location: `android/app/src/main/java/com/phonebridge/camera/CameraEncoder.kt`*
```kotlin
class CameraEncoder(private val context: Context, private val onEncodedFrame: (ByteArray, Int, Long) -> Unit) {
    private var mediaCodec: MediaCodec? = null
    private var inputSurface: Surface? = null

    fun start(): Boolean {
        try {
            val format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, 1920, 1080).apply {
                setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
                setInteger(MediaFormat.KEY_BIT_RATE, 8000000) // 8 Mbps
                setInteger(MediaFormat.KEY_FRAME_RATE, 30)
                setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
            }
            mediaCodec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC).apply {
                configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
                inputSurface = createInputSurface()
                start()
            }
            return true
        } catch (e: Exception) {
            return false
        }
    }
}
```

### B. Windows: Shared Memory IPC (`shared_frame.h`)
*Location: `windows/virtual-camera/shared_frame.h`*
```cpp
namespace phonebridge::shared {
inline constexpr wchar_t kMappingName[] = L"Global\\PhoneBridgeCameraFrame";
inline constexpr uint32_t kWidth = 1920;
inline constexpr uint32_t kHeight = 1080;
inline constexpr uint32_t kFrameBytes = kWidth * kHeight * 3 / 2; // NV12
inline constexpr uint32_t kSlotCount = 3;

struct Header {
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    volatile LONG latestSlot;
    volatile LONG64 frameCounter;
    volatile LONG64 lastWriteTickMs;
};

class Writer {
public:
    bool open() {
        PSECURITY_DESCRIPTOR sd = nullptr;
        ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GR;;;LS)", SDDL_REVISION_1, &sd, nullptr);
        SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
        m_map = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, kTotalBytes, kMappingName);
        m_view = MapViewOfFile(m_map, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        return m_view != nullptr;
    }
    bool write(const uint8_t* nv12, uint32_t w, uint32_t h, int64_t timestampUs) {
        // Writes tightly-packed NV12 frame into next lock-free slot
        return true;
    }
};

class Reader {
public:
    bool readLatest(uint8_t* dst, int64_t* timestampUs = nullptr) {
        // Reads latest complete slot from shared memory without blocking
        return true;
    }
};
}
```

### C. Windows: COM DLL Entry Point & Exports (`dllmain.cpp`)
*Location: `windows/virtual-camera/dllmain.cpp`*
```cpp
#include <windows.h>
#include "phonebridge_media_source.h"

static const GUID CLSID_PhoneBridgeMediaSource =
    { 0xe6b65c58, 0x4d2a, 0x4c20, { 0x9f, 0x16, 0x36, 0x87, 0x98, 0x13, 0x5c, 0xc4 } };

class PhoneBridgeClassFactory : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) { ... }
    STDMETHODIMP_(ULONG) AddRef() { return 2; }
    STDMETHODIMP_(ULONG) Release() { return 1; }
    STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObject) {
        auto* source = new phonebridge::PhoneBridgeMediaSource();
        return source->QueryInterface(riid, ppvObject);
    }
    STDMETHODIMP LockServer(BOOL) { return S_OK; }
};

extern "C" __declspec(dllexport) STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (rclsid == CLSID_PhoneBridgeMediaSource) {
        auto* pFactory = new PhoneBridgeClassFactory();
        HRESULT hr = pFactory->QueryInterface(riid, ppv);
        pFactory->Release();
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}
```

---

## 6. Troubleshooting Guide
- **Error `0x80070057` (`E_INVALIDARG`)**: Usually occurs during format negotiation if Media Foundation attributes (stride, framerate, sample size) mismatch or if the shared memory reader (`shared::Reader`) fails to fetch a frame. Ensure test pattern mode or real H.264 decoding is active.
- **Error `0x80004002` (`E_NOINTERFACE`)**: Occurs when the Windows Camera Frame Server (`svchost.exe`) cannot locate the COM In-Process Server registration in HKLM (`SOFTWARE\Classes\CLSID\{...}\InprocServer32`) or when a 32/64-bit architecture mismatch occurs (must be strictly x64).
  **Error `0xA00F429E`<`PageOpenFailed`> (0x80070057)
- **Error `0xA00F429F`<`WindowShowFailed`> (0x80070057)
- **Error `0xA00F4246`<`ErrorRetryFailed`> (0x80070057)
