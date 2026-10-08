# PHONEBRIDGE

## Android Phone as a USB Camera + USB Microphone for Windows

You are a senior software architect and implementation engineer specializing in Android NDK, Windows multimedia, low-latency streaming, Media Foundation, Windows audio drivers, USB/ADB transport, real-time systems, and fault-tolerant multimedia pipelines.

Your task is to design and implement a production-quality application called **PhoneBridge**.

The purpose of PhoneBridge is simple:

> Turn an Android smartphone into a high-quality USB camera and USB microphone for a Windows PC, with both devices usable simultaneously, independently controllable, and capable of automatically recovering from temporary failures or USB disconnections without requiring applications such as Discord, OBS, Teams, Zoom, or browsers to be restarted.

The system must be engineered as a real application rather than a proof-of-concept.

---

# 1. CORE PRODUCT

PhoneBridge consists of:

### Android application

Runs on the user's Android phone.

Responsibilities:

* capture camera frames
* encode camera video
* capture microphone audio
* transmit video/audio to Windows
* receive control commands from Windows
* report capabilities and device state
* detect/handle thermal pressure
* recover from transport failures
* reconnect automatically

### Windows background service

Runs continuously on Windows.

Responsibilities:

* manage the Android connection
* manage the USB/ADB transport
* receive and parse packets
* decode H.264
* buffer audio
* maintain A/V timing
* expose media to Windows virtual devices
* recover from connection/decoder failures
* request video keyframes when necessary
* automatically reconnect

### Windows virtual camera

Appears to applications as:

`PhoneBridge Camera`

Applications such as:

* OBS
* Discord
* Microsoft Teams
* Zoom
* browsers
* Windows Camera
* other webcam software

must be able to select it as a normal camera.

### Windows virtual microphone

Appears to applications as:

`PhoneBridge Microphone`

Applications must be able to select it as a normal Windows recording/input device.

---

# 2. IMPORTANT ARCHITECTURAL DECISION

Do NOT attempt to make a normal Android application behave as a native USB UVC/UAC gadget.

Do not depend on:

* root
* custom kernels
* modified Android ROMs
* Samsung-specific USB gadget functionality
* vendor-specific kernel drivers

The Android application should operate as a normal Android application.

USB is the transport.

Windows owns the virtual camera and virtual microphone.

Architecture:

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

# 3. PRIMARY DESIGN GOALS

The application must prioritize:

1. reliability
2. automatic recovery
3. low latency
4. low CPU usage
5. low memory usage
6. low battery consumption
7. hardware video encoding
8. hardware-accelerated decoding where appropriate
9. independent camera/microphone operation
10. stable Windows virtual devices
11. clean architecture
12. maintainability

Do not optimize for meaningless theoretical latency numbers.

Realistic targets:

### Camera

* acceptable: <100 ms
* good: <70 ms
* excellent: <50 ms

### Audio

* target: <30 ms
* preferred: ~15–25 ms

The system must prioritize stability over shaving a few milliseconds.

---

# 4. NON-GOALS

Do NOT implement these initially:

* Wi-Fi streaming
* Bluetooth transport
* RTSP
* WebRTC
* cloud servers
* remote internet streaming
* 4K
* 60 FPS
* AI background removal
* face tracking
* beauty filters
* virtual backgrounds
* multiple phones
* video effects
* cloud accounts
* user accounts
* online authentication

The architecture must allow future expansion, but these features must not complicate the MVP.

---

# 5. TECHNOLOGY STACK

## Android

Use:

* Kotlin for UI/application orchestration
* C++20 for performance-sensitive/core components
* Android NDK
* Camera2
* MediaCodec
* AudioRecord
* Foreground Service where required
* AIDL/JNI only where appropriate
* CMake

Do not use CameraX for the core capture pipeline.

Camera2 provides the explicit control required by this application.

---

# 6. WINDOWS

Target:

* Windows 11
* x64 initially
* ARM64-compatible architecture where practical

Use:

* C++20 for the core service
* Windows Media Foundation
* MediaCodec on Android
* Media Foundation H.264 decoder on Windows
* Windows virtual camera APIs
* WDM/WaveRT/SYSVAD architecture for the virtual microphone
* C# / WinUI 3 for optional UI

