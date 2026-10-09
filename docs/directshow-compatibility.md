# Câmara DirectShow user-mode / OBS x64

## Implementação e fronteiras

`phonebridge_directshow.dll` é uma segunda source **COM in-process**, não um driver kernel. Tem CLSID `{497D53E0-1D46-4E4B-A959-88A413139466}` e nome **PhoneBridge (DirectShow)**. A DLL/classe Media Foundation existente mantém o seu CLSID e implementação; não se regista um `IMFMediaSource` como `IBaseFilter`.

```text
serviço elevado -> mapping Global\\PhoneBridgeCameraFrame (NV12)
                   ├── Reader -> phonebridge_mediasource.dll -> Frame Server / MF
                   └── Reader -> NV12->YUY2 -> phonebridge_directshow.dll -> OBS x64
```

Implementado:

- `CSource`/`CSourceStream` das **BaseClasses oficiais Microsoft**: `IBaseFilter`, output `IPin`, `IEnumPins` e `IEnumMediaTypes` reais, com ownership COM, reset/clone e negociação herdada.
- Um pin de categoria `PIN_CATEGORY_CAPTURE`, exposta por `IKsPropertySet`. Sem pin preview nem property pages.
- `IAMStreamConfig`: um formato, **YUY2 1920×1080, ~30 fps** (`AvgTimePerFrame=333333` unidades de 100 ns), `FORMAT_VideoInfo`, 4 147 200 bytes por sample. Não se anunciam RGB/MJPEG, outras resoluções, strides ou frame rates não implementados. `SetFormat` exige estado parado, armazena o tipo selecionado e restringe a enumeração a esse tipo; se ligado, só aceita o tipo exato corrente. Para alterar metadados, desligar o pin primeiro. `SetFormat(NULL)` devolve `E_POINTER` (reset é opcional).
- `CBaseOutputPin` negocia `IMemAllocator` com `IMemInputPin` downstream. `DecideBufferSize` pede pelo menos três buffers completos e verifica tamanho, alinhamento, prefixo e contagem efetivos. A BaseClass faz `NotifyAllocator`, commit/decommit e `Receive`.
- Worker push: obtém `IMediaSample`, copia o NV12 através de **`shared::Reader`**, converte `Y0 U Y1 V`, define comprimento/timestamps/sync/preroll/discontinuity, entrega e liberta a referência. Chroma NV12 é replicada nas duas linhas do bloco 2×2; não altera matriz/range de cor.
- Captura só em `Run`; `Pause` não entrega samples e `GetState` devolve `VFW_S_CANT_CUE`. Pacing usa o clock do graph (fallback `GetTickCount64` sem clock), preserva timestamps crescentes em Pause→Run e descarta backlog em vez de emitir rajadas. O timestamp Android não é usado como clock DirectShow.
- Esperas por comandos interruptíveis; `AM_GBF_NOWAIT` evita bloquear no allocator BaseClasses padrão. Allocators personalizados não são obrigados a respeitar NOWAIT; Stop usa decommit para interromper `GetBuffer`. Flush downstream antes de Pause/Stop desbloqueia `Receive`; Stop decommita/junta o worker antes de destruir pins.
- Ausência de frame fresco (>1 s segundo o Reader), mapping indisponível ou leitura inconsistente: **sample preto YUY2**, não um sucesso fictício com imagem antiga. Mapping stale é fechado para permitir reabertura após reinício do writer. Preto não prova ligação ao telefone.
- `.def` exporta `DllGetClassObject`, `DllCanUnloadNow`, `DllRegisterServer`, `DllUnregisterServer`. Factory/unload são fornecidos por `dllentry.cpp`/BaseClasses. Registo COM HKLM x64 e `IFilterMapper2::RegisterFilter` em **`CLSID_VideoInputDeviceCategory`**, não na categoria legacy genérica. Unregister remove apenas esta source.

