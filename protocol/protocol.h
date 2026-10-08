// PhoneBridge wire protocol v1 - constants, enums, error codes.
// All multi-byte integers on the wire are LITTLE ENDIAN.
#pragma once
#include <cstddef>
#include <cstdint>

namespace phonebridge::protocol {

inline constexpr uint32_t kMagic = 0x50484252u;  // serialized LE: 52 42 48 50 ("RBHP")
inline constexpr uint16_t kVersion = 1;
inline constexpr size_t kHeaderSize = 28;  // wire size; NOT sizeof(PacketHeader)
inline constexpr uint32_t kMaxPayloadSize = 4u * 1024u * 1024u;  // 4 MiB

enum class PacketType : uint16_t {
    Hello = 0x0001,
    HelloAck = 0x0002,
    DeviceInfo = 0x0003,
    Capabilities = 0x0004,

    ConfigRequest = 0x0010,
    ConfigResponse = 0x0011,

    StartCamera = 0x0020,
    StopCamera = 0x0021,
    StartMicrophone = 0x0022,
    StopMicrophone = 0x0023,

    VideoConfig = 0x0100,
    VideoFrame = 0x0101,
    AudioConfig = 0x0200,
    AudioFrame = 0x0201,

    RequestKeyframe = 0x0300,

    Heartbeat = 0x0F00,
    HeartbeatAck = 0x0F01,
    Error = 0x0FFE,
    Goodbye = 0x0FFF,
    // 0x1000..0xFFFF reserved for future versions.
};

constexpr bool isKnownType(uint16_t t) {
    switch (static_cast<PacketType>(t)) {
        case PacketType::Hello: case PacketType::HelloAck:
        case PacketType::DeviceInfo: case PacketType::Capabilities:
        case PacketType::ConfigRequest: case PacketType::ConfigResponse:
        case PacketType::StartCamera: case PacketType::StopCamera:
        case PacketType::StartMicrophone: case PacketType::StopMicrophone:
        case PacketType::VideoConfig: case PacketType::VideoFrame:
        case PacketType::AudioConfig: case PacketType::AudioFrame:
        case PacketType::RequestKeyframe:
        case PacketType::Heartbeat: case PacketType::HeartbeatAck:
        case PacketType::Error: case PacketType::Goodbye:
            return true;
    }
    return false;
}

// `flags` layout:
//   bits 0-7  : stream id (StreamId)
//   bit  8    : KEYFRAME      (video: IDR access unit)
//   bit  9    : CONFIG        (video: SPS/PPS codec config data)
//   bit  10   : DISCONTINUITY (sender dropped data before this packet)
//   bits 11-31: reserved, must be 0 in v1 (receivers ignore them)
enum class StreamId : uint8_t { Control = 0, Video = 1, Audio = 2 };
inline constexpr uint32_t kFlagStreamMask = 0xFFu;
inline constexpr uint32_t kFlagKeyframe = 1u << 8;
inline constexpr uint32_t kFlagConfig = 1u << 9;
inline constexpr uint32_t kFlagDiscontinuity = 1u << 10;

constexpr StreamId streamOf(uint32_t flags) {
    return static_cast<StreamId>(flags & kFlagStreamMask);
}

enum class ErrorCode : uint16_t {
    Ok = 0,
    ProtocolVersion,
    InvalidPacket,       // bad magic / malformed header
    InvalidType,
    PacketTooLarge,
    UnsupportedConfig,
    CameraUnavailable,
    AudioUnavailable,
    EncoderFailure,
    DecoderFailure,
    TransportFailure,
    AdbUnauthorized,
    DeviceDisconnected,
    ThermalLimit,
    Timeout,
};

}  // namespace phonebridge::protocol
