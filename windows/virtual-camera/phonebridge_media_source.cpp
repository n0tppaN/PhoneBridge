#include "phonebridge_media_source.h"
#include <new>
#include "phonebridge_media_stream.h"
#include "pb_ks.h"
#include "pb_log.h"
#include "pb_module.h"

namespace phonebridge {

namespace {
const char* knownIid(REFIID r) {
    if (r == IID_IUnknown) return "IUnknown";
    if (r == IID_IMFMediaSource) return "IMFMediaSource";
    if (r == IID_IMFMediaSourceEx) return "IMFMediaSourceEx";
    if (r == IID_IMFMediaEventGenerator) return "IMFMediaEventGenerator";
    if (r == IID_IMFAttributes) return "IMFAttributes";
    if (r == IID_IMFActivate) return "IMFActivate";
    if (r == IID_IMFGetService) return "IMFGetService";
    if (r == pb::kIidIKsControl) return "IKsControl";
    return "(other - see GUID; IMarshal/IAgileObject/INoMarshal are normal and harmless)";
}
}  // namespace

PhoneBridgeMediaSource::PhoneBridgeMediaSource() {
    pb::moduleCount()++;
    MFCreateAttributes(m_store.put(), 8);
    MFCreateEventQueue(m_events.put());
    PB_LOG("Source: constructed");
}

PhoneBridgeMediaSource::~PhoneBridgeMediaSource() {
    PB_LOG("Source: destroyed");
    pb::moduleCount()--;
}

HRESULT PhoneBridgeMediaSource::initialize() {
    if (!m_store || !m_events) return E_OUTOFMEMORY;

    m_store->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    m_store->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_CATEGORY, KSCATEGORY_VIDEO_CAMERA);
    m_store->SetString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, L"C\u00e2mera (PhoneBridge)");

    // ---- media type: NV12 1920x1080 @ 30 ----
    pb::ComPtr<IMFMediaType> type;
    HRESULT hr = MFCreateMediaType(type.put());
    if (FAILED(hr)) return hr;
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, 1920, 1080);
    MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, 30, 1);
    MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE_RANGE_MIN, 30, 1);
    MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE_RANGE_MAX, 30, 1);
    MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    type->SetUINT32(MF_MT_DEFAULT_STRIDE, 1920);
    type->SetUINT32(MF_MT_SAMPLE_SIZE, 1920 * 1080 * 3 / 2);
    type->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);
    type->SetUINT32(MF_MT_AVG_BITRATE, 1920u * 1080u * 12u * 30u);

    pb::ComPtr<IMFMediaType> yuy2;
    hr = MFCreateMediaType(yuy2.put());
    if (FAILED(hr)) return hr;
    hr = type->CopyAllItems(yuy2.Get());
    if (FAILED(hr)) return hr;
    yuy2->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_YUY2);
    yuy2->SetUINT32(MF_MT_DEFAULT_STRIDE, 1920 * 2);
    yuy2->SetUINT32(MF_MT_SAMPLE_SIZE, 1920 * 1080 * 2);

    IMFMediaType* types[2] = {type.Get(), yuy2.Get()};
    hr = MFCreateStreamDescriptor(0, 2, types, m_sd.put());
    if (FAILED(hr)) return hr;

    pb::ComPtr<IMFMediaTypeHandler> handler;
    hr = m_sd->GetMediaTypeHandler(handler.put());
    if (FAILED(hr)) return hr;
    handler->SetCurrentMediaType(type.Get());

    // Stream attributes expected by Frame Server custom sources (from the reference sample, as
    // remembered - if the log shows Frame Server asking for others, add them here).
    m_sd->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE);
    m_sd->SetUINT32(MF_DEVICESTREAM_STREAM_ID, 0);
    m_sd->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1);
    m_sd->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES, MFFrameSourceTypes_Color);

    IMFStreamDescriptor* sds[1] = {m_sd.Get()};
    hr = MFCreatePresentationDescriptor(1, sds, m_pd.put());
    if (FAILED(hr)) return hr;
    m_pd->SelectStream(0);

    auto* stream = new (std::nothrow) PhoneBridgeMediaStream();
    if (!stream) return E_OUTOFMEMORY;
    m_stream.attach(stream);  // takes the initial reference
    hr = stream->initialize(this, m_sd.Get());
    if (FAILED(hr)) return hr;

    PB_LOG("Source: initialized OK");
    return S_OK;
}

