#include "phonebridge_filter.h"
#include "nv12_to_yuy2.h"
#include <algorithm>
#include <cstring>
#include <new>

namespace phonebridge::directshow {
CUnknown* WINAPI CameraFilter::CreateInstance(LPUNKNOWN outer, HRESULT* result) {
    if (!result) return nullptr;
    *result = S_OK;
    try {
        auto* filter = new (std::nothrow) CameraFilter(outer, result);
        if (!filter) *result = E_OUTOFMEMORY;
        return filter;
    } catch (const std::bad_alloc&) {
        *result = E_OUTOFMEMORY;
        return nullptr;
    }
}
CameraFilter::CameraFilter(LPUNKNOWN outer, HRESULT* result)
    : CSource(NAME("PhoneBridge DirectShow Camera"), outer, kFilterClsid, result) {
    m_pin = new (std::nothrow) CapturePin(result, this);
    if (!m_pin) *result = E_OUTOFMEMORY;
    else if (GetPinCount() == 0) {
        // AddPin failed: CSource does not own this allocation.
        delete m_pin;
        m_pin = nullptr;
    }
}
CameraFilter::~CameraFilter() {
    // Stop before CSource deletes its pins. No worker may outlive this DLL.
    CSource::Stop();
}
STDMETHODIMP CameraFilter::GetState(DWORD timeout, FILTER_STATE* state) {
    if (!state) return E_POINTER;
    CAutoLock lock(pStateLock());
    const HRESULT hr = CSource::GetState(timeout, state);
    return SUCCEEDED(hr) && *state == State_Paused ? VFW_S_CANT_CUE : hr;
}
STDMETHODIMP CameraFilter::Run(REFERENCE_TIME start) {
    CAutoLock lock(pStateLock());
    if (m_State == State_Running && m_pin) {
        // Repeated Run can change the graph's stream-time origin. Quiesce the
        // worker before replacing that origin/fallback pacing state.
        const HRESULT pause = m_pin->PauseCapture();
        if (FAILED(pause)) return pause;
    }
    HRESULT hr = CSource::Run(start); // commits allocator via Pause when stopped
    if (SUCCEEDED(hr) && m_pin) hr = m_pin->StartCapture(start, m_pClock);
    return hr;
}
STDMETHODIMP CameraFilter::Pause() {
    CAutoLock lock(pStateLock());
    if (m_State == State_Running && m_pin) {
        const HRESULT hr = m_pin->PauseCapture();
        if (FAILED(hr)) return hr;
    }
    return CSource::Pause();
}
STDMETHODIMP CameraFilter::SetSyncSource(IReferenceClock* clock) {
    CAutoLock lock(pStateLock());
    const HRESULT hr = CSource::SetSyncSource(clock);
    if (SUCCEEDED(hr) && m_pin) m_pin->UpdateClock(clock);
    return hr;
}

CapturePin::CapturePin(HRESULT* result, CameraFilter* filter)
    : CSourceStream(NAME("PhoneBridge Capture"), result, filter, L"Capture") {
    // CSourceStream registers this pin in its base constructor. Do not throw
    // after registration: the owning CSource must be able to delete the pin.
    if (FAILED(*result)) return;
    try { m_nv12.resize(shared::kFrameBytes); }
    catch (const std::bad_alloc&) { *result = E_OUTOFMEMORY; return; }
    *result = MakeType(&m_preferred);
}
CapturePin::~CapturePin() { UpdateClock(nullptr); }
STDMETHODIMP CapturePin::NonDelegatingQueryInterface(REFIID iid, void** object) {
    if (!object) return E_POINTER;
    if (iid == IID_IAMStreamConfig) return GetInterface(static_cast<IAMStreamConfig*>(this), object);
    if (iid == IID_IKsPropertySet) return GetInterface(static_cast<IKsPropertySet*>(this), object);
    return CSourceStream::NonDelegatingQueryInterface(iid, object);
}
HRESULT CapturePin::MakeType(CMediaType* type) const {
    if (!type) return E_POINTER;
    FreeMediaType(*type);
    type->InitMediaType();
    auto* video = reinterpret_cast<VIDEOINFOHEADER*>(type->AllocFormatBuffer(sizeof(VIDEOINFOHEADER)));
    if (!video) return E_OUTOFMEMORY;
    std::memset(video, 0, sizeof(*video));
    video->AvgTimePerFrame = kFrameInterval;
    video->dwBitRate = static_cast<DWORD>(kSampleBytes * 8 * 30);
    video->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    video->bmiHeader.biWidth = kWidth;
    video->bmiHeader.biHeight = kHeight; // YUV is top-down even with positive biHeight
    video->bmiHeader.biPlanes = 1;
    video->bmiHeader.biBitCount = 16;
    video->bmiHeader.biCompression = MAKEFOURCC('Y', 'U', 'Y', '2');
    video->bmiHeader.biSizeImage = kSampleBytes;
    type->SetType(&MEDIATYPE_Video);
    type->SetSubtype(&MEDIASUBTYPE_YUY2);
    type->SetFormatType(&FORMAT_VideoInfo);
    type->SetTemporalCompression(FALSE);
    type->SetSampleSize(kSampleBytes);
    return S_OK;
}
HRESULT CapturePin::GetMediaType(int position, CMediaType* type) {
    if (!type) return E_POINTER;
    if (position < 0) return E_INVALIDARG;
    if (position > 0) return VFW_S_NO_MORE_ITEMS;
    CAutoLock lock(m_pFilter->pStateLock());
    return type->Set(m_preferred);
}
HRESULT CapturePin::CheckMediaType(const CMediaType* type) {
    if (!type) return E_POINTER;
    CAutoLock lock(m_pFilter->pStateLock());
    if (*type->Type() != MEDIATYPE_Video || *type->Subtype() != MEDIASUBTYPE_YUY2 ||
        *type->FormatType() != FORMAT_VideoInfo || type->FormatLength() != sizeof(VIDEOINFOHEADER) ||
        !type->Format() || !type->IsFixedSize() || type->IsTemporalCompressed() ||
        type->GetSampleSize() != kSampleBytes || type->pUnk) return VFW_E_TYPE_NOT_ACCEPTED;
    const auto* video = reinterpret_cast<const VIDEOINFOHEADER*>(type->Format());
    const auto& bitmap = video->bmiHeader;
    const RECT full{0, 0, kWidth, kHeight};
    const auto validRect = [&full](const RECT& r) {
        const bool unspecified = r.left == 0 && r.top == 0 && r.right == 0 && r.bottom == 0;
        return unspecified || EqualRect(&r, &full);
    };
    if (bitmap.biSize != sizeof(BITMAPINFOHEADER) || bitmap.biWidth != kWidth ||
    bitmap.biHeight != kHeight || bitmap.biPlanes != 1 || bitmap.biBitCount != 16 ||
        bitmap.biCompression != MAKEFOURCC('Y', 'U', 'Y', '2') || bitmap.biSizeImage != kSampleBytes ||
        video->AvgTimePerFrame != kFrameInterval || !validRect(video->rcSource) ||
        !validRect(video->rcTarget)) return VFW_E_TYPE_NOT_ACCEPTED;
    if (m_formatSelected && *type != m_preferred) return VFW_E_TYPE_NOT_ACCEPTED;
    return S_OK;
}
STDMETHODIMP CapturePin::SetFormat(AM_MEDIA_TYPE* type) {
    if (!type) return E_POINTER; // NULL reset is optional; not exposed here
    CAutoLock lock(m_pFilter->pStateLock());
    if (!m_pFilter->IsStopped()) return VFW_E_NOT_STOPPED;
    // Validate structure before the BaseClass copy reads its format block.
    if (type->cbFormat != sizeof(VIDEOINFOHEADER) || !type->pbFormat || type->pUnk)
        return VFW_E_INVALIDMEDIATYPE;
    HRESULT hr = S_OK;
    CMediaType proposed(*type, &hr);
    if (FAILED(hr)) return hr;
    const bool selected = m_formatSelected;
    m_formatSelected = false;
    hr = CheckMediaType(&proposed);
    m_formatSelected = selected;
    if (FAILED(hr)) return VFW_E_INVALIDMEDIATYPE;
    // The wire format is fixed. Connected callers may re-select the exact
    // current type, but must disconnect before changing even its metadata.
    if (IsConnected() && proposed != m_mt) return VFW_E_INVALIDMEDIATYPE;
    // proposed is already a successful deep copy; transfer ownership without
    // another allocation, so failure cannot destroy the old preferred type.
    FreeMediaType(m_preferred);
    static_cast<AM_MEDIA_TYPE&>(m_preferred) = static_cast<const AM_MEDIA_TYPE&>(proposed);
    proposed.InitMediaType();
    m_formatSelected = true;
    IncrementTypeVersion();
    return S_OK;
}
STDMETHODIMP CapturePin::GetFormat(AM_MEDIA_TYPE** type) {
    if (!type) return E_POINTER;
    *type = nullptr;
    CAutoLock lock(m_pFilter->pStateLock());
    CMediaType format;
    HRESULT hr = format.Set(IsConnected() ? m_mt : m_preferred);
    if (FAILED(hr)) return hr;
    *type = CreateMediaType(&format);
    return *type ? S_OK : E_OUTOFMEMORY;
}
STDMETHODIMP CapturePin::GetNumberOfCapabilities(int* count, int* size) {
    if (!count || !size) return E_POINTER;
    *count = 1;
    *size = sizeof(VIDEO_STREAM_CONFIG_CAPS);
    return S_OK;
}
STDMETHODIMP CapturePin::GetStreamCaps(int index, AM_MEDIA_TYPE** type, BYTE* caps) {
    if (!type || !caps) return E_POINTER;
    *type = nullptr;
    if (index < 0) return E_INVALIDARG;
    if (index > 0) return S_FALSE;
    HRESULT hr = GetFormat(type);
    if (FAILED(hr)) return hr;
    VIDEO_STREAM_CONFIG_CAPS config{};
    config.guid = FORMAT_VideoInfo;
    config.InputSize = config.MinCroppingSize = config.MaxCroppingSize = {kWidth, kHeight};
    config.MinOutputSize = config.MaxOutputSize = {kWidth, kHeight};
    config.CropGranularityX = config.CropGranularityY = 1;
    config.CropAlignX = config.CropAlignY = 1;
    config.OutputGranularityX = config.OutputGranularityY = 1;
    config.MinFrameInterval = config.MaxFrameInterval = kFrameInterval;
    config.MinBitsPerSecond = config.MaxBitsPerSecond = kSampleBytes * 8 * 30;
    std::memcpy(caps, &config, sizeof(config));
    return S_OK;
}
STDMETHODIMP CapturePin::Set(REFGUID set, DWORD id, void*, DWORD, void*, DWORD) {
    if (set != AMPROPSETID_Pin) return E_PROP_SET_UNSUPPORTED;
    if (id != AMPROPERTY_PIN_CATEGORY) return E_PROP_ID_UNSUPPORTED;
    return E_ACCESSDENIED; // real read-only property, not an unimplemented control
}
STDMETHODIMP CapturePin::Get(REFGUID set, DWORD id, void*, DWORD, void* data,
                            DWORD bytes, DWORD* returned) {
    if (set != AMPROPSETID_Pin) return E_PROP_SET_UNSUPPORTED;
    if (id != AMPROPERTY_PIN_CATEGORY) return E_PROP_ID_UNSUPPORTED;
    if (!data && !returned) return E_POINTER;
    if (returned) *returned = sizeof(GUID);
    if (!data) return S_OK;
    if (bytes < sizeof(GUID)) return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    std::memcpy(data, &PIN_CATEGORY_CAPTURE, sizeof(GUID));
    return S_OK;
}
STDMETHODIMP CapturePin::QuerySupported(REFGUID set, DWORD id, DWORD* support) {
    if (!support) return E_POINTER;
    *support = 0;
    if (set != AMPROPSETID_Pin) return E_PROP_SET_UNSUPPORTED;
    if (id != AMPROPERTY_PIN_CATEGORY) return E_PROP_ID_UNSUPPORTED;
    *support = KSPROPERTY_SUPPORT_GET;
    return S_OK;
}
HRESULT CapturePin::DecideBufferSize(IMemAllocator* allocator, ALLOCATOR_PROPERTIES* properties) {
    if (!allocator || !properties) return E_POINTER;
    // CBaseOutputPin calls GetAllocatorRequirements/GetAllocator/NotifyAllocator.
    // Preserve the downstream alignment/prefix/count/size requests if larger.
    properties->cBuffers = std::max(properties->cBuffers, 3L);
    properties->cbBuffer = std::max(properties->cbBuffer, kSampleBytes);
    properties->cbAlign = std::max(properties->cbAlign, 1L);
    properties->cbPrefix = std::max(properties->cbPrefix, 0L);
    ALLOCATOR_PROPERTIES actual{};
    const HRESULT hr = allocator->SetProperties(properties, &actual);
    if (FAILED(hr)) return hr;
    if (actual.cBuffers < properties->cBuffers || actual.cbBuffer < properties->cbBuffer ||
        actual.cbAlign < properties->cbAlign || actual.cbAlign % properties->cbAlign != 0 ||
        actual.cbPrefix < properties->cbPrefix) return E_FAIL;
    return S_OK;
}
void CapturePin::UpdateClock(IReferenceClock* clock) {
    if (clock) clock->AddRef();
    IReferenceClock* previous = nullptr;
    {
        CAutoLock lock(&m_clockLock);
        previous = m_clock;
        m_clock = clock;
    }
    if (previous) previous->Release();
}
HRESULT CapturePin::StartCapture(REFERENCE_TIME start, IReferenceClock* clock) {
    if (!IsConnected() || !ThreadExists()) return S_OK;
    UpdateClock(clock);
    {
        CAutoLock lock(&m_clockLock);
        m_start = start;
        m_runTick = GetTickCount64();
        m_fallbackBase = m_nextTime; // worker is paused and acknowledged
    }
    return CSourceStream::Run();
}
HRESULT CapturePin::PauseCapture() {
    if (!IsConnected() || !ThreadExists()) return S_OK;
    // Unblock a downstream Receive before waiting for the worker's acknowledgement.
    m_flushing.store(true);
    HRESULT hr = DeliverBeginFlush();
    if (FAILED(hr)) { m_flushing.store(false); return hr; }
    hr = CSourceStream::Pause();
    const HRESULT end = DeliverEndFlush();
    m_flushing.store(false);
    return FAILED(hr) ? hr : end;
}
HRESULT CapturePin::Inactive() {
    if (!IsConnected()) return S_OK;
    // Flush first, then the BaseClass decommits the allocator and joins CAMThread.
    m_flushing.store(true);
    const HRESULT begin = DeliverBeginFlush();
    const HRESULT stop = CSourceStream::Inactive();
    const HRESULT end = SUCCEEDED(begin) ? DeliverEndFlush() : S_OK;
    m_flushing.store(false);
    if (FAILED(stop)) return stop;
    return FAILED(begin) ? begin : end;
}
HRESULT CapturePin::OnThreadCreate() {
    m_nextTime = 0;
    m_discontinuity = true;
    m_hadSignal = false;
    return S_OK;
}
HRESULT CapturePin::OnThreadDestroy() {
    m_reader.close();
    UpdateClock(nullptr);
    return S_OK;
}
REFERENCE_TIME CapturePin::CurrentStreamTime() {
    CAutoLock lock(&m_clockLock);
    REFERENCE_TIME now = 0;
    if (m_clock && SUCCEEDED(m_clock->GetTime(&now))) return std::max<REFERENCE_TIME>(0, now - m_start);
    return m_fallbackBase + static_cast<REFERENCE_TIME>(GetTickCount64() - m_runTick) * 10000;
}
HRESULT CapturePin::FillBuffer(IMediaSample* sample) {
    if (!sample) return E_POINTER;
    if (sample->GetSize() < kSampleBytes) return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    BYTE* data = nullptr;
    HRESULT hr = sample->GetPointer(&data);
    if (FAILED(hr)) return hr;
    if (!data) return E_POINTER;
    // Timestamp at capture, not after CPU conversion, so processing time does
    // not add another frame interval to every pacing cycle.
    REFERENCE_TIME start = std::max(m_nextTime, CurrentStreamTime());
    if (start > m_nextTime + kFrameInterval) m_discontinuity = true;
    REFERENCE_TIME end = start + kFrameInterval;
    const bool signal = m_reader.readLatest(m_nv12.data());
    if (signal) {
        if (!nv12ToYuy2(m_nv12.data(), m_nv12.size(), data, kSampleBytes, shared::kWidth, shared::kHeight))
            return E_UNEXPECTED;
    } else {
        fillBlackYuy2(data, kSampleBytes);
        // Release stale mapping so a restarted writer can create a new mapping.
        m_reader.close();
    }
    if (signal != m_hadSignal) m_discontinuity = true;
    m_hadSignal = signal;
    // The Android timestamp is from a different clock domain; use graph stream time.
    if (FAILED(hr = sample->SetActualDataLength(kSampleBytes))) return hr;
    if (FAILED(hr = sample->SetTime(&start, &end))) return hr;
    if (FAILED(hr = sample->SetMediaTime(nullptr, nullptr))) return hr;
    if (FAILED(hr = sample->SetSyncPoint(TRUE))) return hr;
    if (FAILED(hr = sample->SetPreroll(FALSE))) return hr;
    if (FAILED(hr = sample->SetDiscontinuity(m_discontinuity ? TRUE : FALSE))) return hr;
    m_discontinuity = false;
    m_nextTime = end;
    return S_OK;
}
HRESULT CapturePin::DoBufferProcessingLoop() {
    bool running = false; // CSourceStream::Active starts in CMD_PAUSE
    for (;;) {
        Command command;
        if (CheckRequest(&command)) {
            if (command == CMD_STOP) return S_FALSE; // ThreadProc consumes/replies
            if (command == CMD_PAUSE) {
                running = false;
                m_discontinuity = true;
                Reply(S_OK);
            } else if (command == CMD_RUN) {
                running = true;
                Reply(S_OK);
            } else {
                Reply(static_cast<DWORD>(E_UNEXPECTED));
            }
            continue;
        }
        if (!running || m_flushing.load()) {
            WaitForSingleObject(GetRequestHandle(), 10);
            continue;
        }
        const REFERENCE_TIME remaining = m_nextTime - CurrentStreamTime();
        if (remaining > 0) {
            const DWORD wait = static_cast<DWORD>(std::min<REFERENCE_TIME>((remaining + 9999) / 10000, 100));
            WaitForSingleObject(GetRequestHandle(), wait);
            continue;
        }
        IMediaSample* sample = nullptr;
        HRESULT hr = GetDeliveryBuffer(&sample, nullptr, nullptr, AM_GBF_NOWAIT);
        if (FAILED(hr)) {
            // No free buffer/decommit: stay responsive to Stop rather than blocking.
            if (hr != VFW_E_TIMEOUT && !m_flushing.load() && !CheckRequest(&command)) {
                m_pFilter->NotifyEvent(EC_ERRORABORT, hr, 0);
                return hr;
            }
            WaitForSingleObject(GetRequestHandle(), 1);
            continue;
        }
        hr = FillBuffer(sample);
        if (SUCCEEDED(hr)) hr = Deliver(sample); // IMemInputPin::Receive
        sample->Release(); // downstream AddRefs if it retains the buffer
        if (hr != S_OK) {
            // Receive may return S_FALSE while a state-change flush is pending.
            if (m_flushing.load() || CheckRequest(&command)) continue;
            if (FAILED(hr)) m_pFilter->NotifyEvent(EC_ERRORABORT, hr, 0);
            return hr;
        }
    }
}
} // namespace phonebridge::directshow
