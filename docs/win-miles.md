# PHONEBRIDGE — WINDOWS IMPLEMENTATION PLAN

The Android side already has a working prototype for:

* ADB/USB transport
* initial connection
* HELLO handshake
* device information
* capability negotiation
* audio streaming
* connection loss detection
* Android-side return to Waiting
* new connection acceptance
* automatic session recreation
* restarting camera/microphone after reconnection

The current Android test successfully produces:

```text
=== ETAPA 1: Conexao Inicial ===
Handshake
Device info
Capabilities
Audio frames received

Simulated abrupt connection loss

=== ETAPA 2: RECONEXAO AUTOMATICA ===
Handshake
Device info
Capabilities
Camera/microphone restart requested
Audio frames received

TESTE DE RECONEXAO AUTOMATICA CONCLUIDO COM SUCESSO!
```

Do NOT rewrite the working Android transport unnecessarily.

The next task is to implement the Windows side incrementally.

The objective is NOT to generate the entire Windows application at once.

Implement one milestone at a time, compile and test each one before moving to the next.

---

# GLOBAL WINDOWS ARCHITECTURE

The Windows side should eventually become:

```text
Android
   │
   │ USB / ADB
   ▼
AdbTransport
   │
   ▼
ConnectionManager
   │
   ▼
ProtocolEngine
   │
   ├───────────────┐
   ▼               ▼
VideoPipeline    AudioPipeline
   │               │
   ▼               ▼
H264 Decoder     Audio Buffer
   │               │
   ▼               ▼
Virtual Camera   Virtual Microphone
   │               │
   └───────┬───────┘
           ▼
    Windows Applications
```

The architecture must keep transport, protocol, media processing, recovery and virtual devices separate.

---

# MILESTONE 0 — WINDOWS FOUNDATION

## Objective

Create the Windows application/service foundation without implementing virtual camera or microphone yet.

Implement:

```text
windows/service/
    transport/
    protocol/
    connection/
    diagnostics/
```

The service must be able to connect to the already-working Android server.

---

## 0.1 AdbTransport

Implement:

```cpp
class ITransport
{
public:
    virtual ~ITransport() = default;

    virtual bool connect() = 0;
    virtual void disconnect() = 0;

    virtual bool send(const uint8_t* data, size_t size) = 0;
    virtual bool receive(uint8_t* data, size_t size) = 0;

    virtual bool isConnected() const = 0;
};
```

Implement:

```text
AdbTransport
```

It should connect to the existing ADB-forwarded port.

The current test uses:

```text
127.0.0.1:27183
```

Do not hard-code the port into the architecture.

Make it configurable.

---

# 0.2 Protocol Parser

Implement a proper streaming packet parser.

It MUST support:

```text
partial packet
multiple packets in one read
packet split across multiple reads
```

Never assume:

```text
socket read == packet
```

The parser should maintain an internal receive buffer.

Algorithm:

```text
receive bytes
     ↓
append buffer
     ↓
check header
     ↓
validate header
     ↓
check complete payload
     ↓
extract packet
     ↓
process packet
     ↓
repeat
```

Add tests for all of these cases.

---

# 0.3 Protocol Validation

Validate:

* magic
* protocol version
* message type
* payload size
* sequence
* timestamps where applicable

Define a maximum payload size.

Never allocate arbitrary amounts of memory based on a received payload size.

---

# 0.4 Connection State Machine

