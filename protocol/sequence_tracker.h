// Per-stream sequence check (wrap-safe, 32-bit serial arithmetic).
#pragma once
#include <cstdint>

namespace phonebridge::protocol {

class SequenceTracker {
public:
    enum class Result { InOrder, Gap, Duplicate };
    struct Outcome { Result result; uint32_t lost; };

    // First packet after construction/reset is always accepted.
    Outcome observe(uint32_t seq) {
        if (!started_) { started_ = true; next_ = seq + 1; return {Result::InOrder, 0}; }
        const uint32_t delta = seq - next_;  // modulo 2^32
        if (delta == 0) { ++next_; return {Result::InOrder, 0}; }
        if (delta < 0x80000000u) { next_ = seq + 1; return {Result::Gap, delta}; }
        return {Result::Duplicate, 0};  // older than expected: stale/duplicate/reordered
    }
    void reset() { started_ = false; }

private:
    bool started_ = false;
    uint32_t next_ = 0;
};

}  // namespace phonebridge::protocol
