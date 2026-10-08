# Compatibilidade DirectShow/OBS

## Diagnóstico

A source atual é uma **Media Foundation Virtual Camera**. A Câmara do Windows e browsers modernos conseguem enumerá-la, mas o OBS no Windows usa a fonte `Video Capture Device`, baseada em DirectShow/KS. Por isso o PhoneBridge pode funcionar na Câmara do Windows e continuar ausente no dropdown do OBS.

Adicionar YUY2 à Media Foundation melhora a negociação quando o consumidor já encontrou a source, mas não cria um filtro DirectShow.

## Opções

### Filtro DirectShow user-mode

É a opção mais interessante para este projeto se o objetivo imediato for OBS sem instalar um driver kernel:

```text
shared memory NV12
       ├── Media Foundation source -> Câmara do Windows / browsers modernos
       └── DirectShow source filter -> OBS / aplicações legacy
```

Vantagens:

- não exige VB-Audio;
- não exige assinatura de driver kernel;
- pode reutilizar o `shared_frame::Reader` existente;
- resolve a enumeração no OBS através de `CLSID_VideoInputDeviceCategory`.

Limitações:

- é uma segunda implementação de source, com COM, pins, allocator, media samples e registo DirectShow;
- não substitui a Media Foundation source;
- não garante compatibilidade com aplicações que exigem uma webcam KS/PnP verdadeira;
- precisa de DLL x64, registo HKLM e testes no Windows real.

### Driver AVStream/KS

É a solução mais ampla para compatibilidade com aplicações antigas e modernas, mas é muito mais complexa: WDK, instalação, assinatura/test-signing, lifecycle de dispositivo e potencialmente Secure Boot. Não deve ser adicionada como stub.

## Estado no repositório

A Media Foundation source está implementada. O filtro DirectShow **ainda não está implementado** neste commit; o `CMakeLists.txt` não deve fingir que uma DLL DirectShow existe. A próxima implementação deve incluir:

- `IBaseFilter` e output `IPin`;
- `IEnumPins`/`IEnumMediaTypes`;
- `IAMStreamConfig` com YUY2 1920x1080@30;
- allocator e entrega de `IMediaSample` a partir de `shared::Reader`;
- `DllRegisterServer` em `CLSID_VideoInputDeviceCategory`;
- install/uninstall integrado no host;
- ferramenta de teste com `ICreateDevEnum`;
- validação OBS x64 e source múltiplas vezes.

Não chamar a source Media Foundation de “driver”: é uma DLL COM in-process server.
