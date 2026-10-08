# PhoneBridge Desktop UI

A interface `tools/phonebridge_gui.ps1` replica a linguagem visual da app Android:

- fundo escuro e cartões com bordas discretas;
- estado global `Stopped` / `Running`;
- deteção real do telefone através de `adb devices`;
- botão de início bloqueado sem telefone Android ligado;
- seleção independente de `Câmera virtual`, `Microfone virtual` ou ambos;
- estado individual `Ativa/Desligada` e `Ativo/Desligado`;
- arranque e paragem do `phonebridge_service.exe`.

## Utilização

1. Compile os binários Windows normalmente.
2. Confirme que existe um destes executáveis:

   - `windows/build-v7-main/Release/phonebridge_service.exe`
   - `windows/build/Release/phonebridge_service.exe`
   - `windows/build/x64/Release/phonebridge_service.exe`

3. Ligue o telefone por USB e aceite a autorização de depuração USB.
4. Execute no PowerShell:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\tools\phonebridge_gui.ps1
```

O serviço recebe um dos modos seguintes, conforme a seleção:

```text
--camera       apenas vídeo
--microphone   apenas áudio
--both         vídeo e áudio
```

O `h264_decoder.cpp` não é alterado pela funcionalidade da UI.
