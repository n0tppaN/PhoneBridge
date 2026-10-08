// Incremental stream parser. A read may contain half a packet, one packet,
// or many packets; feed() handles all of them.
//
// Memory is bounded: header is validated (incl. payloadSize <= maxPayload)
// BEFORE any payload storage is allocated.
// After an error the parser is "failed" (a corrupt TCP-like stream cannot be
// resynchronised safely). The owner must reset() after reconnecting.
#pragma once
#include <functional>
#include <span>
#include "packet.h"

namespace phonebridge::protocol {

class PacketParser {
public:
    using PacketCallback = std::function<void(Packet&&)>;

    explicit PacketParser(uint32_t maxPayload = kMaxPayloadSize) : maxPayload_(maxPayload) {}

    // Consumes all of `data`. Invokes `onPacket` for each complete packet.
    // Returns Ok or the sticky error code.
    ErrorCode feed(std::span<const uint8_t> data, const PacketCallback& onPacket);

    void reset();
    ErrorCode error() const { return error_; }
    // True when no partial packet is buffered.
    bool atBoundary() const { return state_ == State::Header && headerFill_ == 0; }

private:
    enum class State { Header, Payload };

    void emit(const PacketCallback& cb);

    uint32_t maxPayload_;
    State state_ = State::Header;
    ErrorCode error_ = ErrorCode::Ok;
    HeaderBytes headerBuf_{};
    size_t headerFill_ = 0;
    size_t payloadFill_ = 0;
    Packet current_;
};

}  // namespace phonebridge::protocol
