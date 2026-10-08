#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <new>
#include "phonebridge_media_source.h"
#include "pb_log.h"
#include "pb_module.h"

// CLSID {E6B65C58-4D2A-4C20-9F16-368798135CC4}
static const GUID CLSID_PhoneBridgeMediaSource =
    {0xe6b65c58, 0x4d2a, 0x4c20, {0x9f, 0x16, 0x36, 0x87, 0x98, 0x13, 0x5c, 0xc4}};
static const wchar_t kClsidString[] = L"{E6B65C58-4D2A-4C20-9F16-368798135CC4}";

static HMODULE g_module = nullptr;

namespace {

// Class factory with its OWN reference count (the old one shared the module counter).
class ClassFactory final : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_ref; }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG c = --m_ref;
        if (c == 0) delete this;
        return c;
    }

    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        PB_LOG("Factory: CreateInstance riid={%s}", pb::log::guidToString(riid).c_str());

        auto* source = new (std::nothrow) phonebridge::PhoneBridgeMediaSource();  // refcount = 1
        if (!source) return E_OUTOFMEMORY;
        HRESULT hr = source->initialize();
        if (SUCCEEDED(hr)) hr = source->QueryInterface(riid, ppv);  // +1 on success
        source->Release();  // drop our initial reference (object dies if QI failed)
        PB_LOG("Factory: CreateInstance -> 0x%08lx", static_cast<unsigned long>(hr));
        return hr;
    }

    STDMETHODIMP LockServer(BOOL lock) override {
        if (lock) pb::moduleCount()++; else pb::moduleCount()--;
        return S_OK;
    }

private:
    std::atomic<ULONG> m_ref{1};
};

HRESULT writeKey(const wchar_t* subKey, const wchar_t* defaultValue, const wchar_t* threadingModel) {
    HKEY key = nullptr;
    LONG r = RegCreateKeyExW(HKEY_LOCAL_MACHINE, subKey, 0, nullptr, 0, KEY_WRITE | KEY_WOW64_64KEY, nullptr, &key, nullptr);
    if (r != ERROR_SUCCESS) return HRESULT_FROM_WIN32(r);
    r = RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(defaultValue),
                       static_cast<DWORD>((wcslen(defaultValue) + 1) * sizeof(wchar_t)));
    if (r == ERROR_SUCCESS && threadingModel)
        r = RegSetValueExW(key, L"ThreadingModel", 0, REG_SZ, reinterpret_cast<const BYTE*>(threadingModel),
                           static_cast<DWORD>((wcslen(threadingModel) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return HRESULT_FROM_WIN32(r);
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    PB_LOG("DllGetClassObject clsid={%s} riid={%s}", pb::log::guidToString(rclsid).c_str(),
           pb::log::guidToString(riid).c_str());
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (rclsid != CLSID_PhoneBridgeMediaSource) return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new (std::nothrow) ClassFactory();
    if (!factory) return E_OUTOFMEMORY;
    const HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow(void) { return pb::moduleCount() == 0 ? S_OK : S_FALSE; }

// regsvr32 must run elevated: Frame Server (LOCAL SERVICE) only sees machine-wide (HKLM) registrations.
STDAPI DllRegisterServer(void) {
    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(g_module, path, MAX_PATH) == 0) return HRESULT_FROM_WIN32(GetLastError());

    wchar_t clsidKey[160];
    wsprintfW(clsidKey, L"SOFTWARE\\Classes\\CLSID\\%s", kClsidString);
    HRESULT hr = writeKey(clsidKey, L"PhoneBridge Camera Media Source", nullptr);
    if (FAILED(hr)) return hr;

    wchar_t inprocKey[200];
    wsprintfW(inprocKey, L"SOFTWARE\\Classes\\CLSID\\%s\\InprocServer32", kClsidString);
    return writeKey(inprocKey, path, L"Both");
}

STDAPI DllUnregisterServer(void) {
    wchar_t clsidKey[160];
    wsprintfW(clsidKey, L"SOFTWARE\\Classes\\CLSID\\%s", kClsidString);
    HKEY root = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\CLSID", 0, DELETE | KEY_WOW64_64KEY | KEY_READ | KEY_WRITE, &root) != ERROR_SUCCESS)
        return S_OK;
    RegDeleteTreeW(root, kClsidString);
    RegCloseKey(root);
    return S_OK;
}
