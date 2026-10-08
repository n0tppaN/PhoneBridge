#ifndef NOMINMAX
#define NOMINMAX
#endif
#pragma once
#include <chrono>
#include <string>
#include <algorithm>

namespace phonebridge {

class RecoveryManager {
public:
    explicit RecoveryManager(int initialBackoffMs = 250, int maxBackoffMs = 5000)
        : m_initialBackoff(initialBackoffMs), m_maxBackoff(maxBackoffMs), m_currentBackoff(initialBackoffMs) {}

    void reset() {
        m_currentBackoff = m_initialBackoff;
        m_retryCount = 0;
    }

    int getNextBackoffMs() {
        int backoff = m_currentBackoff;
        m_currentBackoff = (std::min)(m_currentBackoff * 2, m_maxBackoff);
        m_retryCount++;
        return backoff;
    }

    void recordSuccess() {
        reset();
    }

    bool shouldRetry() const { return m_retryCount < MAX_RETRIES; }

private:
    int m_initialBackoff;
    int m_maxBackoff;
    int m_currentBackoff;
    int m_retryCount{0};
    static constexpr int MAX_RETRIES = 1000;
};

} // namespace phonebridge
