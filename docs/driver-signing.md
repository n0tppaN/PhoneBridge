# Windows Virtual Camera & Virtual Audio Driver Architecture and Signing Guide

This document specifies the technical architecture and driver signing requirements for PhoneBridge virtual devices on Windows 11.

---

## 1. Virtual Camera Architecture (`MFCreateVirtualCamera`)

Windows 11 supports native user-mode Media Foundation Virtual Cameras without requiring kernel-mode video drivers.

### API Reference
- **API:** `MFCreateVirtualCamera` / `IMFVirtualCamera`
- **Lifetime:** Managed by the Windows Background Service (`phonebridge_service.exe`).
- **Device Name:** `PhoneBridge Camera`

### Fallback Behavior on USB Disconnect
When the Android phone disconnects:
1. `PhoneBridge Camera` remains registered in the Windows system device tree.
2. The virtual camera output pipe streams black fallback frames (YUV420/NV12) at 1920x1080 @ 30 FPS.
3. Windows applications (Zoom, Discord, OBS, Teams) do not lose their camera handle or crash.
4. Upon reconnection, the C++ transport manager sends `REQUEST_KEYFRAME` (0x0300) to Android, receives the new H.264 IDR frame, and seamlessly resumes video feed.

---

## 2. Virtual Audio Driver Architecture (WDM / SYSVAD WaveRT)

Windows requires a selectable audio recording endpoint to expose `PhoneBridge Microphone` to WASAPI and DirectSound applications.

### Driver Architecture
- **Base Architecture:** Microsoft SYSVAD WaveRT reference audio driver.
- **Kernel/User Boundary:** Shared memory ring buffer between user-mode `phonebridge_service.exe` and kernel-mode `phonebridge_audio.sys`.
- **Fallback Behavior:** On USB disconnection, the driver feeds digital silence (0x0000 PCM 48kHz 16-bit mono samples) into the ring buffer.

---

## 3. Driver Signing & Distribution Requirements

### Development Environment (Test Signing)
1. Enable Windows Test Signing mode:
   ```cmd
   bcdedit /set testsigning on
   ```
2. Create and sign with a local self-signed test certificate (`MakeCert` / `SignTool`).

### Production Environment (WHQL & Microsoft Partner Center)
1. **Extended Validation (EV) Code Signing Certificate:** Required to sign in to the Microsoft Hardware Developer Program.
2. **Microsoft Hardware Lab Kit (HLK) / WHQL:**
   - Execute HLK test suites for audio drivers.
   - Submit HLK log package to Microsoft Partner Center portal.
3. **Attestation Signing / Windows Hardware Compatibility Program:**
   - Microsoft digitally signs the driver package (.sys, .inf, .cat).
   - Enables seamless installation on consumer Windows 11 machines without security warnings.