The core Windows service must not depend on the UI being open.

---

# 7. TRANSPORT

## MVP TRANSPORT: ADB OVER USB

The initial USB transport should use Android Debug Bridge.

ADB is acceptable for the MVP because it allows:

* USB transport
* reliable stream sockets
* development without requiring a custom kernel driver
* rapid implementation

However:

**ADB must be hidden behind an abstraction.**

Create:

```cpp
class ITransport
{
public:
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool send(...) = 0;
    virtual bool receive(...) = 0;
    virtual bool isConnected() const = 0;
    virtual ~ITransport() = default;
};
```

Implement:

```text
AdbTransport
```

Future transports may include:

```text
NativeUsbTransport
WifiTransport
```

but they are NOT required for the MVP.

---

# 8. ADB MANAGEMENT

The Windows application must be capable of managing the ADB connection.

It should:

1. detect connected Android devices
2. detect whether ADB is available
3. detect unauthorized devices
4. provide a useful error if USB debugging authorization is missing
5. automatically establish the required ADB forwarding/reverse forwarding
6. reconnect if the ADB socket disappears
7. restart the forwarding connection when required
8. avoid requiring the user to manually execute ADB commands

ADB executable management should be abstracted.

The application may:

* bundle a compatible ADB binary
* use a configured system ADB
* communicate with an existing ADB server

Choose the cleanest maintainable solution.

---

# 9. PROTOCOL

The protocol must be explicitly defined and versioned.

All multi-byte integers must use:

**Little Endian**

This is appropriate for the initial ARM/x86 targets.

Protocol version:

```text
VERSION = 1
```

Packet header:

```cpp
struct PacketHeader
{
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint32_t flags;
    uint64_t timestamp;
    uint32_t payloadSize;
    uint32_t sequence;
};
```

Define a constant magic number.

All fields must have explicitly documented sizes and meanings.

---

# 10. PACKET FRAMING

The protocol MUST NOT assume that:

```text
one socket read == one packet
```

A socket/ADB read may contain:

* half of a packet
* exactly one packet
* several packets

The parser must therefore maintain a receive buffer and incrementally parse complete packets.

Example:

```text
read()
    ↓
append to receive buffer
    ↓
if header incomplete:
    wait
    ↓
parse header
    ↓
validate payloadSize
    ↓
if payload incomplete:
    wait
    ↓
extract complete packet
    ↓
process
    ↓
repeat
```

The parser must support multiple packets in one read.

Never perform unsafe allocations based directly on untrusted `payloadSize`.

Define a maximum packet payload size.

Reject:

* invalid magic
* unsupported protocol version
* invalid packet type
* payload exceeding maximum
* malformed headers
* impossible sequence values where applicable

---

# 11. MESSAGE TYPES

At minimum define:

```text
HELLO
HELLO_ACK

DEVICE_INFO
CAPABILITIES

CONFIG_REQUEST
CONFIG_RESPONSE

START_CAMERA
STOP_CAMERA

START_MICROPHONE
STOP_MICROPHONE

VIDEO_CONFIG
VIDEO_FRAME

AUDIO_CONFIG
AUDIO_FRAME

REQUEST_KEYFRAME

HEARTBEAT
HEARTBEAT_ACK

ERROR

GOODBYE
```

Reserve space for future message types.

---

# 12. TIMESTAMPS

All media packets must contain timestamps.

Use a monotonic clock.

Do NOT derive timestamps solely from:

```text
frameNumber × frameInterval
```

because real hardware clocks and scheduling are not perfectly periodic.

Maintain:

* timestamp
* sequence number
* stream identity

for every media packet.

---

# 13. CAPABILITY NEGOTIATION

On connection:

```text
Windows → HELLO
Android → HELLO_ACK
Android → DEVICE_INFO
Android → CAPABILITIES
Windows → CONFIG_REQUEST
Android → CONFIG_RESPONSE
```

Capabilities should include:

### Camera

