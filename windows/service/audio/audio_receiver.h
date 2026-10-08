#pragma once
#include "audio_ring_buffer.h"
#include <string>
#include <fstream>
#include <cstdint>

namespace phonebridge::audio {

class AudioReceiver {
public:
    // The ring is the production-side sink. WAV output is enabled only by
    // PHONEBRIDGE_DIAGNOSTIC_WAV=1 and is never created by default.
    explicit AudioReceiver(size_t ringBufferCapacity = 19200, const std::string& wavOutputPath = "windows_output_audio.wav");
    ~AudioReceiver();

    bool handleAudioConfig(const uint8_t* payload, size_t size);
    bool handleAudioFrame(const uint8_t* payload, size_t size, uint64_t timestampUs);

    AudioRingBuffer& ringBuffer() { return m_ringBuffer; }

private:
    void writeWavHeader();
    void finalizeWavFile();

    AudioRingBuffer m_ringBuffer;
    std::string m_wavOutputPath;
    std::ofstream m_wavFile;
    uint32_t m_sampleRate{48000};
    uint16_t m_channels{1};
    uint16_t m_bitsPerSample{16};
    uint32_t m_dataBytesWritten{0};
    bool m_wavInitialized{false};

};

} // namespace phonebridge::audio
