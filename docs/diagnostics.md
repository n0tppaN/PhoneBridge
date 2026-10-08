# PhoneBridge Diagnostics & Telemetry Specification

## Discord: Câmara funciona no Windows mas não no Discord

1. Fechar completamente o Discord, a Câmara do Windows, OBS e browsers.
2. Confirmar a câmara na Câmara do Windows e fechá-la antes de abrir o Discord.
3. No Discord, selecionar manualmente `Câmara (PhoneBridge)` e reiniciar o cliente.
4. Desativar temporariamente hardware acceleration no Discord.
5. Registar novamente a DLL x64 com `regsvr32` elevado se a source tiver sido recompilada.
6. Consultar `C:\ProgramData\PhoneBridge\logs\phonebridge_camera.log`.

Os logs `format=NV12`/`format=YUY2`, `RequestSample`, `MEMediaSample` e `IKsControl` são especialmente relevantes. A source agora anuncia a categoria `KSCATEGORY_VIDEO_CAMERA` e NV12/YUY2 para acomodar consumidores que rejeitam uma source apenas NV12.

## Áudio sem VB-Audio

O áudio é mantido em ring buffer e não é enviado para colunas nem reproduzido por `waveOut`. Um WAV só é criado quando `PHONEBRIDGE_DIAGNOSTIC_WAV=1`. Discord/Teams só podem selecionar um microfone quando existe um endpoint de captura Windows; isso requer um driver WaveRT/SYSVAD assinado. Não é possível substituir esse contrato por uma aplicação user-mode.

This document outlines the diagnostic metrics, logging categories, and operational health monitoring for PhoneBridge across Android and Windows.

---

## 1. Connection Diagnostics
- **Connection State:** `DISCONNECTED`, `WAITING_FOR_CLIENT`, `HANDSHAKING`, `STREAMING`, `RECONNECTING`
- **Reconnect Count:** Tracks the number of automatic reconnection attempts after transport drops.
- **Transport Latency:** Measured via round-trip time of `HEARTBEAT` / `HEARTBEAT_ACK` packets.
- **Heartbeat Status:** Active watchdog checks if last received packet/heartbeat exceeds 6000ms.

---

## 2. Video Diagnostics (Camera & H.264)
- **FPS:** Instantaneous and rolling average frames per second delivered by `CameraEncoder`.
- **Resolution:** Active capture dimensions (1920x1080 or 1280x720).
- **Bitrate:** Target and actual encoded bitrate (~8 Mbps).
- **Keyframes (IDR):** Count of sync frames and forced keyframe requests (`REQUEST_KEYFRAME`).
- **Decoder Resets:** Count of recovery resets triggered on decoder error or transport interruption.

---

## 3. Audio Diagnostics (Microphone PCM)
- **Sample Rate:** 48,000 Hz
- **Channels:** Mono (1 channel)
- **Buffer Fill Level:** Ring buffer occupancy percentage on Windows.
- **Underrun/Overrun Statistics:** Count of buffer starvation or overflow events during transport jitter.

---

## 4. Android Device Health
- **Thermal Status:** Monitored via `PowerManager.OnThermalStatusChangedListener` (`THERMAL_STATUS_NONE` up to `EMERGENCY`).
- **Battery & Charging:** USB power state monitoring to ensure sustainable long-session streaming.
- **Permissions:** Active verification of `CAMERA` and `RECORD_AUDIO` permissions before pipeline startup.
