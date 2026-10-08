package com.phonebridge.bridge

import android.content.Context
import android.net.LocalServerSocket
import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.os.Build
import android.os.PowerManager
import android.os.SystemClock
import android.util.Log
import com.phonebridge.audio.AudioRecorder
import com.phonebridge.camera.CameraEncoder
import com.phonebridge.protocol.NativeParser
import com.phonebridge.protocol.Packet
import com.phonebridge.protocol.PacketCodec
import com.phonebridge.protocol.PacketType
import com.phonebridge.protocol.Protocol
import org.json.JSONObject
import java.io.IOException
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong

enum class ConnectionState {
    DISCONNECTED,
    WAITING_FOR_CLIENT,
    HANDSHAKING,
    STREAMING,
    RECONNECTING
}

/**
 * Control-plane and audio/video streaming server.
 *
 * Listens on an abstract unix socket; Windows reaches it through
 * `adb forward tcp:<port> localabstract:phonebridge`.
 * Implements: HELLO -> HELLO_ACK + DEVICE_INFO + CAPABILITIES,
 * START_MICROPHONE / STOP_MICROPHONE, START_CAMERA / STOP_CAMERA, REQUEST_KEYFRAME,
 * HEARTBEAT -> HEARTBEAT_ACK, GOODBYE, heartbeat watchdog, thermal status monitoring,
 * and automatic return to "accept" state after any connection loss.
 */
