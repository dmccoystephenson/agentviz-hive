// draw_util.h: colours, small maths and shapes shared by the hive's drawing.
#pragma once

#include <algorithm>
#include <cmath>

#include "strata_kit.h"

namespace hive {

using strata_kit::Host;
using strata_kit::Rgb;

constexpr float kPi = 3.14159265f;
constexpr float kSqrt3 = 1.7320508f;
constexpr float kWallH = 0.45f;  // comb wall height
constexpr float kHover = 1.0f;   // where a busy bee flies

const Rgb kWax{0.78f, 0.55f, 0.16f};
const Rgb kWaxTop{0.92f, 0.70f, 0.28f};
const Rgb kWaxFloor{0.42f, 0.26f, 0.07f};
const Rgb kHoney{1.0f, 0.62f, 0.05f};
const Rgb kBlack{0.05f, 0.04f, 0.03f};
const Rgb kYellow{0.98f, 0.74f, 0.10f};
const Rgb kFuzz{0.62f, 0.42f, 0.14f};
const Rgb kWing{0.85f, 0.92f, 1.0f};
const Rgb kAlarm{1.0f, 0.55f, 0.1f};
const Rgb kStallRed{1.0f, 0.08f, 0.05f};
const Rgb kWhite{1.0f, 1.0f, 1.0f};

inline float fract(float x) { return x - std::floor(x); }
inline float hash1(float n) { return fract(std::sin(n * 12.9898f) * 43758.547f); }
inline Rgb mix(Rgb a, Rgb b, float k) { return {a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k}; }
inline Rgb scale(Rgb a, float k) { return {a.r * k, a.g * k, a.b * k}; }
inline float smooth(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

// Pointy-top hex grid, cell size 1 (web/hive-model.js uses the same).
inline void axialToWorld(int q, int r, float& x, float& z) {
    x = kSqrt3 * (static_cast<float>(q) + static_cast<float>(r) * 0.5f);
    z = 1.5f * static_cast<float>(r);
}
inline int hexDist(int q, int r) { return (std::abs(q) + std::abs(r) + std::abs(q + r)) / 2; }

inline void ball(Host& h, float x, float y, float z, float sx, float sy, float sz, Rgb c) {
    h.add(STRATA_PRIMITIVE_SPHERE, x, y, z, sx, sy, sz, c);
}
// A flat disc filling one cell (a cylinder of half-height `half` centred at y).
inline void disc(Host& h, float x, float y, float z, float diameter, float half, Rgb c) {
    h.add(STRATA_PRIMITIVE_CYLINDER, x, y, z, diameter, half, diameter, c);
}

// A ring of wide-line segments on the horizontal plane at height y.
inline void ring(Host& h, float x, float y, float z, float rad, Rgb c, float px, float a, float spin = 0.0f) {
    const int n = 24;
    for (int k = 0; k < n; ++k) {
        const float b0 = k * 2.0f * kPi / n + spin, b1 = (k + 1) * 2.0f * kPi / n + spin;
        h.wideLine(x + std::cos(b0) * rad, y, z + std::sin(b0) * rad, x + std::cos(b1) * rad, y,
                   z + std::sin(b1) * rad, c, px, 0.0f, a, STRATA_LINES_ROUND_CAPS);
    }
}

}  // namespace hive