* supported resolutions
* supported FPS
* supported camera IDs
* front/rear
* encoder support
* H.264 profiles if available

### Audio

* supported sample rates
* channel count
* sample formats
* supported input sources

The Windows side must never blindly assume every Android device supports the same configuration.

---

# 14. CAMERA PIPELINE

Use:

```text
Camera2
    ↓
Image/Surface pipeline
    ↓
MediaCodec H.264 encoder
    ↓
H264 access units
    ↓
Packetizer
    ↓
Transport
```

Prefer a direct Camera2 → MediaCodec Surface pipeline to avoid unnecessary CPU copies.

Initial target:

```text
1920 × 1080
30 FPS
H.264
~8–12 Mbps
```

Fallback:

```text
1280 × 720
30 FPS
```

If the device cannot sustain 1080p30, automatically fall back to a supported configuration.

---

# 15. VIDEO KEYFRAME CONTROL

This is critical for recovery.

The Windows side must be able to request a new keyframe.

Define:

```text
REQUEST_KEYFRAME
```

Windows sends:

```text
REQUEST_KEYFRAME
```

Android responds by requesting a sync frame from MediaCodec using the appropriate codec parameter, such as:

```text
MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME
```

This must happen when:

* the Windows decoder is restarted
* the connection is re-established
* packets were lost
* the decoder reports an unrecoverable state
* the first video frame after reconnect requires a clean decoder state

The Android encoder should also use a reasonable periodic IDR/keyframe interval.

---

# 16. CAMERA RECOVERY

The Windows video decoder must not become permanently stuck because of a damaged or incomplete GOP.

If the decoder fails:

```text
flush/reset decoder
        ↓
discard incomplete frames
        ↓
request keyframe
        ↓
wait for next IDR
        ↓
resume decoding
```

Do not feed arbitrary delta frames into a freshly reset decoder.

---

# 17. CAMERA ROTATION

Handle:

* portrait
* landscape
* device rotation
* front camera mirroring
* rear camera

Rotation must be communicated explicitly.

Do not assume:

```text
width > height == landscape
```

---

# 18. PREVIEW

The Android app should have a camera preview.

However:

**Preview and capture must be architecturally separate.**

The user should be able to:

* preview while streaming
* disable preview while continuing streaming
* optionally allow screen-off streaming

Do NOT automatically stop the camera merely because the screen turns off.

Screen-off behavior must be an explicit policy.

---

# 19. AUDIO PIPELINE

Use:

```text
AudioRecord
    ↓
PCM
    ↓
audio buffering
    ↓
clock correction / resampling
    ↓
packetization
    ↓
transport
```

Initial format:

```text
48 kHz
16-bit
mono
PCM
```

Avoid Opus initially.

Raw PCM simplifies:

* latency
* debugging
* synchronization
* recovery
* implementation

Compression can be added later if bandwidth becomes a problem.

---

# 20. AUDIO CLOCK DRIFT

This MUST be implemented from the beginning.

The Android audio hardware clock and Windows audio clock will not be perfectly identical.

Over time this causes:

* buffer growth
* buffer depletion
* audio glitches
* A/V drift

Implement an asynchronous resampling strategy.

The system should monitor ring-buffer fill level.

For example:

```text
buffer too full
    ↓
slightly increase effective playback rate

buffer too empty
    ↓
slightly decrease effective playback rate
```

Use a high-quality lightweight resampler.

Possible implementations:

* SpeexDSP
* libsoxr
* custom high-quality interpolation

Choose the simplest solution that meets the latency and CPU targets.

The correction must be extremely small and inaudible.

Do NOT repeatedly drop large blocks of audio.

---

# 21. AUDIO RING BUFFER

The Windows audio pipeline must use a bounded ring buffer.

It must:

* never grow indefinitely
* avoid unbounded memory allocation
* tolerate short transport interruptions
* expose fill level
* support underrun/overrun statistics

During temporary disconnection:

```text
output silence
```

Do not block the Windows audio engine waiting indefinitely for Android.

---

# 22. A/V SYNCHRONIZATION

Video and audio must use monotonic timestamps.

At connection/startup:

