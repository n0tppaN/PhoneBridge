# PhoneBridge — estado técnico e roadmap

## Implementado/testável

- Parser incremental e testes de chunking.
- Handshake Android/Windows e reconnect básico.
- Camera2/MediaCodec Android, decoder H.264 e shared memory NV12.
- Virtual camera Media Foundation no Windows 11.
- UI React/WebView2 e controlo de streams.
- Formatos de câmara anunciados: NV12 e YUY2.

## Experimental

- Compatibilidade com Discord e consumidores Media Foundation restritivos.
- Múltiplos consumidores simultâneos.
- Recovery após suspensão, troca de USB e vários dispositivos ADB.

## Não implementado

- Endpoint `Microfone (PhoneBridge)` selecionável em Discord/Teams.
- Driver WaveRT/SYSVAD de áudio de produção.
- Assinatura/distribuição de driver para Secure Boot.
- Testes end-to-end Windows automatizados e installer com rollback.

## Decisão sem VB-Audio

Não usar `waveOut` como pseudo-microfone: é saída de áudio e nunca será input do Discord. O código remove essa tentativa. A implementação correta exige um driver virtual de captura; até existir infraestrutura de assinatura e distribuição, o áudio fica limitado a transporte, ring buffer e diagnóstico.

## 🐛 Known Bugs & Quirks
1. **UAC Elevation Requirement**: Because the virtual camera requires registering a COM In-Process Server in `HKLM` for the Windows Camera Frame Server (`svchost.exe`), `phonebridge_service.exe` must run with Administrator privileges.
2. **Process Visibility in Task Manager**: Standard user PowerShell scripts cannot directly inspect or kill elevated C++ backend processes (`phonebridge_service.exe`) via `Get-Process` without explicit elevation handling. Handled via session state tracking in the GUI.

## 🚀 Roadmap & Future Improvements
- [ ] Implement bi-directional low-latency audio feedback (PC microphone -> Android speaker).
- [ ] Add adjustable resolution & bitrate sliders in the Android Compose UI.
- [ ] Enhance web frontend (`wwwroot`) with real-time video preview rendering using WebRTC/WebSocket streams.
- [ ] Package automated installer / MSIX installer for the Windows virtual camera driver and COM registration.
