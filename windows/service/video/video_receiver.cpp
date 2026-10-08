#include "video_receiver.h"
#include <cstdlib>
#include <iostream>
#include "decoded_frame_buffer.h"
#include "h264_decoder.h"
#include "pb_log.h"
#include "protocol.h"
#include "video_utils.h"

namespace phonebridge::video {

VideoReceiver::VideoReceiver(const std::string& h264OutputPath) : m_h264OutputPath(h264OutputPath) {
    // The raw stream dump grows ~1 MB per second, so it is OFF unless PHONEBRIDGE_DUMP_H264=1.
    char* dump = nullptr;
    size_t dumpLen = 0;
    const bool wantDump = (_dupenv_s(&dump, &dumpLen, "PHONEBRIDGE_DUMP_H264") == 0 && dump && dump[0] == '1');
    free(dump);
    if (wantDump) {
        m_h264File.open(m_h264OutputPath, std::ios::binary);
        if (m_h264File.is_open()) m_fileInitialized = true;
    }
}

VideoReceiver::~VideoReceiver() {
    if (m_fileInitialized && m_h264File.is_open()) {
        m_h264File.close();
        std::cout << "[VideoReceiver] H.264 debug stream saved: " << m_h264OutputPath << "\n";
    }
}

bool VideoReceiver::ensureDecoder() {
    if (!m_decoder) {
        m_decoder = std::make_unique<H264Decoder>();
        m_decoder->setFrameCallback([this](const uint8_t* nv12, uint32_t w, uint32_t h, int64_t tsUs) {
            DecodedFrame f;
            f.data.assign(nv12, nv12 + size_t(w) * h * 3 / 2);
            f.width = w;
            f.height = h;
            f.timestampUs = tsUs;
            getDecodedFrameBuffer().push(std::move(f));
            if (++m_pushed == 1) PB_LOG("VideoReceiver: first decoded frame pushed to the virtual camera (%ux%u)", w, h);
        });
    }
    if (m_decoder->isInitialized()) return true;

    if (m_initFailed && (++m_initRetryCounter % 90) != 0) return false;  // retry about every 3 s
    if (m_decoder->initialize(m_width, m_height)) {
        m_initFailed = false;
        m_decoderW = m_width;
        m_decoderH = m_height;
        return true;
    }
    if (!m_initFailed) std::cerr << "[VideoReceiver] H.264 decoder failed to start - see phonebridge_camera.log\n";
    m_initFailed = true;
    return false;
}

bool VideoReceiver::handleVideoConfig(const uint8_t* payload, size_t size) {
    const char* json = reinterpret_cast<const char*>(payload);
    const int w = jsonInt(json, size, "width", static_cast<int>(m_width));
    const int h = jsonInt(json, size, "height", static_cast<int>(m_height));
    const int fps = jsonInt(json, size, "fps", static_cast<int>(m_fps));
    if (w > 0 && h > 0) {
        m_width = static_cast<uint32_t>(w);
        m_height = static_cast<uint32_t>(h);
    }
    if (fps > 0) m_fps = static_cast<uint32_t>(fps);
    std::cout << "[VideoReceiver] Video configuration received: " << m_width << "x" << m_height << " @" << m_fps
              << " (" << size << " bytes).\n";
    PB_LOG("VideoReceiver: VIDEO_CONFIG %ux%u@%u", m_width, m_height, m_fps);

    // A VIDEO_CONFIG means a (re)started stream: begin from a clean decoder state.
    if (m_decoder && m_decoder->isInitialized()) {
        if (m_width != m_decoderW || m_height != m_decoderH) m_decoder->shutdown();  // re-created on next frame
        else m_decoder->reset();
    }
    return true;
}

bool VideoReceiver::handleVideoFrame(const uint8_t* payload, size_t size, uint32_t flags, uint64_t timestampUs) {
    if (size == 0) return false;

    if (m_fileInitialized && m_h264File.is_open())  // raw stream, handy for debugging with ffplay/VLC
        m_h264File.write(reinterpret_cast<const char*>(payload), static_cast<std::streamsize>(size));

    const bool keyframe = (flags & protocol::kFlagKeyframe) != 0;
    const bool config = (flags & protocol::kFlagConfig) != 0;
    if (keyframe) std::cout << "[VideoReceiver] Keyframe (IDR) received. Size: " << size << " bytes\n";

    if (!ensureDecoder()) return false;
    return m_decoder->decode(payload, size, timestampUs, keyframe, config);
}

}  // namespace phonebridge::video
