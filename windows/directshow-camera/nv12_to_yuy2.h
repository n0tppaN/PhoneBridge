#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

namespace phonebridge::directshow {
// Tightly packed, even-sized NV12 -> top-down YUY2 (Y0 U Y1 V).
// Chroma is shared by each 2x2 NV12 block: replicate its U/V on both rows.
// No colour-matrix/range conversion, scaling, or bottom-up RGB interpretation.
inline bool nv12ToYuy2(const uint8_t* src, size_t srcBytes, uint8_t* dst,
                       size_t dstBytes, uint32_t width, uint32_t height) noexcept {
    if (!src || !dst || width < 2 || height < 2 || (width & 1) || (height & 1)) return false;
    if (size_t(width) > std::numeric_limits<size_t>::max() / height / 2) return false;
    const size_t pixels = size_t(width) * height;
    if (srcBytes < pixels + pixels / 2 || dstBytes < pixels * 2) return false;
    const uint8_t* uv = src + pixels;
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* luma = src + size_t(y) * width;
        const uint8_t* chroma = uv + size_t(y / 2) * width;
        uint8_t* out = dst + size_t(y) * width * 2;
        for (uint32_t x = 0; x < width; x += 2) {
            const size_t offset = size_t(x) * 2;
            out[offset] = luma[x];
            out[offset + 1] = chroma[x];
            out[offset + 2] = luma[x + 1];
            out[offset + 3] = chroma[x + 1];
        }
    }
    return true;
}
inline void fillBlackYuy2(uint8_t* dst, size_t bytes) noexcept {
    // Limited-range black, neutral chroma; caller supplies a full YUY2 sample.
    for (size_t i = 0; i + 3 < bytes; i += 4) {
        dst[i] = 16; dst[i + 1] = 128; dst[i + 2] = 16; dst[i + 3] = 128;
    }
}
} // namespace phonebridge::directshow