**Permissões de shared memory:** o único ajuste no código partilhado com MF é adicionar **leitura para Interactive Users (`IU`)** à ACL do writer. SYSTEM/admin continuam com escrita; LOCAL SERVICE continua com leitura. Isto permite OBS não elevado ler frames. Não dá escrita a OBS, mas permite a outros utilizadores/processos interativos da máquina ler o vídeo; não existe isolamento por aplicação/sessão. É necessário reiniciar o serviço e fechar leitores antigos para recriar o mapping com a nova ACL.

## BaseClasses / strmbase: origem e build explícitos

Os Windows SDK atuais fornecem `dshow.h`/`strmif.h` e `strmiids.lib`, mas **não garantem `streams.h`/`strmbase.lib`**. Não confundir `strmiids` (GUIDs) com `strmbase` (implementação de filtros).

Usar o repositório **oficial Microsoft** [Windows-classic-samples](https://github.com/microsoft/Windows-classic-samples/tree/master/Samples/Win7Samples/multimedia/directshow/baseclasses), diretório `Samples/Win7Samples/multimedia/directshow/baseclasses`. A revisão estática usou o commit `434f6002bdf9cf9829406c3ff2b33387982d6168` (31 fontes correspondentes ao `.vcproj`). Registar/pinar o commit do checkout na máquina de build. Não obter DLLs/libs de terceiros.

```powershell
# Developer PowerShell for VS 2022; Desktop C++, Windows 11 SDK, CMake; x64
# Download apenas de código-fonte oficial, efetuado pelo utilizador, não pelo CMake:
git clone https://github.com/microsoft/Windows-classic-samples.git C:\src\Windows-classic-samples
$base = 'C:/src/Windows-classic-samples/Samples/Win7Samples/multimedia/directshow/baseclasses'
cmake -S windows -B windows/build -A x64 `
  -DPHONEBRIDGE_BUILD_DIRECTSHOW=ON `
  "-DPHONEBRIDGE_DIRECTSHOW_BASECLASSES_DIR=$base"
cmake --build windows/build --config Release
ctest --test-dir windows/build -C Release --output-on-failure
```

`windows/cmake/DirectShowBaseClasses.cmake` descobre a pasta pelo cache `PHONEBRIDGE_DIRECTSHOW_BASECLASSES_DIR` ou variável de ambiente homónima. Exige `streams.h` e uma lista explícita dos 31 `.cpp` do projeto oficial; se faltar algo, **falha a configuração com instruções claras**. Não faz download nem procura bibliotecas binárias arbitrárias.

### Compatibilidade com VS 2022+/MSVC atual

Algumas revisões das BaseClasses oficiais ainda contêm duas declarações C++ antigas que o MSVC atual rejeita ao compilar o filtro consumidor. Depois de clonar o repositório Microsoft, executar:

```powershell
.\tools\patch_directshow_baseclasses.ps1 `
  -BaseClasses 'C:\src\Windows-classic-samples\Samples\Win7Samples\multimedia\directshow\baseclasses'
```

O script apenas remove a qualificação redundante de `CTransInPlaceFilter::Copy` em `transip.h` e do destrutor de `CAggDirectDraw` em `videoctl.h`. Não altera a lógica das BaseClasses nem instala binários.

O target `phonebridge_strmbase` constrói **`strmbase.lib` em Release / `strmbasd.lib` em Debug** a partir dessas fontes, com `/MD`/`/MDd`, Unicode e compatibilidade MSVC do sample antigo. A DLL DirectShow usa o mesmo CRT. A DLL MF preserva o `/MT` original. A DLL DirectShow Release exige o **Microsoft Visual C++ 2015–2022 Redistributable x64** (origem oficial Microsoft); não distribuir build Debug nem runtime Debug.

Alternativa histórica: abrir `baseclasses.sln`/`.vcproj` oficial, converter/retarget para VS 2022/Windows SDK e construir x64 Release/Debug. Contudo a integração PhoneBridge não consome esse binário: compila as fontes no mesmo build para impedir mistura x86/x64, CRT ou Debug/Release.

Para compilar apenas a source MF sem esta dependência:

```powershell
cmake -S windows -B windows/build-mf -A x64 -DPHONEBRIDGE_BUILD_DIRECTSHOW=OFF
cmake --build windows/build-mf --config Release
```

Este modo **não oferece dispositivo DirectShow/OBS**. O instalador da aplicação exige ambas as DLLs; o script permite explicitamente `-MediaFoundationOnly` para instalações MF-only.

## Instalação/desinstalação

1. Fechar OBS, browsers, GraphStudioNext e outros clientes (DirectShow carrega a DLL no processo consumidor).
2. Instalar VC++ Runtime x64 oficial e compilar Release.
3. PowerShell **x64 elevada**:

```powershell
.\windows\scripts\install_camera.ps1 `
  -Dll .\windows\build\Release\phonebridge_mediasource.dll `
  -DirectShowDll .\windows\build\Release\phonebridge_directshow.dll
# Reiniciar phonebridge_service.exe elevado; ligar o telefone.
.\tools\check_directshow_camera.ps1
.\windows\build\Release\phonebridge_dshow_diagnostic.exe --run
# Remover ambas as fontes, preservando serviço/app/logs:
.\windows\scripts\uninstall_camera.ps1
```

Os scripts e `DriverInstaller.cs` registam ambas as DLLs com `System32\regsvr32.exe` x64, verificam códigos de saída e reiniciam Frame Server em `finally`. O `.csproj` copia DLL DirectShow/diagnóstico se tiverem sido construídos; o instalador não declara sucesso se faltar a DLL. A instalação das duas sources **não é uma transação com rollback**; uma falha depois do primeiro registo pode deixar instalação parcial, explicitamente reportada. DLL bloqueada deve ser libertada fechando o cliente, não terminando processos à força.

## Diagnóstico e critérios de aceitação Windows

`phonebridge_dshow_diagnostic.exe` não inspeciona apenas o registo: chama **`ICreateDevEnum::CreateClassEnumerator(CLSID_VideoInputDeviceCategory)`**, lê `FriendlyName`/CLSID do moniker e ativa via `BindToObject(IBaseFilter)`. Valida um output pin, reset/clone/skip de `IEnumPins`, enumeração/reset/clone de media types, `IAMStreamConfig`, rejeição de RGB e categoria do pin. Códigos: **0** contrato enumerado/ativado, **1** erro COM/contrato, **2** PhoneBridge ausente. O wrapper PowerShell devolve **3** se não houver diagnóstico compilado, sem afirmar enumeração testada.

`--run` liga o pin diretamente ao Null Renderer e faz três ciclos Run→Pause→Run→Stop. Isto exerce conexão/allocator/lifecycle, **não verifica pixels entregues, FPS efetivo, consumo de CPU ou OBS**. Pode bloquear se um downstream/clock tiver defeito: executar num processo de diagnóstico separado e aplicar timeout externo em automação Windows.

Checklist obrigatório antes de considerar a compatibilidade validada:

- Compilar Release **e Debug** com VS 2022 x64; inspecionar exports com `dumpbin /exports`, imports/arquitetura com `dumpbin /headers /dependents` e registar Release.
- Executar diagnóstico como **utilizador normal**, com e sem serviço/telefone. Esperar preto quando sem sinal; com telefone, comprovar imagem, orientação e U/V usando chart de cor.
- OBS x64 → **Video Capture Device** → **PhoneBridge (DirectShow)** → Custom 1920×1080, YUY2, 30 fps. Testar troca de cenas, desativar/reativar source, fechar/reabrir OBS e repetir vários ciclos.
- Testar pausas/reinício do serviço, unplug USB, refresh stale e duas instâncias/consumidores; verificar ausência de deadlock e timestamps crescentes.
- Desinstalar: não voltar a enumerar/ativar este CLSID; fonte MF continua independente durante testes DirectShow. Não usar ausência do nome MF na categoria DirectShow como erro.

## Validação realizada e riscos concretos

Nesta implementação **não havia Windows, MSVC, SDK nem OBS**. Foi feita revisão estática contra assinaturas e código-fonte oficiais BaseClasses; testes portáveis GCC/ASan/UBSan do conversor e do protocolo, incluindo ordem/chroma, limites, preto e guards 1080p. Isto **não equivale a uma compilação MSVC nem a uma validação COM/OBS**.

Riscos/limitações restantes:

- Sample BaseClasses antigo: podem ser necessários ajustes de compatibilidade do checkout/SDK ao construir com VS 2022; testar ambos os configs, não baixar libs desconhecidas.
- Apenas x64, um formato fixo VideoInfo/YUY2; apps que exigem VideoInfo2, MJPEG, formato variável, webcam PnP/KS ou pin preview não são atendidas. Não há `IAMStreamControl`, seeking, áudio, controlos de exposição, nem property pages; interfaces não implementadas não são anunciadas.
- Downstream `Receive`/`BeginFlush` e `IReferenceClock::GetTime` devem cumprir seus contratos; um consumidor defeituoso ainda pode bloquear Pause/Stop. Testes de stress Windows pendentes.
- Pacing é best-effort, não hard real-time; cópia/conversão CPU 1080p e política de frame latest/duplicação quando telefone mais lento precisam de medição. A preservação de YUV não corrige eventual matriz/range de cor mal interpretada no OBS.
- ACL `IU` dá visibilidade do vídeo a processos interativos; mapping global permanece single-writer e sem isolamento multiutilizador. O Reader/seqlock original não foi reescrito nem validado aqui sob stress concorrente.
- Instalação parcial, VC++ Runtime ausente, DLL carregada por cliente e entradas de registo de arquitetura errada são modos de falha concretos; o diagnóstico separa registo de ativação.

## Referências oficiais usadas

- [How to Write a Source Filter for DirectShow](https://learn.microsoft.com/en-us/windows/win32/directshow/how-to-write-a-source-filter-for-directshow)
- [Using the DirectShow Base Classes](https://learn.microsoft.com/en-us/windows/win32/directshow/using-the-directshow-base-classes) e [código BaseClasses Microsoft](https://github.com/microsoft/Windows-classic-samples/tree/master/Samples/Win7Samples/multimedia/directshow/baseclasses)
- [Negotiating Allocators](https://learn.microsoft.com/en-us/windows/win32/directshow/negotiating-allocators)
- [IEnumPins](https://learn.microsoft.com/en-us/windows/win32/api/strmif/nn-strmif-ienumpins) / [IEnumMediaTypes](https://learn.microsoft.com/en-us/windows/win32/api/strmif/nn-strmif-ienummediatypes)
- [IMemAllocator::GetBuffer](https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-imemallocator-getbuffer), [IMemInputPin::Receive](https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-imeminputpin-receive) e [IMediaSample](https://learn.microsoft.com/en-us/windows/win32/api/strmif/nn-strmif-imediasample)
- [IAMStreamConfig](https://learn.microsoft.com/en-us/windows/win32/api/strmif/nn-strmif-iamstreamconfig) / [SetFormat](https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-iamstreamconfig-setformat)
- [Pin Requirements for Capture Filters](https://learn.microsoft.com/en-us/windows/win32/directshow/pin-requirements-for-capture-filters)
- [Producing Data in a Capture Filter](https://learn.microsoft.com/en-us/windows/win32/directshow/producing-data-in-a-capture-filter)
- [Implementing DllRegisterServer](https://learn.microsoft.com/en-us/windows/win32/directshow/implementing-dllregisterserver)
