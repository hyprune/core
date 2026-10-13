#include "Walkability.hpp"
#include "Hands.hpp"
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
    for (auto &[id, box] : namedApproachZones(w, std::move(ignore)))
        out.push_back(box);
    return out;
}
std::vector<std::pair<std::string, Box>> namedApproachZones(const world::World &w, std::function<bool(unsigned)> ignore) {
    std::vector<std::pair<std::string, Box>> out;
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
        out.push_back({a.value("id", ""), b});
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
    auto body = bodyBox(feet, b);
    if (hitsCollision(s, body))
        return WalkProblem{feet, "walls", "the body hits collision at " + at(feet)};
    for (auto &o : s.obstacles)
        if (boxBox(o, body))
            return WalkProblem{feet, "walls", "the body hits a placed object at " + at(feet)};
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
            problem->along = d;
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
bool lineBlocked(const PlacementScene &s, Vec3 a, Vec3 b) {
    Vec3 d = b - a;
    float l = length(d);
    if (l < 1e-4f || s.collision.empty())
        return false;
    auto hit = raycast(a, d * (1 / l), s.collision, s.index);
    return hit && hit->distance < l - 1e-3f;
}
std::vector<Vec3> groundLine(const PlacementScene &s, Vec3 from, const std::vector<Vec3> &points, float step, float lift) {
    auto floorNear = [&](Vec3 p, float y) { return floorBelow(s, Vec3{p.x, y + 1.f, p.z}, 3.5f); };
    if (auto floor = s.collision.empty() ? std::nullopt : floorNear(from, from.y))
        from.y = *floor; // the start on its floor too (the route's first point, the feet)
    std::vector<Vec3> out{from + Vec3{0, lift, 0}};
    // A chord across a crest (a ramp's top edge, a hill top) can dip under the ground between
    // two samples: such a leg gets a midpoint on the floor, down to 1/8 of a step.
    auto add = [&](auto &self, Vec3 p, int depth) -> void {
        Vec3 a = out.back();
        if (depth < 3 && lineBlocked(s, a, p)) {
            Vec3 m = (a + p) * .5f;
            if (auto floor = floorBelow(s, Vec3{m.x, std::max(a.y, p.y) + .5f, m.z}, 1.f + std::abs(a.y - p.y)))
                m.y = *floor + lift;
            self(self, m, depth + 1);
            self(self, p, depth + 1);
            return;
        }
        out.push_back(p);
    };
    float ground = from.y; // floorNear: from 1 m above a reference height down to 2.5 m below
    Vec3 a = from;
    for (auto b : points) {
        Vec3 d = b - a;
        float flat = std::hypot(d.x, d.z);
        int n = s.collision.empty() ? 1 : std::max(1, int(std::ceil(flat / step)));
        for (int k = 1; k < n; ++k) {
            Vec3 p = a + d * (float(k) / n);
            auto floor = floorNear(p, ground);
            if (!floor)
                floor = floorNear(p, p.y);
            if (floor)
                ground = *floor, p.y = *floor;
            else
                ground = p.y;
            add(add, p + Vec3{0, lift, 0}, 0);
        }
        // A route point on its floor too (a zone's destination height has slack below it).
        if (auto floor = s.collision.empty() ? std::nullopt : floorNear(b, b.y))
            b.y = *floor;
        add(add, b + Vec3{0, lift, 0}, 0);
        ground = b.y;
        a = b;
    }
    return out;
}
} // namespace hyprune::interaction