Implement an explicit state machine:

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
```

Do not represent this using many independent booleans.

---

# 0.5 Handshake

The Windows implementation must correctly process the existing Android messages:

```text
HELLO
HELLO_ACK
DEVICE_INFO
CAPABILITIES
```

Verify the actual format used by the Android implementation before changing anything.

Do not invent a second incompatible protocol.

---

# 0.6 Heartbeat

Implement:

```text
HEARTBEAT
HEARTBEAT_ACK
```

The heartbeat must detect a dead connection even if the socket does not immediately report an error.

Use configurable timeout values.

---

# MILESTONE 0 ACCEPTANCE TEST

Run the existing Android server and execute:

```powershell
.\tools\pc_test_reconnect.ps1
```

The Windows implementation must:

1. connect
2. complete handshake
3. receive capabilities
4. receive audio packets
5. detect connection loss
6. transition to RECONNECTING
7. reconnect
8. complete handshake again
9. continue receiving data

Do not proceed until this is stable.

---

# MILESTONE 1 — ROBUST AUTOMATIC RECONNECTION

Now make reconnection a proper production subsystem.

Implement:

```text
RecoveryManager
ConnectionManager
```

---

## 1.1 Connection Failure Detection

Handle:

* socket closed
* send failure
* receive failure
* heartbeat timeout
* malformed protocol
* ADB forwarding failure
* Android unavailable

Any of these should transition the system into a recovery state.

---

# 1.2 Reconnection Backoff

Use bounded exponential backoff:

```text
250 ms
500 ms
1 s
2 s
4 s
5 s
5 s
5 s
...
```

Do not continuously reconnect at maximum CPU speed.

---

# 1.3 ADB Recovery

The Windows side should be able to recreate the ADB forwarding when necessary.

Handle:

```text
ADB server restarted
ADB device disappears
ADB device returns
forwarding disappears
forwarding becomes invalid
```

If the device is unauthorized, expose a useful diagnostic:

```text
Android device detected but ADB authorization is required.
Unlock the phone and accept the USB debugging prompt.
```

---

# 1.4 Session Reset

Every successful reconnect creates a new session.

The previous session must not leak:

* packet buffers
* decoder state
* audio buffers
* sequence state
* timestamps
* threads
* handles

Use an explicit session ID.

---

# 1.5 Reconnection Sequence

The final sequence should be:

```text
connection failure
       ↓
preserve virtual devices
       ↓
output safe fallback
       ↓
close old transport
       ↓
reset session state
       ↓
attempt ADB recovery
       ↓
connect
       ↓
HELLO
       ↓
DEVICE_INFO
       ↓
CAPABILITIES
       ↓
CONFIGURATION
       ↓
restart requested streams
       ↓
STREAMING
```

This must be deterministic.

---

# MILESTONE 1 ACCEPTANCE TEST

Repeatedly:

```text
connect
stream
kill connection
wait
reconnect
stream
```

Perform this at least 20 times.

There must be:

* no memory leak
* no stuck state
* no duplicated worker threads
* no stale session data
* no need to restart the Windows service

---

# MILESTONE 2 — WINDOWS AUDIO RECEIVER

Implement the Windows audio pipeline WITHOUT the final virtual microphone first.

Architecture:

```text
ADB
 ↓
ProtocolEngine
 ↓
AudioPacketReceiver
 ↓
AudioJitterBuffer
 ↓
RingBuffer
 ↓
AudioOutputTestSink
```

The first goal is to prove that Windows can continuously consume the Android PCM stream.

---

# 2.1 Audio Format

Initial format:

```text
48000 Hz
16-bit
mono
PCM signed little-endian
```

Do not introduce Opus yet.

---

# 2.2 Audio Packet Validation

Validate:

* packet size
* sequence
* timestamp
* sample count
* format

Reject malformed audio packets safely.

---

# 2.3 Ring Buffer

Implement a bounded ring buffer.

It must expose:

```text
capacity
available samples
fill percentage
underruns
overruns
```

Never allow it to grow indefinitely.

---

# 2.4 Audio Clock Drift

Implement asynchronous resampling now.

Do not postpone it until after the virtual microphone.

The Android audio clock and Windows audio clock will drift.

Use ring-buffer fill level as the control signal.

The correction should be extremely small.

Possible implementation:

```text
SpeexDSP
```

or:

```text
libsoxr
```

or a simple high-quality resampler if CPU requirements are better.

Choose one and document the reason.

---

# 2.5 Test Sink

Before creating a kernel driver, create a test output path.

For example:

```text
Android microphone
       ↓
Windows
       ↓
decoded PCM
       ↓
test sink
```

The test sink can:

* calculate RMS
* calculate peak level
* count samples
* detect underruns
* detect overruns
* optionally write a WAV file for debugging

This milestone proves the audio pipeline independently of the driver.

---

# MILESTONE 2 ACCEPTANCE TEST

Run for at least 10 minutes.

Verify:

* continuous samples
* no uncontrolled buffer growth
* no crashes
* clock correction works
* reconnect works
* audio resumes after reconnect

---

# MILESTONE 3 — WINDOWS VIRTUAL MICROPHONE

Only after Milestone 2 is stable.

Implement the real Windows virtual microphone.

Use:

```text
WDM
WaveRT
SYSVAD-derived architecture
```

The virtual device should appear as:

```text
PhoneBridge Microphone
```

Important:

The kernel driver must NOT know about:

* ADB
* USB
* Android
* protocol packets

The user-mode service feeds the driver.

Architecture:

```text
Android
   ↓
ADB
   ↓
Windows Service
   ↓
