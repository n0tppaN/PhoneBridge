#pragma once
#include <string>
#include <cstdint>
#include <cstddef>

namespace phonebridge::audio {

class AudioIpcBridge {
public:
    explicit AudioIpcBridge(const std::string& sharedMemoryName = "Local\\PhoneBridgeAudioSharedMemory", size_t sizeBytes = 192000);
    ~AudioIpcBridge();

    bool initialize();
    bool writeSharedMemory(const uint8_t* data, size_t size);
    void shutdown();

private:
    std::string m_shmName;
    size_t m_sizeBytes;
    void* m_mappedView{nullptr};
    void* m_fileMapping{nullptr};
    bool m_initialized{false};
};

} // namespace phonebridge::audio
