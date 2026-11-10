#include "math/trig.h"

#include <cmath>
#include <mutex>

namespace rf {
namespace {

constexpr int kSineBits = 13;
constexpr int kSineSize = 1 << kSineBits;  // 8192
constexpr int kSineMask = kSineSize - 1;

constexpr int kAtanSize = 4097;  // index/4096 in [0,1], plus endpoint

fixed_t g_sine[kSineSize];
uint32_t g_atan[kAtanSize];
bool g_ready = false;
std::once_flag g_once;

void buildTables() {
    constexpr double twoPi = 6.283185307179586476925286766559;
    for (int i = 0; i < kSineSize; ++i) {
        const double a = twoPi * static_cast<double>(i) / kSineSize;
        long long v = std::llround(std::sin(a) * 65536.0);
        if (v > 65536) v = 65536;
        if (v < -65536) v = -65536;
        g_sine[i] = static_cast<fixed_t>(v);
    }
    for (int i = 0; i <= 4096; ++i) {
        const double r = static_cast<double>(i) / 4096.0;
        const double a = std::atan(r);  // [0, pi/4]
        const double bam = a / twoPi * 4294967296.0;
        g_atan[i] = static_cast<uint32_t>(std::llround(bam)) & 0xFFFFFFFFu;
    }
    g_ready = true;
}

}  // namespace

void initTrigTables() {
    std::call_once(g_once, buildTables);
}

bool trigTablesReady() { return g_ready; }

fixed_t sinTableAt(angle_t a) { return g_sine[(a >> (32 - kSineBits)) & kSineMask]; }

fixed_t cosTableAt(angle_t a) {
    return g_sine[((a >> (32 - kSineBits)) + (kSineSize / 4)) & kSineMask];
}

angle_t angleFromDegrees(float degrees) {
    // Normalise into [0,360) without fmod's sign quirks.
    double d = std::fmod(static_cast<double>(degrees), 360.0);
    if (d < 0.0) d += 360.0;
    const double bam = d / 360.0 * 4294967296.0;
    return static_cast<angle_t>(static_cast<uint64_t>(std::llround(bam)) & 0xFFFFFFFFull);
}

float angleToDegrees(angle_t a) {
    return static_cast<float>(static_cast<double>(a) * 360.0 / 4294967296.0);
}

angle_t atan2Fixed(fixed_t y, fixed_t x) {
    if (x == 0 && y == 0) return 0;
    const fixed_t ax = absv(x);
    const fixed_t ay = absv(y);

    uint32_t base;  // atan(|y|/|x|) in [0, 90 degrees]
    if (ax >= ay) {
        // ratio = ay/ax in Q16.16, mapped to table index [0,4096].
        const uint32_t ratio = static_cast<uint32_t>(
            (static_cast<uint64_t>(ay) << FRACBITS) / static_cast<uint64_t>(ax));
        base = g_atan[ratio >> 4];
    } else {
        const uint32_t ratio = static_cast<uint32_t>(
            (static_cast<uint64_t>(ax) << FRACBITS) / static_cast<uint64_t>(ay));
        base = ANG90 - g_atan[ratio >> 4];
    }

    if (y >= 0) {
        if (x >= 0) return base;                    // quadrant I
        return ANG180 - base;                       // quadrant II
    }
    if (x < 0) return ANG180 + base;                // quadrant III
    return ANG360 - base;                           // quadrant IV
}

angle_t angleLerp(angle_t a, angle_t b, fixed_t t) {
    const int32_t d = angleDelta(a, b);
    const fixed_t step = mul(d, t);
    return a + static_cast<uint32_t>(step);
}

}  // namespace rf
