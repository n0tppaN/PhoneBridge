package com.phonebridge.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

/** PhoneBridge palette: near-black background, high-contrast text, one blue->teal accent. */
object PB {
    val Bg = Color(0xFF0A0C10)
    val Surface = Color(0xFF12161C)
    val SurfaceHigh = Color(0xFF1A2029)
    val Border = Color(0xFF283040)

    val TextPrimary = Color(0xFFF3F6FA)    // ~17:1 on Bg
    val TextSecondary = Color(0xFFA3AEBD)  // ~8:1 on Surface
    val TextDisabled = Color(0xFF6B7686)

    val Accent = Color(0xFF5B8CFF)
    val AccentSoft = Color(0xFF1B2A4D)
    val Teal = Color(0xFF36D1C4)
    val OnAccent = Color(0xFF04121A)       // dark text on the accent gradient (~6:1)

    val Live = Color(0xFF3DDC97)
    val Warn = Color(0xFFFFB547)
    val Danger = Color(0xFFFF5C6C)
}

private val PBColors = darkColorScheme(
    primary = PB.Accent,
    onPrimary = PB.OnAccent,
    background = PB.Bg,
    onBackground = PB.TextPrimary,
    surface = PB.Surface,
    onSurface = PB.TextPrimary,
    outline = PB.Border,
    error = PB.Danger,
)

@Composable
fun PhoneBridgeTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = PBColors) {
        Surface(color = PB.Bg, contentColor = PB.TextPrimary, content = content)
    }
}
