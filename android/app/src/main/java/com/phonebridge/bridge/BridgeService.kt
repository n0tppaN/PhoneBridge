package com.phonebridge.bridge

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder

/** Foreground service that owns the [BridgeServer]. */
class BridgeService : Service() {
    private var server: BridgeServer? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        startInForeground()
        if (server == null) {
            server = BridgeServer(this) { BridgeStatus.post(it) }.also { it.start() }
        }
        // NOT_STICKY: Android 12+/14+ forbid starting camera/mic foreground services from background.
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        server?.stop()
        server = null
        BridgeStatus.post("Stopped")
        super.onDestroy()
    }

    private fun startInForeground() {
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(
            NotificationChannel(CHANNEL_ID, "PhoneBridge", NotificationManager.IMPORTANCE_LOW)
        )
        val notification = Notification.Builder(this, CHANNEL_ID)
            .setContentTitle("PhoneBridge")
            .setContentText("Ready for the Windows connection")
            .setSmallIcon(android.R.drawable.ic_menu_camera)
            .setOngoing(true)
            .build()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            startForeground(
                NOTIFICATION_ID, notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_CAMERA or ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE,
            )
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
    }

    private companion object {
        const val CHANNEL_ID = "phonebridge"
        const val NOTIFICATION_ID = 1
    }
}
