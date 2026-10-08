// PacketHeader + explicit little-endian (de)serialization.
// Never memcpy the struct: its in-memory layout is padded (sizeof == 32).
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>
#include "protocol.h"

namespace phonebridge::protocol {

struct PacketHeader {
    uint32_t magic = kMagic;      // constant kMagic
    uint16_t version = kVersion;  // protocol version
    uint16_t type = 0;            // PacketType
    uint32_t flags = 0;           // see flag layout in protocol.h
    uint64_t timestamp = 0;       // sender monotonic clock, microseconds
    uint32_t payloadSize = 0;     // bytes following the header, <= kMaxPayloadSize
    uint32_t sequence = 0;        // per-stream counter, wraps at 2^32
};

using HeaderBytes = std::array<uint8_t, kHeaderSize>;

namespace detail {
inline void put16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
inline void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i)); }
inline void put64(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; ++i) p[i] = uint8_t(v >> (8 * i)); }
inline uint16_t get16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline uint32_t get32(const uint8_t* p) { uint32_t v = 0; for (int i = 3; i >= 0; --i) v = (v << 8) | p[i]; return v; }
inline uint64_t get64(const uint8_t* p) { uint64_t v = 0; for (int i = 7; i >= 0; --i) v = (v << 8) | p[i]; return v; }
}  // namespace detail

inline HeaderBytes encodeHeader(const PacketHeader& h) {
    HeaderBytes b{};
    detail::put32(&b[0], h.magic);
    detail::put16(&b[4], h.version);
    detail::put16(&b[6], h.type);
    detail::put32(&b[8], h.flags);
    detail::put64(&b[12], h.timestamp);
    detail::put32(&b[20], h.payloadSize);
    detail::put32(&b[24], h.sequence);
    return b;
}

inline PacketHeader decodeHeader(const HeaderBytes& b) {
    PacketHeader h;
    h.magic = detail::get32(&b[0]);
    h.version = detail::get16(&b[4]);
    h.type = detail::get16(&b[6]);
    h.flags = detail::get32(&b[8]);
    h.timestamp = detail::get64(&b[12]);
    h.payloadSize = detail::get32(&b[20]);
    h.sequence = detail::get32(&b[24]);
    return h;
}

// Structural validation only (no stateful checks). Order matters: magic first.
inline ErrorCode validateHeader(const PacketHeader& h, uint32_t maxPayload = kMaxPayloadSize) {
    if (h.magic != kMagic) return ErrorCode::InvalidPacket;
    if (h.version != kVersion) return ErrorCode::ProtocolVersion;
    if (!isKnownType(h.type)) return ErrorCode::InvalidType;
    if (h.payloadSize > maxPayload) return ErrorCode::PacketTooLarge;
    return ErrorCode::Ok;
}

// Serializes header+payload; payloadSize is set from `payload`.
inline std::vector<uint8_t> serializePacket(PacketHeader h, std::span<const uint8_t> payload) {
    h.payloadSize = static_cast<uint32_t>(payload.size());
    const HeaderBytes hb = encodeHeader(h);
    std::vector<uint8_t> out;
    out.reserve(kHeaderSize + payload.size());
    out.insert(out.end(), hb.begin(), hb.end());
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

struct Packet {
    PacketHeader header;
    std::vector<uint8_t> payload;
};

}  // namespace phonebridge::protocol
