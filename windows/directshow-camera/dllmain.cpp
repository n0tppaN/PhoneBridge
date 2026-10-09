#include "phonebridge_filter.h"
#include <wrl/client.h>
#include <iterator>

using namespace phonebridge::directshow;
using Microsoft::WRL::ComPtr;
// dllentry.cpp in strmbase supplies DllGetClassObject/DllCanUnloadNow and
// delegates construction and reference counting through these templates.
CFactoryTemplate g_Templates[] = {
    {kFriendlyName, &kFilterClsid, CameraFilter::CreateInstance, nullptr, nullptr}
};
int g_cTemplates = static_cast<int>(sizeof(g_Templates) / sizeof(g_Templates[0]));
extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);
static HMODULE g_module = nullptr;
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) g_module = module;
    return DllEntryPoint(module, reason, reserved);
}

namespace {
class ComScope {
public:
    ComScope() : hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComScope() { if (SUCCEEDED(hr)) CoUninitialize(); }
    HRESULT result() const { return hr == RPC_E_CHANGED_MODE ? S_OK : hr; }
private:
    HRESULT hr;
};
HRESULT WriteKey(const wchar_t* path, const wchar_t* value, const wchar_t* threading = nullptr) {
    HKEY key = nullptr;
    LONG error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0,
                                KEY_WRITE | KEY_WOW64_64KEY, nullptr, &key, nullptr);
    if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
    error = RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(value),
                          static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)));
    if (error == ERROR_SUCCESS && threading) {
        error = RegSetValueExW(key, L"ThreadingModel", 0, REG_SZ,
                              reinterpret_cast<const BYTE*>(threading),
                              static_cast<DWORD>((wcslen(threading) + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(key);
    return HRESULT_FROM_WIN32(error);
}
HRESULT DeleteComKey() {
    HKEY root = nullptr;
    LONG error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\CLSID", 0,
                              KEY_READ | KEY_WRITE | KEY_WOW64_64KEY, &root);
    if (error == ERROR_FILE_NOT_FOUND) return S_OK;
    if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
    error = RegDeleteTreeW(root, kClsidString);
    RegCloseKey(root);
    return error == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(error);
}
HRESULT Mapper(ComPtr<IFilterMapper2>& mapper) {
    return CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER,
                            IID_PPV_ARGS(mapper.GetAddressOf()));
}
} // namespace

STDAPI DllRegisterServer() {
    ComScope com;
    if (FAILED(com.result())) return com.result();
    wchar_t modulePath[32768]{};
    const DWORD length = GetModuleFileNameW(g_module, modulePath, static_cast<DWORD>(std::size(modulePath)));
    if (!length) return HRESULT_FROM_WIN32(GetLastError());
    if (length >= std::size(modulePath)) return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    HRESULT hr = WriteKey(kComKey, kFriendlyName);
    if (FAILED(hr)) return hr;
    hr = WriteKey(kInprocKey, modulePath, L"Both");
    if (FAILED(hr)) { DeleteComKey(); return hr; }
    ComPtr<IFilterMapper2> mapper;
    hr = Mapper(mapper);
    if (SUCCEEDED(hr)) {
        REGPINTYPES type{&MEDIATYPE_Video, &MEDIASUBTYPE_YUY2};
        REGFILTERPINS2 pin{};
        pin.dwFlags = REG_PINFLAG_B_OUTPUT;
        pin.cInstances = 1;
        pin.nMediaTypes = 1;
        pin.lpMediaType = &type;
        pin.clsPinCategory = &PIN_CATEGORY_CAPTURE;
        REGFILTER2 filter{};
        filter.dwVersion = 2;
        filter.dwMerit = MERIT_DO_NOT_USE; // capture device selected explicitly
        filter.cPins2 = 1;
        filter.rgPins2 = &pin;
        hr = mapper->RegisterFilter(kFilterClsid, kFriendlyName, nullptr,
                                     &CLSID_VideoInputDeviceCategory, kClsidString, &filter);
        if (FAILED(hr)) mapper->UnregisterFilter(&CLSID_VideoInputDeviceCategory, kClsidString, kFilterClsid);
    }
    if (FAILED(hr)) DeleteComKey();
    return hr;
}
STDAPI DllUnregisterServer() {
    ComScope com;
    if (FAILED(com.result())) return com.result();
    ComPtr<IFilterMapper2> mapper;
    HRESULT hr = Mapper(mapper);
    if (FAILED(hr)) return hr;
    hr = mapper->UnregisterFilter(&CLSID_VideoInputDeviceCategory, kClsidString, kFilterClsid);
    if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) hr = S_OK;
    if (FAILED(hr)) return hr;
    return DeleteComKey();
}
