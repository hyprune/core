#include "Walkability.hpp"
#include <format>
namespace hyprune::interaction {
namespace {
std::string at(Vec3 p) { return std::format("({:.2f}, {:.2f}, {:.2f})", p.x, p.y, p.z); }
// The body's capsule as a vertical box above a step: half extents 0.85 r across (the
// capsule's cross-section), from the step height to the head.
Box bodyBox(Vec3 root, const Body &b) {
    float bottom = maxStep * .6f, top = b.height;
    Box box;
    box.centre = root + Vec3{0, (bottom + top) * .5f, 0};
    box.half = {b.radius * .85f, (top - bottom) * .5f, b.radius * .85f};
    return box;
}
bool hitsCollision(const PlacementScene &s, const Box &box) {
    auto lo = box.low(), hi = box.high();
    auto test = [&](unsigned i) { return (!s.ignore || !s.ignore(i)) && boxTriangle(box, s.collision[i]); };
    if (s.index) {
        for (auto i : s.index->overlap(lo, hi))
            if (test(i))
                return true;
        return false;
    }
    for (unsigned i = 0; i < s.collision.size(); ++i)
        if (test(i))
            return true;
    return false;
}
} // namespace
std::vector<Box> approachZones(const world::World &w, std::function<bool(unsigned)> ignore) {
    std::vector<Box> out;
    PlacementScene floorOnly;
    floorOnly.collision = w.collision;
    floorOnly.index = &w.collisionIndex;
    floorOnly.ignore = std::move(ignore);
    auto vec = [](const world::Json &j) { return Vec3{j[0].get<float>(), j[1].get<float>(), j[2].get<float>()}; };
    for (auto &a : w.anchors) {
        if (!a.contains("pose") || !a.contains("size"))
            continue;
        Vec3 centre = vec(a["pose"]["position"]);
        auto q = a["pose"]["rotation"].get<std::array<float, 4>>();
        float width = a["size"][0].get<float>(), height = a["size"][1].get<float>();
        auto right = rotate(q, {1, 0, 0}), normal = rotate(q, {0, 0, 1});
        auto front = centre + normal * .75f;
        float top = centre.y + height * .5f;
        float floor = floorBelow(floorOnly, front, 60).value_or(centre.y - height * .5f);
        Box b;
        b.axes = {right, Vec3{0, 1, 0}, normalized(Vec3{normal.x, 0, normal.z})};
        if (length(b.axes[2]) < .5f)
            continue; // floor or ceiling screens have no approach
        b.axes[0] = normalized(cross(b.axes[1], b.axes[2]));
        float low = floor + .1f;
        if (top <= low)
            continue;
        b.centre = Vec3{front.x, (low + top) * .5f, front.z};
        b.half = {width * .5f + .3f, (top - low) * .5f, .75f};
        out.push_back(b);
    }
    return out;
}
std::optional<float> floorAt(const PlacementScene &s, Vec3 root, float within) {
    return floorBelow(s, root + Vec3{0, within + .05f, 0}, 2 * within + .05f);
}
std::optional<WalkProblem> checkSpot(const PlacementScene &s, Vec3 root, const Body &b, bool strict, float within) {
    if (s.collision.empty())
        return std::nullopt;
    auto floor = floorAt(s, root, within);
    if (!floor)
        return WalkProblem{root, "floor", "no walkable floor at " + at(root)};
    Vec3 feet{root.x, *floor, root.z};
    if (hitsCollision(s, bodyBox(feet, b)))
        return WalkProblem{feet, "walls", "the body hits collision at " + at(feet)};
    if (s.playable && !s.playable->cells.empty() && !s.playable->contains(feet + Vec3{0, .05f, 0}, 0))
        return WalkProblem{feet, "playable", "outside the playable volume at " + at(feet)};
    Box probe;
    probe.centre = feet + Vec3{0, .6f, 0};
    probe.half = {b.radius * .5f, .4f, b.radius * .5f};
    for (auto &a : s.approaches)
        if (boxBox(a, probe))
            return WalkProblem{feet, "approach", "in front of a screen at " + at(feet)};
    if (strict)
        for (auto &k : s.keepClear)
            if (boxBox(k, probe))
                return WalkProblem{feet, "keep-clear", "in a keep-clear volume at " + at(feet)};
    return std::nullopt;
}
std::vector<WalkProblem> checkRoute(const PlacementScene &s, const sim::Route &r, const Body &b, size_t limit) {
    std::vector<WalkProblem> out;
    if (s.collision.empty() || r.samples.empty())
        return out;
    std::vector<float> along;
    for (float d = 0; d < r.length(); d += walkStep)
        along.push_back(d);
    along.push_back(r.length());
    std::optional<float> previous;
    for (float d : along) {
        Vec3 p = r.at(d);
        if (previous)
            p.y = *previous; // the floor follows the previous sample's, within a step
        auto problem = checkSpot(s, p, b, false, previous ? maxStep : maxStep);
        if (problem) {
            if (problem->rule == "floor" && previous)
                problem->rule = "ledge", problem->message = "a drop or gap at " + at(r.at(d));
            out.push_back(*problem);
            if (out.size() >= limit)
                break;
            previous.reset();
            continue;
        }
        previous = floorAt(s, p);
    }
    return out;
}
std::vector<WalkProblem> checkLeg(const PlacementScene &s, Vec3 from, Vec3 to, const Body &b, size_t limit) {
    return checkRoute(s, sim::Route::build({from, to}, {sim::Segment::Line, sim::Segment::Line}, false), b, limit);
}
} // namespace hyprune::interaction
