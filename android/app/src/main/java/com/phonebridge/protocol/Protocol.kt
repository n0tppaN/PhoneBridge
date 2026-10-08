package com.phonebridge.protocol

import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Mirrors <repo>/protocol/protocol.h. Wire format is little endian. */
object Protocol {
    const val MAGIC = 0x50484252
    const val VERSION = 1
    const val HEADER_SIZE = 28
    const val MAX_PAYLOAD = 4 * 1024 * 1024

    const val STREAM_CONTROL = 0
    const val STREAM_VIDEO = 1
    const val STREAM_AUDIO = 2
    const val FLAG_KEYFRAME = 1 shl 8
    const val FLAG_CONFIG = 1 shl 9
    const val FLAG_DISCONTINUITY = 1 shl 10

    /** adb: `adb forward tcp:27183 localabstract:phonebridge` */
    const val SOCKET_NAME = "phonebridge"
}

enum class PacketType(val id: Int) {
    HELLO(0x0001), HELLO_ACK(0x0002), DEVICE_INFO(0x0003), CAPABILITIES(0x0004),
    CONFIG_REQUEST(0x0010), CONFIG_RESPONSE(0x0011),
    START_CAMERA(0x0020), STOP_CAMERA(0x0021), START_MICROPHONE(0x0022), STOP_MICROPHONE(0x0023),
    VIDEO_CONFIG(0x0100), VIDEO_FRAME(0x0101), AUDIO_CONFIG(0x0200), AUDIO_FRAME(0x0201),
    REQUEST_KEYFRAME(0x0300),
    HEARTBEAT(0x0F00), HEARTBEAT_ACK(0x0F01), ERROR(0x0FFE), GOODBYE(0x0FFF);

    companion object {
        private val byId = entries.associateBy { it.id }
        fun fromId(id: Int): PacketType? = byId[id]
    }
}

class Packet(
    val type: Int,
    val flags: Int,
    val timestampUs: Long,
    val sequence: Int,
    val payload: ByteArray,
)

object PacketCodec {
    fun encode(type: Int, flags: Int, timestampUs: Long, sequence: Int, payload: ByteArray): ByteArray {
        val bb = ByteBuffer.allocate(Protocol.HEADER_SIZE + payload.size).order(ByteOrder.LITTLE_ENDIAN)
        bb.putInt(Protocol.MAGIC)
        bb.putShort(Protocol.VERSION.toShort())
        bb.putShort(type.toShort())
        bb.putInt(flags)
        bb.putLong(timestampUs)
        bb.putInt(payload.size)
        bb.putInt(sequence)
        bb.put(payload)
        return bb.array()
    }

    /** `raw` must be one complete packet already validated by [NativeParser]. */
    fun decode(raw: ByteArray): Packet {
        val bb = ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN)
        bb.getInt()                                  // magic
        bb.getShort()                                // version
        val type = bb.getShort().toInt() and 0xFFFF
        val flags = bb.getInt()
        val ts = bb.getLong()
        val size = bb.getInt()
        val seq = bb.getInt()
        return Packet(type, flags, ts, seq, raw.copyOfRange(Protocol.HEADER_SIZE, Protocol.HEADER_SIZE + size))
    }
}
