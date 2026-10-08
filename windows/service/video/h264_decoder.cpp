#include "h264_decoder.h"
#include <cstring>
#include "pb_log.h"
#include "video_utils.h"

namespace phonebridge::video {

namespace {
std::string toUtf8(const wchar_t* w) {
    char a[256] = {};
    WideCharToMultiByte(CP_UTF8, 0, w, -1, a, sizeof(a), nullptr, nullptr);
    return a;
}
}  // namespace

H264Decoder::H264Decoder() = default;
H264Decoder::~H264Decoder() { shutdown(); }

// ---------------------------------------------------------------- setup
bool H264Decoder::initialize(uint32_t width, uint32_t height) {
    if (m_initialized) return true;
    m_inW = width;
    m_inH = height;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) {
        m_comInit = true;
        m_comThread = std::this_thread::get_id();
    } else if (hr != RPC_E_CHANGED_MODE) {
        PB_LOG("Decoder: CoInitializeEx failed 0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }
    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        PB_LOG("Decoder: MFStartup failed 0x%08lX", static_cast<unsigned long>(hr));
        shutdown();
        return false;
    }
    m_mfStarted = true;

    if (!createTransform() || !setInputType() || !selectOutputType()) {
        shutdown();
        return false;
    }
    m_mft->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    m_mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    m_mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

    m_needKeyframe = true;
    m_feedConfigBeforeKey = true;
    m_framesIn = m_framesOut = m_dropped = m_badFrames = 0;
    m_initialized = true;
    PB_LOG("Decoder: ready (%ux%u requested). Waiting for SPS/PPS + first keyframe.", width, height);
    return true;
}

bool H264Decoder::createTransform() {
    MFT_REGISTER_TYPE_INFO in{MFMediaType_Video, MFVideoFormat_H264};
    IMFActivate** activates = nullptr;
    UINT32 count = 0;
    HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER, &in,
                           nullptr, &activates, &count);
    if (FAILED(hr) || count == 0) {
        PB_LOG("Decoder: no H.264 decoder MFT found (hr=0x%08lX count=%u)", static_cast<unsigned long>(hr), count);
        return false;
    }
    WCHAR* name = nullptr;
    UINT32 nameLen = 0;
    if (SUCCEEDED(activates[0]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &nameLen))) {
        PB_LOG("Decoder: using MFT \"%s\" (%u candidate(s))", toUtf8(name).c_str(), count);
        CoTaskMemFree(name);
    }
    hr = activates[0]->ActivateObject(IID_IMFTransform, reinterpret_cast<void**>(m_mft.put()));
    for (UINT32 i = 0; i < count; ++i) activates[i]->Release();
    CoTaskMemFree(activates);
    if (FAILED(hr)) {
        PB_LOG("Decoder: ActivateObject failed 0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }

    pb::ComPtr<IMFAttributes> attrs;
    if (SUCCEEDED(m_mft->GetAttributes(attrs.put()))) {
        const HRESULT lh = attrs->SetUINT32(MF_LOW_LATENCY, TRUE);
        PB_LOG("Decoder: MF_LOW_LATENCY -> 0x%08lX", static_cast<unsigned long>(lh));
    }
    return true;
}

bool H264Decoder::setInputType() {
    pb::ComPtr<IMFMediaType> type;
    HRESULT hr = MFCreateMediaType(type.put());
    if (FAILED(hr)) {
        PB_LOG("Decoder: MFCreateMediaType(input) failed 0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }
    if (FAILED(hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
        FAILED(hr = type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264)) ||
        FAILED(hr = MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, m_inW, m_inH)) ||
        FAILED(hr = MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, 30, 1)) ||
        FAILED(hr = MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1)) ||
        FAILED(hr = type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_MixedInterlaceOrProgressive))) {
        PB_LOG("Decoder: setting H264 input attributes failed 0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }
    // The Android codec-config packet is kept in m_config and prepended to
    // the first IDR sample. Do not also advertise it as
    // MF_MT_MPEG_SEQUENCE_HEADER: on the installed Microsoft MFT that
    // combination makes ProcessOutput return E_FAIL when the same SPS/PPS
    // appears in the first Annex-B sample.
    hr = m_mft->SetInputType(0, type.Get(), 0);
    if (FAILED(hr)) {
        PB_LOG("Decoder: SetInputType(H264) failed 0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }
    return true;
}

bool H264Decoder::selectOutputType() {
    for (DWORD i = 0;; ++i) {
        pb::ComPtr<IMFMediaType> type;
        HRESULT hr = m_mft->GetOutputAvailableType(0, i, type.put());
        if (hr == MF_E_NO_MORE_TYPES) break;
        if (FAILED(hr)) {
            PB_LOG("Decoder: GetOutputAvailableType(%lu) failed 0x%08lX", static_cast<unsigned long>(i),
                   static_cast<unsigned long>(hr));
            return false;
        }
        GUID sub = GUID_NULL;
        type->GetGUID(MF_MT_SUBTYPE, &sub);
        if (sub != MFVideoFormat_NV12) continue;
        hr = m_mft->SetOutputType(0, type.Get(), 0);
        if (FAILED(hr)) {
            PB_LOG("Decoder: SetOutputType(NV12) failed 0x%08lX", static_cast<unsigned long>(hr));
            return false;
        }
        return updateOutputLayout() && updateStreamInfo();
    }
    PB_LOG("Decoder: the MFT offers no NV12 output type");
    return false;
}

bool H264Decoder::updateOutputLayout() {
    pb::ComPtr<IMFMediaType> type;
    if (FAILED(m_mft->GetOutputCurrentType(0, type.put()))) return false;

    UINT32 w = 0, h = 0;
    MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h);
    const INT32 stride = static_cast<INT32>(MFGetAttributeUINT32(type.Get(), MF_MT_DEFAULT_STRIDE, 0));
    MFVideoArea area{};
    UINT32 got = 0;
    UINT32 dw = w, dh = h;
    if (SUCCEEDED(type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&area), sizeof(area), &got)) &&
        got == sizeof(area) && area.Area.cx > 0 && area.Area.cy > 0) {
        dw = static_cast<UINT32>(area.Area.cx);
        dh = static_cast<UINT32>(area.Area.cy);
    }
    if (dw > w) dw = w;
    if (dh > h) dh = h;
    dw &= ~1u;
    dh &= ~1u;

    m_codedW = w;
    m_codedH = h;
    m_dispW = dw;
    m_dispH = dh;
    m_stride = stride > 0 ? static_cast<uint32_t>(stride) : w;
    m_frame.assign(size_t(dw) * dh * 3 / 2, 0);
    PB_LOG("Decoder: output NV12 coded=%ux%u visible=%ux%u stride=%u", w, h, dw, dh, m_stride);
    return dw > 0 && dh > 0;
}

