// comb.h: the static world: the meadow terrain, the wooden hive frame, the wax
// comb mesh (rebuilt only when the comb grows), the region tints under each
// session's patch, and the flower field the foragers fly to.
#pragma once

#include <cstdint>
#include <vector>

#include "draw_util.h"
#include "../vendor/agentviz-kit/scene/agentviz.h"

namespace hive {

// The scene floats (web/hive-frame.js): comb radius in cells, flower field centre.
struct World {
    int combRadius = 0;
    float meadowX = 14.0f, meadowZ = 0.0f;
};

inline World worldOf(const agentviz::Frame& f) {
    World w;
    w.combRadius = std::clamp(agentviz::toInt(f.scene[0]), 0, 40);
    w.meadowX = f.scene[1];
    w.meadowZ = f.scene[2];
    return w;
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
    void quad(const float* a, const float* b, const float* c, const float* d, const float* n, Rgb colour) {
        const float u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const float v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const float cx = u[1] * v[2] - u[2] * v[1], cy = u[2] * v[0] - u[0] * v[2], cz = u[0] * v[1] - u[1] * v[0];
        const bool flip = cx * n[0] + cy * n[1] + cz * n[2] < 0.0f;
        const uint32_t base = static_cast<uint32_t>(pos.size() / 3);
        for (const float* p : {a, b, c, d}) vert(p[0], p[1], p[2], n, colour);
        if (!flip)
            idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        else
            idx.insert(idx.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
    }
};

struct CellCorners {
    float top[6][3], rim[6][3], base[6][3], floor[6][3];  // outer top, inner top, outer foot, inner foot
};

inline CellCorners cellCorners(float cx, float cz) {
    const float outer = 1.0f, inner = 0.86f;
    CellCorners c{};
    for (int i = 0; i < 6; ++i) {
        const float a = (60.0f * static_cast<float>(i) - 30.0f) * kPi / 180.0f;
        const float ca = std::cos(a), sa = std::sin(a);
        const float ox = cx + ca * outer, oz = cz + sa * outer, ix = cx + ca * inner, iz = cz + sa * inner;
        const float top[3] = {ox, kWallH, oz}, rim[3] = {ix, kWallH, iz}, base[3] = {ox, 0.0f, oz},
                    floor[3] = {ix, 0.02f, iz};
        std::copy(top, top + 3, c.top[i]);
        std::copy(rim, rim + 3, c.rim[i]);
        std::copy(base, base + 3, c.base[i]);
        std::copy(floor, floor + 3, c.floor[i]);
    }
    return c;
}

// One wax cell: the wall's top and inside, the outside on the comb's edge, the floor.
inline void addCell(MeshBuf& m, int q, int r, bool edge) {
    static const float up[3] = {0, 1, 0};
    float cx, cz;
    axialToWorld(q, r, cx, cz);
    const float shade = 0.9f + 0.2f * hash1(static_cast<float>(q * 131 + r * 17));
    const CellCorners c = cellCorners(cx, cz);
    for (int i = 0; i < 6; ++i) {
        const int j = (i + 1) % 6;
        const float mx = (c.top[i][0] + c.top[j][0]) * 0.5f - cx, mz = (c.top[i][2] + c.top[j][2]) * 0.5f - cz;
        const float len = std::sqrt(mx * mx + mz * mz);
        const float out[3] = {mx / len, 0, mz / len}, in[3] = {-mx / len, 0, -mz / len};
        m.quad(c.top[i], c.top[j], c.rim[j], c.rim[i], up, scale(kWaxTop, shade));
        m.quad(c.rim[i], c.rim[j], c.floor[j], c.floor[i], in, scale(kWax, shade));
        if (edge) m.quad(c.top[i], c.top[j], c.base[j], c.base[i], out, scale(kWax, shade * 0.8f));
    }
    const uint32_t base = static_cast<uint32_t>(m.pos.size() / 3);
    m.vert(cx, 0.02f, cz, up, scale(kWaxFloor, shade));
    for (int i = 0; i < 6; ++i) m.vert(c.floor[i][0], 0.02f, c.floor[i][2], up, scale(kWaxFloor, shade * 1.15f));
    for (int i = 0; i < 6; ++i) {
        const uint32_t a = base + 1 + static_cast<uint32_t>(i), b = base + 1 + static_cast<uint32_t>((i + 1) % 6);
        m.idx.insert(m.idx.end(), {base, b, a});  // counter-clockwise from above (z is "down" on screen)
    }
}

// The comb as one static mesh; replaces the previous one in *mesh.
inline void buildComb(Host& h, int radius, uint32_t* mesh) {
    if (!h.engine) return;
    if (*mesh) {
        strata_remove_mesh(h.engine, *mesh);
        *mesh = 0;
        h.meshes = 0;
    }
    MeshBuf m;
    for (int q = -radius; q <= radius; ++q)
        for (int r = std::max(-radius, -q - radius); r <= std::min(radius, -q + radius); ++r)
            addCell(m, q, r, hexDist(q, r) == radius);
    StrataMesh desc{};
    desc.positions = m.pos.data();
    desc.normals = m.nrm.data();
    desc.colors = m.col.data();
    desc.indices = m.idx.data();
    desc.vertex_count = static_cast<int32_t>(m.pos.size() / 3);
    desc.index_count = static_cast<int32_t>(m.idx.size());
    desc.color[0] = desc.color[1] = desc.color[2] = desc.color[3] = 1.0f;
    if (strata_add_mesh(h.engine, &desc, mesh) == STRATA_OK) h.meshes = 1;
}

// ---------------------------------------------------------------- meadow

// A gently rolling green terrain tile under the hive.
inline void setMeadow(Host& h) {
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
}

// The wooden board the comb stands on.
inline void drawFrame(Host& h, const World& w) {
    const float r = static_cast<float>(std::max(w.combRadius, 1));
    const float width = kSqrt3 * (2.0f * r + 1.0f) + 1.2f, depth = 1.5f * 2.0f * r + 3.2f;
    h.add(STRATA_PRIMITIVE_CUBE, 0.0f, -0.35f, 0.0f, width, 0.7f, depth, {0.36f, 0.22f, 0.11f});
}

inline void drawFlowers(Host& h, const World& w, float t) {
    const Rgb petals[4] = {{0.95f, 0.45f, 0.65f}, {0.95f, 0.95f, 0.95f}, {0.6f, 0.4f, 0.95f}, {1.0f, 0.55f, 0.2f}};
    for (int i = 0; i < 22; ++i) {
        const float fx = w.meadowX + (hash1(i * 3.1f + 1.0f) - 0.5f) * 7.0f;
        const float fz = w.meadowZ + (hash1(i * 7.7f + 2.0f) - 0.5f) * 10.0f;
        const float stem = 0.5f + 0.5f * hash1(i * 5.3f);
        const float sway = 0.04f * std::sin(t * 1.3f + i);
        h.add(STRATA_PRIMITIVE_CYLINDER, fx, stem * 0.5f - 0.2f, fz, 0.06f, stem * 0.5f + 0.2f, 0.06f,
              {0.18f, 0.45f, 0.12f});
        for (int k = 0; k < 5; ++k) {
            const float a = k * 2.0f * kPi / 5.0f + i;
            ball(h, fx + sway + std::cos(a) * 0.17f, stem + 0.02f, fz + std::sin(a) * 0.17f, 0.2f, 0.06f, 0.2f,
                 petals[i % 4]);
        }
        ball(h, fx + sway, stem + 0.06f, fz, 0.14f, 0.1f, 0.14f, {1.0f, 0.82f, 0.15f});
    }
}

// ---------------------------------------------------------------- regions

// A zone is one session's 19-cell patch (its centre plus two rings), tinted
// by its place's colour: extras r, g, b, strength (web/hive-frame.js).
inline void drawPatch(Host& h, const agentviz::Zone& z) {
    const float strength = z.appear * z.extra[3];
    if (strength < 0.02f) return;
    const float cx = (z.x0 + z.x1) * 0.5f, cz = (z.z0 + z.z1) * 0.5f;
    const Rgb tint{z.extra[0], z.extra[1], z.extra[2]};
    for (int dq = -2; dq <= 2; ++dq)
        for (int dr = std::max(-2, -dq - 2); dr <= std::min(2, -dq + 2); ++dr) {
            float x, zz;
            axialToWorld(dq, dr, x, zz);
            const int ringIndex = hexDist(dq, dr);
            const float ringDim = ringIndex == 0 ? 1.0f : (ringIndex == 1 ? 0.85f : 0.65f);
            disc(h, cx + x, 0.04f, cz + zz, 1.5f, 0.025f, mix(kWaxFloor, tint, strength * ringDim));
        }
}

}  // namespace hive
