#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>
#include "pb_com.h"

namespace phonebridge::video {

// H.264 (Annex B) -> NV12, using Media Foundation H.264 decoder MFT.
// Not thread-safe: call from ONE thread.
class H264Decoder {
public:
    // nv12 is tightly packed (stride == width) and only valid during the callback.
    using FrameCallback = std::function<void(const uint8_t* nv12, uint32_t width, uint32_t height, int64_t timestampUs)>;

    H264Decoder();
    ~H264Decoder();

    void setFrameCallback(FrameCallback cb) { m_callback = std::move(cb); }

    bool initialize(uint32_t width = 1920, uint32_t height = 1080);
    void reset();
    bool decode(const uint8_t* h264Data, size_t size, uint64_t timestampUs, bool keyframe = false,
                bool codecConfig = false);
    void shutdown();

    bool isInitialized() const { return m_initialized; }
    uint64_t framesIn() const { return m_framesIn; }
    uint64_t framesOut() const { return m_framesOut; }

private:
    bool createTransform();
    bool setInputType();
    bool selectOutputType();
    bool updateOutputLayout();
    bool updateStreamInfo();
    bool feed(const uint8_t* data, size_t size, uint64_t timestampUs, bool keyframe, bool codecConfig);
    void drainOutput();
    void extractFrame(IMFSample* sample);

    FrameCallback m_callback;
    bool m_initialized = false;
    bool m_mfStarted = false;
    bool m_comInit = false;
    std::thread::id m_comThread;

    pb::ComPtr<IMFTransform> m_mft;
    pb::ComPtr<IMFSample> m_outSample;
    bool m_providesSamples = false;

    uint32_t m_inW = 1920, m_inH = 1080;
    uint32_t m_stride = 0, m_codedW = 0, m_codedH = 0, m_dispW = 0, m_dispH = 0;
    std::vector<uint8_t> m_frame;

    std::vector<uint8_t> m_config;
    bool m_needKeyframe = true;
    bool m_feedConfigBeforeKey = true;
    bool m_discontinuityPending = true;

    uint64_t m_framesIn = 0, m_framesOut = 0, m_dropped = 0, m_badFrames = 0;
};

}  // namespace phonebridge::video
