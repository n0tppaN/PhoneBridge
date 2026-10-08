#include "audio_receiver.h"
#include <iostream>
#include <cstring>
#include <string>

namespace phonebridge::audio {

AudioReceiver::AudioReceiver(size_t ringBufferCapacity, const std::string& wavOutputPath)
    : m_ringBuffer(ringBufferCapacity), m_wavOutputPath(wavOutputPath) {
    m_wavFile.open(m_wavOutputPath, std::ios::binary);
    if (m_wavFile.is_open()) {
        writeWavHeader();
        m_wavInitialized = true;
    }
    initWaveOut();
}

AudioReceiver::~AudioReceiver() {
    if (m_waveOutInitialized && m_hWaveOut) {
        waveOutReset(m_hWaveOut);
        waveOutClose(m_hWaveOut);
        m_hWaveOut = nullptr;
        m_waveOutInitialized = false;
    }
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
    m_ringBuffer.write(payload, size);

    // Play live through Virtual Audio Cable or default speakers
    playPcmLive(payload, size);

    // Write to debug WAV file sink
    if (m_wavInitialized && m_wavFile.is_open()) {
        m_wavFile.write(reinterpret_cast<const char*>(payload), size);
        m_dataBytesWritten += static_cast<uint32_t>(size);
    }

    return true;
}

void AudioReceiver::initWaveOut() {
    WAVEFORMATEX wfx{};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = m_channels;
    wfx.nSamplesPerSec = m_sampleRate;
    wfx.wBitsPerSample = m_bitsPerSample;
    wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize = 0;

    UINT numDevs = waveOutGetNumDevs();
    UINT targetDevice = WAVE_MAPPER;

    for (UINT i = 0; i < numDevs; i++) {
        WAVEOUTCAPSW caps{};
        if (waveOutGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            std::wstring devName(caps.szPname);
            std::wcout << L"[AudioReceiver] Audio output device #" << i << L": " << devName << L"\n" << std::flush;
            if (devName.find(L"Cable") != std::wstring::npos || devName.find(L"VB-Audio") != std::wstring::npos) {
                targetDevice = i;
                std::wcout << L"[AudioReceiver] -> Automatically targeted Virtual Audio Cable: " << devName << L"\n" << std::flush;
                break;
            }
        }
    }

    MMRESULT res = waveOutOpen(&m_hWaveOut, targetDevice, &wfx, 0, 0, CALLBACK_NULL);
    if (res == MMSYSERR_NOERROR) {
        m_waveOutInitialized = true;
        std::cout << "[AudioReceiver] Live audio playback initialized successfully on target device!\n" << std::flush;
    } else {
        std::cout << "[AudioReceiver] waveOutOpen failed with code " << res << "\n" << std::flush;
    }
}

void AudioReceiver::playPcmLive(const uint8_t* data, size_t size) {
    if (!m_waveOutInitialized || !m_hWaveOut || size == 0) return;

    auto* hdr = new WAVEHDR();
    std::memset(hdr, 0, sizeof(WAVEHDR));

    char* buf = new char[size];
    std::memcpy(buf, data, size);

    hdr->lpData = buf;
    hdr->dwBufferLength = static_cast<DWORD>(size);

    if (waveOutPrepareHeader(m_hWaveOut, hdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR) {
        waveOutWrite(m_hWaveOut, hdr, sizeof(WAVEHDR));
    }
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
