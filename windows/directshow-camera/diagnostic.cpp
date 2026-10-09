#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dshow.h>
#include <uuids.h>
#include <wrl/client.h>
#include <cstdio>
#include <cwchar>
#include "phonebridge_guids.h"

using Microsoft::WRL::ComPtr;
using namespace phonebridge::directshow;
namespace {
// CLSID_NullRenderer from Qedit.h. Modern Windows SDKs no longer expose
// qedit.h/this symbol, but the built-in DirectShow filter remains available.
inline constexpr CLSID kNullRendererClsid =
    {0xC1F400A4, 0x3F08, 0x11D3, {0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37}};

void FreeType(AM_MEDIA_TYPE* type) {
    if (!type) return;
    CoTaskMemFree(type->pbFormat);
    if (type->pUnk) type->pUnk->Release();
    CoTaskMemFree(type);
}
bool Check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) { std::fprintf(stderr, "%s: HRESULT 0x%08lx\n", operation, static_cast<unsigned long>(hr)); return false; }
    return true;
}
bool CheckType(const AM_MEDIA_TYPE* type) {
    if (!type || type->majortype != MEDIATYPE_Video || type->subtype != MEDIASUBTYPE_YUY2 ||
        type->formattype != FORMAT_VideoInfo || type->cbFormat != sizeof(VIDEOINFOHEADER) ||
        !type->pbFormat || type->lSampleSize != 1920 * 1080 * 2) return false;
    const auto* video = reinterpret_cast<const VIDEOINFOHEADER*>(type->pbFormat);
    std::printf("  YUY2 %ld x %ld, frame interval %lld (100 ns), sample %lu bytes\n",
        video->bmiHeader.biWidth, video->bmiHeader.biHeight, video->AvgTimePerFrame, type->lSampleSize);
    return video->bmiHeader.biWidth == 1920 && video->bmiHeader.biHeight == 1080 &&
        video->AvgTimePerFrame == 10000000 / 30;
}
bool ValidatePin(IPin* pin) {
    ComPtr<IEnumMediaTypes> types;
    if (!Check(pin->EnumMediaTypes(types.GetAddressOf()), "EnumMediaTypes")) return false;
    AM_MEDIA_TYPE* type = nullptr;
    HRESULT hr = types->Next(1, &type, nullptr);
    bool valid = hr == S_OK && CheckType(type);
    FreeType(type); type = nullptr;
    if (!valid || types->Next(1, &type, nullptr) != S_FALSE) { FreeType(type); return false; }
    if (!Check(types->Reset(), "IEnumMediaTypes::Reset")) return false;
    ComPtr<IEnumMediaTypes> clone;
    if (!Check(types->Clone(clone.GetAddressOf()), "IEnumMediaTypes::Clone")) return false;
    hr = clone->Next(1, &type, nullptr);
    valid = hr == S_OK && CheckType(type);
    FreeType(type); type = nullptr;
    if (!valid) return false;
    ComPtr<IAMStreamConfig> config;
    if (!Check(pin->QueryInterface(IID_PPV_ARGS(config.GetAddressOf())), "IAMStreamConfig")) return false;
    int count = 0, bytes = 0;
    if (!Check(config->GetNumberOfCapabilities(&count, &bytes), "GetNumberOfCapabilities") ||
        count != 1 || bytes != sizeof(VIDEO_STREAM_CONFIG_CAPS)) return false;
    VIDEO_STREAM_CONFIG_CAPS caps{};
    hr = config->GetStreamCaps(0, &type, reinterpret_cast<BYTE*>(&caps));
    valid = Check(hr, "GetStreamCaps") && CheckType(type) &&
        caps.MinFrameInterval == 10000000 / 30 && caps.MaxFrameInterval == caps.MinFrameInterval;
    if (valid) valid = Check(config->SetFormat(type), "SetFormat (stopped)");
    if (valid) {
        GUID saved = type->subtype;
        type->subtype = MEDIASUBTYPE_RGB24;
        valid = FAILED(config->SetFormat(type));
        type->subtype = saved;
    }
    FreeType(type);
    if (!valid) return false;
    ComPtr<IKsPropertySet> properties;
    if (!Check(pin->QueryInterface(IID_PPV_ARGS(properties.GetAddressOf())), "IKsPropertySet")) return false;
    GUID category{};
    DWORD returned = 0;
    return Check(properties->Get(AMPROPSETID_Pin, AMPROPERTY_PIN_CATEGORY, nullptr, 0,
                                 &category, sizeof(category), &returned), "Pin category") &&
        returned == sizeof(GUID) && category == PIN_CATEGORY_CAPTURE;
}
bool Lifecycle(IBaseFilter* source, IPin* pin) {
    ComPtr<IGraphBuilder> graph;
    ComPtr<IBaseFilter> sink;
    ComPtr<IMediaControl> control;
    if (!Check(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER,
                               IID_PPV_ARGS(graph.GetAddressOf())), "Create graph") ||
        !Check(CoCreateInstance(kNullRendererClsid, nullptr, CLSCTX_INPROC_SERVER,
                               IID_PPV_ARGS(sink.GetAddressOf())), "Create Null Renderer") ||
        !Check(graph->AddFilter(source, L"PhoneBridge"), "Add source") ||
        !Check(graph->AddFilter(sink.Get(), L"Null Renderer"), "Add sink")) return false;
    ComPtr<IEnumPins> pins;
    ComPtr<IPin> input;
    if (!Check(sink->EnumPins(pins.GetAddressOf()), "Sink pins") ||
        pins->Next(1, input.GetAddressOf(), nullptr) != S_OK ||
        !Check(graph->ConnectDirect(pin, input.Get(), nullptr), "ConnectDirect") ||
        !Check(graph.As(&control), "IMediaControl")) return false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (!Check(control->Run(), "Run")) { control->Stop(); return false; }
        Sleep(1000);
        if (!Check(control->Pause(), "Pause")) { control->Stop(); return false; }
        FILTER_STATE state;
        if (source->GetState(0, &state) != VFW_S_CANT_CUE || state != State_Paused) { control->Stop(); return false; }
        Sleep(100);
        if (!Check(control->Run(), "Run after Pause")) { control->Stop(); return false; }
        Sleep(500);
        if (!Check(control->Stop(), "Stop")) return false;
    }
    std::puts("Lifecycle to Null Renderer: OK (not a pixel-content/OBS test)");
    return true;
}
int Diagnose(bool run) {
    ComPtr<ICreateDevEnum> devices;
    if (!Check(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                               IID_PPV_ARGS(devices.GetAddressOf())), "ICreateDevEnum")) return 1;
    ComPtr<IEnumMoniker> monikers;
    HRESULT hr = devices->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, monikers.GetAddressOf(), 0);
    if (hr == S_FALSE) { std::puts("No DirectShow video input devices."); return 2; }
    if (!Check(hr, "CreateClassEnumerator")) return 1;
    bool found = false;
    for (;;) {
        ComPtr<IMoniker> moniker;
        hr = monikers->Next(1, moniker.GetAddressOf(), nullptr);
        if (hr == S_FALSE) break;
        if (!Check(hr, "IEnumMoniker::Next")) return 1;
        ComPtr<IPropertyBag> bag;
        if (FAILED(moniker->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(bag.GetAddressOf())))) continue;
        VARIANT name; VariantInit(&name);
        if (SUCCEEDED(bag->Read(L"FriendlyName", &name, nullptr)) && name.vt == VT_BSTR)
            std::printf("Device: %ls\n", name.bstrVal);
        VariantClear(&name);
        VARIANT clsid; VariantInit(&clsid);
        GUID id{};
        bool ours = SUCCEEDED(bag->Read(L"CLSID", &clsid, nullptr)) && clsid.vt == VT_BSTR &&
            SUCCEEDED(CLSIDFromString(clsid.bstrVal, &id)) && id == kFilterClsid;
        VariantClear(&clsid);
        if (!ours) continue;
        found = true;
        ComPtr<IBaseFilter> source;
        if (!Check(moniker->BindToObject(nullptr, nullptr, IID_PPV_ARGS(source.GetAddressOf())), "BindToObject IBaseFilter")) return 1;
        ComPtr<IEnumPins> pins;
        ComPtr<IPin> pin;
        if (!Check(source->EnumPins(pins.GetAddressOf()), "EnumPins") ||
            pins->Next(1, pin.GetAddressOf(), nullptr) != S_OK) return 1;
        PIN_DIRECTION direction;
        if (!Check(pin->QueryDirection(&direction), "QueryDirection") || direction != PINDIR_OUTPUT ||
            !ValidatePin(pin.Get())) return 1;
        ComPtr<IPin> extra;
        if (pins->Next(1, extra.GetAddressOf(), nullptr) != S_FALSE) return 1;
        if (!Check(pins->Reset(), "IEnumPins::Reset")) return 1;
        ComPtr<IEnumPins> clonedPins;
        if (!Check(pins->Clone(clonedPins.GetAddressOf()), "IEnumPins::Clone")) return 1;
        ComPtr<IPin> clonedPin;
        if (clonedPins->Next(1, clonedPin.GetAddressOf(), nullptr) != S_OK ||
            clonedPin.Get() != pin.Get() || pins->Skip(1) != S_OK || pins->Skip(1) != S_FALSE) return 1;
        if (run && !Lifecycle(source.Get(), pin.Get())) return 1;
    }
    if (!found) { std::puts("PhoneBridge DirectShow NOT enumerated. Check x64 registration/DLL dependencies."); return 2; }
    std::puts("PhoneBridge DirectShow enumeration, activation and interfaces: OK");
    return 0;
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    if (argc > 2 || (argc == 2 && wcscmp(argv[1], L"--run") != 0)) {
        std::puts("Usage: phonebridge_dshow_diagnostic.exe [--run]"); return 1;
    }
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (!Check(hr, "CoInitializeEx")) return 1;
    const int result = Diagnose(argc == 2);
    CoUninitialize();
    return result;
}
