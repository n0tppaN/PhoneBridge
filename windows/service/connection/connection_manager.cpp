#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "connection_manager.h"
#include "packet.h"
#include "control/control_protocol.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <cstring>
#include <thread>
#include <algorithm>
#include <span>

namespace phonebridge {

ConnectionManager::ConnectionManager(std::unique_ptr<ITransport> transport)
    : m_transport(std::move(transport)) {}

ConnectionManager::~ConnectionManager() {
    stop();
}

bool ConnectionManager::start() {
    if (m_running.exchange(true)) return true;
    m_state = ConnectionState::DISCOVERING;
    m_workerThread = std::thread(&ConnectionManager::runLoop, this);
    return true;
}

void ConnectionManager::stop() {
    if (!m_running.exchange(false)) return;
    if (m_transport) {
        m_transport->disconnect();
    }
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    m_state = ConnectionState::DISCONNECTED;
}

int64_t ConnectionManager::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool ConnectionManager::isRecent(int64_t lastMs) {
    return lastMs != 0 && (nowMs() - lastMs) < 1500;
}

// Makes the phone match what the user wants. Cheap enough to call on every loop iteration.
void ConnectionManager::syncStreams() {
    const bool wantCam = m_wantCamera.load();
    const bool wantMic = m_wantMic.load();
    if (wantCam != m_sentCamera) {
        if (sendPacket(wantCam ? 0x0020 : 0x0021, nullptr, 0)) {  // START_CAMERA / STOP_CAMERA
            m_sentCamera = wantCam;
            std::cout << "[ConnectionManager] Camera " << (wantCam ? "START" : "STOP") << " requested.\n";
        }
    }
    if (wantMic != m_sentMic) {
        if (sendPacket(wantMic ? 0x0022 : 0x0023, nullptr, 0)) {  // START_MICROPHONE / STOP_MICROPHONE
            m_sentMic = wantMic;
            std::cout << "[ConnectionManager] Microphone " << (wantMic ? "START" : "STOP") << " requested.\n";
        }
    }
}

bool ConnectionManager::sendPacket(uint16_t type, const uint8_t* payload, size_t size) {
    phonebridge::protocol::PacketHeader h{};
    h.magic = phonebridge::protocol::kMagic;
    h.version = phonebridge::protocol::kVersion;
    h.type = type;
    h.flags = 0;
    h.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    h.payloadSize = static_cast<uint32_t>(size);
    h.sequence = m_controlSequence++;

    const auto hb = phonebridge::protocol::encodeHeader(h);
    std::vector<uint8_t> buf(phonebridge::protocol::kHeaderSize + size);
    std::memcpy(buf.data(), hb.data(), phonebridge::protocol::kHeaderSize);
    if (size > 0) {
        std::memcpy(buf.data() + phonebridge::protocol::kHeaderSize, payload, size);
    }

    return m_transport->send(buf.data(), buf.size());
}

bool ConnectionManager::performHandshake() {
    m_state = ConnectionState::HANDSHAKING;
    std::cout << "[ConnectionManager] [Session #" << m_sessionId << "] Sending HELLO...\n";

    std::string helloJson = "{\"protocol\":1}";
    if (!sendPacket(1, reinterpret_cast<const uint8_t*>(helloJson.data()), helloJson.size())) {
        return false;
    }

    uint8_t rxBuf[4096];
    bool gotHelloAck = false;
    bool gotDeviceInfo = false;
    bool gotCapabilities = false;

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    while (std::chrono::steady_clock::now() < deadline && m_running) {
        size_t received = 0;
        if (!m_transport->receive(rxBuf, sizeof(rxBuf), received)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        auto err = m_parser.feed(std::span<const uint8_t>(rxBuf, received), [&](phonebridge::protocol::Packet&& pkt) {
            std::cout << "[ConnectionManager] Received packet type=0x" << std::hex << pkt.header.type << std::dec
                      << ", size=" << pkt.header.payloadSize << "\n";
            if (pkt.header.type == 2) gotHelloAck = true;      // HELLO_ACK
            if (pkt.header.type == 3) {                       // DEVICE_INFO
                gotDeviceInfo = true;
                const std::string json(pkt.payload.begin(), pkt.payload.end());
                setDeviceName(control::deviceDisplayName(json));
            }
            if (pkt.header.type == 4) gotCapabilities = true; // CAPABILITIES
        });

        if (err != phonebridge::protocol::ErrorCode::Ok) {
            std::cerr << "[ConnectionManager] Protocol error in parser.\n";
            return false;
        }

        if (gotHelloAck && gotDeviceInfo && gotCapabilities) {
            std::cout << "[ConnectionManager] Handshake completed successfully for Session #" << m_sessionId << "!\n";
            return true;
        }
    }

    return false;
}

void ConnectionManager::runLoop() {
    std::cout << "[ConnectionManager] Connection Manager started with 5s Auto-Reconnect & Disconnect Detection.\n";

    while (m_running) {
        ++m_sessionId;
        setDeviceName("");
        m_sentCamera = false;
        m_sentMic = false;
        m_lastVideoMs = 0;
        m_lastAudioMs = 0;
        m_lastHeartbeatAckMs = 0;
        m_controlSequence = 0;
        m_state = ConnectionState::CONNECTING;
        std::cout << "[ConnectionManager] [Session #" << m_sessionId << "] Attempting connection to phone...\n";

        if (!m_transport->connect()) {
            m_state = ConnectionState::DISCONNECTED;
            std::cout << "[ConnectionManager] Status: Disconnected (No device / USB unplugged). Retrying in 5 seconds...\n";
            for (int i = 0; i < 50 && m_running; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            continue;
        }

        m_recovery.recordSuccess();
        m_parser.reset();

        if (performHandshake()) {
            m_state = ConnectionState::STREAMING;
            std::cout << "[ConnectionManager] [Session #" << m_sessionId << "] Status: Connected. Starting the streams the user wants...\n";
            syncStreams();  // START_CAMERA / START_MICROPHONE according to m_want*

            uint8_t rxBuf[8192];
            auto lastHeartbeat = std::chrono::steady_clock::now();
            m_lastHeartbeatAckMs = nowMs();

            while (m_running && m_transport->isConnected()) {
                auto now = std::chrono::steady_clock::now();
                if (now - lastHeartbeat >= std::chrono::seconds(1)) {
                    uint64_t ts = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
                    sendPacket(0x0F00, reinterpret_cast<const uint8_t*>(&ts), sizeof(ts)); // HEARTBEAT
                    lastHeartbeat = now;
                }
                if (nowMs() - m_lastHeartbeatAckMs.load() > 6000) {
                    std::cout << "[ConnectionManager] Heartbeat timeout; reconnecting.\n";
                    break;
                }
                syncStreams();  // picks up toggles made in the UI (at most ~1 s later when idle)

                size_t received = 0;
                if (!m_transport->receive(rxBuf, sizeof(rxBuf), received)) {
                    std::cout << "[ConnectionManager] Status: Disconnected (USB unplugged or stopped on phone).\n";
                    break;
                }

                auto& videoRec = m_videoReceiver;
                auto& audioRec = m_audioReceiver;

                const auto parseError = m_parser.feed(std::span<const uint8_t>(rxBuf, received), [this, &videoRec, &audioRec](phonebridge::protocol::Packet&& pkt) {
                    if (pkt.header.type == 0x0100) { // VIDEO_CONFIG
                        videoRec.handleVideoConfig(pkt.payload.data(), pkt.payload.size());
                    } else if (pkt.header.type == 0x0101) { // VIDEO_FRAME
                        ++m_videoFrames;
                        m_lastVideoMs = nowMs();
                        videoRec.handleVideoFrame(pkt.payload.data(), pkt.payload.size(), pkt.header.flags, pkt.header.timestamp);
                    } else if (pkt.header.type == 0x0201) { // AUDIO_FRAME
                        m_lastAudioMs = nowMs();
                        audioRec.handleAudioFrame(pkt.payload.data(), pkt.payload.size(), pkt.header.timestamp);
                    } else if (pkt.header.type == 0x0F01) { // HEARTBEAT_ACK
                        m_lastHeartbeatAckMs = nowMs();
                    }
                });
                if (parseError != phonebridge::protocol::ErrorCode::Ok) {
                    std::cerr << "[ConnectionManager] Protocol error while streaming; reconnecting.\n";
                    break;
                }
            }
        }

        setDeviceName("");
        std::cout << "[ConnectionManager] Cleaning up Session #" << m_sessionId << "...\n";
        m_transport->disconnect();
        m_state = ConnectionState::DISCONNECTED;
        std::cout << "[ConnectionManager] Status: Disconnected. Reconnecting in 5 seconds...\n";

        for (int i = 0; i < 50 && m_running; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    m_state = ConnectionState::DISCONNECTED;
}

} // namespace phonebridge
