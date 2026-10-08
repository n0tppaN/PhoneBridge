#pragma once
#include <cstdint>
#include <vector>

namespace phonebridge {

class VirtualMicrophoneEndpoint {
public:
    VirtualMicrophoneEndpoint();
    ~VirtualMicrophoneEndpoint();

    bool initialize(uint32_t sampleRate, uint32_t channels);
    bool writePcmData(const uint8_t* pcmData, size_t size);
    void shutdown();

private:
    uint32_t m_sampleRate{48000};
    uint32_t m_channels{1};
    bool m_initialized{false};
    std::vector<uint8_t> m_ringBuffer;
};

} // namespace phonebridge
