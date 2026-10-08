// JNI wrapper around the shared C++ PacketParser.
// feed() returns every COMPLETE, VALIDATED packet (header + payload, wire format)
// found in the bytes given; null means the stream is corrupt (see nativeError).
#include <jni.h>

#include <cstdint>
#include <vector>

#include "packet_parser.h"

using namespace phonebridge::protocol;

namespace {
PacketParser* parserOf(jlong h) { return reinterpret_cast<PacketParser*>(h); }
}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_phonebridge_protocol_NativeParser_nativeCreate(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(new PacketParser());
}

JNIEXPORT void JNICALL
Java_com_phonebridge_protocol_NativeParser_nativeDestroy(JNIEnv*, jobject, jlong h) {
    delete parserOf(h);
}

JNIEXPORT void JNICALL
Java_com_phonebridge_protocol_NativeParser_nativeReset(JNIEnv*, jobject, jlong h) {
    parserOf(h)->reset();
}

JNIEXPORT jint JNICALL
Java_com_phonebridge_protocol_NativeParser_nativeError(JNIEnv*, jobject, jlong h) {
    return static_cast<jint>(parserOf(h)->error());
}

JNIEXPORT jobjectArray JNICALL
Java_com_phonebridge_protocol_NativeParser_nativeFeed(JNIEnv* env, jobject, jlong h,
                                                      jbyteArray data, jint len) {
    if (len < 0 || env->GetArrayLength(data) < len) return nullptr;

    std::vector<uint8_t> in(static_cast<size_t>(len));
    env->GetByteArrayRegion(data, 0, len, reinterpret_cast<jbyte*>(in.data()));

    std::vector<std::vector<uint8_t>> packets;
    const ErrorCode ec = parserOf(h)->feed(in, [&](Packet&& p) {
        packets.push_back(serializePacket(p.header, p.payload));
    });
    if (ec != ErrorCode::Ok) return nullptr;

    jclass byteArrayClass = env->FindClass("[B");
    jobjectArray result =
        env->NewObjectArray(static_cast<jsize>(packets.size()), byteArrayClass, nullptr);
    for (size_t i = 0; i < packets.size(); ++i) {
        jbyteArray b = env->NewByteArray(static_cast<jsize>(packets[i].size()));
        env->SetByteArrayRegion(b, 0, static_cast<jsize>(packets[i].size()),
                                reinterpret_cast<const jbyte*>(packets[i].data()));
        env->SetObjectArrayElement(result, static_cast<jsize>(i), b);
        env->DeleteLocalRef(b);
    }
    env->DeleteLocalRef(byteArrayClass);
    return result;
}

}  // extern "C"
