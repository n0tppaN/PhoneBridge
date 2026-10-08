// IMFActivate (which inherits IMFAttributes) implemented by forwarding to a real
// attribute store. The media source derives from this, so ONE object answers to
// IMFMediaSourceEx *and* IMFActivate - whichever the Frame Server asks first.
#pragma once
#include <mfapi.h>
#include <mfidl.h>
#include "pb_com.h"

namespace pb {

class AttributesActivate : public IMFActivate {
public:
    // ---- IMFAttributes (30 methods) -> m_store ----
    STDMETHODIMP GetItem(REFGUID k, PROPVARIANT* v) override { return m_store->GetItem(k, v); }
    STDMETHODIMP GetItemType(REFGUID k, MF_ATTRIBUTE_TYPE* t) override { return m_store->GetItemType(k, t); }
    STDMETHODIMP CompareItem(REFGUID k, REFPROPVARIANT v, BOOL* r) override { return m_store->CompareItem(k, v, r); }
    STDMETHODIMP Compare(IMFAttributes* o, MF_ATTRIBUTES_MATCH_TYPE t, BOOL* r) override { return m_store->Compare(o, t, r); }
    STDMETHODIMP GetUINT32(REFGUID k, UINT32* v) override { return m_store->GetUINT32(k, v); }
    STDMETHODIMP GetUINT64(REFGUID k, UINT64* v) override { return m_store->GetUINT64(k, v); }
    STDMETHODIMP GetDouble(REFGUID k, double* v) override { return m_store->GetDouble(k, v); }
    STDMETHODIMP GetGUID(REFGUID k, GUID* v) override { return m_store->GetGUID(k, v); }
    STDMETHODIMP GetStringLength(REFGUID k, UINT32* n) override { return m_store->GetStringLength(k, n); }
    STDMETHODIMP GetString(REFGUID k, LPWSTR b, UINT32 sz, UINT32* n) override { return m_store->GetString(k, b, sz, n); }
    STDMETHODIMP GetAllocatedString(REFGUID k, LPWSTR* s, UINT32* n) override { return m_store->GetAllocatedString(k, s, n); }
    STDMETHODIMP GetBlobSize(REFGUID k, UINT32* n) override { return m_store->GetBlobSize(k, n); }
    STDMETHODIMP GetBlob(REFGUID k, UINT8* b, UINT32 sz, UINT32* n) override { return m_store->GetBlob(k, b, sz, n); }
    STDMETHODIMP GetAllocatedBlob(REFGUID k, UINT8** b, UINT32* n) override { return m_store->GetAllocatedBlob(k, b, n); }
    STDMETHODIMP GetUnknown(REFGUID k, REFIID r, LPVOID* p) override { return m_store->GetUnknown(k, r, p); }
    STDMETHODIMP SetItem(REFGUID k, REFPROPVARIANT v) override { return m_store->SetItem(k, v); }
    STDMETHODIMP DeleteItem(REFGUID k) override { return m_store->DeleteItem(k); }
    STDMETHODIMP DeleteAllItems() override { return m_store->DeleteAllItems(); }
    STDMETHODIMP SetUINT32(REFGUID k, UINT32 v) override { return m_store->SetUINT32(k, v); }
    STDMETHODIMP SetUINT64(REFGUID k, UINT64 v) override { return m_store->SetUINT64(k, v); }
    STDMETHODIMP SetDouble(REFGUID k, double v) override { return m_store->SetDouble(k, v); }
    STDMETHODIMP SetGUID(REFGUID k, REFGUID v) override { return m_store->SetGUID(k, v); }
    STDMETHODIMP SetString(REFGUID k, LPCWSTR v) override { return m_store->SetString(k, v); }
    STDMETHODIMP SetBlob(REFGUID k, const UINT8* b, UINT32 sz) override { return m_store->SetBlob(k, b, sz); }
    STDMETHODIMP SetUnknown(REFGUID k, IUnknown* u) override { return m_store->SetUnknown(k, u); }
    STDMETHODIMP LockStore() override { return m_store->LockStore(); }
    STDMETHODIMP UnlockStore() override { return m_store->UnlockStore(); }
    STDMETHODIMP GetCount(UINT32* n) override { return m_store->GetCount(n); }
    STDMETHODIMP GetItemByIndex(UINT32 i, GUID* k, PROPVARIANT* v) override { return m_store->GetItemByIndex(i, k, v); }
    STDMETHODIMP CopyAllItems(IMFAttributes* dst) override { return m_store->CopyAllItems(dst); }

protected:
    ComPtr<IMFAttributes> m_store;
};

}  // namespace pb