1. establish a timestamp relationship
2. track timestamps continuously
3. avoid assuming the Android and Windows clocks are identical
4. use buffering and controlled correction

Do not attempt to synchronize by simply counting frames.

The system must remain stable during long sessions.

---

# 23. WINDOWS VIRTUAL CAMERA

Primary target:

```text
Media Foundation virtual camera
```

Use the Windows virtual camera APIs, including:

```text
MFCreateVirtualCamera
IMFVirtualCamera
```

The virtual camera should appear as:

```text
PhoneBridge Camera
```

It should provide decoded frames in a format suitable for Windows applications.

---

# 24. CAMERA COMPATIBILITY

Modern Media Foundation applications are the primary target.

However, compatibility with applications using older webcam/DirectShow paths must be tested.

Do NOT blindly add arbitrary DirectShow registry entries.

If compatibility testing demonstrates that legacy applications require an additional compatibility layer, implement it deliberately and document why it exists.

Do not introduce legacy registration merely because it sounds useful.

---

# 25. WINDOWS VIRTUAL MICROPHONE

The microphone must appear as a genuine Windows input/recording device.

A simple WASAPI loopback device is NOT sufficient.

WASAPI loopback captures output audio; it does not by itself create a selectable microphone endpoint.

The architecture should therefore use a proper Windows virtual audio endpoint.

Use a WDM/WaveRT/SYSVAD-derived architecture as the reference implementation.

Important separation:

```text
Windows Service
       │
       ▼
shared memory / ring buffer
       │
       ▼
virtual audio driver
       │
       ▼
Windows Audio
       │
       ▼
Discord / OBS / Teams / etc.
```

The kernel driver should remain as small as possible.

The driver must NOT directly manage:

* ADB
* USB
* Android
* networking
* video
* application protocol

The user-mode service owns those responsibilities.

---

# 26. DRIVER DEVELOPMENT VS PRODUCTION

Clearly separate:

### Development

Use:

* test signing
* development certificates
* local WDK testing
* VM/test machines if appropriate

### Production

Document the requirements for:

* driver signing
* certificate management
* Microsoft Partner Center
* HLK/WHQL requirements where applicable
* secure distribution
* installation permissions
* driver updates

Do not claim the audio driver is production-ready merely because it works under Windows test-signing mode.

---

# 27. DISCONNECT HANDLING

This is a CORE FEATURE.

The system must assume that connections fail.

Possible failures:

* USB cable disconnected
* USB port reset
* phone locked
* ADB disappears
* ADB server crashes
* Android app crashes
* Android service is killed
* transport socket closes
* malformed packet
* decoder failure
* audio underrun
* temporary USB interruption
* Windows service restart
* phone changes USB state

The application must recover wherever technically possible.

---

# 28. VIRTUAL DEVICES MUST SURVIVE DISCONNECTS

When Android disappears:

### Camera

Continue exposing:

```text
PhoneBridge Camera
```

but output:

```text
black frames
```

or another clearly defined no-signal frame.

### Microphone

Continue exposing:

```text
PhoneBridge Microphone
```

but output:

```text
silence
```

Do NOT unregister the devices simply because the phone temporarily disappeared.

This is critical because applications such as Discord/OBS/Teams may react badly when their selected device disappears.

The Windows service should preserve the virtual device identity.

---

# 29. AUTOMATIC RECONNECTION

The system must automatically attempt reconnection.

Example state machine:

```text
DISCONNECTED
      ↓
DISCOVERING
      ↓
CONNECTING
      ↓
HANDSHAKING
      ↓
CONFIGURING
      ↓
STREAMING
      ↓
DEGRADED
      ↓
RECONNECTING
      ↓
HANDSHAKING
      ↓
CONFIGURING
      ↓
STREAMING
```

The exact state machine should be explicitly implemented rather than represented by dozens of independent booleans.

---

# 30. RECONNECT STRATEGY

When the transport fails:

