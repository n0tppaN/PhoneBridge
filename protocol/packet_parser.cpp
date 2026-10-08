#include "packet_parser.h"
#include <algorithm>
#include <cstring>

namespace phonebridge::protocol {

void PacketParser::reset() {
    state_ = State::Header;
    error_ = ErrorCode::Ok;
    headerFill_ = 0;
    payloadFill_ = 0;
    current_ = Packet{};
}

void PacketParser::emit(const PacketCallback& cb) {
    Packet done = std::move(current_);
    current_ = Packet{};
    state_ = State::Header;
    headerFill_ = 0;
    payloadFill_ = 0;
    if (cb) cb(std::move(done));
}

ErrorCode PacketParser::feed(std::span<const uint8_t> data, const PacketCallback& onPacket) {
    if (error_ != ErrorCode::Ok) return error_;

    while (!data.empty()) {
        if (state_ == State::Header) {
            const size_t n = std::min(kHeaderSize - headerFill_, data.size());
            std::memcpy(headerBuf_.data() + headerFill_, data.data(), n);
            headerFill_ += n;
            data = data.subspan(n);
            if (headerFill_ < kHeaderSize) break;  // need more bytes

            const PacketHeader h = decodeHeader(headerBuf_);
            if (const ErrorCode ec = validateHeader(h, maxPayload_); ec != ErrorCode::Ok) {
                error_ = ec;
                return error_;
            }
            current_.header = h;
            if (h.payloadSize == 0) {
                emit(onPacket);
            } else {
                current_.payload.resize(h.payloadSize);  // safe: already <= maxPayload_
                payloadFill_ = 0;
                state_ = State::Payload;
            }
        } else {
            const size_t want = current_.header.payloadSize - payloadFill_;
            const size_t n = std::min(want, data.size());
            std::memcpy(current_.payload.data() + payloadFill_, data.data(), n);
            payloadFill_ += n;
            data = data.subspan(n);
            if (payloadFill_ == current_.header.payloadSize) emit(onPacket);
        }
    }
    return ErrorCode::Ok;
}

}  // namespace phonebridge::protocol
