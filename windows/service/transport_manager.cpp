#include "transport_manager.h"
#include <iostream>
#include <cstdlib>

namespace phonebridge {

TransportManager::TransportManager(std::string adbPath, uint16_t port)
    : m_adbPath(std::move(adbPath)), m_port(port) {}

TransportManager::~TransportManager() {
    stop();
}

bool TransportManager::start() {
    if (m_running.exchange(true)) return true;
    m_state = TransportState::DISCOVERING;
    m_workerThread = std::thread(&TransportManager::serviceLoop, this);
    return true;
}

void TransportManager::stop() {
    if (!m_running.exchange(false)) return;
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    m_state = TransportState::DISCONNECTED;
}

bool TransportManager::setupAdbForward() {
    std::string cmd = m_adbPath + " forward tcp:" + std::to_string(m_port) + " localabstract:phonebridge";
    int ret = std::system(cmd.c_str());
    return (ret == 0);
}

void TransportManager::handleReconnection() {
    m_state = TransportState::RECONNECTING;
    std::cout << "[TransportManager] Reconnecting with backoff " << m_backoffMs << "ms...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(m_backoffMs));
    m_backoffMs = std::min(m_backoffMs * 2, MAX_BACKOFF_MS);
}

void TransportManager::serviceLoop() {
    std::cout << "[TransportManager] Windows Service Transport Manager started.\n";

    while (m_running) {
        if (!setupAdbForward()) {
            std::cout << "[TransportManager] ADB forward failed. Ensure phone is connected via USB with ADB enabled.\n";
            handleReconnection();
            continue;
        }

        m_state = TransportState::CONNECTING;
        std::cout << "[TransportManager] ADB forward configured on tcp:" << m_port << "\n";

        // Reset backoff on successful setup
        m_backoffMs = 250;
        m_state = TransportState::STREAMING;

        // Keep loop alive monitoring connection
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    m_state = TransportState::DISCONNECTED;
    std::cout << "[TransportManager] Service loop stopped.\n";
}

} // namespace phonebridge
