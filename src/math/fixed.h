// 16.16 fixed-point arithmetic for the whole engine.
//
// Representation
// --------------
//   fixed_t is a signed 32-bit integer. The low FRACBITS (16) bits are the
//   fractional part, the high 16 bits are the integer part.
//
//       fixed = real * 65536
//       real  = fixed / 65536
//
//   Range is [-32768.0, +32767.99998] with resolution 1/65536 (~0.0000153).
//   World coordinates are stored in map units where 1 unit == FRACUNIT.
//
// Multiplication
// --------------
//   FixedMul(a,b) = (int64)a * b >> 16.  The 64-bit intermediate is required:
//   a 32x32 product cannot be represented in 32 bits. The final right shift
//   removes the doubled fractional scale (Q16.16 * Q16.16 = Q32.32, and
//   >>16 gives Q16.16 again).
//
// Division
// --------
//   FixedDiv(a,b) = ((int64)a << 16) / b.  Shifting the dividend left by 16
//   restores the scaling lost when the two fractions divide.
//
// Overflow policy
// ---------------
//   Results that leave the int32 range are saturated to INT32_MIN/INT32_MAX
//   (the alternative, wrapping, can silently turn a huge positive number
//   into a huge negative one). Division by zero returns +/-INT32_MAX or 0
//   depending on the sign of the numerator. These choices are deliberate and
//   are covered by tests; callers in hot paths are expected to keep values in
//   range so saturation never triggers.
//
// Rounding
// --------
//   mul() uses an arithmetic right shift, which truncates toward negative
//   infinity (DOOM's FixedMul behaves the same way). toInt() truncates
//   toward zero. Use toIntRound() when nearest-integer behaviour is wanted.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace rf {

using fixed_t = int32_t;

constexpr int FRACBITS = 16;
constexpr fixed_t FRACUNIT = 1 << FRACBITS;   // 65536
constexpr fixed_t FIXED_MIN = std::numeric_limits<fixed_t>::min();
constexpr fixed_t FIXED_MAX = std::numeric_limits<fixed_t>::max();

// Compile-time conversion for small literals. Not range-checked; use
// fromIntSat() for arbitrary runtime values.
constexpr fixed_t fromInt(int v) { return static_cast<fixed_t>(v * FRACUNIT); }

// Runtime conversion with saturation (int * 65536 can overflow int32).
inline fixed_t fromIntSat(int v) {
    const int64_t r = static_cast<int64_t>(v) * FRACUNIT;
    if (r > FIXED_MAX) return FIXED_MAX;
    if (r < FIXED_MIN) return FIXED_MIN;
    return static_cast<fixed_t>(r);
}

inline int toInt(fixed_t v) { return v / FRACUNIT; }

inline int toIntRound(fixed_t v) {
    const int64_t bias = (v >= 0) ? (FRACUNIT / 2) : -(FRACUNIT / 2);
    return static_cast<int>((static_cast<int64_t>(v) + bias) / FRACUNIT);
}

inline float toFloat(fixed_t v) { return static_cast<float>(v) / static_cast<float>(FRACUNIT); }

// Converts float to fixed, saturating. Used for content generation and
// debug values only; gameplay/render hot paths stay in fixed-point.
inline fixed_t fromFloat(float f) {
    const double scaled = static_cast<double>(f) * static_cast<double>(FRACUNIT);
    if (!(scaled == scaled)) return 0;  // NaN
    if (scaled >= static_cast<double>(FIXED_MAX)) return FIXED_MAX;
    if (scaled <= static_cast<double>(FIXED_MIN)) return FIXED_MIN;
    return static_cast<fixed_t>(std::llround(scaled));
}

inline fixed_t absv(fixed_t v) {
    if (v == FIXED_MIN) return FIXED_MAX;  // -INT_MIN is not representable
    return v < 0 ? -v : v;
}

inline fixed_t clampv(fixed_t v, fixed_t lo, fixed_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Q16.16 * Q16.16 -> Q16.16, 64-bit intermediate, saturating.
inline fixed_t mul(fixed_t a, fixed_t b) {
    const int64_t r = (static_cast<int64_t>(a) * static_cast<int64_t>(b)) >> FRACBITS;
    if (r > FIXED_MAX) return FIXED_MAX;
    if (r < FIXED_MIN) return FIXED_MIN;
    return static_cast<fixed_t>(r);
}

// Q16.16 / Q16.16 -> Q16.16, saturating, zero-protected.
inline fixed_t div(fixed_t a, fixed_t b) {
    if (b == 0) {
        if (a > 0) return FIXED_MAX;
        if (a < 0) return FIXED_MIN;
        return 0;
    }
    const int64_t r = (static_cast<int64_t>(a) << FRACBITS) / b;
    if (r > FIXED_MAX) return FIXED_MAX;
    if (r < FIXED_MIN) return FIXED_MIN;
    return static_cast<int32_t>(r);
}

// (a * b) / c with a 64-bit product and saturation. Used where a*b would
// otherwise overflow before the division (projection, texture stepping).
inline fixed_t mulDiv(fixed_t a, fixed_t b, fixed_t c) {
    if (c == 0) {
        const int64_t ab = static_cast<int64_t>(a) * b;
        if (ab > 0) return FIXED_MAX;
        if (ab < 0) return FIXED_MIN;
        return 0;
    }
    const int64_t r = (static_cast<int64_t>(a) * static_cast<int64_t>(b)) / c;
    if (r > FIXED_MAX) return FIXED_MAX;
    if (r < FIXED_MIN) return FIXED_MIN;
    return static_cast<int32_t>(r);
}

inline fixed_t lerp(fixed_t a, fixed_t b, fixed_t t) {
    return a + mul(b - a, t);
}

// Integer square root of a 64-bit value (no floating point).
inline uint64_t isqrt64(uint64_t v) {
    if (v == 0) return 0;
    uint64_t x = 1;
    // Scale an initial power-of-two estimate up to the value's magnitude.
    while (x * x < v && x < (uint64_t(1) << 31)) x <<= 1;
    // Newton iterations from the doubling estimate.
    uint64_t y = v;
    for (int i = 0; i < 8 && x != 0; ++i) {
        x = (x + v / x) >> 1;
    }
    (void)y;
    return x;
}

// sqrt(value) in fixed point: sqrt(v/65536)*65536 == sqrt(v*65536).
inline fixed_t sqrtFixed(fixed_t v) {
    if (v <= 0) return 0;
    const uint64_t scaled = static_cast<uint64_t>(v) << FRACBITS;
    const uint64_t r = isqrt64(scaled);
    if (r > static_cast<uint64_t>(FIXED_MAX)) return FIXED_MAX;
    return static_cast<fixed_t>(r);
}

}  // namespace rf
