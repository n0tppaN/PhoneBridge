# PhoneBridge

PhoneBridge is a high-performance cross-platform system that connects an Android device (streaming camera and microphone) to a Windows PC via ADB, exposing the stream as a standard Windows Virtual Camera (DirectShow / Media Foundation COM In-Process Server) and audio sink.

---

## System Architecture

```text
+------------------------------------+                +--------------------------------------+
| Android App (Kotlin + Compose)     |                | Windows PC (C++20 & .NET WebView2)   |
|                                    |                |                                      |
|  Camera2 -> MediaCodec (H.264)     | -- TCP over -->| AdbTransport (adb forward)           |
|  AudioRecord -> PCM 48kHz          |    ADB socket  | ConnectionManager (Handshake/Frames) |
|  BridgeServer (Socket Server)      |                |   |                                  |
+------------------------------------+                +--------------------------------------+
                                                      |   +--> VideoReceiver -> H264Decoder  |
                                                      |   +--> AudioReceiver -> waveOut/WAV  |
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
                                                      +--------------------------------------+
                                                      | Windows Camera App / OBS Studio / Zoom|
                                                      +--------------------------------------+
```

---

## Project Structure

- **`android/`**: Native Android application built with Jetpack Compose (Material 3), Camera2 H.264 hardware encoding, and AudioRecord PCM streaming.
- **`windows/`**: Windows C++20 backend service (`phonebridge_service.exe`), virtual camera Media Foundation COM DLL (`phonebridge_mediasource.dll`), and WebView2 desktop UI wrapper (`PhoneBridge.exe`).
- **`protocol/`**: Shared binary protocol definitions and packet parsers.
- **`tools/`**: PowerShell control center GUI (`phonebridge_gui.ps1`) and testing scripts.

---

## Getting Started

### 1. Android App
1. Open the `android/`project in Android Studio.
2. Connect your Android device via USB with USB Debugging enabled.
3. Build and run the app. Tap **Start** to begin streaming.

### 2. Windows PC & Control Center
1. Run `adb forward tcp:27183 tcp:27183` to set up port forwarding.
2. Run the PowerShell control center GUI:
   ```powershell
   .\tools\phonebridge_gui.ps1
   ```
3. Or launch the desktop app package in `windows/dist/PhoneBridge/PhoneBridge.exe`.
