package com.phonebridge.protocol

/**
 * Incremental packet parser implemented in C++ (shared with Windows).
 * Not thread-safe: use from the single reader thread of a connection.
 */
class NativeParser : AutoCloseable {
    private var handle: Long = nativeCreate()

    /** Complete packets (raw wire bytes) in [data]; null if the stream is corrupt. */
    fun feed(data: ByteArray, len: Int): List<ByteArray>? {
        check(handle != 0L) { "parser closed" }
        return nativeFeed(handle, data, len)?.asList()
    }

    fun errorCode(): Int = nativeError(handle)

    fun reset() = nativeReset(handle)

    override fun close() {
        if (handle != 0L) {
            nativeDestroy(handle)
            handle = 0L
        }
    }

    private external fun nativeCreate(): Long
    private external fun nativeDestroy(h: Long)
    private external fun nativeReset(h: Long)
    private external fun nativeError(h: Long): Int
    private external fun nativeFeed(h: Long, data: ByteArray, len: Int): Array<ByteArray>?

    companion object {
        init {
            System.loadLibrary("phonebridge_native")
        }
    }
}
