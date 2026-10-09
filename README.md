# PhoneBridge

**Use o telemóvel Android como câmara (e, em breve, microfone) do seu PC Windows — por USB, sem root, grátis e de código aberto.**

![Licença](https://img.shields.io/badge/licen%C3%A7a-GPL--3.0-blue)
![Windows](https://img.shields.io/badge/Windows-11%20x64-0078D4)
![Android](https://img.shields.io/badge/Android-10%2B-3DDC84)
![Estado](https://img.shields.io/badge/estado-experimental-orange)

> **English, in short:** PhoneBridge streams your Android phone's camera to a Windows 11 PC over USB/ADB and exposes it as a virtual
> webcam (Media Foundation). It works in Windows Camera, web apps, OBS, and Discord. GPL-3.0.

### Android App
<p align="center">
  <img src="docs/images/APP-android.png" width="300" alt="PhoneBridge Android App">
  <img src="docs/images/APP-win.png" width="500" alt="PhoneBridge Windows App">
</p>

---

## Índice

- [O que faz](#o-que-faz)
- [Estado atual](#estado-atual)
- [Como funciona](#como-funciona)
- [Requisitos](#requisitos)
- [Instalação (a partir do código)](#instala%C3%A7%C3%A3o-a-partir-do-c%C3%B3digo)
- [Utilização](#utiliza%C3%A7%C3%A3o)
- [Resolução de problemas](#resolu%C3%A7%C3%A3o-de-problemas)
- [Estrutura do projeto](#estrutura-do-projeto)
- [Desenvolvimento](#desenvolvimento)
- [Segurança e privacidade](#seguran%C3%A7a-e-privacidade)
- [Roadmap](#roadmap)
- [Contribuir](#contribuir)
- [Licença](#licen%C3%A7a)

---

## O que faz

- Transmite a **câmara traseira** do telemóvel (1080p · 30 fps, H.264 por hardware) para o PC por **USB**.
- Mostra-a no Windows como uma **câmara virtual** chamada **"Câmera (PhoneBridge)"**, compatível com OBS, Discord, Câmara do Windows e browsers.
- Tem uma **app Android** (Jetpack Compose) e uma **app Windows** (janela única, escura, com interruptores para Câmara e Microfone).
- Sem dependência de VB-Audio. Tudo corre localmente via USB.

## Estado atual

O PhoneBridge está em fase **experimental** (pré-1.0). O que funciona e o que não funciona:

| Funcionalidade | Estado |
|---|---|
| Transporte USB/ADB, handshake, reconexão | ✅ implementado |
| Captura Android (Camera2 → H.264) e descodificação no PC | ✅ implementado |
| Câmara virtual na **Câmara do Windows**, **aplicações web**, **OBS Studio** e **Discord** | ✅ testado e validado (DirectShow x64) |
| App Windows com interruptores Câmara/Microfone em direto | ✅ implementado |
| Instalação do driver da câmara a partir da própria app | ✅ implementado |
| **Microfone no Windows** | ❌ pendente (requer driver de áudio virtual assinado) |
| Múltiplos consumidores, recovery e installer com rollback | ⏳ pendente no roadmap |

Lista completa em [`BUGS_AND_ROADMAP.md`](BUGS_AND_ROADMAP.md).

## Como funciona

```
 Android (Kotlin + Compose)                         PC Windows 11
┌─────────────────────────────┐                ┌────────────────────────────────────────────┐
│ Camera2 → MediaCodec H.264  │                │ PhoneBridge.exe  (WPF + WebView2, UI React)│
│ AudioRecord → PCM 48 kHz    │                │        │ JSON por linhas (stdin/stdout)    │
│ BridgeServer (socket local) │◀── USB / ADB ─▶│ phonebridge_service.exe  (C++20)           │
└─────────────────────────────┘ adb forward    │   ConnectionManager · VideoReceiver        │
                                tcp:27183 →    │   H.264 → NV12 (Media Foundation MFT)      │
                                localabstract: │        │ memória partilhada                │
                                phonebridge    │        ▼ Global\PhoneBridgeCameraFrame     │
                                               │ phonebridge_mediasource.dll  (COM)         │
                                               │   carregada pelo Windows Camera Frame Server│
                                               └──────────────────┬─────────────────────────┘
                                                                  ▼
                                                   Câmara do Windows · OBS · Discord · browsers
```

1. A app Android escuta num socket local (`localabstract:phonebridge`) e aguarda o PC.
2. O motor do PC faz `adb forward`, liga-se, troca o handshake e pede câmara e/ou microfone ao telemóvel.
3. O vídeo H.264 é descodificado para NV12 e escrito numa **memória partilhada** de 3 posições.
4. Uma DLL COM (`phonebridge_mediasource.dll`), carregada pelo **Windows Camera Frame Server**, lê essa memória e entrega os frames às aplicações.
5. A janela do PhoneBridge arranca o motor (`--ui`) e controla-o por mensagens JSON, uma por linha.

O protocolo binário (cabeçalho de 28 bytes, pacotes de vídeo, áudio, controlo e *heartbeat*) está descrito em [`docs/spec.md`](docs/spec.md).

## Requisitos

**Telemóvel**
- Android 10 (API 29) ou superior, com **depuração USB** ativada e autorizada neste PC.

**PC**
- Windows 11 x64 (recomendado 22H2 ou posterior — a câmara virtual usa `MFCreateVirtualCamera`).
- [Android platform-tools (ADB)](https://developer.android.com/tools/releases/platform-tools) no `PATH`, ou o Android Studio instalado.
- [WebView2 Runtime](https://developer.microsoft.com/microsoft-edge/webview2/) (já vem com o Windows 11).
- [.NET 8 Desktop Runtime](https://dotnet.microsoft.com/download/dotnet/8.0) para correr a app publicada.
- Permissões de **administrador** (a câmara virtual regista uma DLL em `HKLM` e usa memória partilhada global).

## Instalação e Utilização

### 1. App Android
1. Abra a pasta `android/` no Android Studio e instale no telemóvel Android.
2. Abra a app **PhoneBridge** e toque em **Iniciar**.

### 2. Sincronização de Binários e Execução no PC
> **Nota importante:** Após compilar novas alterações no C++, lembre-se de sincronizar os binários novos para a pasta `windows\dist\PhoneBridge`.

1. Certifique-se de que o ADB está ativo (`adb forward tcp:27183 tcp:27183`).
2. Abra `PhoneBridge.exe` na pasta de distribuição (requer privilégios de administrador para registar o driver).
3. Selecione OBS, Discord ou qualquer browser — a **Câmara (PhoneBridge)** estará disponível e totalmente funcional.

## Estrutura do projeto

```
android/                 App Android (Kotlin, Compose, Camera2, MediaCodec, AudioRecord)
protocol/                Protocolo binário partilhado e parser incremental (C++)
windows/
  service/               Motor C++: transporte ADB, ligação, vídeo/áudio, canal de controlo
  virtual-camera/        DLL COM da câmara virtual (Media Foundation DirectShow x64) e memória partilhada
  app/PhoneBridge/       App Windows: janela WPF + WebView2 e lógica de controlo (C#)
ui/                      Ecrã da app Windows (React + Vite + Tailwind)
tools/                   Scripts de teste e diagnóstico (PowerShell)
docs/                    Especificação, diagnóstico, DirectShow, assinatura de drivers
```

## Segurança e privacidade

- Toda a comunicação é **local**: USB e `adb forward` para `127.0.0.1:27183`. O PhoneBridge não faz ligações à Internet.
- A app pede **administrador**: regista uma DLL COM em `HKLM` e cria a memória partilhada global.

## Roadmap

Detalhes em [`BUGS_AND_ROADMAP.md`](BUGS_AND_ROADMAP.md).

- [ ] **Microfone virtual** (driver WaveRT/SYSVAD assinado) para Discord, Teams e browsers.
- [ ] Testes de múltiplos consumidores simultâneos e recovery.
- [ ] Instalador automatizado com rollback.

## Licença

[GPL-3.0](LICENSE).
