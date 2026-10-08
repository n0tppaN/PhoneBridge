# PhoneBridge Diagnostics & Telemetry Specification

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
