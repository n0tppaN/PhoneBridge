package com.phonebridge.camera

import android.annotation.SuppressLint
import android.content.Context
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CaptureRequest
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.os.Bundle
import android.os.Handler
import android.os.HandlerThread
import android.os.SystemClock
import android.util.Log
import android.util.Size
import android.view.Surface
import com.phonebridge.protocol.Protocol
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Camera2 + MediaCodec H.264 video capture and hardware encoding pipeline.
 */
class CameraEncoder(
    private val context: Context,
    private val onVideoFrame: (data: ByteArray, flags: Int, timestampUs: Long) -> Unit
) {
    private val isRunning = AtomicBoolean(false)
    private var cameraDevice: CameraDevice? = null
    private var captureSession: CameraCaptureSession? = null
    private var encoder: MediaCodec? = null
    private var cameraThread: HandlerThread? = null
    private var cameraHandler: Handler? = null
    private var encoderThread: HandlerThread? = null
    private var encoderHandler: Handler? = null

    var width = 1920
        private set
    var height = 1080
        private set

    fun start(): Boolean {
        if (isRunning.getAndSet(true)) return true

        cameraThread = HandlerThread("pb-camera").also { it.start() }
        cameraHandler = Handler(cameraThread!!.looper)

        encoderThread = HandlerThread("pb-encoder").also { it.start() }
        encoderHandler = Handler(encoderThread!!.looper)

        val success = try {
            initEncoderAndCamera()
        } catch (e: Exception) {
            Log.e(TAG, "Error starting CameraEncoder", e)
            false
        }

        if (!success) {
            stop()
            return false
        }
        return true
    }

    fun requestKeyframe() {
        if (!isRunning.get()) return
        try {
            val params = Bundle().apply {
                putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0)
            }
            encoder?.setParameters(params)
            Log.i(TAG, "Keyframe requested from MediaCodec")
        } catch (e: Exception) {
            Log.w(TAG, "Failed to request keyframe", e)
        }
    }

    fun stop() {
        if (!isRunning.getAndSet(false)) return

        try {
            captureSession?.close()
            captureSession = null
        } catch (e: Exception) {
            Log.w(TAG, "Error closing capture session", e)
        }

        try {
            cameraDevice?.close()
            cameraDevice = null
        } catch (e: Exception) {
            Log.w(TAG, "Error closing camera device", e)
        }

        try {
            encoder?.stop()
            encoder?.release()
            encoder = null
        } catch (e: Exception) {
            Log.w(TAG, "Error stopping MediaCodec", e)
        }

        cameraThread?.quitSafely()
        cameraThread = null
        encoderThread?.quitSafely()
        encoderThread = null

        Log.i(TAG, "CameraEncoder stopped")
    }

    @SuppressLint("MissingPermission")
    private fun initEncoderAndCamera(): Boolean {
        val cameraManager = context.getSystemService(Context.CAMERA_SERVICE) as CameraManager
        val cameraId = selectCamera(cameraManager) ?: return false

        // Configure MediaCodec H.264
        val mime = MediaFormat.MIMETYPE_VIDEO_AVC
        val format = MediaFormat.createVideoFormat(mime, width, height).apply {
            setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
            setInteger(MediaFormat.KEY_BIT_RATE, BITRATE)
            setInteger(MediaFormat.KEY_FRAME_RATE, FPS)
            setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, I_FRAME_INTERVAL_SEC)
        }

        val codec = MediaCodec.createEncoderByType(mime)
        encoder = codec

        codec.setCallback(object : MediaCodec.Callback() {
            override fun onInputBufferAvailable(codec: MediaCodec, index: Int) {}

            override fun onOutputBufferAvailable(codec: MediaCodec, index: Int, info: MediaCodec.BufferInfo) {
                if (!isRunning.get()) return
                try {
                    val outBuffer = codec.getOutputBuffer(index) ?: return
                    if (info.size > 0) {
                        outBuffer.position(info.offset)
                        val data = ByteArray(info.size)
                        outBuffer.get(data, 0, info.size)

                        var packetFlags = 0
                        if ((info.flags and MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0) {
                            packetFlags = packetFlags or Protocol.FLAG_KEYFRAME
                        }
                        if ((info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) {
                            packetFlags = packetFlags or Protocol.FLAG_CONFIG
                        }

                        val tsUs = if (info.presentationTimeUs > 0) info.presentationTimeUs else SystemClock.elapsedRealtimeNanos() / 1000
                        onVideoFrame(data, packetFlags, tsUs)
                    }
                    codec.releaseOutputBuffer(index, false)
                } catch (e: Throwable) {
                    Log.e(TAG, "Error processing MediaCodec output buffer", e)
                }
            }

            override fun onError(codec: MediaCodec, e: MediaCodec.CodecException) {
                Log.e(TAG, "MediaCodec error: ${e.message}", e)
            }

            override fun onOutputFormatChanged(codec: MediaCodec, format: MediaFormat) {
                Log.i(TAG, "MediaCodec output format changed: $format")
            }
        }, encoderHandler)

        codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        val surface = codec.createInputSurface()
        codec.start()

        // Open Camera
        val latch = CountDownLatch(1)
        var openedDevice: CameraDevice? = null

        cameraManager.openCamera(cameraId, object : CameraDevice.StateCallback() {
            override fun onOpened(camera: CameraDevice) {
                openedDevice = camera
                cameraDevice = camera
                latch.countDown()
            }

            override fun onDisconnected(camera: CameraDevice) {
                camera.close()
                if (cameraDevice == camera) cameraDevice = null
                latch.countDown()
            }

            override fun onError(camera: CameraDevice, error: Int) {
                Log.e(TAG, "Camera error $error")
                camera.close()
                if (cameraDevice == camera) cameraDevice = null
                latch.countDown()
            }
        }, cameraHandler)

        if (!latch.await(3, TimeUnit.SECONDS) || openedDevice == null) {
            Log.e(TAG, "Failed to open camera within timeout")
            return false
        }

        // Start Capture Session
        val sessionLatch = CountDownLatch(1)
        val builder = openedDevice!!.createCaptureRequest(CameraDevice.TEMPLATE_RECORD).apply {
            addTarget(surface)
            set(CaptureRequest.CONTROL_MODE, CaptureRequest.CONTROL_MODE_AUTO)
            set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_VIDEO)
        }

        openedDevice!!.createCaptureSession(listOf(surface), object : CameraCaptureSession.StateCallback() {
            override fun onConfigured(session: CameraCaptureSession) {
                captureSession = session
                try {
                    session.setRepeatingRequest(builder.build(), null, cameraHandler)
                } catch (e: Exception) {
                    Log.e(TAG, "Failed to set repeating capture request", e)
                }
                sessionLatch.countDown()
            }

            override fun onConfigureFailed(session: CameraCaptureSession) {
                Log.e(TAG, "Camera capture session configuration failed")
                sessionLatch.countDown()
            }
        }, cameraHandler)

        return sessionLatch.await(3, TimeUnit.SECONDS) && captureSession != null
    }

    private fun selectCamera(manager: CameraManager): String? {
        for (id in manager.cameraIdList) {
            val chars = manager.getCameraCharacteristics(id)
            val facing = chars.get(CameraCharacteristics.LENS_FACING)
            if (facing == CameraCharacteristics.LENS_FACING_BACK) {
                selectBestSize(chars)
                return id
            }
        }
        if (manager.cameraIdList.isNotEmpty()) {
            val id = manager.cameraIdList[0]
            selectBestSize(manager.getCameraCharacteristics(id))
            return id
        }
        return null
    }

    private fun selectBestSize(chars: CameraCharacteristics) {
        val map = chars.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP) ?: return
        val sizes = map.getOutputSizes(Surface::class.java) ?: return

        val target1080p = Size(1920, 1080)
        val target720p = Size(1280, 720)

        if (sizes.contains(target1080p)) {
            width = 1920
            height = 1080
        } else if (sizes.contains(target720p)) {
            width = 1280
            height = 720
        } else if (sizes.isNotEmpty()) {
            width = sizes[0].width
            height = sizes[0].height
        }
        Log.i(TAG, "Selected camera resolution: ${width}x${height}")
    }

    companion object {
        private const val TAG = "CameraEncoder"
        private const val FPS = 30
        private const val BITRATE = 8_000_000 // 8 Mbps
        private const val I_FRAME_INTERVAL_SEC = 2
    }
}