// ---------------- IUnknown ----------------
STDMETHODIMP PhoneBridgeMediaSource::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    HRESULT hr = S_OK;

    if (riid == IID_IUnknown || riid == IID_IMFMediaEventGenerator || riid == IID_IMFMediaSource ||
        riid == IID_IMFMediaSourceEx) {
        *ppv = static_cast<IMFMediaSourceEx*>(this);
    } else if (riid == IID_IMFActivate) {
        *ppv = static_cast<IMFActivate*>(this);
    } else if (riid == IID_IMFAttributes) {
        *ppv = static_cast<IMFAttributes*>(this);
    } else if (riid == IID_IMFGetService) {
        *ppv = static_cast<IMFGetService*>(this);
    } else if (riid == pb::kIidIKsControl) {
        *ppv = static_cast<IKsControl*>(this);
    } else {
        hr = E_NOINTERFACE;
    }
    if (SUCCEEDED(hr)) AddRef();
    PB_LOG("Source: QueryInterface %s {%s} -> 0x%08lx", knownIid(riid), pb::log::guidToString(riid).c_str(),
           static_cast<unsigned long>(hr));
    return hr;
}

STDMETHODIMP_(ULONG) PhoneBridgeMediaSource::AddRef() { return ++m_ref; }

STDMETHODIMP_(ULONG) PhoneBridgeMediaSource::Release() {
    const ULONG c = --m_ref;
    if (c == 0) delete this;
    return c;
}

// ---------------- IMFMediaEventGenerator ----------------
STDMETHODIMP PhoneBridgeMediaSource::GetEvent(DWORD flags, IMFMediaEvent** ev) {
    pb::ComPtr<IMFMediaEventQueue> q;
    { std::lock_guard<std::mutex> g(m_lock); if (m_shutdown) return MF_E_SHUTDOWN; q = m_events; }
    return q->GetEvent(flags, ev);  // may block: must not hold the lock
}
STDMETHODIMP PhoneBridgeMediaSource::BeginGetEvent(IMFAsyncCallback* cb, IUnknown* state) {
    std::lock_guard<std::mutex> g(m_lock);
    if (m_shutdown) return MF_E_SHUTDOWN;
    return m_events->BeginGetEvent(cb, state);
}
STDMETHODIMP PhoneBridgeMediaSource::EndGetEvent(IMFAsyncResult* res, IMFMediaEvent** ev) {
    std::lock_guard<std::mutex> g(m_lock);
    if (m_shutdown) return MF_E_SHUTDOWN;
    return m_events->EndGetEvent(res, ev);
}
STDMETHODIMP PhoneBridgeMediaSource::QueueEvent(MediaEventType met, REFGUID ext, HRESULT hr, const PROPVARIANT* val) {
    std::lock_guard<std::mutex> g(m_lock);
    if (m_shutdown) return MF_E_SHUTDOWN;
    return m_events->QueueEventParamVar(met, ext, hr, val);
}

// ---------------- IMFMediaSource ----------------
STDMETHODIMP PhoneBridgeMediaSource::GetCharacteristics(DWORD* c) {
    PB_LOG("Source: GetCharacteristics");
    if (!c) return E_POINTER;
    *c = MFMEDIASOURCE_IS_LIVE;
    return S_OK;
}

STDMETHODIMP PhoneBridgeMediaSource::CreatePresentationDescriptor(IMFPresentationDescriptor** pd) {
    if (!pd) return E_POINTER;
    std::lock_guard<std::mutex> g(m_lock);
    if (m_shutdown) return MF_E_SHUTDOWN;
    PB_LOG("Source: CreatePresentationDescriptor");
    return m_pd->Clone(pd);
}

STDMETHODIMP PhoneBridgeMediaSource::Start(IMFPresentationDescriptor* pd, const GUID* fmt, const PROPVARIANT* pos) {
    PB_LOG("Source: Start pd=%p fmt=%s pos.vt=%d", static_cast<void*>(pd),
           fmt ? pb::log::guidToString(*fmt).c_str() : "null", pos ? static_cast<int>(pos->vt) : -1);
    if (!pd) { PB_LOG("Source: Start -> E_INVALIDARG (pd is null)"); return E_INVALIDARG; }
    if (fmt && *fmt != GUID_NULL) return MF_E_UNSUPPORTED_TIME_FORMAT;

    std::lock_guard<std::mutex> g(m_lock);
    if (m_shutdown) return MF_E_SHUTDOWN;

    BOOL selected = FALSE;
    pb::ComPtr<IMFStreamDescriptor> sd;
    HRESULT hr = pd->GetStreamDescriptorByIndex(0, &selected, sd.put());
    if (FAILED(hr)) return hr;

    if (selected) {
        const MediaEventType evType = m_streamAnnounced ? MEUpdatedStream : MENewStream;
        m_streamAnnounced = true;
        hr = m_events->QueueEventParamUnk(evType, GUID_NULL, S_OK, static_cast<IMFMediaStream*>(static_cast<IMFMediaStream2*>(m_stream.Get())));
        if (FAILED(hr)) return hr;
        m_stream->start();
    }
    return m_events->QueueEventParamVar(MESourceStarted, GUID_NULL, S_OK, pos);
}

