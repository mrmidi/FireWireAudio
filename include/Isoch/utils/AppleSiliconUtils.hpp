#pragma once

#include <mach/mach_time.h>
#include <cstdint>

namespace FWA {
namespace Isoch {
namespace AppleSilicon {

// ─── NEW: Safe mach_absolute_time conversion for Apple Silicon ───
inline uint64_t now_ns() {
    static mach_timebase_info_data_t tb{};
    if (tb.denom == 0) {
        mach_timebase_info(&tb);
    }
    return mach_absolute_time() * tb.numer / tb.denom;
}

// Convert mach time units to nanoseconds
inline uint64_t mach_to_ns(uint64_t mach_time) {
    static mach_timebase_info_data_t tb{};
    if (tb.denom == 0) {
        mach_timebase_info(&tb);
    }
    return mach_time * tb.numer / tb.denom;
}

// Convert nanoseconds to microseconds
inline double ns_to_us(uint64_t ns) {
    return static_cast<double>(ns) / 1000.0;
}
// ───────────────────────────────────────────────────────

} // namespace AppleSilicon
} // namespace Isoch
} // namespace FWA