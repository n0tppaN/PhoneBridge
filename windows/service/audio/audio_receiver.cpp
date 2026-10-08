#include "audio_receiver.h"
#include <iostream>
#include <cstring>
#include <string>
#include <cstdlib>

namespace phonebridge::audio {

AudioReceiver::AudioReceiver(size_t ringBufferCapacity, const std::string& wavOutputPath)
    : m_ringBuffer(ringBufferCapacity), m_wavOutputPath(wavOutputPath) {
    const char* diagnostic = std::getenv("PHONEBRIDGE_DIAGNOSTIC_WAV");
    if (diagnostic && diagnostic[0] == '1') {
        m_wavFile.open(m_wavOutputPath, std::ios::binary);
        if (m_wavFile.is_open()) {
            writeWavHeader();
            m_wavInitialized = true;
        }
    }
}

AudioReceiver::~AudioReceiver() {
    if (m_wavInitialized) {
        finalizeWavFile();
    }
}

bool AudioReceiver::handleAudioConfig(const uint8_t* payload, size_t size) {
    std::cout << "[AudioReceiver] Audio configuration received (" << size << " bytes).\n";
    return true;
}

bool AudioReceiver::handleAudioFrame(const uint8_t* payload, size_t size, uint64_t timestampUs) {
    if (size == 0) return false;

    // Write to ring buffer
    if (m_ringBuffer.write(payload, size) == 0) return false;

    // Write to debug WAV file sink
    if (m_wavInitialized && m_wavFile.is_open()) {
        m_wavFile.write(reinterpret_cast<const char*>(payload), size);
        m_dataBytesWritten += static_cast<uint32_t>(size);
    }

    return true;
}

void AudioReceiver::writeWavHeader() {
    char dummyHeader[44] = {0};
    m_wavFile.write(dummyHeader, 44);
}

void AudioReceiver::finalizeWavFile() {
    m_wavFile.seekp(0, std::ios::beg);

    uint32_t sampleRate = m_sampleRate;
    uint16_t channels = m_channels;
    uint16_t bitsPerSample = m_bitsPerSample;
    uint32_t dataSize = m_dataBytesWritten;
    uint32_t riffSize = 36 + dataSize;
    uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8);
    uint16_t blockAlign = channels * (bitsPerSample / 8);

    m_wavFile.write("RIFF", 4);
    m_wavFile.write(reinterpret_cast<const char*>(&riffSize), 4);
    m_wavFile.write("WAVE", 4);
    m_wavFile.write("fmt ", 4);
    uint32_t fmtSize = 16;
    m_wavFile.write(reinterpret_cast<const char*>(&fmtSize), 4);
    uint16_t audioFormat = 1; // PCM
    m_wavFile.write(reinterpret_cast<const char*>(&audioFormat), 2);
    m_wavFile.write(reinterpret_cast<const char*>(&channels), 2);
    m_wavFile.write(reinterpret_cast<const char*>(&sampleRate), 4);
    m_wavFile.write(reinterpret_cast<const char*>(&byteRate), 4);
    m_wavFile.write(reinterpret_cast<const char*>(&blockAlign), 2);
    m_wavFile.write(reinterpret_cast<const char*>(&bitsPerSample), 2);
    m_wavFile.write("data", 4);
    m_wavFile.write(reinterpret_cast<const char*>(&dataSize), 4);

    m_wavFile.close();
    std::cout << "[AudioReceiver] Finalized debug WAV file: " << m_wavOutputPath << " (" << dataSize << " bytes PCM).\n";
}

} // namespace phonebridge::audio
