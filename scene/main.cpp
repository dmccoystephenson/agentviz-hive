// agentviz-hive: Claude Code sessions as a beehive comb, on the Strata web kit.
//
// The comb is a hex grid of wax cells (one static mesh, rebuilt only when it
// has to grow). Each place (repo) tints a region of cells; each session is a
// big bee in the centre cell of a 19-cell "flower" of the comb; its subagents
// are worker bees on the cells around it. The page (web/hive.js) owns the
// layout and easing and hands this file three row arrays every frame; every
// animation here is a function of scene time t and those rows.
//
// Rows (floats):
//   cells  stride 7: x, z, r, g, b, ring (0 centre, 1, 2), alpha
//   bees   stride 16: homeX, homeZ, leadX, leadZ, role (0 lead, 1 worker),
//          state (0 offline, 1 dim, 2 idle, 3 busy, 4 waiting), action
//          (0 none, 1 edit, 2 run, 3 read, 4 web, 5 delegate, 6 think, 7 other),
//          seed, selected, r, g, b (the session's colour), alpha, stalled,
//          active (worker: 1 active, 0 resting), spare
//   trails stride 6: x0, z0, x1, z1, intensity, seed   (two sessions, one place)
#define STRATA_KIT_MAIN
#include "strata_kit.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using strata_kit::Rgb;
constexpr float kPi = 3.14159265f;
constexpr float kSqrt3 = 1.7320508f;
constexpr float kWallH = 0.45f;   // comb wall height
constexpr float kHover = 1.0f;   // where a busy bee flies
constexpr int kCellStride = 7, kBeeStride = 16, kTrailStride = 6;

struct State {
    std::vector<float> cells, bees, trails;
    int combRadius = 0, builtRadius = -1;
    float meadow[2] = {14.0f, 0.0f};
    uint32_t combMesh = 0;
} g;

const Rgb kWax{0.78f, 0.55f, 0.16f};
const Rgb kWaxTop{0.92f, 0.70f, 0.28f};
const Rgb kWaxFloor{0.42f, 0.26f, 0.07f};
const Rgb kHoney{1.0f, 0.62f, 0.05f};
const Rgb kBlack{0.05f, 0.04f, 0.03f};
const Rgb kYellow{0.98f, 0.74f, 0.10f};
const Rgb kFuzz{0.62f, 0.42f, 0.14f};
const Rgb kWing{0.85f, 0.92f, 1.0f};

