#include "Placement.hpp"
#include <limits>
namespace hyprune::interaction {
namespace {
constexpr float touch = .02f; // resting contact allowed (RFC-0011 rule 2)
float rayTriangle(Vec3 o, Vec3 d, const Triangle &t) {
    auto e = t.b - t.a, f = t.c - t.a, p = cross(d, f);
    float det = dot(e, p);
    if (std::abs(det) < 1e-9f)
        return std::numeric_limits<float>::infinity();
    auto s = o - t.a;
    float u = dot(s, p) / det;
    if (u < 0 || u > 1)
        return std::numeric_limits<float>::infinity();
    auto q = cross(s, e);
    float v = dot(d, q) / det;
    if (v < 0 || u + v > 1)
        return std::numeric_limits<float>::infinity();
    float distance = dot(f, q) / det;
    return distance > 1e-5f ? distance : std::numeric_limits<float>::infinity();
}
bool segmentTriangle(Vec3 a, Vec3 b, const Triangle &t) {
    auto d = b - a;
    float l = length(d);
    if (l < 1e-7f)
        return false;
    return rayTriangle(a, d / l, t) <= l;
}
template <class Visit> void candidates(const PlacementScene &s, Vec3 low, Vec3 high, Visit visit) {
    if (s.index && s.index->size() == s.collision.size()) {
        for (auto id : s.index->overlap(low, high))
            if (!s.ignore || !s.ignore(id))
                visit(s.collision[id]);
        return;
    }
    for (unsigned id = 0; id < s.collision.size(); ++id) {
        if (s.ignore && s.ignore(id))
            continue;
        auto &t = s.collision[id];
        Vec3 lo{std::min({t.a.x, t.b.x, t.c.x}), std::min({t.a.y, t.b.y, t.c.y}), std::min({t.a.z, t.b.z, t.c.z})};
        Vec3 hi{std::max({t.a.x, t.b.x, t.c.x}), std::max({t.a.y, t.b.y, t.c.y}), std::max({t.a.z, t.b.z, t.c.z})};
        if (lo.x <= high.x && hi.x >= low.x && lo.y <= high.y && hi.y >= low.y && lo.z <= high.z && hi.z >= low.z)
            visit(t);
    }
}
// Nearest hit along a ray (ignoring the selection), with the triangle's normal.
std::optional<std::pair<float, Vec3>> cast(const PlacementScene &s, Vec3 o, Vec3 d, float maximum) {
    std::optional<std::pair<float, Vec3>> best;
    auto test = [&](unsigned id) {
        if (s.ignore && s.ignore(id))
            return;
        auto &t = s.collision[id];
        float h = rayTriangle(o, d, t);
        if (h <= maximum && (!best || h < best->first))
            best = std::pair{h, normalized(cross(t.b - t.a, t.c - t.a))};
    };
    if (s.index && s.index->size() == s.collision.size())
        for (auto id : s.index->ray(o, d))
            test(id);
    else
        for (unsigned id = 0; id < s.collision.size(); ++id)
            test(id);
    return best;
}
} // namespace
float Box::extent(Vec3 d) const {
    return std::abs(dot(axes[0], d)) * half.x + std::abs(dot(axes[1], d)) * half.y + std::abs(dot(axes[2], d)) * half.z;
}
Box Box::grown(float by) const {
    Box b = *this;
    b.half = {std::max(.005f, half.x + by), std::max(.005f, half.y + by), std::max(.005f, half.z + by)};
    return b;
}
Vec3 Box::low() const {
    return centre - Vec3{extent({1, 0, 0}), extent({0, 1, 0}), extent({0, 0, 1})};
}
Vec3 Box::high() const {
    return centre + Vec3{extent({1, 0, 0}), extent({0, 1, 0}), extent({0, 0, 1})};
}
Box aabbBox(Vec3 low, Vec3 high) {
    Box b;
    b.centre = (low + high) * .5f;
    b.half = (high - low) * .5f;
    return b;
}
bool boxTriangle(const Box &b, const Triangle &t) {
    auto local = [&](Vec3 p) {
        p = p - b.centre;
        return Vec3{dot(p, b.axes[0]), dot(p, b.axes[1]), dot(p, b.axes[2])};
    };
    Vec3 v[3] = {local(t.a), local(t.b), local(t.c)}, edges[3] = {v[1] - v[0], v[2] - v[1], v[0] - v[2]};
    auto separates = [&](Vec3 axis) {
        if (dot(axis, axis) < 1e-12f)
            return false;
        float a = dot(axis, v[0]), c = dot(axis, v[1]), e = dot(axis, v[2]);
        float r = std::abs(axis.x) * b.half.x + std::abs(axis.y) * b.half.y + std::abs(axis.z) * b.half.z;
        return std::min({a, c, e}) > r || std::max({a, c, e}) < -r;
    };
    for (auto axis : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}}) {
        if (separates(axis))
            return false;
        for (auto edge : edges)
            if (separates(cross(edge, axis)))
                return false;
    }
    return !separates(cross(edges[0], edges[1]));
}
bool boxBox(const Box &a, const Box &b) {
    auto separated = [&](Vec3 axis) {
        if (dot(axis, axis) < 1e-10f)
            return false;
        axis = normalized(axis);
        return std::abs(dot(b.centre - a.centre, axis)) > a.extent(axis) + b.extent(axis);
    };
    for (int i = 0; i < 3; ++i) {
        if (separated(a.axes[i]) || separated(b.axes[i]))
            return false;
        for (int j = 0; j < 3; ++j)
            if (separated(cross(a.axes[i], b.axes[j])))
                return false;
    }
    return true;
}
float boxDistance(const Box &b, Vec3 p) {
    auto d = p - b.centre;
    float sum = 0;
    for (int i = 0; i < 3; ++i) {
        float h = i == 0 ? b.half.x : i == 1 ? b.half.y : b.half.z, v = std::abs(dot(d, b.axes[i])) - h;
        if (v > 0)
            sum += v * v;
    }
    return std::sqrt(sum);
}
bool boxSegment(const Box &b, Vec3 a, Vec3 c, float radius) {
    // Sampled: distance from the segment to the box, at most 1 cm between samples.
    int n = std::clamp(int(length(c - a) / .01f), 1, 400);
    for (int k = 0; k <= n; ++k)
        if (boxDistance(b, a + (c - a) * (float(k) / n)) <= radius)
            return true;
    return false;
}
bool triangleTriangle(const Triangle &x, const Triangle &y) {
    return segmentTriangle(x.a, x.b, y) || segmentTriangle(x.b, x.c, y) || segmentTriangle(x.c, x.a, y) ||
           segmentTriangle(y.a, y.b, x) || segmentTriangle(y.b, y.c, x) || segmentTriangle(y.c, y.a, x);
}
std::optional<float> floorBelow(const PlacementScene &s, Vec3 from, float down) {
    auto hit = cast(s, from, {0, -1, 0}, down);
    if (!hit)
        return {};
    auto n = hit->second;
    if (std::abs(n.y) < .65f)
        return {};
    return from.y - hit->first;
}
std::set<std::string> brokenSoftRules(const PlacementScene &s, const Box &shape, std::span<const Triangle> own) {
    std::set<std::string> waived;
    for (int i = 0; i < 4; ++i) {
        auto v = checkPlacement(s, shape, own, waived);
        if (v.ok || v.rule == "walls" || v.rule == "playable" || v.rule == "player" || v.rule == "support")
            break;
        waived.insert(v.rule);
    }
    return waived;
}
PlacementVerdict checkPlacement(const PlacementScene &s, const Box &shape, std::span<const Triangle> own,
                                const std::set<std::string> &waived) {
    auto fail = [](const char *rule, const char *message) { return PlacementVerdict{false, rule, message, true}; };
    float below = shape.extent({0, 1, 0});
    Vec3 base = shape.centre - Vec3{0, below, 0};
    // 1. Inside the world: centre and support point in the playable union, above the respawn height.
    if (s.playable && (!s.playable->contains(shape.centre, 0) || !s.playable->contains(base + Vec3{0, .05f, 0}, 0)))
        return fail("playable", "Outside the playable area");
    if (base.y < s.respawnBelow)
        return fail("playable", "Below the world");
    // 2. Not in walls: the bounds shrunk by the resting contact allowance; with the
    //    selection's own collision, a bounds hit is confirmed against its triangles.
    auto inner = shape.grown(-touch);
    std::vector<Triangle> shrunk;
    if (!own.empty()) {
        shrunk.reserve(own.size());
        auto pull = [&](Vec3 p) {
            auto d = p - shape.centre;
            Vec3 out = shape.centre;
            for (int i = 0; i < 3; ++i) {
                float h = i == 0 ? shape.half.x : i == 1 ? shape.half.y : shape.half.z;
                float k = h > touch ? (h - touch) / h : .5f;
                out += shape.axes[i] * (dot(d, shape.axes[i]) * k);
            }
            return out;
        };
        for (auto &t : own)
            shrunk.push_back({pull(t.a), pull(t.b), pull(t.c)});
    }
    bool walls = false;
    candidates(s, inner.low(), inner.high(), [&](const Triangle &t) {
        if (walls || !boxTriangle(inner, t))
            return;
        if (shrunk.empty()) {
            walls = true;
            return;
        }
        for (auto &o : shrunk)
            if (triangleTriangle(o, t)) {
                walls = true;
                return;
            }
    });
    if (walls)
        return fail("walls", "Inside a wall");
    // Wholly inside solid geometry: back faces seen in (nearly) every direction.
    int inside = 0, open = 0;
    for (auto d : {Vec3{1, 0, 0}, Vec3{-1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, -1, 0}, Vec3{0, 0, 1}, Vec3{0, 0, -1}}) {
        auto hit = cast(s, shape.centre, d, 40);
        if (!hit)
            ++open;
        else if (dot(hit->second, d) > 0)
            ++inside;
    }
    if (inside >= 5 && open == 0)
        return fail("walls", "Inside solid geometry");
    // 3. Supported, per the object's surfaces.
    if (!s.collision.empty() && !s.support.empty() && s.support != "any") {
        bool held = false;
        auto holds = [&](const std::string &surface) {
            if (surface == "floor")
                return floorBelow(s, base + Vec3{0, .05f, 0}, .12f).has_value();
            if (surface == "wall") {
                auto back = shape.axes[2] * -1.f;
                return cast(s, shape.centre, back, shape.extent(back) + .05f).has_value();
            }
            if (surface == "ceiling")
                return cast(s, shape.centre, {0, 1, 0}, shape.extent({0, 1, 0}) + .05f).has_value();
            return false;
        };
        size_t start = 0;
        while (!held && start <= s.support.size()) {
            auto end = s.support.find(',', start);
            held = holds(s.support.substr(start, end == std::string::npos ? std::string::npos : end - start));
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
        if (!held)
            return fail("support", s.support.starts_with("floor") ? "Needs a floor under it"
                                   : s.support.starts_with("wall") ? "Needs a wall behind it"
                                                                   : "Needs a ceiling above it");
    }
    // 4. Windows stay visible.
    for (auto &w : s.windows)
        if (!waived.contains("window") && boxBox(inner, w))
            return fail("window", "Covers a window");
    // 5. Screen approaches and window zones stay free. A zone whose floor is this object (a
    // slab the screens stand on, checked again at load without its own collision in the scene)
    // starts 0.1 m above that floor, as it would over committed collision.
    auto ownTop = [&](const Box &a) -> std::optional<float> {
        Vec3 from{a.centre.x, a.centre.y + a.half.y, a.centre.z};
        std::optional<float> best;
        for (auto &t : own) {
            auto e1 = t.b - t.a, e2 = t.c - t.a;
            Vec3 d{0, -1, 0};
            auto p = cross(d, e2);
            float det = dot(e1, p);
            if (std::abs(det) < 1e-9f)
                continue;
            auto q = from - t.a;
            float u = dot(q, p) / det;
            if (u < 0 || u > 1)
                continue;
            auto r = cross(q, e1);
            float v = dot(d, r) / det;
            if (v < 0 || u + v > 1)
                continue;
            float k = dot(e2, r) / det;
            if (k > 0 && (!best || from.y - k > *best))
                best = from.y - k;
        }
        return best;
    };
    for (auto &a : s.approaches) {
        if (waived.contains("approach") || !boxBox(inner, a))
            continue;
        auto top = ownTop(a);
        if (top && *top <= a.centre.y) {
            Box raised = a;
            float low = std::max(*top + .1f, a.centre.y - a.half.y), high = a.centre.y + a.half.y;
            if (high <= low)
                continue;
            raised.centre.y = (low + high) * .5f;
            raised.half.y = (high - low) * .5f;
            if (!boxBox(inner, raised))
                continue;
        }
        return fail("approach", "Blocks a screen approach");
    }
    for (auto &z : s.zones)
        if (!waived.contains("zone") && boxBox(inner, z))
            return fail("zone", "Inside a window zone");
    // 6. Ways stay open (collidable or taller than 0.3 m).
    bool tall = shape.extent({0, 1, 0}) * 2 > .3f;
    if ((!own.empty() || tall) && !waived.contains("keep-clear"))
        for (auto &k : s.keepClear)
            if (boxBox(inner, k))
                return fail("keep-clear", "Blocks a way");
    // 7. Not into the player (collidable shapes).
    if (s.player && !own.empty() && boxSegment(inner, s.playerLow, s.playerHigh, s.playerRadius))
        return fail("player", "Into the player");
    PlacementVerdict ok;
    ok.supported = !s.collision.empty() && floorBelow(s, base + Vec3{0, .05f, 0}, .12f).has_value();
    if (s.collision.empty())
        ok.supported = true;
    return ok;
}
} // namespace hyprune::interaction