1. immediately stop consuming invalid transport data
2. preserve virtual camera
3. preserve virtual microphone
4. output black video
5. output silence
6. release broken transport resources
7. detect whether Android is still connected
8. recreate ADB forwarding if necessary
9. reconnect
10. perform HELLO handshake
11. renegotiate capabilities/configuration
12. restart required streams
13. reset decoder
14. request keyframe
15. resume audio
16. resume video
17. return to STREAMING

Use bounded exponential backoff.

Example:

```text
250 ms
500 ms
1 s
2 s
4 s
5 s maximum
```

Avoid aggressive reconnect loops that consume CPU.

---

# 31. ANDROID RECONNECTION

The Android application must also tolerate:

* socket closure
* Windows service restart
* USB interruption
* ADB restart
* transport timeout

The app should:

* detect transport loss
* stop sending to the dead socket
* release/recreate transport resources
* retain capture state where safe
* reconnect when possible
* restart streams if required

Do not require the user to manually restart the Android application after every transient failure.

---

# 32. HEARTBEAT

Implement:

```text
HEARTBEAT
HEARTBEAT_ACK
```

Use them to detect dead connections.

Timeouts must be configurable.

A heartbeat failure must trigger the reconnection state machine.

Do not rely exclusively on TCP/ADB socket errors because some failures may result in a connection that appears alive but is no longer delivering useful data.

---

# 33. ANDROID THERMAL MANAGEMENT

The phone may become hot because it is simultaneously doing:

* Camera2 capture
* H.264 encoding
* Audio capture
* USB communication
* screen rendering

Monitor Android thermal status where available.

If thermal pressure becomes significant:

1. record diagnostics
2. warn the user if appropriate
3. optionally reduce FPS
4. optionally reduce resolution
5. preserve audio if possible
6. avoid sudden total failure

Configuration should support policies such as:

```text
NORMAL
PERFORMANCE_REDUCED
THERMAL_WARNING
THERMAL_EMERGENCY
```

Do not overreact to short temperature spikes.

---

# 34. BATTERY MANAGEMENT

Avoid unnecessary battery drain.

Use:

* hardware H.264 encoding
* zero/minimal-copy pipelines
* bounded queues
* no unnecessary polling
* no excessive wake locks
* efficient heartbeat intervals
* optional screen-off streaming

USB charging should naturally help during long sessions, but the software must still be efficient.

---

# 35. ANDROID PERMISSIONS

Request only required permissions.

At minimum:

```text
CAMERA
RECORD_AUDIO
```

Use foreground service mechanisms appropriate to the Android version.

Clearly explain to the user why camera/microphone access is required.

The application must not secretly record or transmit media when the user has not enabled the corresponding feature.

---

# 36. INDEPENDENT CAMERA AND MICROPHONE CONTROL

Camera and microphone must be independent.

Valid states include:

```text
Camera OFF
Microphone OFF

Camera ON
Microphone OFF

Camera OFF
Microphone ON

Camera ON
Microphone ON
```

Starting/stopping one must not unnecessarily restart the other.

---

# 37. WINDOWS SERVICE ARCHITECTURE

Recommended structure:

```text
WindowsService
│
├── TransportManager
│
├── ConnectionManager
│
├── ProtocolEngine
│
├── CapabilityManager
│
├── VideoPipeline
│   ├── H264Decoder
│   ├── VideoBuffer
│   ├── TimestampManager
│   └── KeyframeManager
│
├── AudioPipeline
│   ├── AudioBuffer
│   ├── RingBuffer
│   ├── Resampler
│   └── TimestampManager
│
├── DeviceManager
│   ├── VirtualCamera
│   └── VirtualMicrophone
│
├── RecoveryManager
│
├── Diagnostics
│
└── IPC
```

Each subsystem must have a clearly defined responsibility.

---

# 38. THREADING

Do not create one giant worker thread.

Recommended separation:

```text
Transport Thread
Protocol Thread
Video Decode Thread
Video Output Thread
Audio Receive Thread
Audio Resample Thread
Audio Output Thread
Recovery/Connection Thread
Diagnostics Thread
```

Use bounded queues between stages.

Avoid:

* unbounded queues
* blocking UI operations
* locks held during expensive processing
* unnecessary thread creation
* busy waiting

---

# 39. MEMORY MANAGEMENT

