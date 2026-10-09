#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <streams.h>
#include <atomic>
#include <vector>
#include "phonebridge_guids.h"
#include "../virtual-camera/shared_frame.h"

namespace phonebridge::directshow {
inline constexpr LONG kWidth = static_cast<LONG>(shared::kWidth);
inline constexpr LONG kHeight = static_cast<LONG>(shared::kHeight);
inline constexpr LONG kSampleBytes = kWidth * kHeight * 2;
inline constexpr REFERENCE_TIME kFrameInterval = 10000000 / 30;

class CapturePin;
class CameraFilter final : public CSource {
public:
    static CUnknown* WINAPI CreateInstance(LPUNKNOWN outer, HRESULT* result);
    CameraFilter(LPUNKNOWN outer, HRESULT* result);
    ~CameraFilter() override;
    STDMETHODIMP GetState(DWORD timeout, FILTER_STATE* state) override;
    STDMETHODIMP Run(REFERENCE_TIME start) override;
    STDMETHODIMP Pause() override;
    STDMETHODIMP SetSyncSource(IReferenceClock* clock) override;
private:
    CapturePin* m_pin = nullptr; // CSource owns and deletes registered pins.
};

class CapturePin final : public CSourceStream, public IAMStreamConfig, public IKsPropertySet {
public:
    CapturePin(HRESULT* result, CameraFilter* filter);
    ~CapturePin() override;
    DECLARE_IUNKNOWN;
    STDMETHODIMP NonDelegatingQueryInterface(REFIID iid, void** object) override;
    STDMETHODIMP SetFormat(AM_MEDIA_TYPE* type) override;
    STDMETHODIMP GetFormat(AM_MEDIA_TYPE** type) override;
    STDMETHODIMP GetNumberOfCapabilities(int* count, int* size) override;
    STDMETHODIMP GetStreamCaps(int index, AM_MEDIA_TYPE** type, BYTE* caps) override;
    STDMETHODIMP Set(REFGUID set, DWORD id, void* instance, DWORD instanceBytes,
                    void* data, DWORD dataBytes) override;
    STDMETHODIMP Get(REFGUID set, DWORD id, void* instance, DWORD instanceBytes,
                    void* data, DWORD dataBytes, DWORD* returned) override;
    STDMETHODIMP QuerySupported(REFGUID set, DWORD id, DWORD* support) override;

    HRESULT StartCapture(REFERENCE_TIME start, IReferenceClock* clock);
    HRESULT PauseCapture();
    void UpdateClock(IReferenceClock* clock);
    HRESULT Inactive() override;
protected:
    HRESULT GetMediaType(int position, CMediaType* type) override;
    HRESULT CheckMediaType(const CMediaType* type) override;
    HRESULT DecideBufferSize(IMemAllocator* allocator, ALLOCATOR_PROPERTIES* properties) override;
    HRESULT FillBuffer(IMediaSample* sample) override;
    HRESULT OnThreadCreate() override;
    HRESULT OnThreadDestroy() override;
    HRESULT DoBufferProcessingLoop() override;
private:
    HRESULT MakeType(CMediaType* type) const;
    REFERENCE_TIME CurrentStreamTime();
    // Worker never takes the filter state lock: Stop/Pause wait for CAMThread.
    CCritSec m_clockLock;
    IReferenceClock* m_clock = nullptr;
    REFERENCE_TIME m_start = 0;
    ULONGLONG m_runTick = 0;
    REFERENCE_TIME m_fallbackBase = 0;
    REFERENCE_TIME m_nextTime = 0; // worker-owned; survives Pause -> Run
    bool m_discontinuity = true;
    bool m_hadSignal = false;
    std::atomic<bool> m_flushing{false};
    CMediaType m_preferred; // filter-state lock, only set while stopped
    bool m_formatSelected = false;
    shared::Reader m_reader;
    std::vector<uint8_t> m_nv12;
};
} // namespace phonebridge::directshow
