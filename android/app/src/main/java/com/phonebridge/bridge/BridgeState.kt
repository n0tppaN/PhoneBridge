package com.phonebridge.bridge

/** What the screen needs to know, derived from the status text the bridge posts. */
enum class Phase { Stopped, Waiting, PcConnected, Streaming, Error }

data class BridgeUiState(
    val phase: Phase,
    val camera: Boolean = false,
    val mic: Boolean = false,
    val detail: String = "",
)

/**
 * Maps the status strings emitted by BridgeServer/BridgeService to a [BridgeUiState].
 * Known strings: "Stopped", "Waiting for Windows (adb forward)...", "Disconnected - waiting for Windows...",
 * "Windows connected", "Connected - handshake OK", "Connected - streaming camera & mic",
 * "Connected - streaming camera", "Connected - streaming audio", "Server error: ... Retrying...".
 */
fun classifyBridgeStatus(raw: String): BridgeUiState {
    val t = raw.trim()
    return when {
        t.equals("Stopped", ignoreCase = true) -> BridgeUiState(Phase.Stopped)
        t.startsWith("Server error", ignoreCase = true) -> BridgeUiState(Phase.Error, detail = t)
        t.contains("streaming", ignoreCase = true) -> BridgeUiState(
            Phase.Streaming,
            camera = t.contains("camera", ignoreCase = true),
            mic = t.contains("mic", ignoreCase = true) || t.contains("audio", ignoreCase = true),
        )
        t.startsWith("Connected", ignoreCase = true) ||
            t.startsWith("Windows connected", ignoreCase = true) -> BridgeUiState(Phase.PcConnected)
        else -> BridgeUiState(Phase.Waiting, detail = t) // "Waiting...", "Disconnected - waiting..." and anything new
    }
}
