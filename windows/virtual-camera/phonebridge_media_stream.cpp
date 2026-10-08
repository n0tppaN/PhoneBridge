#include "phonebridge_media_stream.h"
#include <chrono>
#include <cstring>
#include <new>
#include "pb_ks.h"
#include "pb_log.h"
#include "pb_module.h"

#define PB_HR(x) \
    do { \
        HRESULT _hr = (x); \
        if (FAILED(_hr)) { \
            PB_LOG("FAIL: %s -> 0x%08lX", #x, (unsigned long)_hr); \
            return; \
        } \
    } while (0)

namespace phonebridge {

PhoneBridgeMediaStream::PhoneBridgeMediaStream() {
    pb::moduleCount()++;
    MFCreateEventQueue(m_events.put());
}

PhoneBridgeMediaStream::~PhoneBridgeMediaStream() {
    shutdown();
    pb::moduleCount()--;
}

HRESULT PhoneBridgeMediaStream::initialize(IMFMediaSource* parent, IMFStreamDescriptor* descriptor) {
    if (!parent || !descriptor || !m_events) return E_INVALIDARG;
    m_source = parent;
    m_sd = descriptor;
    m_thread = std::thread([this] { deliveryLoop(); });
    return S_OK;
}

void PhoneBridgeMediaStream::start() {
    {
        std::lock_guard<std::mutex> g(m_mutex);
        m_started = true;
        m_state = MF_STREAM_STATE_RUNNING;
    }
    m_events->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, nullptr);
    m_cv.notify_all();
    PB_LOG("Stream: started");
}

void PhoneBridgeMediaStream::stop() {
    {
        std::lock_guard<std::mutex> g(m_mutex);
        m_started = false;
        m_state = MF_STREAM_STATE_STOPPED;
        m_tokens.clear();
    }
    m_events->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, nullptr);
    PB_LOG("Stream: stopped");
}

void PhoneBridgeMediaStream::shutdown() {
    {
        std::lock_guard<std::mutex> g(m_mutex);
        if (m_shutdownDone) return;
        m_shutdownDone = true;
        m_exit = true;
        m_started = false;
        m_tokens.clear();
    }
    m_cv.notify_all();
    if (m_thread.joinable() && m_thread.get_id() != std::this_thread::get_id()) m_thread.join();
    if (m_events) m_events->Shutdown();
    std::lock_guard<std::mutex> g(m_mutex);
    m_source.reset();
    m_sd.reset();
    PB_LOG("Stream: shutdown");
}

void PhoneBridgeMediaStream::deliveryLoop() {
    for (;;) {
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            m_cv.wait(lk, [&] {
                return m_exit || (m_state == MF_STREAM_STATE_RUNNING && !m_tokens.empty());
            });
            if (m_exit) return;
        }

        pb::ComPtr<IUnknown> token;
        {
            std::lock_guard<std::mutex> g(m_mutex);
            if (m_exit) return;
            if (m_state != MF_STREAM_STATE_RUNNING || m_tokens.empty()) continue;
            token = std::move(m_tokens.front());
            m_tokens.pop_front();
        }
        deliverOne(token);
    }
}

void PhoneBridgeMediaStream::deliverOne(const pb::ComPtr<IUnknown>& token) {
    PB_LOG("Stream: deliverOne() token=%p bufferBytes=%u", static_cast<void*>(token.Get()), shared::kFrameBytes);

    pb::ComPtr<IMFMediaBuffer> buffer;
    PB_HR(MFCreateMemoryBuffer(shared::kFrameBytes, buffer.put()));

    BYTE* data = nullptr;
    PB_HR(buffer->Lock(&data, nullptr, nullptr));

    int64_t tsUs = 0;
    const bool signal = m_reader.readLatest(data, &tsUs);
    if (!signal) {
        // Standalone test pattern generator: moving bright vertical bar on dark gray background
        static uint32_t counter = 0;
        counter++;
        const uint32_t w = shared::kWidth;
        const uint32_t h = shared::kHeight;
        std::memset(data, 64, size_t(w) * h);
        uint32_t barWidth = 200;
        uint32_t barX = (counter * 8) % (w - barWidth);
        for (uint32_t y = 0; y < h; ++y) {
            std::memset(data + size_t(y) * w + barX, 220, barWidth);
        }
        uint8_t* uv = data + size_t(w) * h;
        std::memset(uv, 128, size_t(w) * h / 2);
    }

    PB_HR(buffer->Unlock());
    PB_HR(buffer->SetCurrentLength(shared::kFrameBytes));

    if (signal != m_hadSignal) {
        m_hadSignal = signal;
        PB_LOG(signal ? "Stream: SIGNAL acquired (real frames from the service)"
                      : "Stream: NO SIGNAL (delivering test pattern; reader open error=%lu)",
               static_cast<unsigned long>(m_reader.lastOpenError()));
    }

    pb::ComPtr<IMFSample> sample;
    PB_HR(MFCreateSample(sample.put()));
    PB_HR(sample->AddBuffer(buffer.Get()));
    PB_HR(sample->SetSampleTime(MFGetSystemTime()));
    PB_HR(sample->SetSampleDuration(333333));
    PB_HR(sample->SetUINT32(MFSampleExtension_CleanPoint, TRUE));
    if (token) {
        PB_HR(sample->SetUnknown(MFSampleExtension_Token, token.Get()));
    }

    HRESULT hrQ = m_events->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, sample.Get());
    if (FAILED(hrQ)) {
        PB_LOG("Stream: MEMediaSample FAILED hr=0x%08lX", (unsigned long)hrQ);
    } else {
        PB_LOG("Stream: MEMediaSample queued successfully (width=%u, height=%u, token=%s)",
               shared::kWidth, shared::kHeight, token ? "yes" : "none");
    }
}

