// Lets the desktop UI drive the engine over stdin/stdout (see control_protocol.h).
// Header-only on purpose: no CMake change needed.
#pragma once
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include "connection/connection_manager.h"
#include "control/control_protocol.h"

namespace phonebridge::control {

class ControlChannel {
public:
    ControlChannel(ConnectionManager& cm, std::atomic<bool>& running) : m_cm(cm), m_running(running) {}
    ~ControlChannel() { stop(); }

    void start() {
        emitLine("{\"event\":\"ready\"}");
        m_reporter = std::thread([this] { reportLoop(); });
        // The reader blocks on stdin; it is intentionally detached (the process exits via ExitProcess).
        std::thread([this] { readLoop(); }).detach();
    }

    void stop() {
        m_stop = true;
        if (m_reporter.joinable()) m_reporter.join();
    }

    static void emitLine(const std::string& line) {
        std::lock_guard<std::mutex> g(outputMutex());
        std::cout << line << std::endl;  // endl = flush, the UI reads line by line
    }

private:
    static std::mutex& outputMutex() { static std::mutex m; return m; }

    static const char* stateName(ConnectionState s) {
        switch (s) {
            case ConnectionState::DISCONNECTED: return "disconnected";
            case ConnectionState::DISCOVERING:  return "discovering";
            case ConnectionState::CONNECTING:   return "connecting";
            case ConnectionState::HANDSHAKING:  return "handshaking";
            case ConnectionState::CONFIGURING:  return "configuring";
            case ConnectionState::STREAMING:    return "streaming";   // = phone connected, handshake done
            case ConnectionState::RECONNECTING: return "reconnecting";
            case ConnectionState::FAILED:       return "failed";
        }
        return "disconnected";
    }

    void emitStatus() {
        std::lock_guard<std::mutex> g(m_statusMutex);
        const auto now = std::chrono::steady_clock::now();
        const uint64_t frames = m_cm.videoFrameCount();
        const double secs = std::chrono::duration<double>(now - m_fpsTime).count();
        if (secs >= 0.9) {  // refresh the fps estimate about once per second
            m_fps = secs > 0 ? static_cast<double>(frames - m_fpsFrames) / secs : 0.0;
            m_fpsFrames = frames;
            m_fpsTime = now;
        }
        StatusView s;
        s.state = stateName(m_cm.getState());
        s.session = m_cm.sessionId();
        s.camera = m_cm.cameraLive();
        s.mic = m_cm.micLive();
        s.wantCamera = m_cm.wantCamera();
        s.wantMic = m_cm.wantMic();
        s.device = m_cm.deviceName();
        s.fpsTenths = s.camera ? static_cast<int>(m_fps * 10.0 + 0.5) : 0;
        emitLine(formatStatus(s));
    }

    void reportLoop() {
        while (!m_stop && m_running) {
            emitStatus();
            for (int i = 0; i < 5 && !m_stop && m_running; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    void readLoop() {
        std::string line;
        while (std::getline(std::cin, line)) {
            std::string cmd;
            if (!jsonString(line, "cmd", cmd)) continue;
            if (cmd == "quit") break;
            if (cmd == "set") {
                bool cam = m_cm.wantCamera();
                bool mic = m_cm.wantMic();
                jsonBool(line, "camera", cam);  // missing key = keep the current value
                jsonBool(line, "mic", mic);
                m_cm.setWanted(cam, mic);
                emitStatus();
            }
        }
        m_running = false;  // "quit", or stdin closed because the UI went away -> shut the engine down
    }

    ConnectionManager& m_cm;
    std::atomic<bool>& m_running;
    std::atomic<bool> m_stop{false};
    std::thread m_reporter;

    std::mutex m_statusMutex;
    uint64_t m_fpsFrames = 0;
    double m_fps = 0.0;
    std::chrono::steady_clock::time_point m_fpsTime = std::chrono::steady_clock::now();
};

}  // namespace phonebridge::control
