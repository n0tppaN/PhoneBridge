#include <cstdio>
#include <cstdlib>
#include <random>
#include "packet_parser.h"
#include "sequence_tracker.h"

using namespace phonebridge::protocol;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static std::vector<uint8_t> makePacket(PacketType t, uint32_t seq, size_t payloadLen, uint8_t fill = 0xAB) {
    PacketHeader h; h.type = uint16_t(t); h.sequence = seq; h.timestamp = 123456789012ull + seq;
    h.flags = uint32_t(StreamId::Video) | kFlagKeyframe;
    std::vector<uint8_t> p(payloadLen, fill);
    return serializePacket(h, p);
}

static void testWireFormat() {
    PacketHeader h; h.type = 0x0101; h.flags = 0x01020304; h.timestamp = 0x1122334455667788ull;
    h.payloadSize = 0xA0B0C0D0; h.sequence = 0x0A0B0C0D;
    auto b = encodeHeader(h);
    CHECK(b.size() == 28);
    CHECK(b[0] == 0x52 && b[1] == 0x42 && b[2] == 0x48 && b[3] == 0x50);  // magic LE
    CHECK(b[4] == 1 && b[5] == 0);                                         // version LE
    CHECK(b[6] == 0x01 && b[7] == 0x01);
    CHECK(b[8] == 0x04 && b[11] == 0x01);
    CHECK(b[12] == 0x88 && b[19] == 0x11);
    CHECK(b[20] == 0xD0 && b[23] == 0xA0);
    CHECK(b[24] == 0x0D && b[27] == 0x0A);
    auto r = decodeHeader(b);
    CHECK(r.timestamp == h.timestamp && r.payloadSize == h.payloadSize && r.sequence == h.sequence && r.flags == h.flags);
}

static void testSingleAndEmpty() {
    PacketParser p; std::vector<Packet> out;
    auto cb = [&](Packet&& k) { out.push_back(std::move(k)); };
    auto a = makePacket(PacketType::VideoFrame, 1, 100);
    auto b = makePacket(PacketType::Heartbeat, 2, 0);
    CHECK(p.feed(a, cb) == ErrorCode::Ok);
    CHECK(p.feed(b, cb) == ErrorCode::Ok);
    CHECK(out.size() == 2 && out[0].payload.size() == 100 && out[1].payload.empty());
    CHECK(p.atBoundary());
    CHECK(streamOf(out[0].header.flags) == StreamId::Video);
}

static void testByteByByte() {
    PacketParser p; std::vector<Packet> out;
    auto cb = [&](Packet&& k) { out.push_back(std::move(k)); };
    auto a = makePacket(PacketType::AudioFrame, 7, 960);
    for (size_t i = 0; i < a.size(); ++i) {
        CHECK(p.feed(std::span(&a[i], 1), cb) == ErrorCode::Ok);
        if (i + 1 < a.size()) CHECK(out.empty());
    }
    CHECK(out.size() == 1 && out[0].header.sequence == 7 && out[0].payload.size() == 960);
}

static void testMultiplePerRead() {
    PacketParser p; std::vector<Packet> out;
    std::vector<uint8_t> all;
    for (uint32_t i = 0; i < 5; ++i) { auto k = makePacket(PacketType::AudioFrame, i, 10 + i); all.insert(all.end(), k.begin(), k.end()); }
    auto extra = makePacket(PacketType::VideoFrame, 9, 50);
    all.insert(all.end(), extra.begin(), extra.begin() + 10);  // trailing partial
    CHECK(p.feed(all, [&](Packet&& k) { out.push_back(std::move(k)); }) == ErrorCode::Ok);
    CHECK(out.size() == 5 && !p.atBoundary());
    CHECK(p.feed(std::span(extra).subspan(10), [&](Packet&& k) { out.push_back(std::move(k)); }) == ErrorCode::Ok);
    CHECK(out.size() == 6 && p.atBoundary());
}

