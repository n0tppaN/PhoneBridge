package com.phonebridge.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

// Small hand-drawn line icons (no icon library needed -> smaller, lighter app).

fun DrawScope.drawPowerGlyph(center: Offset, size: Float, color: Color, stroke: Float) {
    val r = size / 2f
    drawArc(
        color = color,
        startAngle = -60f,
        sweepAngle = 300f,
        useCenter = false,
        topLeft = Offset(center.x - r, center.y - r + size * 0.06f),
        size = Size(size, size),
        style = Stroke(width = stroke, cap = StrokeCap.Round),
    )
    drawLine(
        color = color,
        start = Offset(center.x, center.y - r * 1.05f),
        end = Offset(center.x, center.y - r * 0.15f),
        strokeWidth = stroke,
        cap = StrokeCap.Round,
    )
}

fun DrawScope.drawCameraGlyph(center: Offset, width: Float, color: Color, stroke: Float) {
    val h = width * 0.68f
    val top = center.y - h / 2f + h * 0.08f
    val left = center.x - width / 2f
    // viewfinder bump
    val bumpW = width * 0.34f
    drawRoundRect(
        color = color,
        topLeft = Offset(center.x - bumpW / 2f, top - h * 0.14f),
        size = Size(bumpW, h * 0.3f),
        cornerRadius = CornerRadius(width * 0.06f),
        style = Stroke(width = stroke),
    )
    // body
    drawRoundRect(
        color = color,
        topLeft = Offset(left, top),
        size = Size(width, h),
        cornerRadius = CornerRadius(width * 0.16f),
        style = Stroke(width = stroke),
    )
    // lens
    drawCircle(
        color = color,
        radius = h * 0.26f,
        center = Offset(center.x, top + h / 2f),
        style = Stroke(width = stroke),
    )
}

@Composable
fun IconCamera(color: Color, modifier: Modifier = Modifier, boxSize: Dp = 24.dp) {
    Canvas(modifier.size(boxSize)) {
        drawCameraGlyph(center, size.width * 0.9f, color, 1.8.dp.toPx())
    }
}

@Composable
fun IconMic(color: Color, modifier: Modifier = Modifier, boxSize: Dp = 24.dp) {
    Canvas(modifier.size(boxSize)) {
        val w = size.width
        val h = size.height
        val sw = 1.8.dp.toPx()
        drawRoundRect(
            color = color,
            topLeft = Offset(w * 0.36f, h * 0.06f),
            size = Size(w * 0.28f, h * 0.5f),
            cornerRadius = CornerRadius(w * 0.14f),
            style = Stroke(width = sw),
        )
        drawArc(
            color = color,
            startAngle = 0f,
            sweepAngle = 180f,
            useCenter = false,
            topLeft = Offset(w * 0.22f, h * 0.26f),
            size = Size(w * 0.56f, h * 0.46f),
            style = Stroke(width = sw, cap = StrokeCap.Round),
        )
        drawLine(color, Offset(w * 0.5f, h * 0.72f), Offset(w * 0.5f, h * 0.9f), strokeWidth = sw, cap = StrokeCap.Round)
        drawLine(color, Offset(w * 0.36f, h * 0.92f), Offset(w * 0.64f, h * 0.92f), strokeWidth = sw, cap = StrokeCap.Round)
    }
}

@Composable
fun IconMonitor(color: Color, modifier: Modifier = Modifier, boxSize: Dp = 24.dp) {
    Canvas(modifier.size(boxSize)) {
        val w = size.width
        val h = size.height
        val sw = 1.8.dp.toPx()
        drawRoundRect(
            color = color,
            topLeft = Offset(w * 0.08f, h * 0.14f),
            size = Size(w * 0.84f, h * 0.56f),
            cornerRadius = CornerRadius(w * 0.08f),
            style = Stroke(width = sw),
        )
        drawLine(color, Offset(w * 0.5f, h * 0.7f), Offset(w * 0.5f, h * 0.86f), strokeWidth = sw, cap = StrokeCap.Round)
        drawLine(color, Offset(w * 0.3f, h * 0.88f), Offset(w * 0.7f, h * 0.88f), strokeWidth = sw, cap = StrokeCap.Round)
    }
}
