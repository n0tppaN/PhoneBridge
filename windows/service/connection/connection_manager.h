#pragma once
#include "transport/itransport.h"
#include "packet_parser.h"
#include "recovery_manager.h"
#include "video/video_receiver.h"
#include "audio/audio_receiver.h"
#include <memory>
#include <string>
#include <atomic>
#include <thread>
#include <cstdint>
#include <mutex>

namespace phonebridge {

enum class ConnectionState {
    DISCONNECTED,
    DISCOVERING,
    CONNECTING,
    HANDSHAKING,
    CONFIGURING,
    STREAMING,
    RECONNECTING,
    FAILED
};

class ConnectionManager {
public:
    explicit ConnectionManager(std::unique_ptr<ITransport> transport);
    ~ConnectionManager();

    bool start();
    void stop();

    ConnectionState getState() const { return m_state.load(); }

    // --- control surface used by the desktop UI (all thread-safe) ---------------------------
    // What the user wants streamed. Applied live (START_/STOP_ packets) while connected, and
    // re-applied automatically after every reconnect.
    void setWanted(bool camera, bool mic) { m_wantCamera = camera; m_wantMic = mic; }
    bool wantCamera() const { return m_wantCamera.load(); }
    bool wantMic() const { return m_wantMic.load(); }
    uint32_t sessionId() const { return m_sessionId.load(); }
    // True while data of that kind arrived in the last ~1.5 s.
    bool cameraLive() const { return isRecent(m_lastVideoMs.load()); }
    bool micLive() const { return isRecent(m_lastAudioMs.load()); }
    uint64_t videoFrameCount() const { return m_videoFrames.load(); }
    // Name the phone reported in its DEVICE_INFO packet ("" when not connected).
    std::string deviceName() const { std::lock_guard<std::mutex> g(m_deviceMutex); return m_deviceName; }

private:
    void runLoop();
    bool performHandshake();
    bool sendPacket(uint16_t type, const uint8_t* payload, size_t size);
    void syncStreams();  // send START_/STOP_ packets so the phone matches m_want*
    static int64_t nowMs();
    static bool isRecent(int64_t lastMs);

    std::unique_ptr<ITransport> m_transport;
    phonebridge::protocol::PacketParser m_parser;
    RecoveryManager m_recovery;
    video::VideoReceiver m_videoReceiver;
    audio::AudioReceiver m_audioReceiver;
    std::atomic<bool> m_running{false};
    std::atomic<ConnectionState> m_state{ConnectionState::DISCONNECTED};
    std::thread m_workerThread;
    std::atomic<uint32_t> m_sessionId{0};

    std::atomic<bool> m_wantCamera{true};   // defaults keep the old console behaviour (both on)
    std::atomic<bool> m_wantMic{true};
    bool m_sentCamera{false};               // worker thread only: what the phone was last told
    bool m_sentMic{false};
    std::atomic<int64_t> m_lastVideoMs{0};
    std::atomic<int64_t> m_lastAudioMs{0};
    std::atomic<uint64_t> m_videoFrames{0};
    mutable std::mutex m_deviceMutex;
    std::string m_deviceName;
    void setDeviceName(std::string name) { std::lock_guard<std::mutex> g(m_deviceMutex); m_deviceName = std::move(name); }
};

} // namespace phonebridge
