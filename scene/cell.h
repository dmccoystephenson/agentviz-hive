// cell.h: what a bee's cell shows: honey (busy), a cap (idle, offline), the
// waiting glow with its beacon and sign, the stalled red flash, the wax
// bridge from a worker to its lead, and the selection ring.
#pragma once

#include "bee.h"

namespace hive {

// The per-frame budget of point lights the cells may add.
struct Lights {
    int used = 0;
    bool take(int limit) { return used++ < limit; }
};

// Busy: honey in the cell, filled deeper the more tool calls the session has made.
inline float honeyFill(int toolCount) {
    return std::clamp(std::log1p(static_cast<float>(std::max(toolCount, 0))) / std::log1p(150.0f), 0.1f, 1.0f);
}

inline void drawCap(Host& h, const Bee& b) {
    const Rgb grey{0.42f, 0.41f, 0.40f};
    const Rgb wax = mix(scale(kWax, b.state == State::Dim ? 0.45f : 0.6f), b.band, 0.2f);
    disc(h, b.x, kWallH - 0.02f, b.z, 1.62f, 0.04f, b.offline() ? grey : wax);
}

// A question mark of wide lines: a hook and a dot. It faces the default camera,
// which looks along +z with +x to the screen's left, so x runs mirrored.
inline void drawQuestion(Host& h, float x, float y, float z, Rgb c) {
    const int n = 10;
    for (int i = 0; i < n; ++i) {
        const float a0 = kPi * (1.0f - 1.3f * i / n), a1 = kPi * (1.0f - 1.3f * (i + 1) / n);
        h.wideLine(x - 0.35f * std::cos(a0), y + 0.45f + 0.35f * std::sin(a0), z, x - 0.35f * std::cos(a1),
                   y + 0.45f + 0.35f * std::sin(a1), z, c, 6.0f, 0.12f);
    }
    h.wideLine(x - 0.35f * std::cos(-0.3f * kPi), y + 0.45f + 0.35f * std::sin(-0.3f * kPi), z, x, y - 0.05f, z, c,
               6.0f, 0.12f);
    ball(h, x, y - 0.4f, z, 0.16f, 0.16f, 0.16f, c);
}

// A padlock: a body and a shackle arc.
inline void drawPadlock(Host& h, float x, float y, float z, Rgb c) {
    h.add(STRATA_PRIMITIVE_CUBE, x, y - 0.2f, z, 0.7f, 0.55f, 0.3f, c);
    const int n = 10;
    for (int i = 0; i < n; ++i) {
        const float a0 = kPi * i / n, a1 = kPi * (i + 1) / n;
        h.wideLine(x + 0.24f * std::cos(a0), y + 0.1f + 0.3f * std::sin(a0), z, x + 0.24f * std::cos(a1),
                   y + 0.1f + 0.3f * std::sin(a1), z, c, 6.0f, 0.1f);
    }
    ball(h, x, y - 0.2f, z - 0.16f, 0.12f, 0.16f, 0.06f, kBlack);  // the keyhole
}

// Waiting: the cell glows and pulses, rings ripple out, a beacon column of
// light rises, and a sign floats in it: "?" for a question, a padlock for a permission prompt.
inline void drawWaitingCell(Host& h, const Bee& b, float t, float pulse, bool reduced, Lights& lights) {
    disc(h, b.x, 0.1f, b.z, 1.55f, 0.08f, mix(Rgb{1.0f, 0.45f, 0.05f}, Rgb{1.0f, 0.95f, 0.6f}, pulse));
    for (int k = 0; k < 3; ++k) {
        const float s = reduced ? 0.3f * k : fract(t * 0.8f + k / 3.0f);
        ring(h, b.x, kWallH + 0.05f, b.z, 0.9f + 1.6f * s, kAlarm, 5.0f, (1.0f - s) * 0.95f);
    }
    for (int k = 0; k < 16; ++k) {
        const float y = std::fmod(k * 0.5f + (reduced ? 0.0f : t * 1.6f), 8.0f);
        h.point(b.x, 0.6f + y, b.z, 6.0f, 1.3f - y * 0.1f, {1.0f, 0.75f, 0.25f}, 0.95f * (1.0f - y / 8.0f));
    }
    h.wideLine(b.x, 0.5f, b.z, b.x, 8.5f, b.z, {1.0f, 0.6f, 0.15f}, 3.0f, 0.12f, 0.35f + 0.3f * pulse);
    if (lights.take(3)) h.light(b.x, 1.6f, b.z, 7.0f, {1.0f, 0.65f, 0.25f}, 2.0f + 2.0f * pulse, 10);
    const float signY = 3.4f + (reduced ? 0.0f : 0.15f * std::sin(t * 2.0f));
    const Rgb sign{1.0f, 0.82f, 0.2f};
    if (b.reason == agentviz::WaitingReason::Permission)
        drawPadlock(h, b.x, signY, b.z, sign);
    else if (b.reason == agentviz::WaitingReason::Question)
        drawQuestion(h, b.x, signY, b.z, sign);
}

// Stalled (a lead busy but silent for 5 min, or a worker quiet that long): the cell flashes red.
inline void drawStalled(Host& h, const Bee& b, float t, bool reduced, Lights& lights) {
    const float on = reduced ? 1.0f : (std::sin(t * 8.0f) > 0.0f ? 1.0f : 0.15f);
    const float size = b.lead ? 1.0f : 0.8f;
    disc(h, b.x, kWallH - 0.03f, b.z, 1.5f * size, 0.03f, scale(kStallRed, on));  // above the deepest honey
    ring(h, b.x, kWallH + 0.06f, b.z, 1.05f * size, {1.0f, 0.1f, 0.05f}, 4.0f, on);
    if (lights.take(2)) h.light(b.x, 1.2f, b.z, 4.0f, {1.0f, 0.1f, 0.05f}, 2.0f * on, 8);
}

// The lead's cell for its state; a worker's cell is plain wax.
inline void drawLeadCell(Host& h, const Bee& b, float t, float pulse, bool reduced, Lights& lights) {
    if (b.offline() || b.resting())
        drawCap(h, b);
    else if (b.waiting())
        drawWaitingCell(h, b, t, pulse, reduced, lights);
    else {
        const float half = 0.08f + 0.12f * honeyFill(b.toolCount);
        disc(h, b.x, 0.02f + half, b.z, 1.45f, half, mix(kHoney, b.band, 0.15f));
    }
}

inline void drawCell(Host& h, const Bee& b, float t, bool reduced, Lights& lights) {
    const float pulse = reduced ? 0.5f : 0.5f + 0.5f * std::sin(t * 6.0f);
    if (b.lead)
        drawLeadCell(h, b, t, pulse, reduced, lights);
    else  // the wax bridge from a worker's cell to its lead's
        h.wideLine(b.x, kWallH + 0.03f, b.z, b.leadX, kWallH + 0.03f, b.leadZ, b.band, 2.0f, 0.07f,
                   (b.active ? 0.55f : 0.25f) * b.appear);
    if (b.stalled) drawStalled(h, b, t, reduced, lights);
}

inline void drawSelection(Host& h, const Bee& b, float t, Lights& lights) {
    if (b.highlight == agentviz::Highlight::None) return;
    const bool selected = b.highlight == agentviz::Highlight::Selected;
    ring(h, b.x, kWallH + 0.08f, b.z, b.lead ? 1.15f : 0.85f, kWhite, selected ? 3.0f : 2.0f, selected ? 0.95f : 0.5f,
         t * 0.6f);
    if (selected && !b.offline() && lights.take(3)) h.light(b.x, 2.0f, b.z, 5.0f, {1.0f, 0.95f, 0.8f}, 1.2f, 5);
}

}  // namespace hive