STDMETHODIMP PhoneBridgeMediaStream::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    HRESULT hr = S_OK;
    if (riid == IID_IUnknown || riid == IID_IMFMediaEventGenerator || riid == IID_IMFMediaStream ||
        riid == IID_IMFMediaStream2) {
        *ppv = static_cast<IMFMediaStream2*>(this);
    } else if (riid == pb::kIidIKsControl) {
        *ppv = static_cast<IKsControl*>(this);
    } else {
        hr = E_NOINTERFACE;
    }
    if (SUCCEEDED(hr)) AddRef();
    PB_LOG("Stream: QueryInterface {%s} -> 0x%08lx", pb::log::guidToString(riid).c_str(), static_cast<unsigned long>(hr));
    return hr;
}

STDMETHODIMP_(ULONG) PhoneBridgeMediaStream::AddRef() { return ++m_ref; }
STDMETHODIMP_(ULONG) PhoneBridgeMediaStream::Release() {
    const ULONG c = --m_ref;
    if (c == 0) delete this;
    return c;
}

STDMETHODIMP PhoneBridgeMediaStream::GetEvent(DWORD flags, IMFMediaEvent** ev) { return m_events->GetEvent(flags, ev); }
STDMETHODIMP PhoneBridgeMediaStream::BeginGetEvent(IMFAsyncCallback* cb, IUnknown* state) { return m_events->BeginGetEvent(cb, state); }
STDMETHODIMP PhoneBridgeMediaStream::EndGetEvent(IMFAsyncResult* res, IMFMediaEvent** ev) { return m_events->EndGetEvent(res, ev); }
STDMETHODIMP PhoneBridgeMediaStream::QueueEvent(MediaEventType met, REFGUID ext, HRESULT hr, const PROPVARIANT* val) {
    return m_events->QueueEventParamVar(met, ext, hr, val);
}

STDMETHODIMP PhoneBridgeMediaStream::GetMediaSource(IMFMediaSource** src) {
    PB_LOG("Stream: GetMediaSource");
    if (!src) return E_POINTER;
    std::lock_guard<std::mutex> g(m_mutex);
    if (!m_source) return MF_E_SHUTDOWN;
    *src = m_source.Get();
    (*src)->AddRef();
    return S_OK;
}

STDMETHODIMP PhoneBridgeMediaStream::GetStreamDescriptor(IMFStreamDescriptor** sd) {
    PB_LOG("Stream: GetStreamDescriptor");
    if (!sd) return E_POINTER;
    std::lock_guard<std::mutex> g(m_mutex);
    if (!m_sd) return MF_E_SHUTDOWN;
    *sd = m_sd.Get();
    (*sd)->AddRef();
    return S_OK;
}

STDMETHODIMP PhoneBridgeMediaStream::RequestSample(IUnknown* token) {
    std::lock_guard<std::mutex> g(m_mutex);
    if (m_exit) return MF_E_SHUTDOWN;
    if (m_state != MF_STREAM_STATE_RUNNING) {
        PB_LOG("Stream: RequestSample rejected - state is not RUNNING (%d)", static_cast<int>(m_state));
        return MF_E_INVALIDREQUEST;
    }
    PB_LOG("Stream: RequestSample token=%p accepted", token);
    if (m_tokens.size() >= 32) m_tokens.pop_front();
    m_tokens.emplace_back(token);
    m_cv.notify_one();
    return S_OK;
}

STDMETHODIMP PhoneBridgeMediaStream::SetStreamState(MF_STREAM_STATE value) {
    PB_LOG("Stream: SetStreamState %d", static_cast<int>(value));
    {
        std::lock_guard<std::mutex> g(m_mutex);
        m_state = value;
    }
    m_cv.notify_all();
    return S_OK;
}

STDMETHODIMP PhoneBridgeMediaStream::GetStreamState(MF_STREAM_STATE* value) {
    if (!value) return E_POINTER;
    std::lock_guard<std::mutex> g(m_mutex);
    *value = m_state;
    return S_OK;
}

STDMETHODIMP PhoneBridgeMediaStream::KsProperty(PKSPROPERTY p, ULONG pl, LPVOID d, ULONG dl, ULONG* ret) {
    return pb::handleKsProperty("Stream", p, pl, d, dl, ret);
}

STDMETHODIMP PhoneBridgeMediaStream::KsMethod(PKSMETHOD, ULONG, LPVOID, ULONG, ULONG* ret) {
    if (ret) *ret = 0;
    return pb::notSupported();
}

STDMETHODIMP PhoneBridgeMediaStream::KsEvent(PKSEVENT, ULONG, LPVOID, ULONG, ULONG* ret) {
    if (ret) *ret = 0;
    return pb::notSupported();
}

} // namespace phonebridge
