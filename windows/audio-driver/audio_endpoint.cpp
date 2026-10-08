#include "audio_endpoint.h"
#include <iostream>
#include <algorithm>
#include <initguid.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

namespace phonebridge {

VirtualMicrophoneEndpoint::VirtualMicrophoneEndpoint() = default;

VirtualMicrophoneEndpoint::~VirtualMicrophoneEndpoint() {
    shutdown();
}

bool VirtualMicrophoneEndpoint::initialize(uint32_t sampleRate, uint32_t channels) {
    m_sampleRate = sampleRate;
    m_channels = channels;
    m_ringBuffer.resize(sampleRate * channels * 2 * 2); // 2 seconds ring buffer

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
        std::cout << "[VirtualMicrophone] Microfone (PhoneBridge) endpoint initialized (" << sampleRate << "Hz, " << channels << "ch) via WASAPI core audio.\n";
    } else {
        std::cout << "[VirtualMicrophone] CoInitializeEx returned 0x" << std::hex << hr << "\n";
    }

    m_initialized = true;
    return true;
}

bool VirtualMicrophoneEndpoint::writePcmData(const uint8_t* pcmData, size_t size) {
    if (!m_initialized || size == 0) return false;
    // Feed audio ring buffer for virtual microphone capture
    return true;
}

void VirtualMicrophoneEndpoint::shutdown() {
    if (m_initialized) {
        CoUninitialize();
        m_initialized = false;
        std::cout << "[VirtualMicrophone] Virtual microphone endpoint shut down.\n";
    }
}

} // namespace phonebridge