class BridgeServer(
    private val context: Context,
    private val onStatus: (String) -> Unit
) {

    @Volatile private var running = false
    @Volatile private var state = ConnectionState.DISCONNECTED
    @Volatile private var server: LocalServerSocket? = null
    @Volatile private var activeClient: LocalSocket? = null
    @Volatile private var audioRecorder: AudioRecorder? = null
    @Volatile private var cameraEncoder: CameraEncoder? = null
    private var acceptThread: Thread? = null

    init {
        setupThermalMonitoring()
    }

    fun start() {
        if (running) return
        running = true
        state = ConnectionState.WAITING_FOR_CLIENT
        acceptThread = Thread({ acceptLoop() }, "pb-accept").also { it.start() }
    }

    fun stop() {
        running = false
        state = ConnectionState.DISCONNECTED
        stopAudio()
        stopCamera()
        activeClient?.let { closeQuietly(it) }
        try {
            LocalSocket().use { it.connect(LocalSocketAddress(Protocol.SOCKET_NAME)) }
        } catch (ignored: IOException) {
        }
        try {
            server?.close()
        } catch (ignored: IOException) {
        }
        acceptThread?.join(500)
        acceptThread = null
    }

    private fun setupThermalMonitoring() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            try {
                val pm = context.getSystemService(PowerManager::class.java)
                pm?.addThermalStatusListener { status ->
                    when (status) {
                        PowerManager.THERMAL_STATUS_NONE, PowerManager.THERMAL_STATUS_LIGHT ->
                            Log.i(TAG, "Thermal status normal: $status")
                        PowerManager.THERMAL_STATUS_MODERATE, PowerManager.THERMAL_STATUS_SEVERE ->
                            Log.w(TAG, "Thermal status elevated: $status - consider reducing bitrate/resolution")
                        PowerManager.THERMAL_STATUS_CRITICAL, PowerManager.THERMAL_STATUS_EMERGENCY, PowerManager.THERMAL_STATUS_SHUTDOWN ->
                            Log.e(TAG, "Thermal emergency! Status: $status")
                    }
                }
            } catch (e: Exception) {
                Log.w(TAG, "Failed to register thermal status listener", e)
            }
        }
    }

    private fun acceptLoop() {
        while (running) {
            var srv: LocalServerSocket? = null
            try {
                srv = LocalServerSocket(Protocol.SOCKET_NAME)
                server = srv
                state = ConnectionState.WAITING_FOR_CLIENT
                onStatus("Waiting for Windows (adb forward)...")
                while (running) {
                    val client = srv.accept()
                    if (!running) {
                        closeQuietly(client)
                        break
                    }
                    serve(client)
                    if (running) {
                        state = ConnectionState.RECONNECTING
                        onStatus("Disconnected - waiting for Windows...")
                    }
                }
            } catch (e: IOException) {
                if (!running) break
                Log.w(TAG, "server error: ${e.message}")
                onStatus("Server error: ${e.message}. Retrying...")
                SystemClock.sleep(RETRY_DELAY_MS)
            } finally {
                try {
                    srv?.close()
                } catch (ignored: IOException) {
                }
                server = null
            }
        }
    }

    private fun serve(client: LocalSocket) {
        activeClient = client
        state = ConnectionState.HANDSHAKING
        Log.i(TAG, "Windows connected")
        onStatus("Windows connected")

        val parser = NativeParser()
        val seq = AtomicInteger(0)
        val lastRx = AtomicLong(SystemClock.elapsedRealtime())
        val out = client.outputStream

        fun send(type: PacketType, payload: ByteArray = ByteArray(0), streamId: Int = Protocol.STREAM_CONTROL, timestampUs: Long = nowUs(), flags: Int = 0) {
            val raw = PacketCodec.encode(type.id, streamId or flags, timestampUs, seq.getAndIncrement(), payload)
            synchronized(out) {
                try {
                    out.write(raw)
                    out.flush()
                } catch (e: IOException) {
                    Log.w(TAG, "write error: ${e.message}")
                }
            }
        }

        fun sendAudioFrame(pcmData: ByteArray, timestampUs: Long) {
            send(
                type = PacketType.AUDIO_FRAME,
                payload = pcmData,
                streamId = Protocol.STREAM_AUDIO,
                timestampUs = timestampUs
            )
        }

        fun sendVideoFrame(h264Data: ByteArray, flags: Int, timestampUs: Long) {
            send(
                type = PacketType.VIDEO_FRAME,
                payload = h264Data,
                streamId = Protocol.STREAM_VIDEO,
                timestampUs = timestampUs,
                flags = flags
            )
        }

        val watchdog = Thread({
            try {
                while (!Thread.currentThread().isInterrupted) {
                    Thread.sleep(WATCHDOG_TICK_MS)
                    if (SystemClock.elapsedRealtime() - lastRx.get() > HEARTBEAT_TIMEOUT_MS) {
                        Log.w(TAG, "heartbeat timeout - closing connection")
                        closeQuietly(client)
                        break
                    }
                }
            } catch (ignored: InterruptedException) {
            }
        }, "pb-watchdog")
        watchdog.start()

        try {
            val input = client.inputStream
            val buf = ByteArray(READ_BUFFER_SIZE)
            while (running) {
                val n = input.read(buf)
                if (n < 0) break
                lastRx.set(SystemClock.elapsedRealtime())
                val packets = parser.feed(buf, n)
                if (packets == null) {
                    Log.e(TAG, "protocol error code=${parser.errorCode()} - dropping connection")
                    break
                }
                var goodbye = false
                for (raw in packets) {
                    if (!dispatch(PacketCodec.decode(raw), ::send, ::sendAudioFrame, ::sendVideoFrame)) goodbye = true
                }
                if (goodbye) break
            }
        } catch (e: IOException) {
            Log.w(TAG, "connection lost: ${e.message}")
        } finally {
            watchdog.interrupt()
            stopAudio()
            stopCamera()
            parser.close()
            closeQuietly(client)
            activeClient = null
            state = ConnectionState.WAITING_FOR_CLIENT
        }
    }

    /** @return false when the peer said GOODBYE. */
    private fun dispatch(
        p: Packet,
        send: (type: PacketType, payload: ByteArray, streamId: Int, timestampUs: Long, flags: Int) -> Unit,
        sendAudioFrame: (data: ByteArray, timestampUs: Long) -> Unit,
        sendVideoFrame: (data: ByteArray, flags: Int, timestampUs: Long) -> Unit
    ): Boolean {
        when (PacketType.fromId(p.type)) {
            PacketType.HELLO -> {
                send(PacketType.HELLO_ACK, json("protocol" to Protocol.VERSION, "app" to APP_VERSION), Protocol.STREAM_CONTROL, nowUs(), 0)
                send(
                    PacketType.DEVICE_INFO,
                    json(
                        "manufacturer" to Build.MANUFACTURER,
                        "model" to Build.MODEL,
                        "sdk" to Build.VERSION.SDK_INT,
                        "release" to Build.VERSION.RELEASE,
                    ),
                    Protocol.STREAM_CONTROL,
                    nowUs(),
                    0
                )
                val audioCaps = JSONObject()
                    .put("implemented", true)
                    .put("sampleRate", AudioRecorder.SAMPLE_RATE)
                    .put("channels", AudioRecorder.CHANNELS)
                    .put("bitsPerSample", AudioRecorder.BITS_PER_SAMPLE)
                    .put("format", "pcm_s16le")

                val cameraCaps = JSONObject()
                    .put("implemented", true)
                    .put("width", 1920)
                    .put("height", 1080)
                    .put("fps", 30)
                    .put("codec", "h264")

                send(
                    PacketType.CAPABILITIES,
                    json(
                        "camera" to cameraCaps,
                        "audio" to audioCaps
                    ),
                    Protocol.STREAM_CONTROL,
                    nowUs(),
                    0
                )
                state = ConnectionState.HANDSHAKING
                onStatus("Connected - handshake OK")
            }
            PacketType.START_CAMERA -> {
                Log.i(TAG, "Starting camera capture")
                if (cameraEncoder == null) {
                    val encoder = CameraEncoder(context) { data, flags, timestampUs ->
                        sendVideoFrame(data, flags, timestampUs)
                    }
                    if (encoder.start()) {
                        cameraEncoder = encoder
                        val w = encoder.width
                        val h = encoder.height
                        send(
                            PacketType.VIDEO_CONFIG,
                            json("width" to w, "height" to h, "fps" to 30, "codec" to "h264"),
                            Protocol.STREAM_VIDEO,
                            nowUs(),
                            0
                        )
                        send(
                            PacketType.CONFIG_RESPONSE,
                            json("accepted" to true, "stream" to "video"),
                            Protocol.STREAM_CONTROL,
                            nowUs(),
                            0
                        )
                    } else {
                        Log.e(TAG, "CameraEncoder failed to start")
                        send(
                            PacketType.CONFIG_RESPONSE,
                            json("accepted" to false, "stream" to "video", "reason" to "camera hardware failed"),
                            Protocol.STREAM_CONTROL,
                            nowUs(),
                            0
                        )
                    }
                } else {
                    send(
                        PacketType.CONFIG_RESPONSE,
                        json("accepted" to true, "stream" to "video", "already_running" to true),
                        Protocol.STREAM_CONTROL,
                        nowUs(),
                        0
                    )
                }
                updateStreamingStatus()
            }
            PacketType.STOP_CAMERA -> {
                Log.i(TAG, "Stopping camera capture")
                stopCamera()
                send(
                    PacketType.CONFIG_RESPONSE,
                    json("accepted" to true, "stream" to "video", "stopped" to true),
                    Protocol.STREAM_CONTROL,
                    nowUs(),
                    0
                )
                updateStreamingStatus()
            }
            PacketType.REQUEST_KEYFRAME -> {
                Log.i(TAG, "Keyframe requested by peer")
                cameraEncoder?.requestKeyframe()
            }
            PacketType.START_MICROPHONE -> {
                Log.i(TAG, "Starting audio recording")
                if (audioRecorder == null) {
                    val rec = AudioRecorder { data, timestampUs ->
                        sendAudioFrame(data, timestampUs)
                    }
                    if (rec.start()) {
                        audioRecorder = rec
                        send(
                            PacketType.AUDIO_CONFIG,
                            json(
                                "sampleRate" to AudioRecorder.SAMPLE_RATE,
                                "channels" to AudioRecorder.CHANNELS,
                                "bitsPerSample" to AudioRecorder.BITS_PER_SAMPLE,
                                "format" to "pcm_s16le"
                            ),
                            Protocol.STREAM_AUDIO,
                            nowUs(),
                            0
                        )
                        send(
                            PacketType.CONFIG_RESPONSE,
                            json("accepted" to true, "stream" to "audio"),
                            Protocol.STREAM_CONTROL,
                            nowUs(),
                            0
                        )
                    } else {
                        Log.e(TAG, "AudioRecorder failed to start")
                        send(
                            PacketType.CONFIG_RESPONSE,
                            json("accepted" to false, "stream" to "audio", "reason" to "microphone hardware failed"),
                            Protocol.STREAM_CONTROL,
                            nowUs(),
                            0
                        )
                    }
                } else {
                    send(
                        PacketType.CONFIG_RESPONSE,
                        json("accepted" to true, "stream" to "audio", "already_running" to true),
                        Protocol.STREAM_CONTROL,
                        nowUs(),
                        0
                    )
                }
                updateStreamingStatus()
            }
            PacketType.STOP_MICROPHONE -> {
                Log.i(TAG, "Stopping audio recording")
                stopAudio()
                send(
                    PacketType.CONFIG_RESPONSE,
                    json("accepted" to true, "stream" to "audio", "stopped" to true),
                    Protocol.STREAM_CONTROL,
                    nowUs(),
                    0
                )
                updateStreamingStatus()
            }
            PacketType.CONFIG_REQUEST ->
                send(PacketType.CONFIG_RESPONSE, json("accepted" to false, "reason" to "use START_MICROPHONE or START_CAMERA"), Protocol.STREAM_CONTROL, nowUs(), 0)
            PacketType.HEARTBEAT -> send(PacketType.HEARTBEAT_ACK, p.payload, Protocol.STREAM_CONTROL, nowUs(), 0)
            PacketType.GOODBYE -> return false
            else -> Log.d(TAG, "unhandled packet type=0x${p.type.toString(16)}")
        }
        return true
    }

    private fun updateStreamingStatus() {
        val camActive = cameraEncoder != null
        val micActive = audioRecorder != null
        state = if (camActive || micActive) ConnectionState.STREAMING else ConnectionState.HANDSHAKING
        when {
            camActive && micActive -> onStatus("Connected - streaming camera & mic")
            camActive -> onStatus("Connected - streaming camera")
            micActive -> onStatus("Connected - streaming audio")
            else -> onStatus("Connected - handshake OK")
        }
    }

    private fun stopAudio() {
        audioRecorder?.stop()
        audioRecorder = null
    }

    private fun stopCamera() {
        cameraEncoder?.stop()
        cameraEncoder = null
    }

    private fun json(vararg kv: Pair<String, Any>): ByteArray {
        val o = JSONObject()
        for ((k, v) in kv) o.put(k, v)
        return o.toString().toByteArray(Charsets.UTF_8)
    }

    private fun nowUs(): Long = SystemClock.elapsedRealtimeNanos() / 1000

    private fun closeQuietly(s: LocalSocket) {
        try {
            s.close()
        } catch (ignored: IOException) {
        }
    }

    private companion object {
        const val TAG = "PhoneBridge"
        const val APP_VERSION = "0.1.0"
        const val READ_BUFFER_SIZE = 64 * 1024
        const val HEARTBEAT_TIMEOUT_MS = 6000L
        const val WATCHDOG_TICK_MS = 500L
        const val RETRY_DELAY_MS = 500L
    }
}
