# PhoneBridge — estado técnico e roadmap

## Implementado e Validado

- Parser incremental e testes de chunking.
- Handshake Android/Windows e reconnect básico.
- Camera2/MediaCodec Android, decoder H.264 e shared memory NV12.
- Virtual camera Media Foundation no Windows 11.
- Câmara funcional na Câmara do Windows, aplicações web, OBS Studio e Discord.
- DirectShow x64 compilado e validado.
- Solução independente de VB-Audio.
- UI React/WebView2 e controlo de streams.
- Formatos de câmara anunciados: NV12 e YUY2.

## Pendentes / Roadmap

- **Microfone virtual**: Endpoint de áudio selecionável em Discord/Teams (Driver WaveRT/SYSVAD e assinatura).
- **Testes de robustez**: Múltiplos consumidores simultâneos e recovery após suspensão / troca de USB.
- **Instalação**: Installer com rollback e automatização.

## Decisão sem VB-Audio

Não usar `waveOut` como pseudo-microfone: é saída de áudio e nunca será input do Discord. A implementação correta exige um driver virtual de captura dedicado.
