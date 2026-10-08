package com.phonebridge.audio

import android.annotation.SuppressLint
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.os.Process
import android.os.SystemClock
import android.util.Log
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Low-latency PCM 48kHz mono audio recorder using Android [AudioRecord].
 */
class AudioRecorder(
    private val onAudioFrame: (data: ByteArray, timestampUs: Long) -> Unit
) {
    private val isRecording = AtomicBoolean(false)
    private var captureThread: Thread? = null

    fun start(): Boolean {
        if (isRecording.getAndSet(true)) return true

        val minBufSize = AudioRecord.getMinBufferSize(
            SAMPLE_RATE,
            CHANNEL_CONFIG,
            AUDIO_FORMAT
        )

        if (minBufSize <= 0) {
            Log.e(TAG, "AudioRecord.getMinBufferSize failed: $minBufSize")
            isRecording.set(false)
            return false
        }

        // Buffer size: at least double minBufSize to avoid underruns
        val bufSize = minBufSize.coerceAtLeast(CHUNK_SIZE_BYTES * 4)

        captureThread = Thread({ captureLoop(bufSize) }, "pb-audio-rec").also { it.start() }
        return true
    }

    fun stop() {
        if (!isRecording.getAndSet(false)) return
        captureThread?.join(500)
        captureThread = null
    }

    @SuppressLint("MissingPermission")
    private fun captureLoop(bufSize: Int) {
        Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO)

        var record: AudioRecord? = createAudioRecord(bufSize)
        if (record == null) {
            Log.e(TAG, "Failed to create initialized AudioRecord")
            isRecording.set(false)
            return
        }

        try {
            record.startRecording()
            Log.i(TAG, "Audio recording started (48kHz mono PCM)")

            val chunk = ByteArray(CHUNK_SIZE_BYTES)
            while (isRecording.get()) {
                var readTotal = 0
                while (readTotal < CHUNK_SIZE_BYTES && isRecording.get()) {
                    val n = record.read(chunk, readTotal, CHUNK_SIZE_BYTES - readTotal)
                    if (n > 0) {
                        readTotal += n
                    } else {
                        Log.w(TAG, "AudioRecord read non-positive code: $n")
                        SystemClock.sleep(10)
                        break
                    }
                }

                if (readTotal == CHUNK_SIZE_BYTES && isRecording.get()) {
                    val timestampUs = SystemClock.elapsedRealtimeNanos() / 1000
                    onAudioFrame(chunk.clone(), timestampUs)
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Audio capture loop exception", e)
        } finally {
            try {
                if (record.recordingState == AudioRecord.RECORDSTATE_RECORDING) {
                    record.stop()
                }
                record.release()
            } catch (e: Exception) {
                Log.w(TAG, "Error releasing AudioRecord", e)
            }
            isRecording.set(false)
            Log.i(TAG, "Audio recording stopped")
        }
    }

    @SuppressLint("MissingPermission")
    private fun createAudioRecord(bufSize: Int): AudioRecord? {
        val sources = intArrayOf(
            MediaRecorder.AudioSource.VOICE_RECOGNITION,
            MediaRecorder.AudioSource.MIC,
            MediaRecorder.AudioSource.CAMCORDER,
            MediaRecorder.AudioSource.DEFAULT
        )

        for (src in sources) {
            try {
                val rec = AudioRecord(src, SAMPLE_RATE, CHANNEL_CONFIG, AUDIO_FORMAT, bufSize)
                if (rec.state == AudioRecord.STATE_INITIALIZED) {
                    Log.i(TAG, "AudioRecord initialized with source $src")
                    return rec
                }
                rec.release()
            } catch (e: Exception) {
                Log.w(TAG, "Failed to initialize AudioRecord with source $src", e)
            }
        }
        return null
    }

    companion object {
        private const val TAG = "AudioRecorder"
        const val SAMPLE_RATE = 48000
        const val CHANNELS = 1
        const val BITS_PER_SAMPLE = 16
        private const val CHANNEL_CONFIG = AudioFormat.CHANNEL_IN_MONO
        private const val AUDIO_FORMAT = AudioFormat.ENCODING_PCM_16BIT

        // 20ms chunk at 48kHz 16-bit mono = 48000 * (16/8) * 1 * 0.020 = 1920 bytes
        const val CHUNK_SIZE_BYTES = 1920
    }
}