static void testRandomChunking() {
    std::mt19937 rng(42);
    std::vector<uint8_t> stream; std::vector<std::pair<uint32_t, size_t>> expect;
    for (uint32_t i = 0; i < 300; ++i) {
        size_t len = rng() % 3000;
        auto k = makePacket(i % 2 ? PacketType::VideoFrame : PacketType::AudioFrame, i, len, uint8_t(i));
        stream.insert(stream.end(), k.begin(), k.end()); expect.push_back({i, len});
    }
    PacketParser p; size_t idx = 0; bool ok = true;
    for (size_t pos = 0; pos < stream.size();) {
        size_t n = std::min<size_t>(1 + rng() % 5000, stream.size() - pos);
        ErrorCode ec = p.feed(std::span(stream).subspan(pos, n), [&](Packet&& k) {
            if (idx >= expect.size() || k.header.sequence != expect[idx].first || k.payload.size() != expect[idx].second) ok = false;
            else for (auto c : k.payload) if (c != uint8_t(k.header.sequence)) ok = false;
            ++idx;
        });
        CHECK(ec == ErrorCode::Ok); pos += n;
    }
    CHECK(ok && idx == expect.size());
}

static ErrorCode feedBad(std::vector<uint8_t> bytes, PacketParser* pp = nullptr) {
    PacketParser local; PacketParser& p = pp ? *pp : local;
    return p.feed(bytes, [](Packet&&) {});
}

static void testRejects() {
    auto good = makePacket(PacketType::Heartbeat, 1, 0);
    { auto b = good; b[0] ^= 0xFF; CHECK(feedBad(b) == ErrorCode::InvalidPacket); }
    { auto b = good; b[4] = 2; CHECK(feedBad(b) == ErrorCode::ProtocolVersion); }
    { auto b = good; b[6] = 0x77; b[7] = 0x77; CHECK(feedBad(b) == ErrorCode::InvalidType); }
    { auto b = good; b[20] = b[21] = b[22] = b[23] = 0xFF; CHECK(feedBad(b) == ErrorCode::PacketTooLarge); }
    { PacketParser p(1000); auto b = makePacket(PacketType::VideoFrame, 1, 1001); CHECK(feedBad(b, &p) == ErrorCode::PacketTooLarge); }
    // sticky error + reset
    PacketParser p; auto bad = good; bad[0] = 0;
    CHECK(feedBad(bad, &p) == ErrorCode::InvalidPacket);
    CHECK(feedBad(good, &p) == ErrorCode::InvalidPacket);
    p.reset();
    CHECK(feedBad(good, &p) == ErrorCode::Ok);
}

static void testOversizeDoesNotAllocate() {
    // Header claims max+1: must be rejected before any payload buffer is sized.
    PacketHeader h; h.type = uint16_t(PacketType::VideoFrame); h.payloadSize = kMaxPayloadSize + 1;
    auto hb = encodeHeader(h);
    PacketParser p;
    CHECK(p.feed(hb, [](Packet&&) {}) == ErrorCode::PacketTooLarge);
}

static void testSequence() {
    SequenceTracker t; using R = SequenceTracker::Result;
    CHECK(t.observe(100).result == R::InOrder);
    CHECK(t.observe(101).result == R::InOrder);
    auto g = t.observe(105); CHECK(g.result == R::Gap && g.lost == 3);
    CHECK(t.observe(105).result == R::Duplicate);
    CHECK(t.observe(106).result == R::InOrder);
    SequenceTracker w; w.observe(0xFFFFFFFEu);
    CHECK(w.observe(0xFFFFFFFFu).result == R::InOrder);
    CHECK(w.observe(0).result == R::InOrder);
    CHECK(w.observe(1).result == R::InOrder);
}

int main() {
    testWireFormat(); testSingleAndEmpty(); testByteByByte(); testMultiplePerRead();
    testRandomChunking(); testRejects(); testOversizeDoesNotAllocate(); testSequence();
    std::printf(g_fail ? "FAILED (%d)\n" : "ALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
