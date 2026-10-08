#pragma once
#include <cstdint>
#include <cstddef>

namespace phonebridge::video {

class IVideoPacketSink {
public:
    virtual ~IVideoPacketSink() = default;
    virtual void onVideoConfig(const uint8_t* payload, size_t size) = 0;
    virtual void onVideoFrame(const uint8_t* payload, size_t size, uint32_t flags, uint64_t timestampUs) = 0;
};

class VideoPacketReceiver {
public:
    explicit VideoPacketReceiver(IVideoPacketSink& sink) : m_sink(sink) {}

    void processPacket(uint16_t type, uint32_t flags, uint64_t timestampUs, const uint8_t* payload, size_t size) {
        if (type == 0x0100) { // VIDEO_CONFIG
            m_sink.onVideoConfig(payload, size);
        } else if (type == 0x0101) { // VIDEO_FRAME
            m_sink.onVideoFrame(payload, size, flags, timestampUs);
        }
    }

private:
    IVideoPacketSink& m_sink;
};

} // namespace phonebridge::video
