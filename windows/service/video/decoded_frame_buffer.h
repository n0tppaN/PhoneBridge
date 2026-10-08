#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <queue>

namespace phonebridge::video {

struct DecodedFrame {
    std::vector<uint8_t> data;
    uint32_t width;
    uint32_t height;
    int64_t timestampUs;
};

class DecodedFrameBuffer {
public:
    explicit DecodedFrameBuffer(size_t maxFrames = 3) : m_maxFrames(maxFrames) {}

    void push(DecodedFrame&& frame) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.size() >= m_maxFrames) {
            m_queue.pop(); // Drop oldest frame for ultra-low latency
        }
        m_queue.push(std::move(frame));
    }

    bool pop(DecodedFrame& frame) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty()) return false;
        frame = std::move(m_queue.front());
        m_queue.pop();
        return true;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::queue<DecodedFrame> empty;
        std::swap(m_queue, empty);
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

private:
    size_t m_maxFrames;
    std::queue<DecodedFrame> m_queue;
    mutable std::mutex m_mutex;
};

inline DecodedFrameBuffer& getDecodedFrameBuffer() {
    static DecodedFrameBuffer instance;
    return instance;
}

} // namespace phonebridge::video
