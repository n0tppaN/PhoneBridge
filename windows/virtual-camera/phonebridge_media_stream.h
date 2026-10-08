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
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include "pb_com.h"
#include "shared_frame.h"

namespace phonebridge {

// Delivers one NV12 sample every ~33 ms, but only when Frame Server has asked for one
// (RequestSample). Frames come from the shared-memory channel written by the service;
// when there is no fresh frame it delivers a black frame ("no signal").
class PhoneBridgeMediaStream final : public IMFMediaStream2, public IKsControl {
public:
    PhoneBridgeMediaStream();
    HRESULT initialize(IMFMediaSource* parent, IMFStreamDescriptor* descriptor);
    void start();
    void stop();
    void shutdown();  // breaks the reference cycle with the source and joins the thread

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IMFMediaEventGenerator
    STDMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** ev) override;
    STDMETHODIMP BeginGetEvent(IMFAsyncCallback* cb, IUnknown* state) override;
    STDMETHODIMP EndGetEvent(IMFAsyncResult* res, IMFMediaEvent** ev) override;
    STDMETHODIMP QueueEvent(MediaEventType met, REFGUID ext, HRESULT hr, const PROPVARIANT* val) override;

    // IMFMediaStream
    STDMETHODIMP GetMediaSource(IMFMediaSource** src) override;
    STDMETHODIMP GetStreamDescriptor(IMFStreamDescriptor** sd) override;
    STDMETHODIMP RequestSample(IUnknown* token) override;

    // IMFMediaStream2
    STDMETHODIMP SetStreamState(MF_STREAM_STATE value) override;
    STDMETHODIMP GetStreamState(MF_STREAM_STATE* value) override;

    // IKsControl
    STDMETHODIMP KsProperty(PKSPROPERTY p, ULONG pl, LPVOID d, ULONG dl, ULONG* ret) override;
    STDMETHODIMP KsMethod(PKSMETHOD m, ULONG ml, LPVOID d, ULONG dl, ULONG* ret) override;
    STDMETHODIMP KsEvent(PKSEVENT e, ULONG el, LPVOID d, ULONG dl, ULONG* ret) override;

private:
    ~PhoneBridgeMediaStream();
    void deliveryLoop();
    void deliverOne(const pb::ComPtr<IUnknown>& token);

    std::atomic<ULONG> m_ref{1};
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<pb::ComPtr<IUnknown>> m_tokens;  // outstanding RequestSample tokens (may be null)
    pb::ComPtr<IMFMediaSource> m_source;
    pb::ComPtr<IMFStreamDescriptor> m_sd;
    pb::ComPtr<IMFMediaEventQueue> m_events;
    MF_STREAM_STATE m_state = MF_STREAM_STATE_STOPPED;
    bool m_started = false;
    bool m_exit = false;
    bool m_hadSignal = false;
    bool m_shutdownDone = false;
    std::thread m_thread;
    shared::Reader m_reader;  // only touched by the delivery thread
};

}  // namespace phonebridge
