#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
// Media Foundation headers FIRST, then KS headers (the other order collides in strmif.h/ksmedia.h)
#include <ks.h>
#include <ksmedia.h>
#include <ksproxy.h>
#include <mutex>
#include <atomic>
#include "pb_attributes.h"
#include "pb_com.h"

namespace phonebridge {

class PhoneBridgeMediaStream;

// One COM object that is simultaneously:
//   IMFMediaSourceEx   (required by Frame Server)
//   IKsControl         (required by Frame Server)
//   IMFActivate+IMFAttributes (optional per docs, but some Windows builds ask for it)
//   IMFGetService
class PhoneBridgeMediaSource final : public IMFMediaSourceEx,
                               public IMFGetService,
                               public IKsControl,
                               public pb::AttributesActivate {
public:
    PhoneBridgeMediaSource();
    HRESULT initialize();  // builds descriptors; call once after construction

    // IUnknown (single implementation for all bases)
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IMFMediaEventGenerator
    STDMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** ev) override;
    STDMETHODIMP BeginGetEvent(IMFAsyncCallback* cb, IUnknown* state) override;
    STDMETHODIMP EndGetEvent(IMFAsyncResult* res, IMFMediaEvent** ev) override;
    STDMETHODIMP QueueEvent(MediaEventType met, REFGUID ext, HRESULT hr, const PROPVARIANT* val) override;

    // IMFMediaSource
    STDMETHODIMP GetCharacteristics(DWORD* c) override;
    STDMETHODIMP CreatePresentationDescriptor(IMFPresentationDescriptor** pd) override;
    STDMETHODIMP Start(IMFPresentationDescriptor* pd, const GUID* fmt, const PROPVARIANT* pos) override;
    STDMETHODIMP Stop() override;
    STDMETHODIMP Pause() override;
    STDMETHODIMP Shutdown() override;

    // IMFMediaSourceEx
    STDMETHODIMP GetSourceAttributes(IMFAttributes** attrs) override;
    STDMETHODIMP GetStreamAttributes(DWORD streamId, IMFAttributes** attrs) override;
    STDMETHODIMP SetD3DManager(IUnknown* mgr) override;

    // IMFGetService
    STDMETHODIMP GetService(REFGUID service, REFIID riid, LPVOID* ppv) override;

    // IKsControl
    STDMETHODIMP KsProperty(PKSPROPERTY p, ULONG pl, LPVOID d, ULONG dl, ULONG* ret) override;
    STDMETHODIMP KsMethod(PKSMETHOD m, ULONG ml, LPVOID d, ULONG dl, ULONG* ret) override;
    STDMETHODIMP KsEvent(PKSEVENT e, ULONG el, LPVOID d, ULONG dl, ULONG* ret) override;

    // IMFActivate
    STDMETHODIMP ActivateObject(REFIID riid, void** ppv) override;
    STDMETHODIMP ShutdownObject() override;
    STDMETHODIMP DetachObject() override;

private:
    ~PhoneBridgeMediaSource();

    std::atomic<ULONG> m_ref{1};
    std::mutex m_lock;
    pb::ComPtr<IMFMediaEventQueue> m_events;
    pb::ComPtr<IMFPresentationDescriptor> m_pd;
    pb::ComPtr<IMFStreamDescriptor> m_sd;
    pb::ComPtr<PhoneBridgeMediaStream> m_stream;
    bool m_shutdown = false;
    bool m_streamAnnounced = false;
};

}  // namespace phonebridge
