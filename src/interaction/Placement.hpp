#pragma once
// Placement rules for editor transforms (RFC-0011 "Placement rules", the subset that
// applies to M3B transforms of scene nodes and world 0.8 objects). Pure geometry on
// spans: core checks every gizmo preview and refuses to commit a failing pose.
#include "../sim/TriangleIndex.hpp"
#include "../world/Playable.hpp"
#include <functional>
#include <set>
#include <span>
#include <string>
#include <vector>
namespace hyprune::interaction {
// Oriented box: centre, orthonormal axes and half extents along them.
struct Box {
    Vec3 centre{};
    std::array<Vec3, 3> axes{Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};
    Vec3 half{.5f, .5f, .5f};
    float extent(Vec3 direction) const; // support half-width along a unit direction
    Box grown(float by) const;          // negative shrinks (each half at least 5 mm)
    Vec3 low() const;                   // world AABB
    Vec3 high() const;
};
Box aabbBox(Vec3 low, Vec3 high);
bool boxTriangle(const Box &, const Triangle &);
bool boxBox(const Box &, const Box &);
float boxDistance(const Box &, Vec3);
bool boxSegment(const Box &, Vec3 a, Vec3 b, float radius); // capsule overlap
bool triangleTriangle(const Triangle &, const Triangle &);

struct PlacementScene {
    std::span<const Triangle> collision;
    const sim::TriangleIndex *index = nullptr; // over collision, optional
    std::function<bool(unsigned)> ignore;      // the selection's own committed triangles
    const world::PlayableBounds *playable = nullptr;
    float respawnBelow = -1e9f;
    std::vector<Box> windows;    // placed windows with their margin
    std::vector<Box> approaches; // screen approach zones (anchors, tool mounts, workspace walls)
    std::vector<Box> zones;      // placementZones: reserved for windows
    std::vector<Box> keepClear;  // spawn capsules, climbables and keepClear volumes
    // Characters only (Walkability checkSpot): user-placed objects that have no collision
    // proxy, as their bounds; a character walks round them like round collision.
    std::vector<Box> obstacles;
    bool player = false;
    Vec3 playerLow{}, playerHigh{}; // capsule segment
    float playerRadius = .3f;
    // Rule 3 per movable.surfaces (RFC-0011 P1): "floor" needs an upward surface (normal
    // y >= 0.65) within 7 cm below the base centre, "wall" collision within 5 cm behind
    // (the shape's -Z axis), "ceiling" collision within 5 cm above, a comma list when any
    // of them holds; "any" and "" (scene nodes, world 0.8 instances: advisory only) never
    // fail. Worlds without collision skip it.
    std::string support;
};
struct PlacementVerdict {
    bool ok = true;
    std::string rule;    // "playable", "walls", "support", "window", "approach", "zone", "keep-clear", "player"
    std::string message; // short, for the readout and runtime.notice
    bool supported = true; // advisory: rests on an upward surface (or floats)
};
// shape: the candidate bounds; own: the selection's collision triangles at the
// candidate pose (empty when it has none: the shrunk bounds stand in).
// waived: soft rules (window, approach, zone, keep-clear) not enforced, for selections
// whose start pose already breaks them; walls, playable and player always apply.
PlacementVerdict checkPlacement(const PlacementScene &, const Box &shape, std::span<const Triangle> own,
                                const std::set<std::string> &waived = {});
// The soft rules a pose already breaks (what an edit of it may keep breaking).
std::set<std::string> brokenSoftRules(const PlacementScene &, const Box &shape, std::span<const Triangle> own);
// Highest upward surface (normal y >= 0.65) under a point within `down` metres, else none.
std::optional<float> floorBelow(const PlacementScene &, Vec3 from, float down);
} // namespace hyprune::interaction
