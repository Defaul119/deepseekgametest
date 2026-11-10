// Fixed-point 2D vector used for positions, directions and plane maths.
#pragma once

#include "math/fixed.h"
#include "math/trig.h"

namespace rf {

struct Vec2 {
    fixed_t x = 0;
    fixed_t y = 0;

    constexpr Vec2() = default;
    constexpr Vec2(fixed_t ix, fixed_t iy) : x(ix), y(iy) {}
};

inline Vec2 operator+(const Vec2& a, const Vec2& b) { return {static_cast<fixed_t>(a.x + b.x), static_cast<fixed_t>(a.y + b.y)}; }
inline Vec2 operator-(const Vec2& a, const Vec2& b) { return {static_cast<fixed_t>(a.x - b.x), static_cast<fixed_t>(a.y - b.y)}; }
inline Vec2 operator-(const Vec2& a) { return {static_cast<fixed_t>(-a.x), static_cast<fixed_t>(-a.y)}; }
inline Vec2 operator*(const Vec2& a, fixed_t s) { return {mul(a.x, s), mul(a.y, s)}; }
inline Vec2 operator*(fixed_t s, const Vec2& a) { return a * s; }
inline Vec2 operator/(const Vec2& a, fixed_t s) { return {div(a.x, s), div(a.y, s)}; }

inline Vec2& operator+=(Vec2& a, const Vec2& b) { a.x = static_cast<fixed_t>(a.x + b.x); a.y = static_cast<fixed_t>(a.y + b.y); return a; }
inline Vec2& operator-=(Vec2& a, const Vec2& b) { a.x = static_cast<fixed_t>(a.x - b.x); a.y = static_cast<fixed_t>(a.y - b.y); return a; }
inline Vec2& operator*=(Vec2& a, fixed_t s) { a.x = mul(a.x, s); a.y = mul(a.y, s); return a; }

inline fixed_t dot(const Vec2& a, const Vec2& b) {
    return static_cast<fixed_t>(mul(a.x, b.x) + mul(a.y, b.y));
}
inline fixed_t cross(const Vec2& a, const Vec2& b) { return mul(a.x, b.y) - mul(a.y, b.x); }

inline fixed_t lengthSquared(const Vec2& a) {
    return static_cast<fixed_t>(mul(a.x, a.x) + mul(a.y, a.y));
}
inline fixed_t length(const Vec2& a) { return sqrtFixed(lengthSquared(a)); }
inline fixed_t distance(const Vec2& a, const Vec2& b) { return length(b - a); }
inline fixed_t distanceSquared(const Vec2& a, const Vec2& b) { return lengthSquared(b - a); }

inline Vec2 normalized(const Vec2& a) {
    const fixed_t len = length(a);
    if (len == 0) return {0, 0};
    return a / len;
}

// Angle 0 points along +x; angles increase toward +y. The world uses
// screen-like coordinates (y grows downward on the map), so a positive
// angle turns clockwise in world space.
inline Vec2 angleVector(angle_t a) { return {cosTableAt(a), sinTableAt(a)}; }

inline Vec2 perpendicular(const Vec2& a) { return {static_cast<fixed_t>(-a.y), a.x}; }

// Viewer's right vector in the y-down world. For dir=(cos a, sin a),
// right = (-sin a, cos a): facing +x gives (0, 1), which is screen-right
// when y grows downward.
inline Vec2 rightVector(angle_t a) {
    return {static_cast<fixed_t>(-sinTableAt(a)), cosTableAt(a)};
}

}  // namespace rf
