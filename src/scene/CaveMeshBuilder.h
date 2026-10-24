#pragma once
// CPU-side preparation of the cave mesh for the GPU. No GL calls: safe on any thread.
//
//  * de-duplicates vertices (98,094 -> ~62k for the shipped cave) so indices fit in 16 bits
//  * packs a vertex into 24 bytes (was 36): float3 pos, unorm8 rgb + lava flag, float2 uv
//  * sorts triangles into "opaque" (never needs alpha test / blending) and "cutout"
//    (touches transparent or translucent atlas texels). 99.6% of the shipped cave is opaque,
//    which lets the GPU skip discard + blending and keep early-Z on nearly every fragment.
#include "scene/CaveConstants.h"
#include "scene/ImageDecode.h"
#include "scene/MeshVertex.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace animated_mojang::scene {

struct CaveGpuVertex {
    float x, y, z;
    std::uint8_t r, g, b, lava; // lava: 255 = animated lava face
    float u, v;
};
static_assert(sizeof(CaveGpuVertex) == 24, "GPU vertex layout is 24 bytes");

struct CaveMeshCpu {
    std::vector<CaveGpuVertex> vertices;
    std::vector<std::uint32_t> indices; // [opaque triangles][cutout triangles]
    std::uint32_t opaqueIndexCount = 0;
    std::uint32_t cutoutIndexCount = 0;
    std::size_t sourceVertexCount = 0;
    bool empty() const { return vertices.empty() || indices.empty(); }
};

namespace detail {

inline std::uint8_t toUnorm8(float c) {
    return static_cast<std::uint8_t>(std::clamp(c, 0.f, 1.f) * 255.f + 0.5f);
}

struct VertexHash {
    std::size_t operator()(CaveGpuVertex const& v) const noexcept {
        std::uint64_t h = 1469598103934665603ull; // FNV-1a over the 24 raw bytes
        auto const* p = reinterpret_cast<std::uint8_t const*>(&v);
        for (std::size_t i = 0; i < sizeof(v); ++i) h = (h ^ p[i]) * 1099511628211ull;
        return static_cast<std::size_t>(h);
    }
};
struct VertexEq {
    bool operator()(CaveGpuVertex const& a, CaveGpuVertex const& b) const noexcept {
        return std::memcmp(&a, &b, sizeof(a)) == 0;
    }
};

// True if any atlas texel this triangle can sample has alpha < 255.
// Conservative: the sampled rectangle is rounded outwards; lava is checked over all animation frames.
inline bool triangleNeedsAlpha(Vertex const* t, Rgba8Image const& atlas, bool lava) {
    const int W = atlas.width, H = atlas.height;
    float umin = std::min({t[0].u, t[1].u, t[2].u}), umax = std::max({t[0].u, t[1].u, t[2].u});
    float vmin = std::min({t[0].v, t[1].v, t[2].v}), vmax = std::max({t[0].v, t[1].v, t[2].v});
    double fy1 = double(vmax) * H;
    if (lava) fy1 += double(lavaFrameCount - 1) * lavaFramePx * (double(H) / caveAtlasHeight);
    int x0 = std::max(0, int(std::floor(double(umin) * W - 1e-4)));
    int y0 = std::max(0, int(std::floor(double(vmin) * H - 1e-4)));
    int x1 = std::min(W, std::max(x0 + 1, int(std::ceil(double(umax) * W + 1e-4))));
    int y1 = std::min(H, std::max(y0 + 1, int(std::ceil(fy1 + 1e-4))));
    for (int y = y0; y < y1; ++y) {
        std::uint8_t const* row = atlas.data() + (std::size_t(y) * W) * 4u;
        for (int x = x0; x < x1; ++x)
            if (row[x * 4 + 3] < 255) return true;
    }
    return false;
}

} // namespace detail

// `atlas` may be null/empty: every triangle is then treated as cutout (always correct, just slower).
inline CaveMeshCpu buildCaveMesh(std::vector<Vertex> const& raw, Rgba8Image const* atlas) {
    CaveMeshCpu mesh;
    mesh.sourceVertexCount = raw.size();
    const bool classify = atlas && *atlas;
    std::unordered_map<CaveGpuVertex, std::uint32_t, detail::VertexHash, detail::VertexEq> lookup;
    lookup.reserve(raw.size());
    mesh.vertices.reserve(raw.size() * 2 / 3 + 16);

    std::vector<std::uint32_t> opaque, cutout;
    opaque.reserve(raw.size());
    for (std::size_t i = 0; i + 2 < raw.size(); i += 3) {
        const bool lava = (raw[i].texture & animatedLavaFlag) != 0;
        const bool needsAlpha = !classify || detail::triangleNeedsAlpha(&raw[i], *atlas, lava);
        auto& out = needsAlpha ? cutout : opaque;
        for (std::size_t k = 0; k < 3; ++k) {
            Vertex const& s = raw[i + k];
            CaveGpuVertex g{s.position.x, s.position.y, s.position.z,
                            detail::toUnorm8(s.r), detail::toUnorm8(s.g), detail::toUnorm8(s.b),
                            static_cast<std::uint8_t>(lava ? 255 : 0), s.u, s.v};
            auto [it, inserted] = lookup.try_emplace(g, static_cast<std::uint32_t>(mesh.vertices.size()));
            if (inserted) mesh.vertices.push_back(g);
            out.push_back(it->second);
        }
    }
    mesh.opaqueIndexCount = static_cast<std::uint32_t>(opaque.size());
    mesh.cutoutIndexCount = static_cast<std::uint32_t>(cutout.size());
    mesh.indices = std::move(opaque);
    mesh.indices.insert(mesh.indices.end(), cutout.begin(), cutout.end());
    return mesh;
}

} // namespace animated_mojang::scene
