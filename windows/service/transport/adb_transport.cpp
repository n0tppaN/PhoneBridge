#include "adb_transport.h"
#include <iostream>
#include <ws2tcpip.h>
#include <cstdlib>
#include <windows.h>

namespace phonebridge {

AdbTransport::AdbTransport(std::string host, uint16_t port, std::string adbPath)
    : m_host(std::move(host)), m_port(port), m_adbPath(std::move(adbPath)) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
}

AdbTransport::~AdbTransport() {
    disconnect();
    WSACleanup();
}

bool AdbTransport::setupAdbForward() {
    std::string actualAdb = m_adbPath;

    // Automatically locate adb.exe in standard Android SDK path if not found in PATH
    char* userProfile = nullptr;
    size_t len = 0;
    if (_dupenv_s(&userProfile, &len, "USERPROFILE") == 0 && userProfile != nullptr) {
        std::string sdkAdb = std::string(userProfile) + "\\AppData\\Local\\Android\\Sdk\\platform-tools\\adb.exe";
        free(userProfile);

        DWORD attr = GetFileAttributesA(sdkAdb.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            actualAdb = "\"" + sdkAdb + "\"";
        }
    }

    std::string cmd = actualAdb + " forward tcp:" + std::to_string(m_port) + " localabstract:phonebridge";
    std::cout << "[AdbTransport] Executing ADB command: " << cmd << "\n";
    return (std::system(cmd.c_str()) == 0);
}

bool AdbTransport::connect() {
    if (m_connected) return true;
    if (!setupAdbForward()) {
        std::cerr << "[AdbTransport] Failed to execute adb forward.\n";
        return false;
    }

    m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_socket == INVALID_SOCKET) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);
    inet_pton(AF_INET, m_host.c_str(), &addr.sin_addr);

    if (::connect(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        return false;
    }

    m_connected = true;
    std::cout << "[AdbTransport] Connected to " << m_host << ":" << m_port << "\n";
    return true;
}

void AdbTransport::disconnect() {
    if (!m_connected) return;
    if (m_socket != INVALID_SOCKET) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
    m_connected = false;
    std::cout << "[AdbTransport] Disconnected.\n";
}

bool AdbTransport::send(const uint8_t* data, size_t size) {
    if (!m_connected || m_socket == INVALID_SOCKET) return false;
    size_t totalSent = 0;
    while (totalSent < size) {
        int sent = ::send(m_socket, reinterpret_cast<const char*>(data + totalSent), static_cast<int>(size - totalSent), 0);
        if (sent == SOCKET_ERROR) {
            m_connected = false;
            return false;
        }
        totalSent += sent;
    }
    return true;
}

bool AdbTransport::receive(uint8_t* data, size_t size, size_t& received) {
    if (!m_connected || m_socket == INVALID_SOCKET) return false;
    int res = ::recv(m_socket, reinterpret_cast<char*>(data), static_cast<int>(size), 0);
    if (res <= 0) {
        m_connected = false;
        received = 0;
        return false;
    }
    received = static_cast<size_t>(res);
    return true;
}

} // namespace phonebridge