float fract(float x) { return x - std::floor(x); }
float hash1(float n) { return fract(std::sin(n * 12.9898f) * 43758.547f); }
Rgb mix(Rgb a, Rgb b, float k) { return {a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k}; }
Rgb scale(Rgb a, float k) { return {a.r * k, a.g * k, a.b * k}; }
float smooth(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

void axialToWorld(int q, int r, float& x, float& z) {
    x = kSqrt3 * (static_cast<float>(q) + static_cast<float>(r) * 0.5f);
    z = 1.5f * static_cast<float>(r);
}

// ---------------------------------------------------------------- comb mesh

struct MeshBuf {
    std::vector<float> pos, nrm, col;
    std::vector<uint32_t> idx;
    void vert(float x, float y, float z, const float* n, Rgb c) {
        pos.insert(pos.end(), {x, y, z});
        nrm.insert(nrm.end(), {n[0], n[1], n[2]});
        col.insert(col.end(), {c.r, c.g, c.b});
    }
    // A quad a b c d (as a fan) facing n: the winding is fixed to match n.
    void quad(const float* a, const float* b, const float* c, const float* d, const float* n, Rgb col) {
        const float u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const float v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const float cx = u[1] * v[2] - u[2] * v[1], cy = u[2] * v[0] - u[0] * v[2], cz = u[0] * v[1] - u[1] * v[0];
        const bool flip = cx * n[0] + cy * n[1] + cz * n[2] < 0.0f;
        const uint32_t base = static_cast<uint32_t>(pos.size() / 3);
        vert(a[0], a[1], a[2], n, col);
        vert(b[0], b[1], b[2], n, col);
        vert(c[0], c[1], c[2], n, col);
        vert(d[0], d[1], d[2], n, col);
        if (!flip)
            idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        else
            idx.insert(idx.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
    }
};

void buildComb(strata_kit::Host& h) {
    if (!h.engine) return;
    if (g.combMesh) {
        strata_remove_mesh(h.engine, g.combMesh);
        g.combMesh = 0;
        h.meshes = 0;
    }
    const int R = g.combRadius;
    MeshBuf m;
    const float ro = 1.0f, ri = 0.86f;
    const float up[3] = {0, 1, 0};
    for (int q = -R; q <= R; ++q)
        for (int r = std::max(-R, -q - R); r <= std::min(R, -q + R); ++r) {
            float cx, cz;
            axialToWorld(q, r, cx, cz);
            const float shade = 0.9f + 0.2f * hash1(static_cast<float>(q * 131 + r * 17));
            float oc[6][3], ic[6][3], ob[6][3], ib[6][3];
            for (int i = 0; i < 6; ++i) {
                const float a = (60.0f * static_cast<float>(i) - 30.0f) * kPi / 180.0f;
                const float ca = std::cos(a), sa = std::sin(a);
                const float o[3] = {cx + ca * ro, kWallH, cz + sa * ro}, in[3] = {cx + ca * ri, kWallH, cz + sa * ri};
                for (int k = 0; k < 3; ++k) {
                    oc[i][k] = o[k];
                    ic[i][k] = in[k];
                    ob[i][k] = o[k];
                    ib[i][k] = in[k];
                }
                ob[i][1] = 0.0f;
                ib[i][1] = 0.02f;
            }
            for (int i = 0; i < 6; ++i) {
                const int j = (i + 1) % 6;
                const float mx = (oc[i][0] + oc[j][0]) * 0.5f - cx, mz = (oc[i][2] + oc[j][2]) * 0.5f - cz;
                const float len = std::sqrt(mx * mx + mz * mz);
                const float out[3] = {mx / len, 0, mz / len}, in[3] = {-mx / len, 0, -mz / len};
                m.quad(oc[i], oc[j], ic[j], ic[i], up, scale(kWaxTop, shade));
                m.quad(ic[i], ic[j], ib[j], ib[i], in, scale(kWax, shade));
                if (std::max({std::abs(q), std::abs(r), std::abs(q + r)}) == R)
                    m.quad(oc[i], oc[j], ob[j], ob[i], out, scale(kWax, shade * 0.8f));
            }
            // The cell's floor: a hexagon fan.
            const uint32_t base = static_cast<uint32_t>(m.pos.size() / 3);
            m.vert(cx, 0.02f, cz, up, scale(kWaxFloor, shade));
            for (int i = 0; i < 6; ++i) m.vert(ib[i][0], 0.02f, ib[i][2], up, scale(kWaxFloor, shade * 1.15f));
            for (int i = 0; i < 6; ++i) {
                const uint32_t a = base + 1 + static_cast<uint32_t>(i), b = base + 1 + static_cast<uint32_t>((i + 1) % 6);
                m.idx.insert(m.idx.end(), {base, b, a});  // counter-clockwise from above (z is "down" on screen)
            }
        }
    StrataMesh mesh{};
    mesh.positions = m.pos.data();
    mesh.normals = m.nrm.data();
    mesh.colors = m.col.data();
    mesh.indices = m.idx.data();
    mesh.vertex_count = static_cast<int32_t>(m.pos.size() / 3);
    mesh.index_count = static_cast<int32_t>(m.idx.size());
    mesh.color[0] = mesh.color[1] = mesh.color[2] = mesh.color[3] = 1.0f;
    if (strata_add_mesh(h.engine, &mesh, &g.combMesh) == STRATA_OK) h.meshes = 1;
    g.builtRadius = R;
}

// ------------------------------------------------------------------ helpers

void ring(strata_kit::Host& h, float x, float y, float z, float rad, Rgb c, float px, float a, float spin = 0.0f,
          uint32_t flags = STRATA_LINES_ROUND_CAPS) {
    const int n = 24;
    for (int k = 0; k < n; ++k) {
        const float b0 = k * 2.0f * kPi / n + spin, b1 = (k + 1) * 2.0f * kPi / n + spin;
        h.wideLine(x + std::cos(b0) * rad, y, z + std::sin(b0) * rad, x + std::cos(b1) * rad, y,
                   z + std::sin(b1) * rad, c, px, 0.0f, a, flags);
    }
}

struct Pose {
    float x, y, z, heading;
    int wings;      // 0 folded, 1 flapping, 2 blur (run), 3 alarm
    float bodyLift; // head-down tilt for edit (moves the head lower)
};

// A point on the flight loop between a and b, out and back, arcing up by lift.
void arc(float ax, float az, float bx, float bz, float lift, float k, float& x, float& y, float& z) {
    const float s = smooth(k);
    x = ax + (bx - ax) * s;
    z = az + (bz - az) * s;
    y = kHover + lift * std::sin(kPi * s);
}

// Where a bee is at time t (see the row layout at the top).
Pose beePose(const float* b, float t, bool reduced) {
    const float hx = b[0], hz = b[1], lx = b[2], lz = b[3], seed = b[7];
    const int role = static_cast<int>(b[4]), state = static_cast<int>(b[5]), action = static_cast<int>(b[6]);
    const bool active = b[14] > 0.5f;
    if (reduced) t = 0.0f;
    const float ph = seed * 6.283f;
    Pose p{hx, kWallH + 0.3f, hz, ph, 0, 0.0f};
    if (state <= 2 || (role == 1 && !active)) return p;  // resting on its cap / its cell's rim
    if (state == 4) {  // waiting: an alarm dance, fast tight circles with hops
        const float a = t * 9.0f + ph;
        p.x = hx + std::cos(a) * 0.32f;
        p.z = hz + std::sin(a) * 0.32f;
        p.y = kHover + 0.45f * std::abs(std::sin(t * 7.0f));
        p.heading = a + kPi * 0.5f;
        p.wings = 3;
        return p;
    }
    p.y = kHover;
    p.wings = 1;
    // Workers fly a short loop to their lead and back every few seconds.
    if (role == 1 && action != 4) {
        const float cyc = fract(t / 6.5f + seed);
        if (cyc < 0.34f) {
            const float k = cyc / 0.34f;
            const float s = k < 0.5f ? k * 2.0f : (1.0f - k) * 2.0f;
            arc(hx, hz, lx + (hx - lx) * 0.35f, hz + (lz - hz) * 0.0f + (lz - hz) * 0.65f, 0.0f, s, p.x, p.y, p.z);
            p.y = kHover + 0.7f * std::sin(kPi * k);
            // Sideways offset so out and back are two lanes of a loop.
            const float dx = lx - hx, dz = lz - hz, len = std::sqrt(dx * dx + dz * dz) + 1e-4f;
            const float side = std::sin(kPi * k) * 0.35f;
            p.x += -dz / len * side;
            p.z += dx / len * side;
            p.heading = std::atan2(dz, dx) + (k < 0.5f ? 0.0f : kPi);
            return p;
        }
    }
    switch (action) {
        case 1: {  // edit: walking the rim, head down to the wax
            const float a = t * 0.7f + ph;
            p.x = hx + std::cos(a) * 0.42f;
            p.z = hz + std::sin(a) * 0.42f;
            p.y = kWallH + 0.3f + 0.06f * std::abs(std::sin(t * 5.0f + ph));
            p.heading = a + kPi * 0.5f;
            p.bodyLift = 1.0f;
            p.wings = 0;
            break;
        }
        case 2:  // run: hovering on the spot, buzzing
            p.x = hx + 0.04f * std::sin(t * 37.0f + ph);
            p.z = hz + 0.04f * std::cos(t * 41.0f + ph);
            p.y = kHover + 0.05f * std::sin(t * 23.0f);
            p.heading = ph + 0.2f * std::sin(t * 3.0f);
            p.wings = 2;
            break;
        case 3: {  // read: walking slowly over the neighbouring cells
            const float a = t * 0.35f + ph;
            const float r = 0.75f + 0.25f * std::sin(t * 0.9f + ph);
            p.x = hx + std::cos(a) * r;
            p.z = hz + std::sin(a) * r;
            p.y = kWallH + 0.32f;
            p.heading = a + kPi * 0.5f;
            p.wings = 0;
            break;
        }
        case 4: {  // web: off the comb to the flower field and back
            const float cyc = fract(t / 9.0f + seed);
            const float fx = g.meadow[0] + (hash1(seed * 91.0f) - 0.5f) * 5.0f;
            const float fz = g.meadow[1] + (hash1(seed * 37.0f) - 0.5f) * 7.0f;
            const float dx = fx - hx, dz = fz - hz;
            if (cyc < 0.4f) {
                arc(hx, hz, fx, fz, 3.0f, cyc / 0.4f, p.x, p.y, p.z);
                p.heading = std::atan2(dz, dx);
            } else if (cyc < 0.6f) {
                p.x = fx + 0.2f * std::cos(t * 2.0f);
                p.z = fz + 0.2f * std::sin(t * 2.0f);
                p.y = 0.9f + 0.1f * std::sin(t * 4.0f);
                p.heading = t * 2.0f + kPi * 0.5f;
            } else {
                arc(fx, fz, hx, hz, 3.0f, (cyc - 0.6f) / 0.4f, p.x, p.y, p.z);
                p.heading = std::atan2(-dz, -dx);
            }
            break;
        }
        case 5: {  // delegate: the waggle dance, a figure of eight with a waggling run
            const float cyc = fract(t / 3.2f + seed);
            const float side = cyc < 0.5f ? 1.0f : -1.0f;
            const float k = fract(cyc * 2.0f);
            if (k < 0.45f) {  // the straight waggle run up the middle
                const float s = k / 0.45f;
                p.x = hx;
                p.z = hz + 0.45f - 0.9f * s;
                p.x += 0.12f * std::sin(t * 40.0f);
                p.heading = -kPi * 0.5f;
            } else {  // the return loop on one side
                const float s = (k - 0.45f) / 0.55f;
                const float a = kPi * s;
                p.x = hx + side * 0.45f * std::sin(a);
                p.z = hz - 0.45f * std::cos(a);
                p.heading = kPi * 0.5f + side * (a - kPi * 0.5f) * 0.0f;
                p.heading = std::atan2(0.45f * std::sin(a), side * 0.45f * std::cos(a));
            }
            p.y = kWallH + 0.32f;
            p.wings = 1;
            break;
        }
        case 6:  // think: still
            p.y = kWallH + 0.32f;
            p.wings = 0;
            break;
        default:  // other / none: a lazy hover
            p.x = hx + 0.25f * std::sin(t * 0.8f + ph);
            p.z = hz + 0.25f * std::sin(t * 1.3f + ph);
            p.y = kHover + 0.1f * std::sin(t * 2.0f);
            p.heading = ph + 0.6f * std::sin(t * 0.5f);
            break;
    }
    return p;
}

void drawBee(strata_kit::Host& h, const Pose& p, float k, Rgb band, float dim, bool lead, float t, bool alarm) {
    const float dx = std::cos(p.heading), dz = std::sin(p.heading);
    const float nx = -dz, nz = dx;
    auto at = [&](float along, float up, float side, float& x, float& y, float& z) {
        x = p.x + (dx * along + nx * side) * k;
        y = p.y + up * k;
        z = p.z + (dz * along + nz * side) * k;
    };
    float x, y, z;
    const Rgb yellow = alarm ? mix(kYellow, Rgb{1.0f, 0.3f, 0.1f}, 0.5f + 0.5f * std::sin(t * 14.0f)) : kYellow;
    // Head (lower when building wax), thorax, striped abdomen.
    at(0.34f, -0.08f * p.bodyLift, 0, x, y, z);
    h.add(STRATA_PRIMITIVE_SPHERE, x, y, z, 0.24f * k, 0.24f * k, 0.24f * k, scale(kBlack, dim));
    at(0.1f, 0.0f, 0, x, y, z);
    h.add(STRATA_PRIMITIVE_SPHERE, x, y, z, 0.32f * k, 0.30f * k, 0.32f * k, scale(kFuzz, dim));
    const int segs = lead ? 5 : 3;
    for (int i = 0; i < segs; ++i) {
        const float along = -0.14f - 0.15f * static_cast<float>(i);
        const float s = (lead ? 0.40f : 0.36f) * (1.0f - 0.12f * static_cast<float>(i));
        at(along, 0.04f * p.bodyLift * static_cast<float>(i), 0, x, y, z);
        h.add(STRATA_PRIMITIVE_SPHERE, x, y, z, s * k, s * k, s * k, scale(i % 2 == 0 ? yellow : kBlack, dim));
    }
    // The session's colour on the bee's back: workers share their lead's.
    at(0.1f, 0.16f, 0, x, y, z);
    h.add(STRATA_PRIMITIVE_SPHERE, x, y, z, 0.16f * k, 0.1f * k, 0.16f * k, scale(band, dim));
    if (lead) {  // a small crown of gold beads on the lead's head
        for (int i = 0; i < 5; ++i) {
            const float a = static_cast<float>(i) * 2.0f * kPi / 5.0f;
            at(0.34f + 0.09f * std::cos(a), 0.15f, 0.09f * std::sin(a), x, y, z);
            h.add(STRATA_PRIMITIVE_SPHERE, x, y, z, 0.07f * k, 0.09f * k, 0.07f * k, scale(Rgb{1.0f, 0.8f, 0.2f}, dim));
        }
    }
    // Wings.
    float flap = 0.0f;
    if (p.wings == 1) flap = std::sin(t * 30.0f);
    if (p.wings == 3) flap = std::sin(t * 45.0f);
    for (int s = -1; s <= 1; s += 2) {
        const float spread = p.wings == 0 ? 0.12f : 0.26f;
        at(0.0f, 0.16f + (p.wings == 0 ? 0.0f : 0.08f * flap), s * spread, x, y, z);
        if (p.wings == 2 || p.wings == 3) {  // a blur of wing: soft points
            for (int j = 0; j < 3; ++j) {
                float bx, by, bz;
                at(0.0f - 0.05f * j, 0.16f + 0.05f * std::sin(t * 50.0f + j), s * (0.25f + 0.07f * j), bx, by, bz);
                h.point(bx, by, bz, 0.0f, 0.55f * k, alarm ? Rgb{1.0f, 0.85f, 0.6f} : kWing, 0.45f);
            }
        } else {
            h.add(STRATA_PRIMITIVE_SPHERE, x, y, z, 0.34f * k, 0.03f * k, 0.22f * k, scale(kWing, 0.85f * dim));
        }
    }
    // Antennae.
    for (int s = -1; s <= 1; s += 2) {
        float x0, y0, z0, x1, y1, z1;
        at(0.42f, 0.02f, s * 0.05f, x0, y0, z0);
        at(0.62f, 0.2f, s * 0.12f, x1, y1, z1);
        h.wideLine(x0, y0, z0, x1, y1, z1, scale(kBlack, dim), 1.5f, 0.025f * k);
    }
}

void drawFlowers(strata_kit::Host& h, float t) {
    const Rgb petals[4] = {{0.95f, 0.45f, 0.65f}, {0.95f, 0.95f, 0.95f}, {0.6f, 0.4f, 0.95f}, {1.0f, 0.55f, 0.2f}};
    for (int i = 0; i < 22; ++i) {
        const float fx = g.meadow[0] + (hash1(i * 3.1f + 1.0f) - 0.5f) * 7.0f;
        const float fz = g.meadow[1] + (hash1(i * 7.7f + 2.0f) - 0.5f) * 10.0f;
        const float ht = 0.5f + 0.5f * hash1(i * 5.3f);
        const float sway = 0.04f * std::sin(t * 1.3f + i);
        h.add(STRATA_PRIMITIVE_CYLINDER, fx, ht * 0.5f - 0.2f, fz, 0.06f, ht * 0.5f + 0.2f, 0.06f, {0.18f, 0.45f, 0.12f});
        const Rgb pc = petals[i % 4];
        for (int k = 0; k < 5; ++k) {
            const float a = k * 2.0f * kPi / 5.0f + i;
            h.add(STRATA_PRIMITIVE_SPHERE, fx + sway + std::cos(a) * 0.17f, ht + 0.02f, fz + std::sin(a) * 0.17f, 0.2f,
                  0.06f, 0.2f, pc);
        }
        h.add(STRATA_PRIMITIVE_SPHERE, fx + sway, ht + 0.06f, fz, 0.14f, 0.1f, 0.14f, {1.0f, 0.82f, 0.15f});
    }
}

}  // namespace

void strata_kit_configure(strata_kit::Options& o) {
    o.name = "agentviz-hive";
    o.farPlane = 500.0f;
}

void strata_kit_engine_ready(strata_kit::Host& h) {
    // A meadow under the hive frame.
    const int n = 129;
    const float cell = 1.0f, x0 = -(n - 1) * cell * 0.5f;
    std::vector<float> heights(n * n), colors(n * n * 3);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const float x = x0 + i * cell, z = x0 + j * cell;
            const size_t k = static_cast<size_t>(j) * n + i;
            heights[k] = 0.15f * std::sin(x * 0.21f) * std::cos(z * 0.17f);
            const float v = 0.04f * std::sin(x * 1.3f) * std::cos(z * 1.1f);
            colors[k * 3] = 0.16f + v;
            colors[k * 3 + 1] = 0.34f + v;
            colors[k * 3 + 2] = 0.10f;
        }
    StrataTerrainTile tile{};
    tile.origin[0] = x0;
    tile.origin[1] = -0.9f;
    tile.origin[2] = x0;
    tile.cell_size = cell;
    tile.samples_x = n;
    tile.samples_z = n;
    tile.heights = heights.data();
    tile.colors = colors.data();
    tile.color[3] = 1.0f;
    strata_set_terrain(h.engine, &tile);
    h.terrainSamples = n * n;
    g.combMesh = 0;  // a restored context starts with no meshes
    g.builtRadius = -1;
    if (g.combRadius > 0) buildComb(h);
}

