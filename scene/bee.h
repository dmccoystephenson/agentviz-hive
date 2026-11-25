// bee.h: one bee per session (a big crowned lead) or subagent (a worker):
// where it is at scene time t for what it is doing, and its body.
#pragma once

#include "comb.h"

namespace hive {

using agentviz::Action;
using agentviz::State;

// The hive's reading of one wire agent: the common fields plus its extras
// (web/hive-frame.js): lead x/z, the session's colour r/g/b, active.
struct Bee {
    float x, z;          // home: the centre of its cell
    float leadX, leadZ;  // its lead's cell (a lead's own)
    bool lead, active, stalled;
    State state;
    Action action;
    agentviz::WaitingReason reason;
    float seed, appear, pulse;
    int toolCount;
    agentviz::Highlight highlight;
    Rgb band;

    bool waiting() const { return state == State::Waiting; }
    bool offline() const { return state == State::Offline; }
    // Resting: an idle or dim lead, or a resting worker.
    bool resting() const { return state == State::Idle || state == State::Dim || (!lead && !active); }
    bool working() const { return state == State::Busy && (lead || active); }
};

inline Bee beeOf(const agentviz::Agent& a) {
    const float* e = a.extra;
    Bee b{};
    b.x = a.x;
    b.z = a.z;
    b.leadX = e[0];
    b.leadZ = e[1];
    b.band = {e[2], e[3], e[4]};
    b.active = e[5] > 0.5f;
    b.lead = a.lead();
    b.stalled = a.stalled;
    b.state = a.state;
    b.action = a.action;
    b.reason = a.waitingReason;
    b.seed = a.seed;
    b.appear = a.appear;
    b.pulse = a.pulse;
    b.toolCount = a.toolCount;
    b.highlight = a.highlight;
    return b;
}

enum class Wings { Folded, Flapping, Blur, Alarm };

struct Pose {
    float x, y, z, heading;
    Wings wings;
    float headDown;  // 1 tilts the head down to the wax (edit)
};

// A point on a flight from a to b, arcing up by lift.
inline void arc(float ax, float az, float bx, float bz, float lift, float k, Pose& p) {
    const float s = smooth(k);
    p.x = ax + (bx - ax) * s;
    p.z = az + (bz - az) * s;
    p.y = kHover + lift * std::sin(kPi * s);
}

// Waiting: an alarm dance, fast tight circles with hops.
inline Pose alarmPose(const Bee& b, float t, float phase) {
    const float a = t * 9.0f + phase;
    return {b.x + std::cos(a) * 0.32f, kHover + 0.45f * std::abs(std::sin(t * 7.0f)), b.z + std::sin(a) * 0.32f,
            a + kPi * 0.5f, Wings::Alarm, 0.0f};
}

// A worker's short loop out to its lead and back, k 0..1; two lanes, out and back.
inline Pose visitLeadPose(const Bee& b, float k) {
    Pose p{b.x, kHover, b.z, 0.0f, Wings::Flapping, 0.0f};
    const float there = k < 0.5f ? k * 2.0f : (1.0f - k) * 2.0f;
    arc(b.x, b.z, b.x + (b.leadX - b.x) * 0.35f, b.z + (b.leadZ - b.z) * 0.65f, 0.0f, there, p);
    p.y = kHover + 0.7f * std::sin(kPi * k);
    const float dx = b.leadX - b.x, dz = b.leadZ - b.z, len = std::sqrt(dx * dx + dz * dz) + 1e-4f;
    const float side = std::sin(kPi * k) * 0.35f;
    p.x += -dz / len * side;
    p.z += dx / len * side;
    p.heading = std::atan2(dz, dx) + (k < 0.5f ? 0.0f : kPi);
    return p;
}

// A walk around the cell's centre at radius r: edit walks the rim, read the neighbours.
inline Pose walkPose(const Bee& b, float angle, float r, float y, float headDown) {
    return {b.x + std::cos(angle) * r, y, b.z + std::sin(angle) * r, angle + kPi * 0.5f, Wings::Folded, headDown};
}

inline void flowerOf(const Bee& b, const World& w, float& fx, float& fz) {
    fx = w.meadowX + (hash1(b.seed * 91.0f) - 0.5f) * 5.0f;
    fz = w.meadowZ + (hash1(b.seed * 37.0f) - 0.5f) * 7.0f;
}

// Web: off the comb to a flower in the field, a moment on it, and back.
inline Pose foragePose(const Bee& b, const World& w, float t) {
    Pose p{b.x, kHover, b.z, 0.0f, Wings::Flapping, 0.0f};
    const float cycle = fract(t / 9.0f + b.seed);
    float fx, fz;
    flowerOf(b, w, fx, fz);
    if (cycle < 0.4f) {
        arc(b.x, b.z, fx, fz, 3.0f, cycle / 0.4f, p);
        p.heading = std::atan2(fz - b.z, fx - b.x);
    } else if (cycle < 0.6f) {
        p = {fx + 0.2f * std::cos(t * 2.0f), 0.9f + 0.1f * std::sin(t * 4.0f), fz + 0.2f * std::sin(t * 2.0f),
             t * 2.0f + kPi * 0.5f, Wings::Flapping, 0.0f};
    } else {
        arc(fx, fz, b.x, b.z, 3.0f, (cycle - 0.6f) / 0.4f, p);
        p.heading = std::atan2(b.z - fz, b.x - fx);
    }
    return p;
}

// Delegate: the waggle dance, a straight waggling run then a loop back on alternate sides.
inline Pose wagglePose(const Bee& b, float t) {
    Pose p{b.x, kWallH + 0.32f, b.z, 0.0f, Wings::Flapping, 0.0f};
    const float cycle = fract(t / 3.2f + b.seed);
    const float side = cycle < 0.5f ? 1.0f : -1.0f;
    const float k = fract(cycle * 2.0f);
    if (k < 0.45f) {
        p.x = b.x + 0.12f * std::sin(t * 40.0f);
        p.z = b.z + 0.45f - 0.9f * (k / 0.45f);
        p.heading = -kPi * 0.5f;
    } else {
        const float a = kPi * (k - 0.45f) / 0.55f;
        p.x = b.x + side * 0.45f * std::sin(a);
        p.z = b.z - 0.45f * std::cos(a);
        p.heading = std::atan2(0.45f * std::sin(a), side * 0.45f * std::cos(a));
    }
    return p;
}

inline Pose actionPose(const Bee& b, const World& w, float t, float phase) {
    switch (b.action) {
        case Action::Edit:
            return walkPose(b, t * 0.7f + phase, 0.42f, kWallH + 0.3f + 0.06f * std::abs(std::sin(t * 5.0f + phase)),
                            1.0f);
        case Action::Run:  // hovering on the spot, buzzing
            return {b.x + 0.04f * std::sin(t * 37.0f + phase), kHover + 0.05f * std::sin(t * 23.0f),
                    b.z + 0.04f * std::cos(t * 41.0f + phase), phase + 0.2f * std::sin(t * 3.0f), Wings::Blur, 0.0f};
        case Action::Read:
            return walkPose(b, t * 0.35f + phase, 0.75f + 0.25f * std::sin(t * 0.9f + phase), kWallH + 0.32f, 0.0f);
        case Action::Web: return foragePose(b, w, t);
        case Action::Delegate: return wagglePose(b, t);
        case Action::Think: return {b.x, kWallH + 0.32f, b.z, phase, Wings::Folded, 0.0f};
        default:  // other / none: a lazy hover
            return {b.x + 0.25f * std::sin(t * 0.8f + phase), kHover + 0.1f * std::sin(t * 2.0f),
                    b.z + 0.25f * std::sin(t * 1.3f + phase), phase + 0.6f * std::sin(t * 0.5f), Wings::Flapping, 0.0f};
    }
}

// Where a bee is at time t.
inline Pose beePose(const Bee& b, const World& w, float t, bool reduced) {
    if (reduced) t = 0.0f;
    const float phase = b.seed * 6.283f;
    if (b.offline() || b.resting()) return {b.x, kWallH + 0.3f, b.z, phase, Wings::Folded, 0.0f};
    if (b.waiting()) return alarmPose(b, t, phase);
    if (!b.lead && b.action != Action::Web) {  // workers visit their lead every few seconds
        const float cycle = fract(t / 6.5f + b.seed);
        if (cycle < 0.34f) return visitLeadPose(b, cycle / 0.34f);
    }
    return actionPose(b, w, t, phase);
}

// ---------------------------------------------------------------- the body

struct BodyFrame {
    const Pose& p;
    float size;
    float dx, dz;  // heading
    void at(float along, float up, float side, float& x, float& y, float& z) const {
        x = p.x + (dx * along - dz * side) * size;
        y = p.y + up * size;
        z = p.z + (dz * along + dx * side) * size;
    }
};

inline void drawTorso(Host& h, const BodyFrame& f, bool lead, Rgb yellow, Rgb band, float dim) {
    const float k = f.size, headDown = f.p.headDown;
    float x, y, z;
    f.at(0.34f, -0.08f * headDown, 0, x, y, z);
    ball(h, x, y, z, 0.24f * k, 0.24f * k, 0.24f * k, scale(kBlack, dim));
    f.at(0.1f, 0.0f, 0, x, y, z);
    ball(h, x, y, z, 0.32f * k, 0.30f * k, 0.32f * k, scale(kFuzz, dim));
    const int segments = lead ? 5 : 3;
    for (int i = 0; i < segments; ++i) {
        const float s = (lead ? 0.40f : 0.36f) * (1.0f - 0.12f * static_cast<float>(i));
        f.at(-0.14f - 0.15f * static_cast<float>(i), 0.04f * headDown * static_cast<float>(i), 0, x, y, z);
        ball(h, x, y, z, s * k, s * k, s * k, scale(i % 2 == 0 ? yellow : kBlack, dim));
    }
    // The session's colour on the bee's back: workers share their lead's.
    f.at(0.1f, 0.16f, 0, x, y, z);
    ball(h, x, y, z, 0.16f * k, 0.1f * k, 0.16f * k, scale(band, dim));
    if (!lead) return;
    for (int i = 0; i < 5; ++i) {  // a small crown of gold beads on the lead's head
        const float a = static_cast<float>(i) * 2.0f * kPi / 5.0f;
        f.at(0.34f + 0.09f * std::cos(a), 0.15f, 0.09f * std::sin(a), x, y, z);
        ball(h, x, y, z, 0.07f * k, 0.09f * k, 0.07f * k, scale(Rgb{1.0f, 0.8f, 0.2f}, dim));
    }
}

inline void drawWings(Host& h, const BodyFrame& f, float t, float dim) {
    const Wings wings = f.p.wings;
    const float k = f.size;
    const bool blur = wings == Wings::Blur || wings == Wings::Alarm;
    const float flap = wings == Wings::Flapping ? std::sin(t * 30.0f) : (wings == Wings::Alarm ? std::sin(t * 45.0f) : 0.0f);
    const float spread = wings == Wings::Folded ? 0.12f : 0.26f;
    for (int s = -1; s <= 1; s += 2) {
        if (blur) {  // a blur of wing: soft points
            for (int j = 0; j < 3; ++j) {
                float x, y, z;
                f.at(-0.05f * j, 0.16f + 0.05f * std::sin(t * 50.0f + j), s * (0.25f + 0.07f * j), x, y, z);
                h.point(x, y, z, 0.0f, 0.55f * k, wings == Wings::Alarm ? Rgb{1.0f, 0.85f, 0.6f} : kWing, 0.45f);
            }
            continue;
        }
        float x, y, z;
        f.at(0.0f, 0.16f + (wings == Wings::Folded ? 0.0f : 0.08f * flap), s * spread, x, y, z);
        ball(h, x, y, z, 0.34f * k, 0.03f * k, 0.22f * k, scale(kWing, 0.85f * dim));
    }
}

inline void drawAntennae(Host& h, const BodyFrame& f, float dim) {
    for (int s = -1; s <= 1; s += 2) {
        float x0, y0, z0, x1, y1, z1;
        f.at(0.42f, 0.02f, s * 0.05f, x0, y0, z0);
        f.at(0.62f, 0.2f, s * 0.12f, x1, y1, z1);
        h.wideLine(x0, y0, z0, x1, y1, z1, scale(kBlack, dim), 1.5f, 0.025f * f.size);
    }
}

inline float beeSize(const Bee& b) { return b.lead ? 1.45f : 0.95f; }

inline void drawBee(Host& h, const Bee& b, const Pose& p, float t) {
    const float dim = b.resting() ? 0.55f : 1.0f;
    const Rgb yellow = b.waiting() ? mix(kYellow, Rgb{1.0f, 0.3f, 0.1f}, 0.5f + 0.5f * std::sin(t * 14.0f)) : kYellow;
    const BodyFrame f{p, beeSize(b), std::cos(p.heading), std::sin(p.heading)};
    drawTorso(h, f, b.lead, yellow, b.band, dim);
    drawWings(h, f, t, dim);
    drawAntennae(h, f, dim);
}

}  // namespace hive