The application must use bounded buffers.

Never allow a network/USB problem to cause:

```text
queue grows forever
```

All queues must have defined maximum sizes.

On overload, use explicitly defined policies:

* drop stale video frames
* preserve audio where possible
* never silently corrupt protocol data

Video is generally more disposable than audio.

---

# 40. VIDEO BACKPRESSURE

If Windows cannot process video fast enough:

Prefer dropping old video frames rather than allowing latency to grow indefinitely.

For example:

```text
Frame 1
Frame 2
Frame 3
Frame 4
Frame 5
```

If the decoder/output is delayed, discard stale frames when appropriate rather than displaying them several seconds late.

The objective is:

> low and bounded latency, not guaranteed delivery of every video frame.

---

# 41. AUDIO PRIORITY

Audio should be treated differently.

Avoid arbitrary audio frame dropping.

Use:

* bounded buffering
* asynchronous resampling
* controlled underrun recovery
* silence insertion only when necessary

Audio continuity is more important than preserving every video frame.

---

# 42. ERROR HANDLING

Define structured error codes.

Examples:

```text
ERR_PROTOCOL_VERSION
ERR_INVALID_PACKET
ERR_PACKET_TOO_LARGE
ERR_UNSUPPORTED_CONFIG
ERR_CAMERA_UNAVAILABLE
ERR_AUDIO_UNAVAILABLE
ERR_ENCODER_FAILURE
ERR_DECODER_FAILURE
ERR_TRANSPORT_FAILURE
ERR_ADB_UNAUTHORIZED
ERR_DEVICE_DISCONNECTED
ERR_THERMAL_LIMIT
ERR_TIMEOUT
```

Errors should be:

* logged
* categorized
* recoverable where possible
* presented to the user when action is required

---

# 43. LOGGING

Implement structured logs.

Each log entry should contain where useful:

```text
timestamp
severity
component
event
connection/session ID
sequence number
error code
additional context
```

Example:

```text
[INFO] Transport connected
[INFO] Android device authorized
[INFO] Capabilities received
[INFO] Camera configured 1920x1080@30
[INFO] Microphone configured 48000Hz mono
[WARN] Video decoder reset
[INFO] Requesting keyframe
[WARN] USB transport lost
[INFO] Reconnection attempt 2
[INFO] Transport restored
[INFO] Streaming resumed
```

Never log microphone audio or camera image data.

---

# 44. DIAGNOSTICS

Expose useful statistics:

### Connection

* connection state
* reconnect count
* transport latency
* heartbeat status

### Video

* FPS
* resolution
* bitrate
* dropped frames
* decoder resets
* keyframes
* decode latency

### Audio

* sample rate
* buffer fill
* underruns
* overruns
* resampling correction
* audio latency

### Android

* thermal state
* encoder state
* camera state
* microphone state

These diagnostics should be available to the Windows UI and/or logs.

---

# 45. WINDOWS UI

The UI should be simple.

Do not create a bloated control panel.

Main screen:

```text
PHONEBRIDGE

Device:
    Galaxy / Android Device

USB:
    Connected ●

Camera:
    ON
    1920 × 1080 @ 30 FPS

Microphone:
    ON
    48 kHz Mono

Status:
    Streaming

[ Camera ON/OFF ]
[ Microphone ON/OFF ]

Diagnostics
```

If disconnected:

```text
USB:
    Disconnected

Status:
    Reconnecting...
```

The service must continue running even if the UI is closed.

---

# 46. SECURITY AND PRIVACY

The system is local-only.

No cloud infrastructure.

No media upload.

No remote account.

No telemetry containing user media.

The application should clearly indicate when:

* camera is active
* microphone is active
* connection is active

ADB authorization must be respected.

Windows IPC must use appropriate access control so arbitrary processes cannot inject fake camera/microphone data into the service.

---

# 47. REPOSITORY STRUCTURE

Use a structure similar to:

