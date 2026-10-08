// IKsControl helper shared by the media source and its stream.
// Microsoft requires IKsControl on the source and that PROPSETID_VIDCAP_VIDEOCONTROL
// is supported. Everything unknown is LOGGED and answered with "set not found";
// refine the answers from the log (see COMO_APLICAR.md).
#pragma once
#include <windows.h>
#include <ks.h>
#include <ksmedia.h>
#include <ksproxy.h>
#include <cstring>
#include "pb_log.h"

namespace pb {

// IKsControl interface ID: {28F54685-06FD-11D2-B27A-00A0C9223196}
// Written out by hand on purpose: the SDK/linker setup differs between machines and a previous
// hand-typed value (28f11f40-...) was WRONG, which made Frame Server's request for IKsControl fail.
// If the log ever shows a failing QueryInterface for a different GUID, send it - do not guess.
inline constexpr GUID kIidIKsControl =
    {0x28F54685, 0x06FD, 0x11D2, {0xB2, 0x7A, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96}};

inline HRESULT notSupported() { return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND); }

inline HRESULT handleKsPropertyImpl(const char* who, PKSPROPERTY prop, ULONG propLen, void* data,
                                ULONG dataLen, ULONG* bytesReturned) {
    if (bytesReturned) *bytesReturned = 0;
    if (!prop || propLen < sizeof(KSPROPERTY)) return E_INVALIDARG;

    PB_LOG("%s KsProperty set=%s id=%lu flags=0x%lx propLen=%lu dataLen=%lu", who,
           log::guidToString(prop->Set).c_str(), static_cast<unsigned long>(prop->Id),
           static_cast<unsigned long>(prop->Flags), propLen, dataLen);

    if (IsEqualGUID(prop->Set, PROPSETID_VIDCAP_VIDEOCONTROL)) {
        if (prop->Id == KSPROPERTY_VIDEOCONTROL_CAPS && (prop->Flags & KSPROPERTY_TYPE_GET)) {
            if (!data || dataLen < sizeof(KSPROPERTY_VIDEOCONTROL_CAPS_S))
                return HRESULT_FROM_WIN32(ERROR_MORE_DATA);
            std::memset(data, 0, sizeof(KSPROPERTY_VIDEOCONTROL_CAPS_S));  // no flip, no trigger
            if (bytesReturned) *bytesReturned = sizeof(KSPROPERTY_VIDEOCONTROL_CAPS_S);
            return S_OK;
        }
        if (prop->Id == KSPROPERTY_VIDEOCONTROL_MODE) {
            if (prop->Flags & KSPROPERTY_TYPE_SET) return S_OK;  // accept, ignore
            if (prop->Flags & KSPROPERTY_TYPE_GET) {
                if (!data || dataLen < sizeof(KSPROPERTY_VIDEOCONTROL_MODE_S))
                    return HRESULT_FROM_WIN32(ERROR_MORE_DATA);
                std::memset(data, 0, sizeof(KSPROPERTY_VIDEOCONTROL_MODE_S));
                if (bytesReturned) *bytesReturned = sizeof(KSPROPERTY_VIDEOCONTROL_MODE_S);
                return S_OK;
            }
        }
    }
    return notSupported();
}

inline HRESULT handleKsProperty(const char* who, PKSPROPERTY prop, ULONG propLen, void* data,
                                ULONG dataLen, ULONG* bytesReturned) {
    const HRESULT hr = handleKsPropertyImpl(who, prop, propLen, data, dataLen, bytesReturned);
    PB_LOG("%s KsProperty answered 0x%08lx", who, static_cast<unsigned long>(hr));
    return hr;
}

}  // namespace pb
