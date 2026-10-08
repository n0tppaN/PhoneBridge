#pragma once
#include "itransport.h"
#include <string>
#include <winsock2.h>

namespace phonebridge {

class AdbTransport : public ITransport {
public:
    AdbTransport(std::string host = "127.0.0.1", uint16_t port = 27183, std::string adbPath = "adb");
    ~AdbTransport() override;

    bool connect() override;
    void disconnect() override;

    bool send(const uint8_t* data, size_t size) override;
    bool receive(uint8_t* data, size_t size, size_t& received) override;

    bool isConnected() const override { return m_connected; }

private:
    bool setupAdbForward();

    std::string m_host;
    uint16_t m_port;
    std::string m_adbPath;
    SOCKET m_socket{INVALID_SOCKET};
    bool m_connected{false};
};

} // namespace phonebridge
