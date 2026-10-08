# PhoneBridge Technical Specification & Milestones

Android Phone as a USB Camera + USB Microphone for Windows.

---

## Architecture Overview

```text
ANDROID PHONE
─────────────────────────────────────────
Camera2
   │
   ▼
Camera Capture
   │
   ▼
MediaCodec H.264 Encoder
   │
   ▼
Video Packetizer
   │
   ├───────────────┐
   │               │
AudioRecord       Control
   │               │
   ▼               │
Audio Pipeline     │
   │               │
   └───────┬───────┘
           ▼
      ITransport
           │
           ▼
        USB / ADB

USB CABLE
═════════════════════════════════════════

WINDOWS PC
─────────────────────────────────────────
ADB / USB Transport
        │
        ▼
TransportManager
        │
        ▼
ProtocolEngine
   ┌────┴─────┐
   │          │
   ▼          ▼
Video       Audio
Pipeline    Pipeline
   │          │
   ▼          ▼
H.264       Ring Buffer
Decoder         │
   │            │
   ▼            ▼
Virtual       Virtual
Camera        Microphone
   │            │
   └─────┬──────┘
         ▼
 Windows Applications
```

---

## Project Milestones

### Milestone 0 — Foundation (Completed)
- Repository structure
- Wire protocol v1 & packet parser (C++ / JNI)
- Abstract socket server on Android (`localabstract:phonebridge`)
- `HELLO` / `HELLO_ACK` handshake, `DEVICE_INFO`, `CAPABILITIES`
- `HEARTBEAT` / `HEARTBEAT_ACK` & watchdog
- Test clients in Python and PowerShell (`tools/pc_test_client.ps1`)

---

### Milestone 1 — Audio Pipeline & Capture
- Android `AudioRecord` integration (48kHz, 16-bit PCM, Mono)
- Audio packetization (`AUDIO_CONFIG`, `AUDIO_FRAME`)
- Control commands (`START_MICROPHONE`, `STOP_MICROPHONE`)
- Capability report update (`"audio": {"implemented": true}`)
- Windows audio receiver / ring buffer & verification test script (writing `.wav` file / playback)

---

### Milestone 2 — Camera Pipeline & Video Capture
- Android `Camera2` capture pipeline
- `MediaCodec` H.264 hardware encoding
- Video packetization (`VIDEO_CONFIG`, `VIDEO_FRAME`)
- Control commands (`START_CAMERA`, `STOP_CAMERA`, `REQUEST_KEYFRAME`)
- Frame timestamps & sequence tracking

---

### Milestone 3 — Simultaneous Operation
- Simultaneous Camera + Microphone streaming
- Performance optimization (latency, CPU, battery, thermal management)

---

### Milestone 4 — Failure Recovery & Virtual Devices
- Automatic reconnection & state recovery
- Windows background service
- Windows Virtual Camera (`MFCreateVirtualCamera`)
- Windows Virtual Microphone (WDM/WaveRT/SYSVAD driver)
- Virtual device persistence across USB disconnections (black frame / silence fallback)

---

### Milestone 5 — Production Quality
- Windows UI (WinUI 3 / C#)
- Installer & driver signing documentation
- Full diagnostics & logging
