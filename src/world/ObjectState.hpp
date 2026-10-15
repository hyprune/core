#pragma once
// Per-instance draw state of world objects, set in the tick by the machines
// prototype (HYPRUNE_MACHINES_PROTOTYPE; RFC-0013 draft) and read by the
// renderer. Nothing here is part of the world format or IPC.
#include "sim/Math.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
namespace hyprune::world {
struct MeshPaint {
    bool hidden = false;
    Vec3 offset{};          // asset-space translation applied after the node animation
    bool painted = false;   // base colour and emission replaced by a palette entry
    Vec3 colour{1, 1, 1};   // linear
    float emission = 0;     // emissive = colour * emission when painted
    float emissiveScale = 1;
};
struct ObjectDrawState {
    bool bound = false;          // false: drawn as authored
    std::vector<double> turns;   // per asset animation: cycle position; empty: the authored clock
    std::vector<MeshPaint> meshes; // per asset mesh; empty: authored
    float saturation = 1, brightness = 1;
    bool moving = false;         // motion in progress: keep frames coming while in view
    uint64_t revision = 0;       // bumps on any change that is not continuous motion
    // Data-driven behaviours (RFC-0014 amendment): a display-only transform in asset space,
    // drawn as model x display (collision, picking and placement rules keep the rest pose),
    // and a texture coordinate offset for every mesh. Both renderers draw them.
    bool displayed = false;
    Mat4 display = Mat4::identity();
    float uvU = 0, uvV = 0;
};
// The largest factor `display` stretches a length by (for bounds), and whether it scales
// unevenly (normals then need the inverse transpose).
inline float displayStretch(const Mat4 &m) {
    float s = 0;
    for (int c = 0; c < 3; ++c)
        s = std::max(s, std::sqrt(m.v[c * 4] * m.v[c * 4] + m.v[c * 4 + 1] * m.v[c * 4 + 1] + m.v[c * 4 + 2] * m.v[c * 4 + 2]));
    return s;
}
inline bool displayUneven(const Mat4 &m) {
    float l[3];
    for (int c = 0; c < 3; ++c)
        l[c] = std::sqrt(m.v[c * 4] * m.v[c * 4] + m.v[c * 4 + 1] * m.v[c * 4 + 1] + m.v[c * 4 + 2] * m.v[c * 4 + 2]);
    return std::abs(l[0] - l[1]) > 1e-4f || std::abs(l[1] - l[2]) > 1e-4f;
}
// An extra draw of an asset (or of one node subtree of it): pooled items on a
// track, effect pieces, count copies. Lit and culled with its owner instance.
struct ObjectExtra {
    unsigned asset = 0;
    int node = -1;               // asset node whose subtree is drawn; -1: the whole asset
    size_t owner = 0;            // world object instance
    Mat4 model = Mat4::identity();
    // relative: model = owner instance model x local, exactly (the offload worker rebuilds
    // the model from its own copy of the instance); otherwise model is world space.
    Mat4 local = Mat4::identity();
    bool relative = false;
    MeshPaint paint;             // applied to the meshes in paintMeshes (null: every mesh drawn)
    const std::vector<unsigned> *paintMeshes = nullptr; // asset mesh indices; owned by the machines runtime
    float scale = 1;             // uniform scale inside model, for the bounds
    float saturation = 1, brightness = 1;
};
// Text on a label slot: a rectangle (centre, half extents along right/up) facing +normal.
struct WorldLabel {
    size_t owner = 0;
    Vec3 centre, right, up;      // right/up: half width/height vectors, world space
    std::string text;
    Vec3 colour{.55f, 1.f, .85f};
    float intensity = 2.5f;
    float saturation = 1, brightness = 1;
};
} // namespace hyprune::world
