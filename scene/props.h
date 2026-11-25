// props.h: what goes with each action (wax, sparks, probing antennae, a
// forager's trail, the dance floor, thought dots), the pollen burst at each
// new tool call, and the scent trails between sessions sharing a repo.
#pragma once

#include "bee.h"

namespace hive {

inline void editProps(Host& h, const Bee& b, const Pose& p, float t, bool reduced) {
    const float fill = reduced ? 0.6f : fract(t * 0.18f + b.seed);
    const float height = 0.05f + fill * (kWallH - 0.08f);
    disc(h, b.x, height * 0.5f + 0.03f, b.z, 1.2f, height * 0.5f, mix(kWaxTop, Rgb{1, 0.95f, 0.75f}, 0.4f));
    for (int j = 0; j < 4; ++j) {  // flakes of wax in the air
        const float s = fract(t * 0.9f + j * 0.25f + b.seed);
        h.point(p.x + 0.2f * std::cos(j * 1.7f), p.y - 0.3f * s, p.z + 0.2f * std::sin(j * 1.7f), 0.0f, 0.1f,
                {1.0f, 0.95f, 0.7f}, 0.9f * (1.0f - s));
    }
}

inline void runProps(Host& h, const Bee& b, const Pose& p, float t) {
    for (int j = 0; j < 8; ++j) {  // sparks thrown off the buzzing bee
        const float s = fract(t * 1.4f + j * 0.125f + b.seed);
        const float a = j * 2.4f + b.seed * 10.0f;
        h.point(p.x + std::cos(a) * 0.9f * s, p.y + 0.3f * s, p.z + std::sin(a) * 0.9f * s, 0.0f, 0.14f,
                {1.0f, 0.9f, 0.4f}, 0.9f * (1.0f - s));
    }
}

inline void readProps(Host& h, const Bee& b, const Pose& p, float t) {
    const float k = beeSize(b), dx = std::cos(p.heading), dz = std::sin(p.heading);
    for (int s = -1; s <= 1; s += 2) {  // antennae probing down into the cells
        const float probe = 0.5f + 0.5f * std::sin(t * 4.0f + s);
        const float tx = p.x + dx * 0.55f * k - dz * s * 0.25f, tz = p.z + dz * 0.55f * k + dx * s * 0.25f;
        const float ty = kWallH - 0.35f * probe;
        h.wideLine(p.x + dx * 0.4f * k, p.y, p.z + dz * 0.4f * k, tx, ty, tz, {0.1f, 0.08f, 0.05f}, 2.0f, 0.03f);
        h.point(tx, ty, tz, 0.0f, 0.18f, {0.5f, 0.9f, 1.0f}, 0.8f);
    }
}

inline void webProps(Host& h, const Bee& b, const World& w, float t, bool reduced) {
    for (int j = 1; j <= 6; ++j) {  // a faint trail behind the forager
        const Pose back = beePose(b, w, t - 0.12f * j, reduced);
        h.point(back.x, back.y, back.z, 0.0f, 0.12f, {1.0f, 1.0f, 0.8f}, 0.5f - 0.07f * j);
    }
}

inline void thinkProps(Host& h, const Pose& p, float size, float t, bool reduced) {
    for (int j = 0; j < 3; ++j) {  // three slow dots over a still bee
        const float on = reduced ? 1.0f : smooth(fract(t * 0.5f - j * 0.2f) * 3.0f);
        h.point(p.x - 0.25f + 0.25f * j, p.y + 0.6f * size + 0.1f * j, p.z, 0.0f, 0.16f, {0.85f, 0.85f, 1.0f},
                0.3f + 0.6f * on);
    }
}

inline void drawProps(Host& h, const Bee& b, const Pose& p, const World& w, float t, bool reduced) {
    if (!b.working()) return;
    const float pulse = reduced ? 0.5f : 0.5f + 0.5f * std::sin(t * 6.0f);
    switch (b.action) {
        case Action::Edit: editProps(h, b, p, t, reduced); break;
        case Action::Run: runProps(h, b, p, t); break;
        case Action::Read: readProps(h, b, p, t); break;
        case Action::Web: webProps(h, b, w, t, reduced); break;
        case Action::Delegate: ring(h, b.x, kWallH + 0.04f, b.z, 0.8f, b.band, 3.0f, 0.5f + 0.4f * pulse, t); break;
        case Action::Think: thinkProps(h, p, beeSize(b), t, reduced); break;
        default: break;
    }
}

// A new tool call: a puff of pollen bursts out of the bee and a ring spreads on its cell.
inline void drawToolPulse(Host& h, const Bee& b, const Pose& p) {
    if (b.pulse < 0.02f) return;
    const float spread = 1.0f - b.pulse;
    for (int j = 0; j < 10; ++j) {
        const float a = j * 0.628f + b.seed * 6.0f;
        h.point(p.x + std::cos(a) * 0.9f * spread, p.y + 0.35f * spread, p.z + std::sin(a) * 0.9f * spread, 0.0f, 0.3f,
                {1.0f, 0.86f, 0.25f}, b.pulse);
    }
    ring(h, b.x, kWallH + 0.07f, b.z, (b.lead ? 0.7f : 0.5f) + 0.6f * spread, {1.0f, 0.85f, 0.3f}, 5.0f, b.pulse);
}

// A scent trail: a glowing purple arc between two sessions in one repo, motes drifting along it.
inline void drawScentTrail(Host& h, const agentviz::Link& l, float t, bool reduced) {
    if (l.alpha < 0.02f) return;
    const Rgb scent{0.85f, 0.35f, 1.0f};
    auto pointAt = [&](float s, float& x, float& y, float& z) {
        x = l.x0 + (l.x1 - l.x0) * s;
        z = l.z0 + (l.z1 - l.z0) * s;
        y = kWallH + 0.25f + 1.4f * std::sin(kPi * s);
    };
    const int segments = 16;
    for (int k = 0; k < segments; ++k) {
        float ax, ay, az, bx, by, bz;
        pointAt(k / static_cast<float>(segments), ax, ay, az);
        pointAt((k + 1) / static_cast<float>(segments), bx, by, bz);
        h.wideLine(ax, ay, az, bx, by, bz, scent, 4.0f, 0.12f, 0.55f * l.alpha);
    }
    for (int k = 0; k < 10; ++k) {
        const float s = fract(k / 10.0f + (reduced ? 0.0f : t * 0.15f) + l.seed);
        float x, y, z;
        pointAt(s, x, y, z);
        h.point(x, y + 0.05f * std::sin(t * 3.0f + k), z, 0.0f, 0.35f, {1.0f, 0.6f, 1.0f}, 0.8f * l.alpha);
    }
}

}  // namespace hive
