#pragma once
#include "audio_ring_buffer.h"
#include <string>
#include <fstream>
#include <cstdint>
#include <windows.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

namespace phonebridge::audio {

class AudioReceiver {
public:
    explicit AudioReceiver(size_t ringBufferCapacity = 19200, const std::string& wavOutputPath = "windows_output_audio.wav");
    ~AudioReceiver();

    bool handleAudioConfig(const uint8_t* payload, size_t size);
    bool handleAudioFrame(const uint8_t* payload, size_t size, uint64_t timestampUs);

    AudioRingBuffer& ringBuffer() { return m_ringBuffer; }

private:
    void writeWavHeader();
    void finalizeWavFile();
    void initWaveOut();
    void playPcmLive(const uint8_t* data, size_t size);

    AudioRingBuffer m_ringBuffer;
    std::string m_wavOutputPath;
    std::ofstream m_wavFile;
    uint32_t m_sampleRate{48000};
    uint16_t m_channels{1};
    uint16_t m_bitsPerSample{16};
    uint32_t m_dataBytesWritten{0};
    bool m_wavInitialized{false};

    HWAVEOUT m_hWaveOut{nullptr};
    bool m_waveOutInitialized{false};
};

} // namespace phonebridge::audio
