#pragma once
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace phonebridge::video {

class H264Decoder;  // kept out of this header on purpose (it pulls in windows.h)

// Receives VIDEO_CONFIG / VIDEO_FRAME packets from the phone, keeps the raw H.264 debug file,
// decodes to NV12 and pushes the frames into getDecodedFrameBuffer(); the virtual camera
// (shared memory -> Frame Server) picks them up from there.
class VideoReceiver {
public:
    explicit VideoReceiver(const std::string& h264OutputPath = "windows_output_video.h264");
    ~VideoReceiver();

    VideoReceiver(const VideoReceiver&) = delete;
    VideoReceiver& operator=(const VideoReceiver&) = delete;

    bool handleVideoConfig(const uint8_t* payload, size_t size);
    bool handleVideoFrame(const uint8_t* payload, size_t size, uint32_t flags, uint64_t timestampUs);

private:
    bool ensureDecoder();

    std::string m_h264OutputPath;
    std::ofstream m_h264File;
    uint32_t m_width{1920};
    uint32_t m_height{1080};
    uint32_t m_fps{30};
    bool m_fileInitialized{false};

    std::unique_ptr<H264Decoder> m_decoder;
    uint32_t m_decoderW{0};
    uint32_t m_decoderH{0};
    bool m_initFailed{false};
    uint32_t m_initRetryCounter{0};
    uint64_t m_pushed{0};
};

}  // namespace phonebridge::video