Audio Ring Buffer
   ↓
IPC / Shared Memory
   ↓
Virtual Audio Driver
   ↓
Windows Audio Engine
   ↓
Discord / OBS / Teams
```

---

# 3.1 Driver Development Environment

Document exactly what is required:

* Visual Studio version
* Windows SDK
* WDK
* test-signing configuration
* test certificate
* administrator privileges

Do not pretend that a test-signed driver is production-ready.

---

# 3.2 Driver Requirements

The driver must:

* expose a normal recording endpoint
* provide stable sample format
* tolerate temporary silence
* continue existing while Android disconnects
* receive silence when the phone is disconnected

---

# 3.3 Device Persistence

The device must remain visible when the Android phone disappears.

Disconnect means:

```text
PhoneBridge Microphone
    remains installed
    remains selectable
    outputs silence
```

---

# MILESTONE 3 ACCEPTANCE TEST

Open Windows Sound settings.

The device must appear as:

```text
PhoneBridge Microphone
```

Then test:

* Sound Recorder
* Discord
* OBS
* browser microphone permission

Disconnect USB.

The microphone must remain selectable.

Reconnect USB.

Audio must resume without restarting the application.

---

# MILESTONE 4 — WINDOWS VIDEO RECEIVER

Now implement the video pipeline independently.

Architecture:

```text
ADB
 ↓
ProtocolEngine
 ↓
VideoPacketReceiver
 ↓
H264 Access Unit Buffer
 ↓
Media Foundation H264 Decoder
 ↓
Decoded Frame Buffer
```

---

# 4.1 H.264 Decoder

Use Windows Media Foundation.

The decoder must correctly handle:

* SPS
* PPS
* IDR
* non-IDR frames
* decoder reset
* reconnect

---

# 4.2 Keyframe Recovery

Implement:

```text
REQUEST_KEYFRAME
```

When decoder state becomes invalid:

```text
reset decoder
     ↓
discard incomplete GOP
     ↓
send REQUEST_KEYFRAME
     ↓
wait for IDR
     ↓
resume
```

Do NOT attempt to decode arbitrary delta frames after a decoder reset.

---

# 4.3 Video Buffer

Use a small bounded buffer.

Target:

```text
2–3 frames
```

If overloaded:

```text
drop stale video frames
```

Do not allow latency to grow indefinitely.

---

# MILESTONE 4 ACCEPTANCE TEST

The Windows service must:

1. receive H.264
2. decode frames
3. report FPS
4. report resolution
5. detect decoder errors
6. reset decoder
7. request a keyframe
8. resume decoding

No virtual camera yet.

---

# MILESTONE 5 — WINDOWS VIRTUAL CAMERA

Only after the decoder works independently.

Use Windows Media Foundation virtual camera APIs:

```text
MFCreateVirtualCamera
IMFVirtualCamera
```

Expose:

```text
PhoneBridge Camera
```

---

# 5.1 Camera Output

The virtual camera should receive decoded frames from:

```text
VideoFrameBuffer
```

Do not connect the virtual camera directly to the ADB transport.

---

# 5.2 Disconnect Behavior

When Android disconnects:

```text
PhoneBridge Camera
    remains installed
    outputs black/no-signal frames
```

Do not remove the camera device.

---

# 5.3 Reconnect Behavior

After reconnect:

```text
new session
    ↓
decoder reset
    ↓
REQUEST_KEYFRAME
    ↓
receive IDR
    ↓
decode
    ↓
virtual camera resumes
```

---

# 5.4 Compatibility Testing

Test:

* Windows Camera
* OBS
* Discord
* browser WebRTC
* Microsoft Teams if available

Modern Media Foundation compatibility is the primary target.

Only introduce DirectShow legacy compatibility if actual testing proves it is necessary.

Do not add arbitrary registry entries preemptively.

---

# MILESTONE 5 ACCEPTANCE TEST

Windows applications must see:

```text
PhoneBridge Camera
```

and receive live video.

Disconnect/reconnect must not require application restart.

---

# MILESTONE 6 — SIMULTANEOUS AUDIO + VIDEO

Now combine:

```text
Video
+
Audio
```

Test:

* simultaneous streaming
* CPU
* RAM
* USB bandwidth
* latency
* buffer stability
* A/V timestamps
* clock drift

---

# 6.1 Independent Controls

The following must all work:

```text
Camera ON
Microphone ON

Camera ON
Microphone OFF

Camera OFF
Microphone ON

