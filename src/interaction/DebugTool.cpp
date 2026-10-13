#include "DebugTool.hpp"
#include "Hands.hpp"
#include "Tools.hpp"
#include "Walkability.hpp"
#include "world/Visibility.hpp"
#include <format>
#include <map>
#include <unordered_map>
namespace hyprune::interaction {
namespace {
constexpr float pi = 3.14159265f, nearDepth = .1f; // past the gizmo view's own near limit
const Rgba orange{1.f, .55f, .1f}, sky{.36f, .72f, 1.f}, mint{.43f, .95f, .69f}, amber{.875f, .74f, .41f},
    blue{.4f, .7f, 1.f}, green{.3f, .9f, .4f}, teal{.25f, .78f, .85f}, magenta{1.f, .3f, .85f}, coral{1.f, .42f, .37f},
    yellow{1.f, .84f, .25f}, violet{.72f, .52f, 1.f}, white{.965f, .96f, .92f}, slate{.55f, .62f, .75f}, red{1.f, .27f, .24f},
    path{.85f, .65f, .18f};
Vec3 vec(const Json &j) { return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()}; }
std::string metres(float v) { return std::format("{:.2f} m", v); }
std::string point(Vec3 p) { return std::format("({:.2f}, {:.2f}, {:.2f})", p.x, p.y, p.z); }
std::array<Vec3, 8> corners(const Box &b) {
    std::array<Vec3, 8> p;
    for (int i = 0; i < 8; ++i)
        p[i] = b.centre + b.axes[0] * ((i & 1) ? b.half.x : -b.half.x) + b.axes[1] * ((i & 2) ? b.half.y : -b.half.y) +
               b.axes[2] * ((i & 4) ? b.half.z : -b.half.z);
    return p;
}
// A box's 12 edges and (with fill) its six faces.
DebugShape boxShape(const Box &b, Rgba color, float fill, bool dashed = false) {
    DebugShape s{color, {}, {}, fill, dashed};
    auto p = corners(b);
    for (int i = 0; i < 8; ++i)
        for (int bit : {1, 2, 4})
            if (!(i & bit))
                s.edges.push_back({p[i], p[i | bit]});
    if (fill > 0)
        for (int axis = 0; axis < 3; ++axis)
            for (int side = 0; side < 2; ++side) {
                int bit = 1 << axis, u = 1 << ((axis + 1) % 3), w = 1 << ((axis + 2) % 3), base = side ? bit : 0;
                s.faces.push_back({p[base], p[base | u], p[base | u | w], p[base | w]});
            }
    return s;
}
// A rectangle in a plane (centre, half extents along right/up) and its fill.
DebugShape rectangle(Vec3 centre, Vec3 right, Vec3 up, float halfWidth, float halfHeight, Rgba color, float fill) {
    std::vector<Vec3> p = {centre - right * halfWidth - up * halfHeight, centre + right * halfWidth - up * halfHeight,
                           centre + right * halfWidth + up * halfHeight, centre - right * halfWidth + up * halfHeight};
    DebugShape s{color, {}, {}, fill};
    for (size_t i = 0; i < 4; ++i)
        s.edges.push_back({p[i], p[(i + 1) % 4]});
    if (fill > 0)
        s.faces.push_back(p);
    return s;
}
void circle(DebugShape &s, Vec3 centre, float radius, int segments = 24) {
    for (int j = 0; j < segments; ++j) {
        float a = j * 2 * pi / segments, b = (j + 1) * 2 * pi / segments;
        s.edges.push_back({centre + Vec3{radius * std::cos(a), 0, radius * std::sin(a)},
                           centre + Vec3{radius * std::cos(b), 0, radius * std::sin(b)}});
    }
}
void arrow(DebugShape &s, Vec3 from, Vec3 to) {
    s.edges.push_back({from, to});
    auto d = to - from;
    if (length(d) < 1e-4f)
        return;
    auto f = normalized(d), side = normalized(cross(f, {0, 1, 0}));
    if (length(side) < .5f)
        side = {1, 0, 0};
    float head = std::min(.25f, length(d) * .35f);
    s.edges.push_back({to, to - f * head + side * (head * .6f)});
    s.edges.push_back({to, to - f * head - side * (head * .6f)});
}
void cross3(DebugShape &s, Vec3 p, float r) {
    s.edges.push_back({p - Vec3{r, 0, 0}, p + Vec3{r, 0, 0}});
    s.edges.push_back({p - Vec3{0, r, 0}, p + Vec3{0, r, 0}});
    s.edges.push_back({p - Vec3{0, 0, r}, p + Vec3{0, 0, r}});
}
void polyline(DebugShape &s, std::span<const Vec3> points) {
    for (size_t i = 1; i < points.size(); ++i)
        s.edges.push_back({points[i - 1], points[i]});
}
Box boxAround(Vec3 centre, Vec3 half) {
    Box b;
    b.centre = centre;
    b.half = half;
    return b;
}
Box segmentBox(Vec3 a, Vec3 b, float radius) {
    Box out;
    out.centre = (a + b) * .5f;
    out.half = {radius, radius, radius};
    auto d = b - a;
    if (length(d) > 1e-4f) {
        Vec3 x = normalized(d), up = std::abs(x.y) < .9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        Vec3 z = normalized(cross(x, up)), y = cross(z, x);
        out.axes = {x, y, z};
        out.half = {length(d) * .5f + radius, radius, radius};
    }
    return out;
}
// Which area lists name an anchor or a placement zone.
std::string ownerArea(const world::World &w, const std::string &id) {
    for (auto &a : w.areas) {
        if (a.value("primaryAnchor", "") == id)
            return a.value("id", "");
        for (auto key : {"toolAnchors", "dataAnchors", "freePlacementZones"})
            for (auto &v : a.value(key, Json::array()))
                if (v == id)
                    return a.value("id", "");
    }
    return "";
}
std::string areaName(const world::World &w, const std::string &id) {
    for (auto &zone : w.manifest.value("zones", Json::array()))
        if (zone.value("id", "") == id)
            return zone.value("name", id);
    return id;
}
// ---- layers ----
void windows(std::vector<DebugItem> &out, const DebugInputs &in) {
    auto &w = *in.world;
    for (auto &win : in.windows) {
        DebugItem item{win.id, win.free ? "Window in the world" : win.mount.empty() ? "Window on a wall" : "Window on a mount",
                       win.appClass, debugArea(w, win.box.centre)};
        if (win.free)
            item.rule = "window", item.blocks = "Covers a window";
        item.details = {"Title: " + win.title, std::format("Size {:.2f} x {:.2f} m (with its margin)", win.box.half.x * 2, win.box.half.y * 2)};
        if (!win.mount.empty())
            item.details.push_back("Mounted on " + win.mount);
        item.color = orange;
        item.shapes.push_back(boxShape(win.box, orange, .08f));
        item.volume = win.box;
        out.push_back(std::move(item));
    }
    for (auto &a : w.anchors) {
        if (!a.contains("pose") || !a.contains("size"))
            continue;
        auto mount = a.value("mount", Json::object());
        std::string role = mount.value("role", "mounted-workspace");
        Rgba color = role == "tool-mount" ? mint : role == "data-object" ? amber : sky;
        Vec3 centre = vec(a["pose"]["position"]);
        auto q = a["pose"]["rotation"].get<std::array<float, 4>>();
        auto right = rotate(q, {1, 0, 0}), up = rotate(q, {0, 1, 0}), normal = rotate(q, {0, 0, 1});
        float hw = a["size"][0].get<float>() * .5f, hh = a["size"][1].get<float>() * .5f;
        std::string id = a.value("id", "");
        DebugItem item{id,
                       role == "tool-mount" ? "Tool mount frame" : role == "data-object" ? "Data object frame" : "Workspace wall frame",
                       ownerArea(w, id), debugArea(w, centre)};
        item.details = {std::format("{:.2f} x {:.2f} m, mount {}, interaction {}", hw * 2, hh * 2, mount.value("type", "-"),
                                    mount.value("interaction", "-"))};
        if (a.contains("screenType"))
            item.details.push_back("Screen " + a["screenType"].get<std::string>());
        item.color = color;
        auto frame = rectangle(centre, right, up, hw, hh, color, .07f);
        arrow(frame, centre, centre + normal * std::clamp(std::min(hw, hh), .3f, 1.f));
        item.shapes.push_back(std::move(frame));
        Box b;
        b.centre = centre;
        b.axes = {right, up, normal};
        b.half = {hw, hh, .05f};
        item.volume = b;
        out.push_back(std::move(item));
    }
    for (auto &o : w.manifest.value("dataObjects", Json::array())) {
        if (!o.contains("pose"))
            continue;
        Vec3 c = vec(o["pose"]["position"]);
        std::string id = o.value("id", "");
        DebugItem item{id, "Data object", o.value("areaId", ownerArea(w, id)), debugArea(w, c)};
        item.details = {"Mount " + o.value("mount", Json::object()).value("type", std::string("-")) + " at " + point(c)};
        item.color = amber;
        item.volume = boxAround(c, {.2f, .2f, .2f});
        item.shapes.push_back(boxShape(*item.volume, amber, .1f));
        out.push_back(std::move(item));
    }
}
DebugItem windowZone(const world::World &w, const Json &z, Rgba color) {
    auto lo = vec(z["bounds"]["min"]), hi = vec(z["bounds"]["max"]);
    std::string id = z.value("id", "");
    DebugItem item{id, "Window zone (free window placement)", ownerArea(w, id), debugArea(w, (lo + hi) * .5f), "zone",
                   "Inside a window zone"};
    std::string sizes;
    for (auto &s : z.value("windowSizes", Json::array()))
        sizes += std::format("{}{:.1f} x {:.1f}", sizes.empty() ? "" : ", ", s[0].get<float>(), s[1].get<float>());
    if (!sizes.empty())
        item.details.push_back("Window sizes " + sizes + " m");
    if (z.contains("clearance"))
        item.details.push_back("Clearance " + metres(z["clearance"].get<float>()));
    item.details.push_back(std::format("{:.1f} x {:.1f} x {:.1f} m", hi.x - lo.x, hi.y - lo.y, hi.z - lo.z));
    item.color = color;
    item.volume = aabbBox(lo, hi);
    item.shapes.push_back(boxShape(*item.volume, color, .1f));
    return item;
}
void windowZones(std::vector<DebugItem> &out, const DebugInputs &in) {
    for (auto &z : in.world->placementZones)
        if (z.contains("bounds"))
            out.push_back(windowZone(*in.world, z, blue));
}
void collision(std::vector<DebugItem> &out, const DebugInputs &in) {
    auto &w = *in.world;
    size_t first = out.size();
    Vec3 r{in.collisionRange, in.collisionRange, in.collisionRange};
    std::vector<unsigned> near;
    if (w.collisionIndex.size() == w.collision.size())
        near = w.collisionIndex.overlap(in.eye - r, in.eye + r);
    else
        for (unsigned i = 0; i < w.collision.size(); ++i)
            near.push_back(i);
    auto distance = [&](unsigned i) {
        auto &t = w.collision[i];
        return length((t.a + t.b + t.c) * (1.f / 3) - in.eye);
    };
    std::vector<std::pair<float, unsigned>> order;
    for (auto i : near)
        if (float d = distance(i); d <= in.collisionRange * 1.2f)
            order.push_back({d, i});
    std::sort(order.begin(), order.end());
    // Items per owner (scene node or object instance) and kind.
    std::map<std::pair<std::string, int>, size_t> items;
    auto ownerOf = [&](unsigned i) -> std::string {
        if (i < w.collisionNodes.size() && w.collisionNodes[i] != world::objectCollisionNode) {
            unsigned node = w.collisionNodes[i];
            auto name = node < w.nodeNames.size() ? w.nodeNames[node] : "";
            return name.empty() ? "node-" + std::to_string(node) : name;
        }
        for (auto &o : w.objectInstances)
            if (i >= o.collisionFirst && i < o.collisionFirst + o.collisionCount)
                return "object:" + o.id;
        return "collision";
    };
    static const char *types[] = {"Collision: floor", "Collision: wall", "Collision: invisible wall (backstop)",
                                  "Collision: object proxy"};
    static const Rgba colors[] = {green, teal, magenta, orange};
    // Feature edges only: an edge shared by two coplanar triangles of one item is a diagonal.
    struct Edge {
        Vec3 a, b, normal;
        size_t item;
        int count = 0;
        bool feature = false;
    };
    std::unordered_map<std::string, Edge> edges;
    auto key = [](Vec3 a, Vec3 b) {
        auto q = [](Vec3 p) { return std::array<long, 3>{std::lround(p.x * 200), std::lround(p.y * 200), std::lround(p.z * 200)}; };
        auto ka = q(a), kb = q(b);
        if (kb < ka)
            std::swap(ka, kb);
        return std::format("{},{},{},{},{},{}", ka[0], ka[1], ka[2], kb[0], kb[1], kb[2]);
    };
    size_t budget = 0;
    for (auto [d, i] : order) {
        if (budget >= in.collisionEdges)
            break;
        auto &t = w.collision[i];
        int kind = i < in.collisionKinds.size() ? in.collisionKinds[i]
                   : i < w.collisionNodes.size() && w.collisionNodes[i] == world::objectCollisionNode ? int(ObjectProxy)
                   : std::abs(normalized(cross(t.b - t.a, t.c - t.a)).y) >= .65f ? int(Floor) : int(Wall);
        auto owner = ownerOf(i);
        auto [it, fresh] = items.try_emplace({owner, kind}, out.size());
        if (fresh) {
            DebugItem item{owner, types[kind], owner.starts_with("object:") ? owner.substr(7) : "", debugArea(w, t.a), "walls",
                           "Inside a wall"};
            item.color = colors[kind];
            DebugShape s{colors[kind], {}, {}, kind == Backstop ? .1f : 0.f};
            s.pickFaces = true;
            s.halo = false;
            item.shapes.push_back(std::move(s));
            out.push_back(std::move(item));
        }
        auto &item = out[it->second];
        item.shapes[0].faces.push_back({t.a, t.b, t.c});
        auto n = normalized(cross(t.b - t.a, t.c - t.a));
        for (auto [a, b] : {std::pair{t.a, t.b}, std::pair{t.b, t.c}, std::pair{t.c, t.a}}) {
            auto &e = edges[key(a, b)];
            if (e.count == 0) {
                e = {a, b, n, it->second, 1, false};
                ++budget;
            } else {
                ++e.count;
                if (e.item != it->second || std::abs(dot(e.normal, n)) < .985f)
                    e.feature = true;
            }
        }
    }
    for (auto &[k, e] : edges)
        if (e.count == 1 || e.feature)
            out[e.item].shapes[0].edges.push_back({e.a, e.b});
    for (size_t i = first; i < out.size(); ++i)
        out[i].details.push_back(std::to_string(out[i].shapes[0].faces.size()) + " triangles within " +
                                 metres(in.collisionRange * 1.2f));
}
// The convex hull of points: one polygon per supporting plane (points on it, ordered).
std::vector<std::vector<Vec3>> hullFaces(const std::vector<Vec3> &points) {
    std::vector<std::pair<Vec3, float>> planes;
    for (size_t i = 0; i < points.size(); i++)
        for (size_t k = i + 1; k < points.size(); k++)
            for (size_t l = k + 1; l < points.size(); l++) {
                auto n = cross(points[k] - points[i], points[l] - points[i]);
                if (length(n) < 1e-6f)
                    continue;
                n = normalized(n);
                float d = dot(n, points[i]);
                bool positive = false, negative = false;
                for (auto p : points) {
                    float s = dot(n, p) - d;
                    positive |= s > 1e-4f;
                    negative |= s < -1e-4f;
                }
                if (positive && negative)
                    continue;
                if (positive)
                    n = n * -1, d = -d;
                bool duplicate = false;
                for (auto &[m, e] : planes)
                    duplicate |= dot(n, m) > .99999f && std::abs(d - e) < 1e-4f;
                if (!duplicate)
                    planes.push_back({n, d});
            }
    std::vector<std::vector<Vec3>> faces;
    for (auto &[n, d] : planes) {
        std::vector<Vec3> on;
        Vec3 c{};
        for (auto p : points)
            if (std::abs(dot(n, p) - d) < 1e-3f)
                on.push_back(p), c += p;
        if (on.size() < 3)
            continue;
        c = c * (1.f / on.size());
        Vec3 u = normalized(on[0] - c), v = cross(n, u);
        std::sort(on.begin(), on.end(), [&](Vec3 a, Vec3 b) {
            return std::atan2(dot(a - c, v), dot(a - c, u)) < std::atan2(dot(b - c, v), dot(b - c, u));
        });
        faces.push_back(on);
    }
    return faces;
}
void playable(std::vector<DebugItem> &out, const DebugInputs &in) {
    auto &w = *in.world;
    auto top = w.manifest.value("playable", Json::object());
    std::optional<float> ceiling;
    if (top.contains("maxFlyHeight"))
        ceiling = top["maxFlyHeight"].get<float>();
    Vec3 lo{1e9f, 0, 1e9f}, hi{-1e9f, 0, -1e9f};
    auto volumes = [&](const Json &declaration, const std::string &area, Rgba color) {
        std::optional<float> own = ceiling;
        if (declaration.contains("maxFlyHeight"))
            own = std::min(own.value_or(1e9f), declaration["maxFlyHeight"].get<float>());
        int index = 0;
        for (auto &v : declaration.value("volumes", Json::array())) {
            DebugItem item{(area.empty() ? std::string("world") : area) + " volume " + std::to_string(++index),
                           area.empty() ? "Playable volume (world)" : "Playable volume", area.empty() ? "world" : area, area};
            item.details.push_back("Placement outside every playable volume is refused: Outside the playable area");
            item.color = color;
            if (v.value("type", "") == "box") {
                auto a = vec(v["min"]), b = vec(v["max"]);
                if (own)
                    b.y = std::min(b.y, *own);
                item.details.push_back("Box " + point(a) + " to " + point(b));
                item.volume = aabbBox(a, b);
                item.shapes.push_back(boxShape(*item.volume, color, .03f));
                lo = {std::min(lo.x, a.x), 0, std::min(lo.z, a.z)}, hi = {std::max(hi.x, b.x), 0, std::max(hi.z, b.z)};
            } else {
                std::vector<Vec3> points;
                for (auto &p : v.value("points", Json::array()))
                    points.push_back(vec(p));
                DebugShape s{color, {}, hullFaces(points), .03f};
                s.pickFaces = true;
                for (auto &f : s.faces)
                    for (size_t i = 0; i < f.size(); ++i)
                        s.edges.push_back({f[i], f[(i + 1) % f.size()]});
                for (auto p : points)
                    lo = {std::min(lo.x, p.x), 0, std::min(lo.z, p.z)}, hi = {std::max(hi.x, p.x), 0, std::max(hi.z, p.z)};
                item.details.push_back("Convex hull of " + std::to_string(points.size()) + " points");
                item.shapes.push_back(std::move(s));
            }
            out.push_back(std::move(item));
        }
    };
    volumes(top, "", sky);
    for (auto &a : w.areas)
        if (a.contains("playable"))
            volumes(a["playable"], a.value("id", ""), mint);
    if (ceiling && lo.x < hi.x) {
        DebugItem item{"fly-ceiling", "Fly ceiling", "world", "", "", ""};
        item.details = {"maxFlyHeight " + metres(*ceiling) + ": flying stops here"};
        item.color = coral;
        float y = *ceiling;
        DebugShape s{coral, {}, {{{lo.x, y, lo.z}, {hi.x, y, lo.z}, {hi.x, y, hi.z}, {lo.x, y, hi.z}}}, .05f};
        s.pickFaces = true;
        float step = std::max(4.f, std::max(hi.x - lo.x, hi.z - lo.z) / 40);
        for (float x = lo.x; x <= hi.x + 1e-3f; x += step)
            s.edges.push_back({{x, y, lo.z}, {x, y, hi.z}});
        for (float z = lo.z; z <= hi.z + 1e-3f; z += step)
            s.edges.push_back({{lo.x, y, z}, {hi.x, y, z}});
        item.shapes.push_back(std::move(s));
        out.push_back(std::move(item));
    }
}
void paths(std::vector<DebugItem> &out, const DebugInputs &in) {
    auto &w = *in.world;
    auto add = [&](Json p, bool authored) {
        if (!p.contains("closed"))
            p["closed"] = false;
        auto &points = p["points"];
        if (!points.is_array() || points.empty())
            return;
        Rgba color = authored ? path : sky;
        std::string id = p.value("id", "");
        DebugItem item{id, authored ? "Path (" + p.value("use", std::string("generic")) + ")" : "User path",
                       p.value("name", Json(nullptr)).is_string() ? p["name"].get<std::string>() : "",
                       debugArea(w, vec(points[0]["position"]))};
        item.color = color;
        DebugShape line{color};
        for (auto &l : pathLines(p))
            line.edges.push_back({l.a, l.b});
        DebugShape marks{white};
        for (auto &v : points)
            cross3(marks, vec(v["position"]), .12f);
        DebugShape stops{coral};
        for (auto &s : p.value("stops", Json::array())) {
            int at = s.value("point", 0);
            if (at < 0 || at >= int(points.size()))
                continue;
            auto c = vec(points[at]["position"]) + Vec3{0, .03f, 0};
            circle(stops, c, .4f, 20);
            std::string what = std::format("Stop at point {}: {} s", at, s.value("seconds", 0.f));
            if (s.contains("clip"))
                what += ", " + s["clip"].get<std::string>();
            if (s.contains("seat"))
                what += ", seat " + s["seat"].get<std::string>();
            if (s.contains("face") && s["face"].contains("anchor"))
                what += ", faces " + s["face"]["anchor"].get<std::string>();
            item.details.push_back(what);
        }
        item.details.insert(item.details.begin(),
                            std::format("{} points, {}{}", points.size(), p["closed"].get<bool>() ? "closed" : "open",
                                        p.contains("speed") && p["speed"].is_number() ? std::format(", {:.1f} m/s", p["speed"].get<float>()) : ""));
        if (item.details.size() > 4)
            item.details.resize(4);
        item.shapes = {std::move(line), std::move(marks), std::move(stops)};
        out.push_back(std::move(item));
    };
    for (auto &p : w.paths)
        add(p, true);
    for (auto &p : in.paths)
        if (p.value("worldId", "") == w.id)
            add(p, false);
    if (!w.walkingEdges.empty()) {
        DebugItem item{"walking-graph", "Walking graph (routes extension)", "world", ""};
        item.color = green;
        item.details = {std::to_string(w.walkingNodes.size()) + " nodes, " + std::to_string(w.walkingEdges.size()) + " edges"};
        std::map<std::string, Vec3> nodes;
        for (auto &n : w.walkingNodes)
            nodes[n.value("id", "")] = vec(n["position"]);
        DebugShape s{{green.r, green.g, green.b, .7f}};
        s.dashed = true;
        for (auto &e : w.walkingEdges)
            if (nodes.contains(e.value("from", "")) && nodes.contains(e.value("to", "")))
                s.edges.push_back({nodes[e.value("from", "")] + Vec3{0, .05f, 0}, nodes[e.value("to", "")] + Vec3{0, .05f, 0}});
        item.shapes.push_back(std::move(s));
        out.push_back(std::move(item));
    }
}
// keepClear volumes, spawn and climb clearances (rule 6) and screen approaches (rule 5).
void keepClear(std::vector<DebugItem> &out, const DebugInputs &in, bool approaches, Rgba keepColor, Rgba approachColor,
               Rgba clearanceColor) {
    auto &w = *in.world;
    auto add = [&](const std::string &id, const std::string &type, const std::string &owner, const Box &b, Rgba color,
                   const std::string &rule, const std::string &blocks, std::vector<std::string> details) {
        DebugItem item{id, type, owner, debugArea(w, b.centre), rule, blocks, std::move(details), color};
        item.volume = b;
        item.shapes.push_back(boxShape(b, color, .08f));
        out.push_back(std::move(item));
    };
    auto size = [](const Box &b) { return std::format("{:.1f} x {:.1f} x {:.1f} m", b.half.x * 2, b.half.y * 2, b.half.z * 2); };
    for (auto &k : w.keepClear) {
        auto b = aabbBox(vec(k["min"]), vec(k["max"]));
        add(k.value("id", ""), "keepClear volume", "world", b, keepColor, "keep-clear", "Blocks a way",
            {size(b), "Refuses collidable or taller than 0.3 m objects"});
    }
    for (auto &[id, b] : in.keepClear)
        add(id, "keepClear volume (development file)", "", b, keepColor, "keep-clear", "Blocks a way", {size(b)});
    for (auto &sp : w.spawns) {
        auto b = aabbBox(sp.position - Vec3{.5f, 0, .5f}, sp.position + Vec3{.5f, 2.f, .5f});
        add("spawn " + sp.id, "Spawn clearance", "world", b, clearanceColor, "keep-clear", "Blocks a way",
            {"The arrival capsule stays free"});
    }
    for (size_t i = 0; i < w.climbables.size(); ++i) {
        auto &c = w.climbables[i];
        Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        for (auto p : {c.bottom, c.top, c.bottomExit, c.topExit})
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)}, hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
        auto b = aabbBox(lo - Vec3{.4f, 0, .4f}, hi + Vec3{.4f, 1.9f, .4f});
        add("climbable " + std::to_string(i + 1), "Climb clearance", "world", b, clearanceColor, "keep-clear", "Blocks a way",
            {"Ladder or climb path with its exits"});
    }
    if (approaches)
        for (auto &[id, b] : namedApproachZones(w))
            add(id + " approach", "Screen approach zone", id, b, approachColor, "approach", "Blocks a screen approach",
                {"In front of " + id + ", " + size(b)});
}
void seats(std::vector<DebugItem> &out, const DebugInputs &in) {
    for (auto &s : in.seats) {
        Vec3 forward{std::sin(s.yaw), 0, std::cos(s.yaw)}, right{std::cos(s.yaw), 0, -std::sin(s.yaw)};
        Rgba color = s.takenBy.empty() ? mint : coral;
        DebugItem item{s.id, s.owner.empty() ? "Seat" : "Seat anchor of an object", s.owner, debugArea(*in.world, s.position)};
        item.color = color;
        item.details = {std::format("Height {:.2f} m, approach {:.2f} m", s.height, s.approach)};
        if (!s.takenBy.empty())
            item.details.push_back("Taken by " + s.takenBy);
        Vec3 top = s.position + Vec3{0, s.height, 0};
        auto seat = rectangle(top, right, forward, .25f, .22f, color, .2f);
        arrow(seat, top, top + forward * .45f);
        seat.edges.push_back({s.position, top});
        DebugShape leg{sky};
        Vec3 approach = s.position + forward * s.approach;
        leg.dashed = true;
        arrow(leg, approach + Vec3{0, .05f, 0}, s.position + Vec3{0, .05f, 0});
        circle(leg, approach + Vec3{0, .05f, 0}, .15f, 12);
        item.shapes = {std::move(seat), std::move(leg)};
        item.volume = boxAround(s.position + Vec3{0, s.height * .5f, 0} + forward * (s.approach * .5f),
                                {.3f, std::max(.2f, s.height * .5f), .3f + s.approach * .5f});
        item.volume->axes = {right, {0, 1, 0}, forward};
        out.push_back(std::move(item));
    }
}
void visibility(std::vector<DebugItem> &out, const DebugInputs &in) {
    auto &w = *in.world;
    auto areas = w.manifest.value("visibility", Json::object()).value("areas", Json::array());
    auto shown = world::visibleAreas(w, in.eye);
    std::vector<int> owned(w.visibility.size(), 0);
    for (auto n : w.nodeVisibility)
        if (n >= 0 && n < int(owned.size()))
            ++owned[n];
    for (size_t i = 0; i < w.visibility.size(); ++i) {
        auto &a = w.visibility[i];
        bool here = in.eye.x >= a.min.x && in.eye.y >= a.min.y && in.eye.z >= a.min.z && in.eye.x <= a.max.x &&
                    in.eye.y <= a.max.y && in.eye.z <= a.max.z;
        // Regions that own scene nodes are drawn or culled from where the eye is; the others
        // are only viewpoints (other regions list them in visibleFrom).
        bool owner = owned[i] > 0;
        Rgba color = here ? yellow : !owner ? Rgba{slate.r, slate.g, slate.b, .45f} : shown[i] ? green : coral;
        std::string id = i < areas.size() ? areas[i].value("id", "") : "area-" + std::to_string(i);
        std::string type = here ? "Visibility region (the eye is here)"
                           : !owner ? "Visibility region (a viewpoint, owns no nodes)"
                           : shown[i] ? "Visibility region (drawn from here)"
                                      : "Visibility region (culled from here)";
        DebugItem item{id, type, owner ? std::to_string(owned[i]) + " scene nodes" : "", debugArea(w, (a.min + a.max) * .5f)};
        item.color = color;
        item.details = {std::format("Visible from {} regions; owns {} scene nodes", a.from.size(), owned[i]),
                        "Bounds " + point(a.min) + " to " + point(a.max)};
        item.volume = aabbBox(a.min, a.max);
        item.shapes.push_back(boxShape(*item.volume, color, owner && shown[i] ? .05f : 0.f, !here && !(owner && shown[i])));
        out.push_back(std::move(item));
    }
}
void steering(std::vector<DebugItem> &out, const DebugInputs &in) {
    for (auto &c : in.characters) {
        DebugItem item{c.id, c.companion ? "Companion (steering)" : "Character (steering)", c.name, debugArea(*in.world, c.position)};
        item.color = mint;
        item.details = {std::format("{}: {:.2f} m/s, offset {:+.2f} m (steering {:+.2f}, pace {:.0f}%)", c.mode, c.speed, c.lateral,
                                    c.steerLateral, c.steerFactor * 100)};
        if (!c.blockedBy.empty())
            item.details.push_back(std::format("Held up by {} for {:.1f} s", c.blockedBy, c.blockedFor));
        if (!c.detours.empty())
            item.details.push_back(std::format("{} detours{}", c.detours.size(), c.activeDetour >= 0 ? ", walking one" : ""));
        Vec3 feet = c.position + Vec3{0, .04f, 0};
        DebugShape body{mint};
        circle(body, feet, c.radius);
        circle(body, feet + Vec3{0, c.height, 0}, c.radius, 16);
        for (int k = 0; k < 4; ++k) {
            float a = k * pi / 2;
            Vec3 o{c.radius * std::cos(a), 0, c.radius * std::sin(a)};
            body.edges.push_back({feet + o, feet + o + Vec3{0, c.height, 0}});
        }
        Vec3 facing{std::sin(c.heading), 0, std::cos(c.heading)};
        arrow(body, feet, feet + facing * (c.radius + .45f));
        item.shapes.push_back(std::move(body));
        // Predicted positions over the steering horizon (what the others plan against).
        DebugShape predicted{sky};
        predicted.dashed = true;
        if (std::hypot(c.velocity.x, c.velocity.z) > .05f)
            for (float t : {.5f, 1.f, 1.5f, 2.f})
                circle(predicted, feet + Vec3{c.velocity.x, 0, c.velocity.z} * t, c.radius, 16);
        item.shapes.push_back(std::move(predicted));
        DebugShape route{{path.r, path.g, path.b, .6f}};
        polyline(route, c.route);
        if (c.routePoint)
            route.edges.push_back({*c.routePoint + Vec3{0, .05f, 0}, feet}); // the offset from the line
        item.shapes.push_back(std::move(route));
        DebugShape detours{violet};
        for (size_t d = 0; d < c.detours.size(); ++d)
            polyline(detours, c.detours[d]);
        polyline(detours, c.legs);
        item.shapes.push_back(std::move(detours));
        if (c.blocker) {
            DebugShape held{red};
            arrow(held, feet + Vec3{0, .9f, 0}, *c.blocker + Vec3{0, .9f, 0});
            item.shapes.push_back(std::move(held));
        }
        item.volume = boxAround(c.position + Vec3{0, c.height * .5f, 0}, {c.radius, c.height * .5f, c.radius});
        out.push_back(std::move(item));
    }
}
void placement(std::vector<DebugItem> &out, const DebugInputs &in) {
    for (auto &win : in.windows)
        if (win.free) {
            DebugItem item{win.id, "Window (rule 4: windows stay visible)", win.appClass, debugArea(*in.world, win.box.centre),
                           "window", "Covers a window"};
            item.details = {"Title: " + win.title};
            item.color = orange;
            item.volume = win.box;
            item.shapes.push_back(boxShape(win.box, orange, .1f));
            out.push_back(std::move(item));
        }
    for (auto &z : in.world->placementZones)
        if (z.contains("bounds"))
            out.push_back(windowZone(*in.world, z, blue));
    keepClear(out, in, true, yellow, violet, yellow);
}
// Ray against an oriented box: entry and exit distances.
std::optional<std::pair<float, float>> rayBox(const Box &b, Vec3 o, Vec3 d) {
    float lo = -1e30f, hi = 1e30f;
    auto p = o - b.centre;
    for (int i = 0; i < 3; ++i) {
        float h = i == 0 ? b.half.x : i == 1 ? b.half.y : b.half.z;
        float e = dot(b.axes[i], p), f = dot(b.axes[i], d);
        if (std::abs(f) < 1e-9f) {
            if (std::abs(e) > h)
                return {};
            continue;
        }
        float t0 = (-h - e) / f, t1 = (h - e) / f;
        if (t0 > t1)
            std::swap(t0, t1);
        lo = std::max(lo, t0), hi = std::min(hi, t1);
        if (lo > hi)
            return {};
    }
    if (hi < 0)
        return {};
    return std::pair{lo, hi};
}
std::optional<float> rayPolygon(std::span<const Vec3> p, Vec3 o, Vec3 d) {
    for (size_t i = 1; i + 1 < p.size(); ++i) {
        auto e = p[i] - p[0], f = p[i + 1] - p[0], q = cross(d, f);
        float det = dot(e, q);
        if (std::abs(det) < 1e-9f)
            continue;
        auto v = o - p[0];
        float u = dot(v, q) / det;
        auto r = cross(v, e);
        float w = dot(d, r) / det, t = dot(f, r) / det;
        if (u >= 0 && w >= 0 && u + w <= 1 && t > 0)
            return t;
    }
    return {};
}
// Eye depth of a point and its clip against the near plane.
float depthOf(const sim::Camera &c, Vec3 p) { return -c.view().point(p).z; }
} // namespace