STDMETHODIMP PhoneBridgeMediaSource::Stop() {
    PB_LOG("Source: Stop");
    std::lock_guard<std::mutex> g(m_lock);
    if (m_shutdown) return MF_E_SHUTDOWN;
    m_stream->stop();
    return m_events->QueueEventParamVar(MESourceStopped, GUID_NULL, S_OK, nullptr);
}

STDMETHODIMP PhoneBridgeMediaSource::Pause() {
    PB_LOG("Source: Pause (live source, not supported)");
    return MF_E_INVALID_STATE_TRANSITION;
}  // live source

STDMETHODIMP PhoneBridgeMediaSource::Shutdown() {
    PB_LOG("Source: Shutdown");
    pb::ComPtr<PhoneBridgeMediaStream> stream;
    pb::ComPtr<IMFMediaEventQueue> events;
    {
        std::lock_guard<std::mutex> g(m_lock);
        if (m_shutdown) return S_OK;
        m_shutdown = true;
        stream = std::move(m_stream);  // break source<->stream reference cycle
        events = m_events;
        m_pd.reset();
        m_sd.reset();
    }
    if (stream) stream->shutdown();
    if (events) events->Shutdown();
    return S_OK;
}

// ---------------- IMFMediaSourceEx ----------------
STDMETHODIMP PhoneBridgeMediaSource::GetSourceAttributes(IMFAttributes** attrs) {
    PB_LOG("Source: GetSourceAttributes");
    if (!attrs) return E_POINTER;
    *attrs = static_cast<IMFAttributes*>(this);  // the source IS the attribute store
    AddRef();
    return S_OK;
}

STDMETHODIMP PhoneBridgeMediaSource::GetStreamAttributes(DWORD streamId, IMFAttributes** attrs) {
    PB_LOG("Source: GetStreamAttributes id=%lu", static_cast<unsigned long>(streamId));
    if (!attrs) return E_POINTER;
    std::lock_guard<std::mutex> g(m_lock);
    if (m_shutdown) return MF_E_SHUTDOWN;
    if (streamId != 0) return MF_E_INVALIDSTREAMNUMBER;
    return m_sd->QueryInterface(IID_IMFAttributes, reinterpret_cast<void**>(attrs));  // descriptor holds them
}

STDMETHODIMP PhoneBridgeMediaSource::SetD3DManager(IUnknown* mgr) {
    PB_LOG("Source: SetD3DManager mgr=%p", static_cast<void*>(mgr));
    return S_OK;  // we use system memory
}  // we use system memory

// ---------------- IMFGetService ----------------
STDMETHODIMP PhoneBridgeMediaSource::GetService(REFGUID service, REFIID riid, LPVOID* ppv) {
    if (ppv) *ppv = nullptr;
    PB_LOG("Source: GetService service={%s} riid={%s} -> unsupported", pb::log::guidToString(service).c_str(),
           pb::log::guidToString(riid).c_str());
    return MF_E_UNSUPPORTED_SERVICE;
}

// ---------------- IKsControl ----------------
STDMETHODIMP PhoneBridgeMediaSource::KsProperty(PKSPROPERTY p, ULONG pl, LPVOID d, ULONG dl, ULONG* ret) {
    return pb::handleKsProperty("Source", p, pl, d, dl, ret);
}
STDMETHODIMP PhoneBridgeMediaSource::KsMethod(PKSMETHOD m, ULONG, LPVOID, ULONG, ULONG* ret) {
    if (ret) *ret = 0;
    PB_LOG("Source: KsMethod set={%s} id=%lu -> not supported", m ? pb::log::guidToString(m->Set).c_str() : "null",
           m ? static_cast<unsigned long>(m->Id) : 0ul);
    return pb::notSupported();
}
STDMETHODIMP PhoneBridgeMediaSource::KsEvent(PKSEVENT e, ULONG, LPVOID, ULONG, ULONG* ret) {
    if (ret) *ret = 0;
    PB_LOG("Source: KsEvent set={%s} id=%lu -> not supported", e ? pb::log::guidToString(e->Set).c_str() : "null",
           e ? static_cast<unsigned long>(e->Id) : 0ul);
    return pb::notSupported();
}

// ---------------- IMFActivate ----------------
STDMETHODIMP PhoneBridgeMediaSource::ActivateObject(REFIID riid, void** ppv) {
    PB_LOG("Source: ActivateObject");
    return QueryInterface(riid, ppv);  // this object is already the media source
}
STDMETHODIMP PhoneBridgeMediaSource::ShutdownObject() { return Shutdown(); }
STDMETHODIMP PhoneBridgeMediaSource::DetachObject() { return S_OK; }

}  // namespace phonebridge