Camera OFF
Microphone OFF
```

Stopping one must not restart the other.

---

# MILESTONE 6 ACCEPTANCE TEST

Use:

```text
OBS
```

with:

```text
Video input:
PhoneBridge Camera

Audio input:
PhoneBridge Microphone
```

Both must work simultaneously.

---

# MILESTONE 7 — FULL FAILURE RECOVERY

This is a dedicated stress milestone.

Test every failure mode.

---

## 7.1 USB Cable Removal

```text
streaming
↓
physically unplug USB
↓
wait 5 seconds
↓
reconnect USB
```

Expected:

```text
virtual devices remain
camera = black
microphone = silence

USB returns
↓
automatic reconnection
↓
keyframe
↓
video resumes
↓
audio resumes
```

---

## 7.2 Android App Crash

Force-stop the Android application.

Expected:

```text
Windows remains alive
virtual devices remain
reconnection begins
Android restarts
session recreated
streams resume
```

---

## 7.3 ADB Server Restart

Restart ADB.

Expected:

```text
connection failure detected
ADB transport recreated
Android rediscovered
session recreated
stream resumes
```

---

## 7.4 Windows Service Restart

Restart the Windows service.

Expected:

```text
service starts
ADB connection recreated
Android handshake
virtual devices restored
streaming resumes
```

Document exactly what happens to existing application handles during service restart.

Do not claim that applications can survive a complete driver/service restart if Windows necessarily invalidates their device handle. The important requirement is that normal transient USB failures do NOT require application restart.

---

## 7.5 Decoder Failure

Force a decoder reset.

Expected:

```text
decoder reset
REQUEST_KEYFRAME
IDR received
video resumes
```

---

## 7.6 Audio Failure

Force an audio pipeline failure.

Expected:

```text
audio pipeline reset
ring buffer recreated
silence during recovery
audio resumes
```

Camera must continue operating.

---

# MILESTONE 7 ACCEPTANCE TEST

Perform all failure tests repeatedly.

The system must always converge back to:

```text
STREAMING
```

when the underlying hardware/software becomes available again.

---

# MILESTONE 8 — PERFORMANCE AND THERMAL TESTING

Only after functionality is complete.

Measure:

### Windows

* CPU usage
* RAM
* decode latency
* audio latency
* dropped frames
* audio underruns
* reconnect time

### Android

* CPU
* battery consumption
* temperature
* thermal state
* encoder performance

Test:

```text
30 minutes
1 hour
2 hours
```

---

# MILESTONE 9 — PRODUCTION HARDENING

Implement:

* Windows UI
* installer
* driver installer
* diagnostics
* logging UI
* configuration
* versioning
* documentation
* crash handling
* automatic startup if desired
* clean uninstall

---

# MILESTONE 10 — COMPATIBILITY

Test:

```text
Windows Camera
OBS
Discord
Teams
Zoom
Chrome
Edge
Firefox
```

Test different Android devices where possible.

Do not assume Samsung-specific behavior.

---

# IMPORTANT DEVELOPMENT RULE

Do NOT jump directly to Milestone 3 or 5.

The correct order is:

```text
M0  Windows connection foundation
 ↓
M1  robust reconnect
 ↓
M2  audio pipeline
 ↓
M3  virtual microphone
 ↓
M4  video pipeline
 ↓
M5  virtual camera
 ↓
M6  simultaneous A/V
 ↓
M7  failure recovery stress testing
 ↓
M8  performance/thermal
 ↓
M9  production
 ↓
M10 compatibility
```

After each milestone:

1. compile
2. run tests
3. inspect logs
4. fix problems
5. document what works
6. only then proceed

Do not silently skip a failing milestone.

---

# IMMEDIATE TASK

Start ONLY with **Milestone 0**.

Before implementing it:

1. inspect the existing repository
2. inspect the current Android protocol implementation
3. inspect `tools/pc_test_reconnect.ps1`
4. identify the exact existing packet format
5. identify the exact existing ADB transport
6. avoid creating a duplicate/incompatible protocol
7. create the Windows project structure
8. implement `ITransport`
9. implement `AdbTransport`
10. implement the streaming packet parser
11. implement the connection state machine
12. implement HELLO/device-info/capabilities handling
13. implement heartbeat
14. make the existing reconnect test pass

At the end of Milestone 0, provide:

```text
- files created
- files modified
- architecture implemented
- tests executed
- test results
- known limitations
- exact command to run the test
```

Then STOP.

Do not implement Milestone 1 until Milestone 0 is confirmed working.
