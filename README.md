# PhoneBridge

Transforma um telemóvel Android numa fonte de **câmara virtual Windows** através de USB/ADB.

> **Estado atual:** a câmara virtual está em fase experimental e foi testada com a Câmara do Windows e aplicações web. O microfone virtual nativo Windows ainda não está incluído: sem um driver de áudio assinado não é possível expor um novo endpoint de microfone ao Discord/Teams apenas com C++ user-mode.

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
                                                      |   +--> AudioReceiver -> ring buffer    |
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

## Estado e limitações

Estão implementados o transporte ADB, handshake, parser incremental, captura Android, decoder H.264, shared memory NV12 e a UI WebView2. O áudio PCM é recebido para buffers e diagnóstico, mas **não é um microfone Windows selecionável**.

O Windows exige um endpoint virtual com driver WaveRT/SYSVAD assinado para que Discord, Teams e browsers o vejam como input. O PhoneBridge não depende de VB-Audio nem tenta usar `waveOut` como microfone. Um WAV de diagnóstico só é criado quando `PHONEBRIDGE_DIAGNOSTIC_WAV=1`.

A câmara usa `MFCreateVirtualCamera` no Windows 11. São anunciados NV12 e YUY2 e existe uma secção de diagnóstico em `docs/diagnostics.md`. A compatibilidade com OBS é uma limitação conhecida: a fonte atual é Media Foundation e o OBS usa normalmente DirectShow. Ver `docs/directshow-compatibility.md` e `tools/check_directshow_camera.ps1`.

## Project Structure

- **`android/`**: Native Android application built with Jetpack Compose (Material 3), Camera2 H.264 hardware encoding, and AudioRecord PCM streaming.
- **`windows/`**: Windows C++20 backend service (`phonebridge_service.exe`), virtual camera Media Foundation COM DLL (`phonebridge_mediasource.dll`), and WebView2 desktop UI wrapper (`PhoneBridge.exe`).
- **`protocol/`**: Shared binary protocol definitions and packet parsers.
- **`tools/`**: PowerShell control center GUI (`phonebridge_gui.ps1`) and testing scripts.

---

## Requisitos

- Windows 11 x64, idealmente build 22000 ou posterior;
- Android 10/API 29 ou posterior;
- Android SDK/ADB e depuração USB autorizada;
- Visual Studio 2022 com Desktop C++, Windows 11 SDK e CMake;
- Android Studio com JDK 17 e SDK 34;
- WebView2 Runtime para a UI.

## Build

### Protocolo e testes portáveis

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

### Windows

```powershell
cmake -S windows -B windows/build -A x64
cmake --build windows/build --config Release
cd ui; npm ci; npm test; npm run build; cd ..
dotnet build windows/app/PhoneBridge/PhoneBridge.csproj -c Release
```

### Android

Abrir `android/` no Android Studio e executar a variante `debug` num dispositivo com depuração USB autorizada.

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
3. Inicie `PhoneBridge.exe` como administrador quando a instalação/Frame Server pedir elevação.

## Diagnóstico rápido do Discord

1. Feche completamente o Discord, incluindo o processo no tray, e abra-o novamente.
2. Confirme primeiro que a câmara funciona na Câmara do Windows.
3. Não use a mesma câmara simultaneamente na Câmara do Windows/OBS e no Discord.
4. Em Discord, desative temporariamente a aceleração de hardware e selecione novamente `Câmara (PhoneBridge)`.
5. Consulte `C:\ProgramData\PhoneBridge\logs\phonebridge_camera.log` e procure erros `RequestSample`, `MFMediaSample` ou `QueryInterface`.
6. Registe a DLL x64 com `windows/scripts/install_camera.ps1` e reinicie o Frame Server.

Para saber o que está implementado versus planeado, consulte `BUGS_AND_ROADMAP.md` e `docs/diagnostics.md`.