bool H264Decoder::updateStreamInfo() {
    MFT_OUTPUT_STREAM_INFO info{};
    HRESULT hr = m_mft->GetOutputStreamInfo(0, &info);
    if (FAILED(hr)) return false;
    // CAN_PROVIDE_SAMPLES means that the MFT may provide a sample, not that
    // it always does. Supplying our own output sample is the safe path.
    m_providesSamples = (info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0;
    m_outSample.reset();
    if (!m_providesSamples) {
        pb::ComPtr<IMFMediaBuffer> buf;
        const DWORD align = info.cbAlignment > 0 ? info.cbAlignment - 1 : 0;
        hr = MFCreateAlignedMemoryBuffer(info.cbSize, align, buf.put());
        if (FAILED(hr)) return false;
        hr = MFCreateSample(m_outSample.put());
        if (FAILED(hr)) return false;
        m_outSample->AddBuffer(buf.Get());
    }
    PB_LOG("Decoder: output buffer %lu bytes, MFT provides samples=%d", static_cast<unsigned long>(info.cbSize),
           m_providesSamples ? 1 : 0);
    return true;
}

// ---------------------------------------------------------------- run time
void H264Decoder::reset() {
    if (!m_initialized) return;
    m_mft->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    m_mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    m_needKeyframe = true;
    m_feedConfigBeforeKey = true;
    m_discontinuityPending = true;
    PB_LOG("Decoder: reset - waiting for keyframe again");
}

bool H264Decoder::decode(const uint8_t* data, size_t size, uint64_t timestampUs, bool keyframe, bool codecConfig) {
    if (!m_initialized || !data || size == 0) return false;

    if (codecConfig) {
        m_config.assign(data, data + size);
        PB_LOG("Decoder: received codec config (SPS/PPS) %zu bytes", size);
        // Recreate the MFT so the sequence header is present before both
        // input and output types are negotiated. Several Windows decoder
        // MFTs reject SetInputType while an output type is already active.
        shutdown();
        if (!initialize(m_inW, m_inH)) {
            PB_LOG("Decoder: failed to initialize with codec config");
            return false;
        }
        m_needKeyframe = true;
        // The Microsoft MFT rejects SPS/PPS as a standalone input sample on
        // some Windows builds. Keep it only as the sequence header; all
        // subsequent samples are complete Annex-B access units.
        m_feedConfigBeforeKey = false;
        return true;
    }

    if (m_needKeyframe) {
        if (!keyframe) {
            if ((m_dropped++ % 60) == 0) 
                PB_LOG("Decoder: waiting for keyframe, dropped %llu frame(s)", static_cast<unsigned long long>(m_dropped));
            return false;
        }
        m_needKeyframe = false;
        // MediaCodec delivers SPS/PPS (codec-config) in a separate buffer.
        // msmpeg2vdec is more reliable when the first input sample contains
        // the complete Annex-B sequence header immediately followed by IDR,
        // just as it appears in the VLC-playable elementary stream.
        if (!m_config.empty()) {
            std::vector<uint8_t> firstAccessUnit;
            firstAccessUnit.reserve(m_config.size() + size);
            firstAccessUnit.insert(firstAccessUnit.end(), m_config.begin(), m_config.end());
            firstAccessUnit.insert(firstAccessUnit.end(), data, data + size);
            PB_LOG("Decoder: keyframe received (%zu bytes), prepending SPS/PPS (%zu bytes)",
                   size, m_config.size());
            return feed(firstAccessUnit.data(), firstAccessUnit.size(), timestampUs, true, false);
        }
        PB_LOG("Decoder: keyframe received (%zu bytes) - decoding starts", size);
    }
    return feed(data, size, timestampUs, keyframe, false);
}

bool H264Decoder::feed(const uint8_t* data, size_t size, uint64_t timestampUs, bool keyframe, bool codecConfig) {
    if (!m_mft || !m_initialized) {
        PB_LOG("Decoder: feed() but MFT not ready");
        return false;
    }

    pb::ComPtr<IMFMediaBuffer> buf;
    HRESULT hr = MFCreateMemoryBuffer(static_cast<DWORD>(size), buf.put());
    if (FAILED(hr)) {
        PB_LOG("Decoder: MFCreateMemoryBuffer(%zu) failed 0x%08lX", size, static_cast<unsigned long>(hr));
        return false;
    }
    BYTE* p = nullptr;
    hr = buf->Lock(&p, nullptr, nullptr);
    if (FAILED(hr)) {
        PB_LOG("Decoder: Lock failed 0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }
    std::memcpy(p, data, size);
    buf->Unlock();
    hr = buf->SetCurrentLength(static_cast<DWORD>(size));
    if (FAILED(hr)) {
        PB_LOG("Decoder: SetCurrentLength failed 0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }

    pb::ComPtr<IMFSample> sample;
    hr = MFCreateSample(sample.put());
    if (FAILED(hr)) {
        PB_LOG("Decoder: MFCreateSample failed 0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }
    hr = sample->AddBuffer(buf.Get());
    if (FAILED(hr)) {
        PB_LOG("Decoder: IMFSample::AddBuffer failed 0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }
    hr = sample->SetSampleTime(static_cast<LONGLONG>(timestampUs) * 10);
    if (FAILED(hr)) {
        PB_LOG("Decoder: IMFSample::SetSampleTime failed 0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }
    hr = sample->SetSampleDuration(333333);
    if (FAILED(hr)) {
        PB_LOG("Decoder: IMFSample::SetSampleDuration failed 0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }
    // The Microsoft H.264 MFT uses these attributes to identify an IDR
    // access unit and to recover cleanly after a stream restart.
    hr = sample->SetUINT32(MFSampleExtension_CleanPoint, keyframe ? TRUE : FALSE);
    if (FAILED(hr)) {
        PB_LOG("Decoder: Set CleanPoint failed 0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }
    if (m_discontinuityPending) {
        hr = sample->SetUINT32(MFSampleExtension_Discontinuity, TRUE);
        if (FAILED(hr)) {
            PB_LOG("Decoder: Set Discontinuity failed 0x%08lX", static_cast<unsigned long>(hr));
            return false;
        }
        m_discontinuityPending = false;
    }

    ++m_framesIn;
    hr = m_mft->ProcessInput(0, sample.Get(), 0);

    PB_LOG("Decoder: ProcessInput returned 0x%08lX (bytes=%zu keyframe=%d)",
           static_cast<unsigned long>(hr), size, keyframe ? 1 : 0);

    if (hr == MF_E_NOTACCEPTING) {
        drainOutput();
        hr = m_mft->ProcessInput(0, sample.Get(), 0);
    }
    if (FAILED(hr)) {
        PB_LOG("Decoder: ProcessInput failed 0x%08lX (%zu bytes, keyframe=%d, config=%d) - resetting",
               static_cast<unsigned long>(hr), size, keyframe ? 1 : 0, codecConfig ? 1 : 0);
        reset();
        return false;
    }
    drainOutput();
    return true;
}

void H264Decoder::drainOutput() {
    int streamChanges = 0;
    for (;;) {
        MFT_OUTPUT_DATA_BUFFER out{};
        out.dwStreamID = 0;
        out.pSample = m_providesSamples ? nullptr : m_outSample.Get();
        if (!m_providesSamples && m_outSample) {
            pb::ComPtr<IMFMediaBuffer> reusableBuffer;
            HRESULT resetLength = m_outSample->GetBufferByIndex(0, reusableBuffer.put());
            if (SUCCEEDED(resetLength)) {
                resetLength = reusableBuffer->SetCurrentLength(0);
            }
            if (FAILED(resetLength)) {
                PB_LOG("Decoder: failed to reset reusable output buffer length 0x%08lX",
                       static_cast<unsigned long>(resetLength));
                return;
            }
        }
        DWORD status = 0;
        const HRESULT hr = m_mft->ProcessOutput(0, 1, &out, &status);

        PB_LOG("Decoder: ProcessOutput -> 0x%08lX status=0x%08lX sample=%s",
               static_cast<unsigned long>(hr), static_cast<unsigned long>(status),
               out.pSample ? "yes" : "no");

        if (out.pEvents) out.pEvents->Release();

        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            if (out.pSample && m_providesSamples) out.pSample->Release();
            return;
        }
        if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
            PB_LOG("Decoder: stream format changed - renegotiating");
            if (m_providesSamples && out.pSample) out.pSample->Release();
            if (++streamChanges > 4 || !selectOutputType()) {
                PB_LOG("Decoder: renegotiation failed - resetting");
                reset();
                return;
            }
            continue;
        }
        if (FAILED(hr)) {
            PB_LOG("Decoder: ProcessOutput failed 0x%08lX", static_cast<unsigned long>(hr));
            if (m_providesSamples && out.pSample) out.pSample->Release();
            return;
        }
        if (out.pSample) {
            extractFrame(out.pSample);
            if (m_providesSamples) out.pSample->Release();
        }
    }
}

void H264Decoder::extractFrame(IMFSample* sample) {
    pb::ComPtr<IMFMediaBuffer> buf;
    if (FAILED(sample->ConvertToContiguousBuffer(buf.put()))) return;
    LONGLONG t100 = 0;
    sample->GetSampleTime(&t100);

    BYTE* data = nullptr;
    size_t len = 0;
    uint32_t pitch = m_stride;
    bool locked2d = false;
    pb::ComPtr<IMF2DBuffer> b2d;
    if (SUCCEEDED(buf->QueryInterface(IID_IMF2DBuffer, reinterpret_cast<void**>(b2d.put())))) {
        LONG pt = 0;
        DWORD contiguous = 0;
        if (SUCCEEDED(b2d->Lock2D(&data, &pt))) {
            if (pt > 0 && SUCCEEDED(b2d->GetContiguousLength(&contiguous))) {
                locked2d = true;
                pitch = static_cast<uint32_t>(pt);
                len = contiguous;
            } else {
                b2d->Unlock2D();
            }
        }
    }
    if (!locked2d) {
        DWORD cur = 0;
        if (FAILED(buf->Lock(&data, nullptr, &cur))) return;
        len = cur;
    }

    const bool ok = copyNv12Cropped(data, len, pitch, m_codedH, m_dispW, m_dispH, m_frame.data());
    if (locked2d) b2d->Unlock2D(); else buf->Unlock();

    if (!ok) {
        if ((m_badFrames++ % 30) == 0)
            PB_LOG("Decoder: bad frame (len=%zu pitch=%u codedH=%u visible=%ux%u)", len, pitch, m_codedH, m_dispW, m_dispH);
        return;
    }
    ++m_framesOut;
    if (m_framesOut == 1) PB_LOG("Decoder: *** FIRST FRAME DECODED %ux%u ***", m_dispW, m_dispH);
    else if ((m_framesOut % 300) == 0)
        PB_LOG("Decoder: %llu frames out (in=%llu, dropped=%llu)", static_cast<unsigned long long>(m_framesOut),
               static_cast<unsigned long long>(m_framesIn), static_cast<unsigned long long>(m_dropped));
    if (m_callback) m_callback(m_frame.data(), m_dispW, m_dispH, static_cast<int64_t>(t100 / 10));
}

void H264Decoder::shutdown() {
    if (m_mft) {
        m_mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        m_mft.reset();
    }
    m_outSample.reset();
    if (m_mfStarted) {
        MFShutdown();
        m_mfStarted = false;
    }
    if (m_comInit && m_comThread == std::this_thread::get_id()) CoUninitialize();
    m_comInit = false;
    m_initialized = false;
}

}  // namespace phonebridge::video
