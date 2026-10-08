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
- Compatibilidade com OBS/DirectShow: ainda não implementada; OBS enumera filtros DirectShow, não esta source Media Foundation.
- Múltiplos consumidores simultâneos.
- Recovery após suspensão, troca de USB e vários dispositivos ADB.

## Não implementado

- Endpoint `Microfone (PhoneBridge)` selecionável em Discord/Teams.
- Driver WaveRT/SYSVAD de áudio de produção.
- Assinatura/distribuição de driver para Secure Boot.
- Testes end-to-end Windows automatizados e installer com rollback.
- Filtro DirectShow/KS x64 com output YUY2 ligado ao mesmo shared memory.

## Decisão sem VB-Audio

Não usar `waveOut` como pseudo-microfone: é saída de áudio e nunca será input do Discord. O código remove essa tentativa. A implementação correta exige um driver virtual de captura; até existir infraestrutura de assinatura e distribuição, o áudio fica limitado a transporte, ring buffer e diagnóstico.
