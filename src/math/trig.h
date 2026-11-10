// Fixed-point trigonometry with binary angle measurement (BAM).
//
// Angles wrap naturally because angle_t is an unsigned 32-bit integer:
// one full turn is 2^32. The top bits select a quadrant:
//
//   ANG90  = 0x40000000
//   ANG180 = 0x80000000
//   ANG270 = 0xC0000000
//
// Sine and cosine come from a 8192-entry table (one entry per 2^32/8192
// units of angle, i.e. 0.0439 degrees), so lookup is a shift and an index.
// atan2 uses a 4097-entry table over the first octant.
#pragma once

#include <cstdint>

#include "math/fixed.h"

namespace rf {

using angle_t = uint32_t;

constexpr angle_t ANG45 = 0x20000000u;
constexpr angle_t ANG90 = 0x40000000u;
constexpr angle_t ANG180 = 0x80000000u;
constexpr angle_t ANG270 = 0xC0000000u;
constexpr angle_t ANG360 = 0x00000000u;  // wraps

// Generates the sine and atan tables. Call once before any trig use.
// Safe to call more than once (idempotent).
void initTrigTables();
bool trigTablesReady();

fixed_t sinTableAt(angle_t a);
fixed_t cosTableAt(angle_t a);

// Fixed-point angle <-> degrees, for debug output and map authoring.
angle_t angleFromDegrees(float degrees);
float angleToDegrees(angle_t a);

// atan2(y, x) in BAM. Result in [0, 2^32).
angle_t atan2Fixed(fixed_t y, fixed_t x);

// Converts a direction vector (fixed) to an angle.
inline angle_t angleOfVector(fixed_t x, fixed_t y) { return atan2Fixed(y, x); }

inline angle_t angleAdd(angle_t a, angle_t b) { return a + b; }
inline angle_t angleSub(angle_t a, angle_t b) { return a - b; }

// Shortest signed delta from a to b, in [-2^31, 2^31).
inline int32_t angleDelta(angle_t a, angle_t b) {
    return static_cast<int32_t>(b - a);
}

// Linear interpolation between angles along the shortest arc.
angle_t angleLerp(angle_t a, angle_t b, fixed_t t);

}  // namespace rf
