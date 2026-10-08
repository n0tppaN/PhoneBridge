#pragma once
#include <cstdint>
#include <cstddef>

namespace phonebridge {

class ITransport {
public:
    virtual ~ITransport() = default;

    virtual bool connect() = 0;
    virtual void disconnect() = 0;

    virtual bool send(const uint8_t* data, size_t size) = 0;
    virtual bool receive(uint8_t* data, size_t size, size_t& received) = 0;

    virtual bool isConnected() const = 0;
};

} // namespace phonebridge
