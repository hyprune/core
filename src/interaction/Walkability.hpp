#pragma once
// Route walkability (RFC-0011 "Walkability (routes)") and standing spots for a body:
// every 0.25 m along the curve an upward collision surface (normal y >= 0.65) within
// 0.35 m of the previous sample's floor, the character's capsule clear of collision,
// inside the playable union, outside approach zones. Core checks routes at load and on
// every edit; the same check gives the agents' sidestep test.
#include "Placement.hpp"
#include "sim/Route.hpp"
#include "world/World.hpp"
#include <optional>
#include <string>
#include <vector>
namespace hyprune::interaction {
// Screen approach zones (RFC-0011 placement rule 5): in front of every anchor, tool mount
// and workspace wall, its width plus 0.3 m each side, from 0.1 m above its floor to its top
// edge, 1.5 m deep. Floor and ceiling screens have none.
std::vector<Box> approachZones(const world::World &, std::function<bool(unsigned)> ignore = {});
// The same zones with the anchor each stands in front of.
std::vector<std::pair<std::string, Box>> namedApproachZones(const world::World &, std::function<bool(unsigned)> ignore = {});
struct Body {
    float radius = .35f, height = 1.8f;
};
struct WalkProblem {
    Vec3 at{};
    std::string rule;    // "floor", "ledge", "walls", "playable", "approach", "keep-clear"
    std::string message; // short, names the position
    float along = -1;    // checkRoute: the arc length where it was found
};
inline constexpr float walkStep = .25f, maxStep = .35f;
// The floor under a root near y (within maxStep above or below it), if any.
std::optional<float> floorAt(const PlacementScene &, Vec3 root, float within = maxStep);
// One root position: floor under it (within `within` of root.y), capsule clear, playable,
// outside approach zones (and keepClear volumes when strict). Returns the problem or none.
std::optional<WalkProblem> checkSpot(const PlacementScene &, Vec3 root, const Body &, bool strict = false,
                                     float within = maxStep);
// A route: samples every 0.25 m (and every authored point). Problems in order, at most
// `limit`; worlds without collision report none (nothing to check against).
std::vector<WalkProblem> checkRoute(const PlacementScene &, const sim::Route &, const Body &, size_t limit = 8);
// A straight leg (seat approach, guide join) checked like a route.
std::vector<WalkProblem> checkLeg(const PlacementScene &, Vec3 from, Vec3 to, const Body &, size_t limit = 8);
// A route as drawn on the ground (the waypoint route line): from `from` through `points`
// (feet heights), a sample every `step` metres laid on the floor below it, `lift` above
// it. Each sample looks for its floor near the previous sample's (up a stair, down a
// slope, over a hill), then near the straight line; with no floor (a gap, a wall) the
// straight line's height is kept. Route points go on the floor near them (if any).
std::vector<Vec3> groundLine(const PlacementScene &, Vec3 from, const std::vector<Vec3> &points, float step = .5f,
                             float lift = .04f);
// Whether collision lies on the straight segment from a to b (not counting b itself).
bool lineBlocked(const PlacementScene &, Vec3 a, Vec3 b);
} // namespace hyprune::interaction
