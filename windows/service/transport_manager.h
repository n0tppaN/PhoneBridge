#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>

namespace phonebridge {

enum class TransportState {
    DISCONNECTED,
    DISCOVERING,
    CONNECTING,
    HANDSHAKING,
    STREAMING,
    RECONNECTING,
    FAILED
};

class ITransport {
public:
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool send(const uint8_t* data, size_t size) = 0;
    virtual bool receive(uint8_t* buffer, size_t size, size_t& received) = 0;
    virtual bool isConnected() const = 0;
    virtual ~ITransport() = default;
};

class TransportManager {
public:
    TransportManager(std::string adbPath = "adb", uint16_t port = 27183);
    ~TransportManager();

    bool start();
    void stop();

    TransportState getState() const { return m_state.load(); }
    bool isStreaming() const { return m_state.load() == TransportState::STREAMING; }

private:
    void serviceLoop();
    bool setupAdbForward();
    void handleReconnection();

    std::string m_adbPath;
    uint16_t m_port;
    std::atomic<bool> m_running{false};
    std::atomic<TransportState> m_state{TransportState::DISCONNECTED};
    std::thread m_workerThread;

    int m_backoffMs{250};
    static constexpr int MAX_BACKOFF_MS = 5000;
};

} // namespace phonebridge
