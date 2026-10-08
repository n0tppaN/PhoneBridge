#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <algorithm>

namespace phonebridge::audio {

class AudioRingBuffer {
public:
    explicit AudioRingBuffer(size_t capacityBytes)
        : m_capacity(capacityBytes), m_buffer(capacityBytes) {}

    size_t write(const uint8_t* data, size_t size) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (size == 0) return 0;

        if (size > m_capacity - m_size) {
            m_overruns++;
            // Drop old data or clip
            size = m_capacity - m_size;
            if (size == 0) return 0;
        }

        size_t firstPart = std::min(size, m_capacity - m_tail);
        std::memcpy(m_buffer.data() + m_tail, data, firstPart);
        if (size > firstPart) {
            std::memcpy(m_buffer.data(), data + firstPart, size - firstPart);
        }

        m_tail = (m_tail + size) % m_capacity;
        m_size += size;
        return size;
    }

    size_t read(uint8_t* data, size_t size) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (size == 0 || m_size == 0) return 0;

        if (size > m_size) {
            m_underruns++;
            size = m_size;
        }

        size_t firstPart = std::min(size, m_capacity - m_head);
        std::memcpy(data, m_buffer.data() + m_head, firstPart);
        if (size > firstPart) {
            std::memcpy(data + firstPart, m_buffer.data(), size - firstPart);
        }

        m_head = (m_head + size) % m_capacity;
        m_size -= size;
        return size;
    }

    size_t capacity() const { return m_capacity; }
    size_t available() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_size;
    }
    size_t freeSpace() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_capacity - m_size;
    }
    double fillPercentage() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return (static_cast<double>(m_size) / m_capacity) * 100.0;
    }

    uint64_t underruns() const { return m_underruns; }
    uint64_t overruns() const { return m_overruns; }

    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_head = 0;
        m_tail = 0;
        m_size = 0;
        m_underruns = 0;
        m_overruns = 0;
    }

private:
    std::vector<uint8_t> m_buffer;
    size_t m_capacity;
    size_t m_head{0};
    size_t m_tail{0};
    size_t m_size{0};
    mutable std::mutex m_mutex;

    uint64_t m_underruns{0};
    uint64_t m_overruns{0};
};

} // namespace phonebridge::audio