const DebugLayerInfo &debugLayerInfo(int layer) {
    static const std::vector<DebugLayerInfo> layers = {
        {"windows", "Window boxes & mounts",
         {{"window", orange}, {"workspace wall", sky}, {"tool mount", mint}, {"data object", amber}}},
        {"window-zones", "Window zones", {{"window zone (objects refused)", blue}}},
        {"collision", "Collision & backstops",
         {{"floor", green}, {"wall", teal}, {"invisible wall / backstop", magenta}, {"object proxy", orange}}},
        {"playable", "Playable volumes & fly ceiling", {{"area volume", mint}, {"world volume", sky}, {"fly ceiling", coral}}},
        {"paths", "Paths & stops",
         {{"route", path}, {"point", white}, {"stop", coral}, {"user path", sky}, {"walking graph", green}}},
        {"keep-clear", "keepClear & approach zones",
         {{"keepClear", yellow}, {"screen approach", violet}, {"spawn / climb clearance", orange}}},
        {"seats", "Seats", {{"seat", mint}, {"approach", sky}, {"taken", coral}}},
        {"visibility", "Visibility regions",
         {{"eye is here", yellow}, {"drawn from here", green}, {"culled from here", coral}, {"viewpoint", slate}}},
        {"steering", "NPC steering",
         {{"body", mint}, {"predicted", sky}, {"route offset", path}, {"detour", violet}, {"held up by", red}}},
        {"placement", "Placement rules",
         {{"covers a window", orange}, {"window zone", blue}, {"screen approach", violet}, {"keepClear", yellow}}}};
    return layers[std::clamp(layer, 0, debugLayerCount - 1)];
}
int debugLayerStep(int layer, int steps) {
    return ((layer + steps) % debugLayerCount + debugLayerCount) % debugLayerCount;
}
std::string debugArea(const world::World &w, Vec3 p) {
    for (auto &zone : w.manifest.value("zones", Json::array())) {
        if (!zone.contains("bounds"))
            continue;
        auto lo = vec(zone["bounds"]["min"]), hi = vec(zone["bounds"]["max"]);
        if (p.x >= lo.x && p.y >= lo.y && p.z >= lo.z && p.x <= hi.x && p.y <= hi.y && p.z <= hi.z)
            return zone.value("id", "");
    }
    return "";
}
std::vector<uint8_t> classifyCollision(const world::World &w) {
    std::vector<uint8_t> kinds(w.collision.size(), Wall);
    bool visual = !w.visualTriangles.empty() && w.visualIndex.size() == w.visualTriangles.size();
    for (size_t i = 0; i < w.collision.size(); ++i) {
        auto &t = w.collision[i];
        if (i < w.collisionNodes.size() && w.collisionNodes[i] == world::objectCollisionNode) {
            kinds[i] = ObjectProxy;
            continue;
        }
        auto n = cross(t.b - t.a, t.c - t.a);
        if (length(n) < 1e-9f)
            continue;
        n = normalized(n);
        if (std::abs(n.y) >= .65f) { // either winding: a walkable slope (or its underside)
            kinds[i] = Floor;
            continue;
        }
        if (!visual)
            continue;
        // Visible geometry within 0.3 m of the centre, on either side: a wall you can see.
        auto c = (t.a + t.b + t.c) * (1.f / 3);
        bool seen = false;
        for (float side : {1.f, -1.f}) {
            auto hit = raycast(c + n * (.3f * side), n * -side, w.visualTriangles, &w.visualIndex);
            seen |= hit && hit->distance < .6f;
        }
        kinds[i] = seen ? Wall : Backstop;
    }
    return kinds;
}
std::vector<DebugItem> debugItems(DebugLayer layer, const DebugInputs &in) {
    std::vector<DebugItem> out;
    if (!in.world)
        return out;
    switch (layer) {
    case DebugLayer::Windows: windows(out, in); break;
    case DebugLayer::WindowZones: windowZones(out, in); break;
    case DebugLayer::Collision: collision(out, in); break;
    case DebugLayer::Playable: playable(out, in); break;
    case DebugLayer::Paths: paths(out, in); break;
    case DebugLayer::KeepClear: keepClear(out, in, true, yellow, violet, orange); break;
    case DebugLayer::Seats: seats(out, in); break;
    case DebugLayer::Visibility: visibility(out, in); break;
    case DebugLayer::Steering: steering(out, in); break;
    case DebugLayer::Placement: placement(out, in); break;
    case DebugLayer::Count: break;
    }
    return out;
}
std::optional<DebugPick> pickDebugItem(std::span<const DebugItem> items, const GizmoView &view, float x, float y) {
    auto o = view.camera.position, d = view.ray(x, y);
    float tolerance = 9 * std::max(1.f, view.height / 1080.f);
    std::optional<DebugPick> best, holding;
    float holdingSize = 1e30f;
    auto consider = [&](int i, float t) {
        if (t > 0 && (!best || t < best->distance))
            best = DebugPick{i, t, o + d * t};
    };
    for (int i = 0; i < int(items.size()); ++i) {
        auto &item = items[i];
        if (item.volume)
            if (auto hit = rayBox(*item.volume, o, d)) {
                if (hit->first > 0)
                    consider(i, hit->first);
                else {
                    float size = item.volume->half.x * item.volume->half.y * item.volume->half.z;
                    if (size < holdingSize)
                        holdingSize = size, holding = DebugPick{i, hit->second, o + d * hit->second};
                }
            }
        for (auto &shape : item.shapes) {
            if (shape.pickFaces)
                for (auto &f : shape.faces)
                    if (auto t = rayPolygon(f, o, d))
                        consider(i, *t);
            for (auto &[a, b] : shape.edges) {
                // Clip to the near plane, then the pixel distance to the projected segment.
                float da = depthOf(view.camera, a), db = depthOf(view.camera, b);
                if (da <= nearDepth && db <= nearDepth)
                    continue;
                Vec3 p = a, q = b;
                if (da < nearDepth)
                    p = a + (b - a) * ((nearDepth - da) / (db - da));
                if (db < nearDepth)
                    q = a + (b - a) * ((nearDepth - da) / (db - da));
                auto sp = view.project(p), sq = view.project(q);
                if (!sp || !sq)
                    continue;
                float dx = sq->x - sp->x, dy = sq->y - sp->y, len2 = dx * dx + dy * dy;
                float s = len2 > 1e-6f ? std::clamp(((x - sp->x) * dx + (y - sp->y) * dy) / len2, 0.f, 1.f) : 0.f;
                float px = sp->x + dx * s - x, py = sp->y + dy * s - y;
                if (px * px + py * py > tolerance * tolerance)
                    continue;
                consider(i, length(p + (q - p) * s - o));
            }
        }
    }
    return best ? best : holding;
}
std::vector<std::string> debugCard(const DebugItem &item) {
    std::vector<std::string> lines{item.id.empty() ? "(unnamed)" : item.id, item.type};
    std::string where;
    if (!item.owner.empty())
        where = "Owner: " + item.owner;
    if (!item.area.empty())
        where += (where.empty() ? "" : "   ") + std::string("Area: ") + item.area;
    if (!where.empty())
        lines.push_back(where);
    lines.push_back(item.blocks.empty() ? "Does not block placement"
                                        : "Blocks placement: " + item.blocks + " (rule " + item.rule + ")");
    for (size_t i = 0; i < item.details.size() && i < 3; ++i)
        lines.push_back(item.details[i]);
    return lines;
}
float debugLineWidth(const GizmoView &view) {
    return 3.f * std::max(1.f, view.height / 1080.f);
}
GizmoFrame debugFrame(std::span<const DebugItem> items, const GizmoView &view, int selected) {
    GizmoFrame frame;
    float width = debugLineWidth(view);
    auto onScreen = [&](const GizmoPoint &a, const GizmoPoint &b) {
        float m = view.width * .25f;
        return !((a.x < -m && b.x < -m) || (a.x > view.width + m && b.x > view.width + m) || (a.y < -m && b.y < -m) ||
                 (a.y > view.height + m && b.y > view.height + m));
    };
    std::vector<GizmoFill> fills;
    std::vector<GizmoStroke> strokes, chosenStrokes;
    constexpr size_t strokeLimit = 16000, fillLimit = 6000;
    auto eye = view.camera.position;
    auto holdsEye = [&](const DebugItem &item) {
        if (!item.volume)
            return false;
        auto d = eye - item.volume->centre;
        auto h = item.volume->half;
        return std::abs(dot(d, item.volume->axes[0])) <= h.x && std::abs(dot(d, item.volume->axes[1])) <= h.y &&
               std::abs(dot(d, item.volume->axes[2])) <= h.z;
    };
    for (int i = 0; i < int(items.size()); ++i) {
        bool chosen = i == selected, inside = holdsEye(items[i]); // its fill would tint the whole view
        for (auto &shape : items[i].shapes) {
            Rgba color = shape.color;
            if (chosen)
                color = {color.r + (1 - color.r) * .35f, color.g + (1 - color.g) * .35f, color.b + (1 - color.b) * .35f, 1};
            if (shape.fill > 0 && !inside)
                for (auto &f : shape.faces) {
                    if (fills.size() >= fillLimit)
                        break;
                    // Clip the polygon against the near plane, then project.
                    std::vector<Vec3> clipped;
                    for (size_t k = 0; k < f.size(); ++k) {
                        auto a = f[k], b = f[(k + 1) % f.size()];
                        float da = depthOf(view.camera, a), db = depthOf(view.camera, b);
                        if (da >= nearDepth)
                            clipped.push_back(a);
                        if ((da >= nearDepth) != (db >= nearDepth))
                            clipped.push_back(a + (b - a) * ((nearDepth - da) / (db - da)));
                    }
                    if (clipped.size() < 3)
                        continue;
                    GizmoFill fill{{}, {color.r, color.g, color.b, std::min(1.f, shape.fill * (chosen ? 2.2f : 1.f)) * color.a}, true};
                    for (auto p : clipped)
                        if (auto s = view.project(p))
                            fill.points.push_back(*s);
                    if (fill.points.size() >= 3)
                        fills.push_back(std::move(fill));
                }
            for (auto &[a, b] : shape.edges) {
                if (strokes.size() >= strokeLimit)
                    break;
                float da = depthOf(view.camera, a), db = depthOf(view.camera, b);
                if (da < nearDepth && db < nearDepth)
                    continue;
                Vec3 p = a, q = b;
                if (da < nearDepth)
                    p = a + (b - a) * ((nearDepth - da) / (db - da));
                if (db < nearDepth)
                    q = a + (b - a) * ((nearDepth - da) / (db - da));
                auto sp = view.project(p), sq = view.project(q);
                if (!sp || !sq || !onScreen(*sp, *sq))
                    continue;
                GizmoStroke s{*sp, *sq, color, width * (chosen ? 1.7f : 1.f), shape.dashed, true, shape.halo || chosen};
                (chosen ? chosenStrokes : strokes).push_back(s);
            }
        }
    }
    // Fills under every line; the selection's lines last, over the rest.
    unsigned sequence = 0;
    for (auto &f : fills) {
        f.sequence = sequence++;
        frame.fills.push_back(std::move(f));
    }
    strokes.insert(strokes.end(), chosenStrokes.begin(), chosenStrokes.end());
    for (auto &s : strokes) {
        s.sequence = sequence++;
        frame.strokes.push_back(s);
    }
    return frame;
}
} // namespace hyprune::interaction