```text
phonebridge/
│
├── android/
│   ├── app/
│   │   ├── src/main/java/
│   │   ├── src/main/cpp/
│   │   ├── src/main/res/
│   │   └── src/main/AndroidManifest.xml
│   │
│   └── CMakeLists.txt
│
├── windows/
│   ├── service/
│   │   ├── transport/
│   │   ├── protocol/
│   │   ├── video/
│   │   ├── audio/
│   │   ├── recovery/
│   │   ├── diagnostics/
│   │   └── ipc/
│   │
│   ├── virtual-camera/
│   │
│   ├── audio-driver/
│   │
│   └── ui/
│
├── protocol/
│   ├── protocol.h
│   ├── packet.h
│   ├── messages.h
│   └── README.md
│
├── tests/
│   ├── protocol/
│   ├── transport/
│   ├── video/
│   ├── audio/
│   └── recovery/
│
├── installer/
│
├── docs/
│   ├── architecture.md
│   ├── protocol.md
│   ├── recovery.md
│   ├── driver-signing.md
│   └── troubleshooting.md
│
└── README.md
```

---

# 48. TESTING

Testing is a first-class requirement.

## Protocol tests

Test:

* partial reads
* multiple packets per read
* corrupted headers
* invalid payload sizes
* unsupported versions
* sequence handling

## Transport tests

Test:

* connect
* disconnect
* reconnect
* ADB unavailable
* unauthorized device
* ADB restart
* USB cable removal

## Video tests

Test:

* normal streaming
* decoder reset
* missing frames
* corrupted frames
* reconnect
* keyframe request
* camera rotation
* resolution changes

## Audio tests

Test:

* normal streaming
* underrun
* overrun
* clock drift
* resampling
* reconnect
* microphone toggling

## Recovery tests

Simulate:

```text
USB unplug
USB reconnect
Android app crash
Windows service restart
ADB server restart
decoder crash
audio pipeline failure
temporary packet loss
phone screen lock
phone screen unlock
```

The system should recover automatically wherever possible.

---

# 49. ACCEPTANCE TEST

The following must work:

1. Connect Android phone via USB.
2. Launch PhoneBridge.
3. Windows detects the phone.
4. Android connects automatically.
5. Windows exposes:

   * PhoneBridge Camera
   * PhoneBridge Microphone
6. Open Windows Camera.
7. Select PhoneBridge Camera.
8. Video works.
9. Open Discord/OBS/Teams.
10. Select PhoneBridge Microphone.
11. Audio works.
12. Camera and microphone operate simultaneously.
13. Disable camera.
14. Microphone continues working.
15. Disable microphone.
16. Camera continues working.
17. Disconnect USB.
18. Windows virtual devices remain present.
19. Camera outputs black/no-signal.
20. Microphone outputs silence.
21. Reconnect USB.
22. PhoneBridge automatically reconnects.
23. Video decoder resets correctly.
24. Windows requests/receives a new keyframe.
25. Video resumes.
26. Audio resumes.
27. Discord/OBS/Teams do NOT need to be restarted.
28. Repeat disconnect/reconnect multiple times.
29. The system remains stable.

This is a mandatory end-to-end requirement.

---

# 50. MILESTONES

## Milestone 0 — Foundation

Implement:

* repository
* protocol
* packet parser
* packet serializer
* ITransport
* AdbTransport
* Android skeleton
* Windows service skeleton
* connection state machine
* HELLO handshake
* capability negotiation
* heartbeat
* logging

Acceptance:

```text
Android ↔ Windows
stable connection
HELLO works
heartbeat works
disconnect detected
reconnect works
```

---

## Milestone 1 — Audio

Implement:

* AudioRecord
* PCM 48kHz mono
* audio packets
* Windows ring buffer
* virtual microphone
* asynchronous resampling
* timestamps
* underrun/overrun recovery

Acceptance:

```text
Phone microphone
        ↓
USB
        ↓
PhoneBridge Microphone
        ↓
Windows Sound Recorder
```

Audio must survive reconnects.

---

## Milestone 2 — Camera

Implement:

* Camera2
* MediaCodec H.264
* packetization
* Windows Media Foundation decoder
* virtual camera
* timestamps
* keyframe requests

Acceptance:

```text
Phone camera
        ↓
H.264
        ↓
USB
        ↓
Windows decoder
        ↓
PhoneBridge Camera
```

