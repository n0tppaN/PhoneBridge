#include "nv12_to_yuy2.h"
#include <array>
#include <cstdio>
#include <vector>

using namespace phonebridge::directshow;
int main() {
    int failures = 0;
    const auto check = [&failures](bool value, const char* name) {
        if (!value) { std::fprintf(stderr, "FAIL: %s\n", name); ++failures; }
    };
    // Four rows, two chroma rows, distinct U and V to catch swapped UV/order.
    const std::array<uint8_t, 24> nv12{1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16,
                                        101,151,102,152, 103,153,104,154};
    const std::array<uint8_t, 32> expected{1,101,2,151,3,102,4,152,
        5,101,6,151,7,102,8,152, 9,103,10,153,11,104,12,154,
        13,103,14,153,15,104,16,154};
    std::array<uint8_t, 32> output{};
    check(nv12ToYuy2(nv12.data(), nv12.size(), output.data(), output.size(), 4, 4), "convert");
    check(output == expected, "Y0 U Y1 V and vertical chroma replication");
    output.fill(77);
    check(!nv12ToYuy2(nv12.data(), 23, output.data(), 32, 4, 4), "short source rejected");
    check(!nv12ToYuy2(nv12.data(), 24, output.data(), 31, 4, 4), "short destination rejected");
    check(output[0] == 77 && output[31] == 77, "invalid buffers not written");
    check(!nv12ToYuy2(nullptr, 24, output.data(), 32, 4, 4), "null source");
    check(!nv12ToYuy2(nv12.data(), 24, nullptr, 32, 4, 4), "null destination");
    check(!nv12ToYuy2(nv12.data(), 24, output.data(), 32, 3, 4), "odd width");
    check(!nv12ToYuy2(nv12.data(), 24, output.data(), 32, 4, 3), "odd height");
    check(!nv12ToYuy2(nv12.data(), 24, output.data(), 32, 0, 4), "empty frame");
    fillBlackYuy2(output.data(), output.size());
    for (size_t i = 0; i < output.size(); i += 4)
        check(output[i] == 16 && output[i+1] == 128 && output[i+2] == 16 && output[i+3] == 128, "black");
    std::vector<uint8_t> fullNv12(1920 * 1080 * 3 / 2, 128);
    std::vector<uint8_t> fullYuy2(1920 * 1080 * 2 + 2, 77);
    check(nv12ToYuy2(fullNv12.data(), fullNv12.size(), fullYuy2.data() + 1,
                     fullYuy2.size() - 2, 1920, 1080), "1080p conversion");
    check(fullYuy2.front() == 77 && fullYuy2.back() == 77, "1080p guards");
    return failures ? 1 : 0;
}