void strata_kit_build(strata_kit::Host& h, double td) {
    const float t = static_cast<float>(td);
    if (g.combRadius > 0 && g.builtRadius != g.combRadius) buildComb(h);
    const bool reduced = h.reducedMotion;

    // The hive frame: a wooden board under the comb.
    const float R = static_cast<float>(std::max(g.combRadius, 1));
    const float w = kSqrt3 * (2.0f * R + 1.0f) + 1.2f, d = 1.5f * 2.0f * R + 3.2f;
    h.add(STRATA_PRIMITIVE_CUBE, 0.0f, -0.35f, 0.0f, w, 0.7f, d, {0.36f, 0.22f, 0.11f});
    drawFlowers(h, t);

    // Region tints.
    const auto& c = g.cells;
    for (size_t i = 0; i + kCellStride <= c.size(); i += kCellStride) {
        const float a = c[i + 6];
        if (a < 0.02f) continue;
        const Rgb col{c[i + 2], c[i + 3], c[i + 4]};
        const float ringDim = c[i + 5] < 0.5f ? 1.0f : (c[i + 5] < 1.5f ? 0.85f : 0.65f);
        h.add(STRATA_PRIMITIVE_CYLINDER, c[i], 0.04f, c[i + 1], 1.5f, 0.025f, 1.5f,
              mix(kWaxFloor, col, a * ringDim));
    }

    // Comb bridges from each worker's cell to its lead, and the bees.
    const auto& b = g.bees;
    int lightsUsed = 0;
    for (size_t i = 0; i + kBeeStride <= b.size(); i += kBeeStride) {
        const float* r = &b[i];
        const float hx = r[0], hz = r[1], alpha = r[12], seed = r[7];
        const int role = static_cast<int>(r[4]), state = static_cast<int>(r[5]), action = static_cast<int>(r[6]);
        const bool selected = r[8] > 0.5f, stalled = r[13] > 0.5f, active = r[14] > 0.5f;
        const Rgb band{r[9], r[10], r[11]};
        if (alpha < 0.02f) continue;
        const float pulse = reduced ? 0.5f : 0.5f + 0.5f * std::sin(t * 6.0f);

        if (role == 1) {
            h.wideLine(hx, kWallH + 0.03f, hz, r[2], kWallH + 0.03f, r[3], band, 2.0f, 0.07f,
                       (active ? 0.55f : 0.25f) * alpha);
        }
        // The cell itself.
        if (role == 0) {
            if (state == 0) {  // offline: an empty grey cap
                h.add(STRATA_PRIMITIVE_CYLINDER, hx, kWallH - 0.02f, hz, 1.62f, 0.04f, 1.62f, {0.42f, 0.41f, 0.40f});
            } else if (state <= 2) {  // idle / dim: a capped, dim cell
                h.add(STRATA_PRIMITIVE_CYLINDER, hx, kWallH - 0.02f, hz, 1.62f, 0.04f, 1.62f,
                      mix(scale(kWax, state == 1 ? 0.45f : 0.6f), band, 0.2f));
            } else if (state == 4) {  // waiting: the cell glows and pulses
                const Rgb hot = mix(Rgb{1.0f, 0.45f, 0.05f}, Rgb{1.0f, 0.95f, 0.6f}, pulse);
                h.add(STRATA_PRIMITIVE_CYLINDER, hx, 0.1f, hz, 1.55f, 0.08f, 1.55f, hot);
                for (int k = 0; k < 3; ++k) {
                    const float s = reduced ? 0.3f * k : fract(t * 0.8f + k / 3.0f);
                    ring(h, hx, kWallH + 0.05f, hz, 0.9f + 1.6f * s, {1.0f, 0.55f, 0.1f}, 5.0f, (1.0f - s) * 0.95f);
                }
                // A beacon column of light, visible from anywhere on the comb.
                for (int k = 0; k < 16; ++k) {
                    const float yy = std::fmod(k * 0.5f + (reduced ? 0.0f : t * 1.6f), 8.0f);
                    h.point(hx, 0.6f + yy, hz, 6.0f, 1.3f - yy * 0.1f, {1.0f, 0.75f, 0.25f}, 0.95f * (1.0f - yy / 8.0f));
                }
                h.wideLine(hx, 0.5f, hz, hx, 8.5f, hz, {1.0f, 0.6f, 0.15f}, 3.0f, 0.12f, 0.35f + 0.3f * pulse);
                h.light(hx, 1.6f, hz, 7.0f, {1.0f, 0.65f, 0.25f}, 2.0f + 2.0f * pulse, 10);
            } else {  // busy: honey in the cell
                h.add(STRATA_PRIMITIVE_CYLINDER, hx, 0.1f, hz, 1.45f, 0.08f, 1.45f, mix(kHoney, band, 0.15f));
            }
            if (stalled) {  // stalled: the cell flashes red
                const float on = reduced ? 1.0f : (std::sin(t * 8.0f) > 0.0f ? 1.0f : 0.15f);
                h.add(STRATA_PRIMITIVE_CYLINDER, hx, 0.16f, hz, 1.5f, 0.06f, 1.5f, scale(Rgb{1.0f, 0.08f, 0.05f}, on));
                ring(h, hx, kWallH + 0.06f, hz, 1.05f, {1.0f, 0.1f, 0.05f}, 4.0f, on);
                if (lightsUsed < 2) h.light(hx, 1.2f, hz, 4.0f, {1.0f, 0.1f, 0.05f}, 2.0f * on, 8);
                ++lightsUsed;
            }
        }
        if (state == 0 && role == 0) {
            if (selected) ring(h, hx, kWallH + 0.08f, hz, 1.15f, {1, 1, 1}, 3.0f, 0.95f, t * 0.6f);
            continue;  // no bee for an offline session
        }

        const Pose p = beePose(r, t, reduced);
        const float dim = (state <= 2 || (role == 1 && !active)) ? 0.55f : 1.0f;
        const float k = role == 0 ? 1.45f : 0.95f;
        drawBee(h, p, k, band, dim, role == 0, reduced ? 0.0f : t, state == 4);

        // Per action props.
        if (state == 3 && (role == 0 || active)) {
            switch (action) {
                case 1: {  // edit: wax rising in the cell, flakes of it in the air
                    const float fill = reduced ? 0.6f : fract(t * 0.18f + seed);
                    const float hgt = 0.05f + fill * (kWallH - 0.08f);
                    h.add(STRATA_PRIMITIVE_CYLINDER, hx, hgt * 0.5f + 0.03f, hz, 1.2f, hgt * 0.5f, 1.2f,
                          mix(kWaxTop, Rgb{1, 0.95f, 0.75f}, 0.4f));
                    for (int j = 0; j < 4; ++j) {
                        const float s = fract(t * 0.9f + j * 0.25f + seed);
                        h.point(p.x + 0.2f * std::cos(j * 1.7f), p.y - 0.3f * s, p.z + 0.2f * std::sin(j * 1.7f), 0.0f,
                                0.1f, {1.0f, 0.95f, 0.7f}, 0.9f * (1.0f - s));
                    }
                    break;
                }
                case 2:  // run: sparks thrown off the buzzing bee
                    for (int j = 0; j < 8; ++j) {
                        const float s = fract(t * 1.4f + j * 0.125f + seed);
                        const float a = j * 2.4f + seed * 10.0f;
                        h.point(p.x + std::cos(a) * 0.9f * s, p.y + 0.3f * s, p.z + std::sin(a) * 0.9f * s, 0.0f,
                                0.14f, {1.0f, 0.9f, 0.4f}, 0.9f * (1.0f - s));
                    }
                    break;
                case 3: {  // read: antennae probing down into the cells
                    const float dx = std::cos(p.heading), dz = std::sin(p.heading);
                    for (int s = -1; s <= 1; s += 2) {
                        const float probe = 0.5f + 0.5f * std::sin(t * 4.0f + s);
                        const float tx = p.x + dx * 0.55f * k - dz * s * 0.25f, tz = p.z + dz * 0.55f * k + dx * s * 0.25f;
                        const float ty = kWallH - 0.35f * probe;
                        h.wideLine(p.x + dx * 0.4f * k, p.y, p.z + dz * 0.4f * k, tx, ty, tz, {0.1f, 0.08f, 0.05f}, 2.0f,
                                   0.03f);
                        h.point(tx, ty, tz, 0.0f, 0.18f, {0.5f, 0.9f, 1.0f}, 0.8f);
                    }
                    break;
                }
                case 4:  // web: a faint trail behind the forager
                    for (int j = 1; j <= 6; ++j) {
                        float q[16];
                        for (int m = 0; m < 16; ++m) q[m] = r[m];
                        const Pose back = beePose(q, t - 0.12f * j, reduced);
                        h.point(back.x, back.y, back.z, 0.0f, 0.12f, {1.0f, 1.0f, 0.8f}, 0.5f - 0.07f * j);
                    }
                    break;
                case 5:  // delegate: the dance floor glows in the lead's colour
                    ring(h, hx, kWallH + 0.04f, hz, 0.8f, band, 3.0f, 0.5f + 0.4f * pulse, t);
                    break;
                case 6:  // think: three slow dots over a still bee
                    for (int j = 0; j < 3; ++j) {
                        const float on = reduced ? 1.0f : smooth(fract(t * 0.5f - j * 0.2f) * 3.0f - 0.0f);
                        h.point(p.x - 0.25f + 0.25f * j, p.y + 0.6f * k + 0.1f * j, p.z, 0.0f, 0.16f,
                                {0.85f, 0.85f, 1.0f}, 0.3f + 0.6f * on);
                    }
                    break;
                default: break;
            }
        }
        if (selected) {
            ring(h, hx, kWallH + 0.08f, hz, role == 0 ? 1.15f : 0.85f, {1, 1, 1}, 3.0f, 0.95f, t * 0.6f);
            if (lightsUsed < 3) h.light(hx, 2.0f, hz, 5.0f, {1.0f, 0.95f, 0.8f}, 1.2f, 5);
        }
    }

    // Scent trails between sessions sharing a place: a glowing arc with drifting motes.
    const auto& tr = g.trails;
    for (size_t i = 0; i + kTrailStride <= tr.size(); i += kTrailStride) {
        const float x0 = tr[i], z0 = tr[i + 1], x1 = tr[i + 2], z1 = tr[i + 3], inten = tr[i + 4], seed = tr[i + 5];
        if (inten < 0.02f) continue;
        const Rgb scent{0.85f, 0.35f, 1.0f};
        const int segs = 16;
        auto ptAt = [&](float s, float& x, float& y, float& z) {
            x = x0 + (x1 - x0) * s;
            z = z0 + (z1 - z0) * s;
            y = kWallH + 0.25f + 1.4f * std::sin(kPi * s);
        };
        for (int k = 0; k < segs; ++k) {
            float ax, ay, az, bx, by, bz;
            ptAt(k / static_cast<float>(segs), ax, ay, az);
            ptAt((k + 1) / static_cast<float>(segs), bx, by, bz);
            h.wideLine(ax, ay, az, bx, by, bz, scent, 4.0f, 0.12f, 0.55f * inten);
        }
        for (int k = 0; k < 10; ++k) {
            const float s = fract(k / 10.0f + (reduced ? 0.0f : t * 0.15f) + seed);
            float x, y, z;
            ptAt(s, x, y, z);
            h.point(x, y + 0.05f * std::sin(t * 3.0f + k), z, 0.0f, 0.35f, {1.0f, 0.6f, 1.0f}, 0.8f * inten);
        }
    }

    // A warm fill light over the comb so it reads at the lowest tier too.
    StrataPointLight fill{};
    fill.position[0] = 0.0f;
    fill.position[1] = 12.0f;
    fill.position[2] = 0.0f;
    fill.range = 40.0f;
    fill.color[0] = 1.0f;
    fill.color[1] = 0.85f;
    fill.color[2] = 0.6f;
    fill.intensity = 0.5f;
    h.fill(fill, {0.06f, 0.05f, 0.03f}, 1);
}

// ------------------------------------------------------------------ exports

STRATA_KIT_EXPORT void hv_set_comb(int radius, float meadowX, float meadowZ) {
    g.combRadius = std::clamp(radius, 1, 40);
    g.meadow[0] = meadowX;
    g.meadow[1] = meadowZ;
}
STRATA_KIT_EXPORT void hv_set_cells(const float* rows, int n) {
    g.cells.assign(rows, rows + static_cast<size_t>(std::max(n, 0)) * kCellStride);
}
STRATA_KIT_EXPORT void hv_set_bees(const float* rows, int n) {
    g.bees.assign(rows, rows + static_cast<size_t>(std::max(n, 0)) * kBeeStride);
}
STRATA_KIT_EXPORT void hv_set_trails(const float* rows, int n) {
    g.trails.assign(rows, rows + static_cast<size_t>(std::max(n, 0)) * kTrailStride);
}
