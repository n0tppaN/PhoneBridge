#pragma once
#include <atomic>

namespace pb {
// Number of live COM objects + server locks. DllCanUnloadNow uses it.
inline std::atomic<long>& moduleCount() {
    static std::atomic<long> c{0};
    return c;
}
}  // namespace pb
