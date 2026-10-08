#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "audio_ipc.h"
#include <iostream>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

namespace phonebridge::audio {

AudioIpcBridge::AudioIpcBridge(const std::string& sharedMemoryName, size_t sizeBytes)
    : m_shmName(sharedMemoryName), m_sizeBytes(sizeBytes) {}

AudioIpcBridge::~AudioIpcBridge() {
    shutdown();
}

bool AudioIpcBridge::initialize() {
    if (m_initialized) return true;

#ifdef _WIN32
    m_fileMapping = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(m_sizeBytes),
        m_shmName.c_str()
    );

    if (!m_fileMapping) {
        std::cerr << "[AudioIpc] Failed to create file mapping: " << GetLastError() << "\n";
        return false;
    }

    m_mappedView = MapViewOfFile(
        m_fileMapping,
        FILE_MAP_ALL_ACCESS,
        0,
        0,
        m_sizeBytes
    );

    if (!m_mappedView) {
        std::cerr << "[AudioIpc] Failed to map view of file: " << GetLastError() << "\n";
        CloseHandle(m_fileMapping);
        m_fileMapping = nullptr;
        return false;
    }

    std::memset(m_mappedView, 0, m_sizeBytes);
#endif

    m_initialized = true;
    std::cout << "[AudioIpc] Audio IPC shared memory initialized: " << m_shmName << "\n";
    return true;
}

bool AudioIpcBridge::writeSharedMemory(const uint8_t* data, size_t size) {
    if (!m_initialized || !m_mappedView || size == 0) return false;
    size_t copySize = (std::min)(size, m_sizeBytes);
    std::memcpy(m_mappedView, data, copySize);
    return true;
}

void AudioIpcBridge::shutdown() {
#ifdef _WIN32
    if (m_mappedView) {
        UnmapViewOfFile(m_mappedView);
        m_mappedView = nullptr;
    }
    if (m_fileMapping) {
        CloseHandle(m_fileMapping);
        m_fileMapping = nullptr;
    }
#endif
    m_initialized = false;
    std::cout << "[AudioIpc] Audio IPC shut down.\n";
}

} // namespace phonebridge::audio