---

## Milestone 3 — Simultaneous operation

Camera + microphone simultaneously.

Test:

* CPU
* RAM
* latency
* thermal behavior
* USB bandwidth
* long sessions

---

## Milestone 4 — Failure Recovery

Implement and test:

* disconnect
* reconnect
* decoder reset
* keyframe request
* ADB restart
* Android process restart
* Windows service restart
* audio recovery
* virtual device persistence

This milestone is mandatory.

---

## Milestone 5 — Production Quality

Implement:

* Windows UI
* installer
* driver installation
* logging UI
* diagnostics
* documentation
* driver signing documentation
* performance tuning
* compatibility testing

---

# 51. IMPORTANT IMPLEMENTATION RULES

Do NOT generate the entire project as one enormous code dump.

Work incrementally.

For every milestone:

1. inspect architecture
2. implement a small coherent component
3. compile
4. test
5. fix errors
6. document
7. continue

Do not create fake implementations merely to make compilation succeed.

If a Windows driver component cannot be genuinely implemented in the current environment, explicitly state what is missing instead of pretending it works.

If a Windows API requires a specific SDK/WDK version, document it.

If a component requires administrator privileges, document it.

If production driver signing is required, document it.

---

# 52. CODE QUALITY

Prefer:

* RAII
* smart pointers
* explicit ownership
* strongly typed enums
* immutable configuration where possible
* bounded queues
* clear interfaces
* unit tests
* structured error handling

Avoid:

* global mutable state
* magic numbers
* giant classes
* giant functions
* arbitrary sleeps
* busy loops
* unbounded queues
* silent error swallowing
* unnecessary copies

---

# 53. STATE MACHINES

Explicitly implement state machines for:

### Connection

```text
DISCONNECTED
DISCOVERING
CONNECTING
AUTHENTICATING
HANDSHAKING
CONFIGURING
STREAMING
DEGRADED
RECONNECTING
FAILED
```

### Camera

```text
OFF
STARTING
RUNNING
STOPPING
ERROR
RECOVERING
```

### Microphone

```text
OFF
STARTING
RUNNING
STOPPING
ERROR
RECOVERING
```

Do not represent complex lifecycle state using many unrelated booleans.

---

# 54. IMPORTANT REAL-WORLD CONSTRAINT

The system must assume that USB is not perfectly reliable.

A temporary transport failure must be considered normal rather than exceptional.

The correct behavior is:

```text
failure
  ↓
detect
  ↓
preserve Windows devices
  ↓
output safe fallback
  ↓
reconnect
  ↓
renegotiate
  ↓
reset pipelines if required
  ↓
request keyframe
  ↓
resume
```

The user should experience this as:

> "The phone temporarily disconnected and then came back."

rather than:

> "Discord lost my microphone and I have to restart everything."

---

# 55. FUTURE EXTENSIBILITY

The architecture should make it possible to add later:

* Wi-Fi transport
* native USB transport
* 1080p60
* 4K
* H.265/HEVC
* Opus
* multiple cameras
* camera controls
* exposure
* focus
* zoom
* torch
* HDR
* advanced diagnostics

But do not implement these now.

The architecture should support them without requiring a rewrite of the core.

---

# 56. FIRST IMPLEMENTATION TASK

Before writing large amounts of code:

Produce a concise technical architecture review containing:

1. final component diagram
2. connection state machine
3. camera state machine
4. microphone state machine
5. packet protocol
6. reconnect sequence
7. Android capture architecture
8. Windows media architecture
9. virtual camera architecture
10. virtual microphone architecture
11. threading model
12. memory/buffer model
13. failure modes
14. testing strategy
15. required Android SDK/NDK versions
16. required Windows SDK/WDK components
17. driver-signing implications

Then begin Milestone 0.

Do not redesign the architecture unnecessarily.

The goal is a real, maintainable application:

> Android phone → USB → Windows → real virtual camera + real virtual microphone

with:

> simultaneous operation + low latency + bounded resources + automatic failure recovery + automatic reconnection + stable Windows device identity.
