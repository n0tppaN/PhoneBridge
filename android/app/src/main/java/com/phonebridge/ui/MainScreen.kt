package com.phonebridge.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.TweenSpec
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.phonebridge.bridge.BridgeStatus
import com.phonebridge.bridge.Phase
import com.phonebridge.bridge.classifyBridgeStatus

@Composable
fun MainScreen(
    onStartService: () -> Unit,
    onStopService: () -> Unit,
    modifier: Modifier = Modifier
) {
    val rawStatus by BridgeStatus.flow.collectAsState()
    val uiState = remember(rawStatus) { classifyBridgeStatus(rawStatus) }

    val statusColor by animateColorAsState(
        targetValue = when (uiState.phase) {
            Phase.Streaming -> PB.Live
            Phase.PcConnected -> PB.Teal
            Phase.Waiting -> PB.Warn
            Phase.Error -> PB.Danger
            Phase.Stopped -> PB.TextDisabled
        },
        animationSpec = TweenSpec(durationMillis = 300),
        label = "statusColor"
    )

    PhoneBridgeTheme {
        Column(
            modifier = modifier
                .fillMaxSize()
                .background(PB.Bg)
                .padding(24.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.SpaceBetween
        ) {
            // Header
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "PhoneBridge",
                    fontSize = 22.sp,
                    fontWeight = FontWeight.Bold,
                    color = PB.TextPrimary
                )

                // Indicador de Estado (Badge)
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier
                        .clip(RoundedCornerShape(12.dp))
                        .background(PB.Surface)
                        .border(1.dp, PB.Border, RoundedCornerShape(12.dp))
                        .padding(horizontal = 12.dp, vertical = 6.dp)
                ) {
                    Box(
                        modifier = Modifier
                            .size(8.dp)
                            .clip(CircleShape)
                            .background(statusColor)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = uiState.phase.name,
                        fontSize = 12.sp,
                        fontWeight = FontWeight.Medium,
                        color = PB.TextSecondary
                    )
                }
            }

            // Card Central com Estado e Detalhes
            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center,
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f)
                    .padding(vertical = 24.dp)
                    .clip(RoundedCornerShape(24.dp))
                    .background(PB.Surface)
                    .border(1.dp, PB.Border, RoundedCornerShape(24.dp))
                    .padding(24.dp)
            ) {
                // Ícone Principal de Monitor
                IconMonitor(color = statusColor, boxSize = 64.dp)

                Spacer(modifier = Modifier.height(24.dp))

                Text(
                    text = rawStatus,
                    fontSize = 15.sp,
                    fontWeight = FontWeight.Medium,
                    color = PB.TextPrimary,
                    modifier = Modifier.padding(horizontal = 16.dp)
                )

                AnimatedVisibility(visible = uiState.phase == Phase.Streaming) {
                    Row(
                        modifier = Modifier.padding(top = 20.dp),
                        horizontalArrangement = Arrangement.spacedBy(16.dp)
                    ) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            IconCamera(color = if (uiState.camera) PB.Live else PB.TextDisabled, boxSize = 20.dp)
                            Spacer(modifier = Modifier.width(6.dp))
                            Text("Vídeo", fontSize = 13.sp, color = PB.TextSecondary)
                        }
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            IconMic(color = if (uiState.mic) PB.Live else PB.TextDisabled, boxSize = 20.dp)
                            Spacer(modifier = Modifier.width(6.dp))
                            Text("Áudio", fontSize = 13.sp, color = PB.TextSecondary)
                        }
                    }
                }
            }

            // Botão Principal Ligar / Desligar
            Button(
                onClick = {
                    if (uiState.phase == Phase.Stopped) onStartService() else onStopService()
                },
                modifier = Modifier
                    .fillMaxWidth()
                    .height(56.dp),
                shape = RoundedCornerShape(16.dp),
                colors = ButtonDefaults.buttonColors(
                    containerColor = if (uiState.phase == Phase.Stopped) PB.Accent else PB.SurfaceHigh,
                    contentColor = if (uiState.phase == Phase.Stopped) PB.OnAccent else PB.Danger
                )
            ) {
                Text(
                    text = if (uiState.phase == Phase.Stopped) "Iniciar Ponte" else "Parar Serviço",
                    fontSize = 16.sp,
                    fontWeight = FontWeight.Bold
                )
            }
        }
    }
}