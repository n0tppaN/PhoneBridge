package com.phonebridge.bridge

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Observable status holder (service -> UI). Safe to call [post] from any thread.
 * The UI observes [flow]; the service keeps calling post("...") exactly as before.
 */
object BridgeStatus {
    private val _flow = MutableStateFlow("Stopped")
    val flow: StateFlow<String> = _flow.asStateFlow()

    val text: String get() = _flow.value

    fun post(s: String) {
        _flow.value = s
    }
}
