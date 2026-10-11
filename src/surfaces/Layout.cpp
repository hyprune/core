#include "Layout.hpp"
#include "ipc/Server.hpp"
#include "interaction/Hands.hpp"
#include "ipc/Text.hpp"
#include "world/Mounts.hpp"
#include <set>
namespace hyprune::surfaces {
namespace {
void need(bool b, int code, const char *why) {
    if (!b)
        throw ipc::Error(code, why);
}
Vec3 vec(const Json &p) {
    return {p[0], p[1], p[2]};
}
// Triangle/OBB separating-axis test: no corner-only tunnelling through thin geometry.
bool triangleBox(const Triangle &t, const Surface &s, float depth, float xyMargin = .04f) {
    auto local = [&](Vec3 p) {
        p = p - s.center;
        return Vec3{dot(p, s.right()), dot(p, s.up()), dot(p, s.normal())};
    };
    Vec3 v[3] = {local(t.a), local(t.b), local(t.c)}, edges[3] = {v[1] - v[0], v[2] - v[1], v[0] - v[2]};
    Vec3 half{s.width * .5f + xyMargin, s.height * .5f + xyMargin, depth};
    auto separates = [&](Vec3 axis) {
        float a = dot(axis, v[0]), b = dot(axis, v[1]), c = dot(axis, v[2]);
        float r = std::abs(axis.x) * half.x + std::abs(axis.y) * half.y + std::abs(axis.z) * half.z;
        return std::min({a, b, c}) > r || std::max({a, b, c}) < -r;
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
float ray(Vec3 o, Vec3 d, const Triangle &t) {
    auto e = t.b - t.a, f = t.c - t.a, p = cross(d, f);
    float det = dot(e, p);
    if (std::abs(det) < 1e-7f)
        return INFINITY;
    auto q = o - t.a;
    float u = dot(q, p) / det;
    if (u < 0 || u > 1)
        return INFINITY;
    auto r = cross(q, e);
    float v = dot(d, r) / det;
    if (v < 0 || u + v > 1)
        return INFINITY;
    float distance = dot(f, r) / det;
    return distance > 0 ? distance : INFINITY;
}
// A zone's resting spot for a window: its horizontal centre, at standing eye height
// above the zone's floor, within its bounds. Never the middle of a tall zone's air.
Vec3 zoneRest(const world::World &w, Vec3 lo, Vec3 hi, float margin, float height) {
    Vec3 c = (lo + hi) * .5f;
    auto floor = interaction::raycast({c.x, hi.y, c.z}, {0, -1, 0}, w.collision, &w.collisionIndex);
    float base = floor && floor->point.y >= lo.y - 1 ? floor->point.y : lo.y;
    float low = lo.y + margin + height * .5f, high = std::max(low, hi.y - margin - height * .5f);
    c.y = std::clamp(base + 1.7f, low, high);
    return c;
}
} // namespace
bool overlaps(const Surface &s, std::span<const Triangle> triangles, float margin) {
    for (const auto &t : triangles)
        if (triangleBox(t, s, margin, margin))
            return true;
    return false;
}
bool pushOut(Surface &s, std::span<const Triangle> triangles) {
    // Sweep the full rectangle along its authored approach. A re-exported anchor
    // behind a wall is invalid even when its final plane does not intersect it.
    auto start = s.center;
    auto approach = s;
    approach.center += s.normal() * 2.f;
    float shift = 0;
    for (const auto &t : triangles)
        if (triangleBox(t, approach, 2.f)) {
            float front =
                std::max({dot(t.a - start, s.normal()), dot(t.b - start, s.normal()), dot(t.c - start, s.normal())});
            if (front >= -.04f && front <= 4.f)
                shift = std::max(shift, front + .05f);
        }
    for (int i = 0; i <= 40; i++) {
        s.center = start + s.normal() * (shift + i * .05f);
        if (!overlaps(s, triangles))
            return true;
    }
    s.center = start;
    return false;
}
Surface carryPlane(const sim::Camera &camera, float width, float height, std::span<const Triangle> triangles,
                   int index) {
    Surface s;
    s.width = width;
    s.height = height;
    s.yaw = -camera.yaw;
    auto forward = camera.forward();
    auto right = normalized(cross(forward, {0, 1, 0}));
    float distance = 2.4f;
    // Sweep the complete plane's support rays from the eye towards its destination.
    // If a wall is too close to hold a full-size tool, shrink it until a safe volume exists.
    for (int attempt = 0; attempt < 14; attempt++) {
        s.center = camera.position + forward * distance +
                   right * ((index % 3 == 1   ? 1
                             : index % 3 == 2 ? -1
                                              : 0) *
                            s.width * 1.05f) +
                   Vec3{0, (index / 3) * s.height * 1.05f, 0};
        if (index == 0)
            s.center = camera.position + forward * distance;
        float limit = distance;
        for (float x : {-.5f, 0.f, .5f})
            for (float y : {-.5f, 0.f, .5f}) {
                Vec3 end = s.center + s.right() * (x * s.width) + s.up() * (y * s.height);
                auto direction = normalized(end - camera.position);
                float len = length(end - camera.position);
                for (auto &t : triangles) {
                    float hit = ray(camera.position, direction, t);
                    if (hit < len + .08f)
                        limit = std::min(limit, std::max(.08f, distance * (hit - .08f) / len));
                }
            }
        s.center = camera.position + (s.center - camera.position) * (limit / distance);
        auto end = s.center;
        auto swept = [&](float fraction) {
            auto displacement = (end - camera.position) * fraction;
            auto volume = s;
            volume.center = camera.position + displacement * .5f;
            volume.width += std::abs(dot(displacement, s.right()));
            volume.height += std::abs(dot(displacement, s.up()));
            float depth = .04f + std::abs(dot(displacement, s.normal())) * .5f;
            return std::any_of(triangles.begin(), triangles.end(),
                               [&](auto &t) { return triangleBox(t, volume, depth); });
        };
        // A full swept OBB also catches slivers between the support rays.
        if (!swept(0)) {
            float lo = 0, hi = 1;
            if (swept(1)) {
                for (int step = 0; step < 14; step++) {
                    float mid = (lo + hi) * .5f;
                    if (swept(mid))
                        hi = mid;
                    else
                        lo = mid;
                }
                s.center = camera.position + (end - camera.position) * lo;
            }
            if (!overlaps(s, triangles))
                return s;
        }
        s.width *= .7f;
        s.height *= .7f;
        distance *= .8f;
    }
    throw ipc::Error(-32005, "no collision-free space for carried window; move to an open area");
}
void anchorPlane(Surface &s, const Json &a, int pw, int ph) {
    s.center = vec(a.at("pose").at("position"));
    s.rotation = a.at("pose").at("rotation").get<std::array<float, 4>>();
    s.yaw = 0;
    float ratio = float(pw) / std::max(1, ph), w = a.at("size")[0], h = a.at("size")[1];
    if (a.at("mount").contains("limits")) {
        auto l = a["mount"]["limits"];
        need(ratio >= l["minAspect"].get<float>() && ratio <= l["maxAspect"].get<float>(), -32007,
             "tool aspect outside mount limits");
        w = std::min(w, l["maxSize"][0].get<float>());
        h = std::min(h, l["maxSize"][1].get<float>());
        s.width = std::min(w, h * ratio);
        s.height = s.width / ratio;
        need(s.width >= l["minSize"][0].get<float>() && s.height >= l["minSize"][1].get<float>(), -32007,
             "tool too small for mount");
    } else {
        s.width = std::min(w, h * ratio);
        s.height = s.width / ratio;
    }
}
Json Layout::rule(const Instance &i) const {
    return {{"appClass", ipc::boundedText(i.appClass, ipc::RuleLimit)},
            {"title", ipc::boundedText(i.title, ipc::RuleLimit)}};
}
bool Layout::matches(const Json &p, const Instance &i) const {
    // Exact class/title and the explicit empty-title class rule; no arbitrary regex execution.
    // Rules hold wire-bounded identity; compare the same bounded form.
    auto r = rule(i);
    return p.at("rule").at("appClass") == r["appClass"] &&
           (p.at("rule").at("title") == "" || p.at("rule").at("title") == r["title"]);
}
const Json *Layout::binding(int id, const world::World *w) const {
    for (const auto *list : {&bindings, &defaults})
        for (const auto &b : *list)
            if (b.at("id") == id && (!w || b.at("worldId") == w->id))
                return &b;
    return nullptr;
}
void Layout::deriveDefaults(const world::World *w) {
    // Explicit homes win by area and by workspace ID (IDs are global across worlds).
    defaults = Json::array();
    if (!w || workspaceBase <= 0)
        return;
    std::set<int> taken = reservedWorkspaces;
    for (const auto &b : bindings)
        taken.insert(b.at("id").get<int>());
    for (const auto &a : w->areas) {
        if (bindings.size() + defaults.size() >= 32)
            break;
        if (std::any_of(bindings.begin(), bindings.end(),
                        [&](auto &b) { return b.at("worldId") == w->id && b.at("areaId") == a.at("id"); }))
            continue;
        auto slot = a.value("workspaceSlot", "");
        if (slot.empty() || slot.find_first_not_of("0123456789") != std::string::npos || slot.size() > 4)
            continue;
        int id = workspaceBase + std::stoi(slot);
        if (!taken.insert(id).second)
            continue;
        defaults.push_back({{"id", id}, {"worldId", w->id}, {"areaId", a.at("id")}});
    }
}
Json Layout::effectiveBindings() const {
    auto all = bindings;
    all.insert(all.end(), defaults.begin(), defaults.end());
    return all;
}
const Json *Layout::anchor(const std::string &id, const world::World *w) const {
    if (w)
        for (const auto &a : w->anchors)
            if (a.at("id") == id)
                return &a;
    return nullptr;
}
const Json *Layout::saved(const Instance &i, const world::World *w) const {
    auto b = binding(i.workspaceId, w);
    if (!w || (!b && i.workspaceId != roomWorkspace))
        return nullptr;
    for (const auto &p : placements)
        if (p.at("worldId") == w->id && (!b || p.at("areaId") == b->at("areaId")) && matches(p, i))
            return &p;
    return nullptr;
}
void Layout::bind(int id, const std::string &worldId, const std::string &areaId, const world::World *w) {
    need(id > 0 && w && worldId == w->id, -32003, "binding requires loaded world");
    need(std::any_of(w->areas.begin(), w->areas.end(), [&](auto &a) { return a.at("id") == areaId; }), -32003,
         "unknown area");
    for (auto &b : bindings)
        need(b.at("id") == id || b.at("worldId") != worldId || b.at("areaId") != areaId, -32004, "area already bound");
    std::erase_if(bindings.get_ref<Json::array_t &>(), [&](auto &b) { return b.at("id") == id; });
    need(bindings.size() < 32, -32008, "binding budget");
    bindings.push_back({{"id", id}, {"worldId", worldId}, {"areaId", areaId}});
    deriveDefaults(w);
}
void Layout::restore(const Json &data) {
    // Private daemon data; bounded and still validated before any handle/pose is resolved.
    auto b = data.value("bindings", Json::array()), p = data.value("placements", Json::array());
    need(b.is_array() && b.size() <= 32 && p.is_array() && p.size() <= 128 && data.dump().size() <= 65536, -32008,
         "layout budget");
    std::set<int> ids;
    std::set<std::string> homes;
    for (auto &v : b)
        need(v.size() == 3 && v.at("id").is_number_integer() && v["id"].get<int>() > 0 && v.at("worldId").is_string() &&
                 v.at("areaId").is_string() && ids.insert(v["id"].get<int>()).second &&
                 homes.insert(v["worldId"].get<std::string>() + ":" + v["areaId"].get<std::string>()).second,
             -32602, "invalid saved binding");
    for (auto &v : p)
        need((v.size() == 4 || v.size() == 6 || (v.contains("content") && (v.size() == 5 || v.size() == 7))) &&
                 v.at("worldId").is_string() && v.at("areaId").is_string() && v.at("anchorId").is_string() &&
                 v.at("rule").size() == 2 && v["rule"]["appClass"].is_string() &&
                 !v["rule"]["appClass"].get<std::string>().empty() && v["rule"]["title"].is_string(),
             -32602, "invalid saved placement");
    for (auto &v : p)
        if (v.contains("pose") || v.contains("size")) {
            need(v.contains("pose") && v.contains("size") && v["anchorId"] == "" && v["pose"].is_object() &&
                     v["pose"].size() == 2,
                 -32602, "invalid user mount");
            auto pos = v["pose"].at("position"), rot = v["pose"].at("rotation"), size = v["size"];
            need(pos.is_array() && pos.size() == 3 && rot.is_array() && rot.size() == 4 && size.is_array() &&
                     size.size() == 2,
                 -32602, "invalid mount dimensions");
            for (auto values : {pos, rot, size})
                for (auto &n : values)
                    need(n.is_number() && std::isfinite(n.get<double>()), -32602, "nonfinite user mount");
            double norm = 0;
            for (auto &n : rot)
                norm += n.get<double>() * n.get<double>();
            need(std::abs(norm - 1) < .002 && size[0].get<float>() > .01f && size[1].get<float>() > .01f &&
                     size[0].get<float>() <= 12 && size[1].get<float>() <= 8,
                 -32602, "invalid user mount pose/size");
        }
    for (auto &v : p)
        if (v.contains("content")) {
            auto &c = v["content"];
            need(c.is_object() && c.size() == 3, -32602, "invalid content sizing");
            for (auto key : {"logicalSize", "baseSize", "worldSize"}) {
                need(c.contains(key) && c[key].is_array() && c[key].size() == 2, -32602, "invalid content dimensions");
                for (auto &n : c[key])
                    need(n.is_number() && std::isfinite(n.get<double>()) && n > 0 && n <= 32768, -32602,
                         "invalid content extent");
            }
        }
    bindings = b;
    placements = p;
    temporaryAnchors.clear();
}
void Layout::pull(const Instance &i, const world::World *w) {
    need(binding(i.workspaceId, w), -32003, "window has no area home");
    need(carried.empty() || carried == i.id, -32004, "already carrying a tool");
    freePoses.erase(i.id);
    carried = i.id;
    recalled.erase(i.id);
    temporaryAnchors.erase(i.id);
}
bool Layout::pulled(const Instance &i, const world::World *w) const {
    if (unplaced.contains(i.id) || freePoses.contains(i.id) || carried == i.id || recalled.contains(i.id) ||
        saved(i, w))
        return true;
    if (!w)
        if (auto b = binding(i.workspaceId, nullptr))
            return std::any_of(placements.begin(), placements.end(), [&](auto &p) {
                return p.at("worldId") == b->at("worldId") && p.at("areaId") == b->at("areaId") && matches(p, i);
            });
    return false;
}
std::string Layout::place(const Instance &i, const std::string &requested, const world::World *w) {
    freePoses.erase(i.id);
    occupiedPoses.erase(i.id);
    auto b = binding(i.workspaceId, w);
    need(b, -32003, "window has no area home");
    const Json *chosen = nullptr;
    for (const auto &a : w->anchors) {
        if (a.at("areaId") != b->at("areaId") || a.at("mount").at("role") != "tool-mount" ||
            (!requested.empty() && a.at("id") != requested))
            continue;
        bool occupied = false;
        for (auto &live : instances)
            if (a.at("id") == live.value("anchorId", "") && live.at("surfaceId") != i.id)
                occupied = true;
        if (occupied)
            continue;
        chosen = &a;
        break;
    }
    need(chosen, -32004, "no free tool mount in owning area");
    std::string id = chosen->at("id");
    std::erase_if(placements.get_ref<Json::array_t &>(), [&](auto &p) {
        return p.at("worldId") == w->id && p.at("areaId") == b->at("areaId") && matches(p, i);
    });
    need(placements.size() < 128, -32008, "placement budget");
    placements.push_back({{"worldId", w->id}, {"areaId", b->at("areaId")}, {"anchorId", id}, {"rule", rule(i)}});
    carried.clear();
    recalled.erase(i.id);
    temporaryAnchors.erase(i.id);
    return id;
}
void Layout::release(const Instance &i, const world::World *w) {
    freePoses.erase(i.id);
    occupiedPoses.erase(i.id);
    auto b = binding(i.workspaceId, w);
    unplaced.erase(i.id);
    content.erase(i.id);
    std::erase(pocketOrder, i.id);
    if (b)
        std::erase_if(placements.get_ref<Json::array_t &>(), [&](auto &p) {
            return p.at("worldId") == w->id && p.at("areaId") == b->at("areaId") && matches(p, i);
        });
    if (carried == i.id)
        carried.clear();
    recalled.erase(i.id);
    temporaryAnchors.erase(i.id);
}
void Layout::recall(std::span<const Instance> live) {
    freePoses.clear();
    occupiedPoses.clear();
    unplaced.clear();
    int n = 0;
    for (auto &i : live)
        recalled[i.id] = n++;
    carried.clear();
}
void Layout::stash(const Instance &i, Surface s) {
    // Durable placement remains remembered, but pocket state suppresses drawing.
    (void)s;
    unplaced.insert(i.id);
    std::erase(pocketOrder, i.id);
    pocketOrder.insert(pocketOrder.begin(), i.id);
    freePoses.erase(i.id);
    occupiedPoses.erase(i.id);
    if (carried == i.id)
        carried.clear();
}
bool Layout::vacant(const Surface &candidate, const std::string &except) const {
    for (const auto *poses : {&freePoses, &occupiedPoses})
        for (const auto &[id, placed] : *poses) {
            if (poses == &occupiedPoses && freePoses.contains(id))
                continue;
            if (id == except || unplaced.contains(id))
                continue;
            auto d = placed.center - candidate.center;
            // Conservative OBB projection, with enough depth clearance for borders.
            bool separated = false;
            for (auto axis : {candidate.right(), candidate.up(), candidate.normal(), placed.right(), placed.up(),
                              placed.normal()}) {
                auto extent = [&](const Surface &s) {
                    return std::abs(dot(s.right(), axis)) * s.width * .5f +
                           std::abs(dot(s.up(), axis)) * s.height * .5f + std::abs(dot(s.normal(), axis)) * .15f;
                };
                if (std::abs(dot(d, axis)) > extent(candidate) + extent(placed) + .08f) {
                    separated = true;
                    break;
                }
            }
            if (!separated)
                return false;
        }
    return true;
}
const Json *Layout::contentFor(const Instance &i, const world::World *w) const {
    if (auto it = content.find(i.id); it != content.end())
        return &it->second;
    if (auto p = saved(i, w); p && p->contains("content"))
        return &p->at("content");
    return nullptr;
}
void Layout::setContent(const Instance &i, const world::World *w, const Json &value) {
    content[i.id] = value;
    for (auto &p : placements)
        if (&p == saved(i, w))
            p["content"] = value;
}
bool reachable(const Surface &s, const world::World &w) {
    if (w.collision.empty())
        return true; // a world without collision has no floor to stand on: fly worlds
    // Floor straight below, close enough under the plane's lower edge to stand and use it.
    auto hit = interaction::raycast(s.center, {0, -1, 0}, w.collision, &w.collisionIndex);
    if (!hit || hit->normal.y < .5f)
        return false;
    float lowest = s.center.y - (std::abs(s.up().y) * s.height + std::abs(s.right().y) * s.width) * .5f;
    return lowest - hit->point.y <= MaxSpawnLift && w.playable.contains(hit->point + Vec3{0, 1.7f, 0});
}
bool Layout::spawn(const Instance &i, const world::World *w, const sim::Camera &camera, Surface s, float aspect) {
    auto desired = s;
    if (!w || hasPlacement(i, w))
        return true;
    const Json *home = nullptr;
    if (auto b = binding(i.workspaceId, w))
        for (auto &a : w->areas)
            if (a.at("id") == b->at("areaId"))
                home = &a;
    if (!home) {
        float nearest = INFINITY;
        for (auto &a : w->areas) {
            float distance = length(world::areaSpawn(*w, a.at("id")).position - camera.position);
            if (distance < nearest) {
                nearest = distance;
                home = &a;
            }
        }
    }
    if (home)
        spawnAreas[i.id] = home->at("id");
    auto forward = camera.forward();
    auto right = normalized(cross(forward, {0, 1, 0}));
    auto up = normalized(cross(right, forward));
    float tangent = std::tan(camera.fov * float(M_PI) / 360.f);
    auto inView = [&](const Surface &c) {
        auto delta = c.center - camera.position;
        float depth = dot(delta, forward);
        return depth > .05f && std::abs(dot(delta, right)) <= depth * tangent * aspect &&
               std::abs(dot(delta, up)) <= depth * tangent;
    };
    auto accept = [&](const Surface &c) {
        freePoses[i.id] = c;
        unplaced.erase(i.id);
        return true;
    };
    // In front of the player first: a bounded fan in the camera plane. Never shrink a
    // client into a postage stamp, stack it over another window or leave it over a drop.
    for (int fan = 0; fan < 9; ++fan) {
        s = desired;
        int column = fan == 0 ? 0 : ((fan + 1) / 2) * (fan % 2 ? 1 : -1);
        s.center += s.right() * (column * (s.width + .15f));
        if (inView(s) && !overlaps(s, w->collision) && vacant(s, i.id) && reachable(s, *w))
            return accept(s);
    }
    // Then the home's reserved free zone: the part nearest the player, at the player's
    // viewing height above the zone's floor. Fit the full oriented rectangle, including
    // clearance; never accept OBB recovery that moves it outside the zone.
    if (home)
        for (auto &id : home->value("freePlacementZones", Json::array()))
            for (auto &zone : w->placementZones) {
                if (zone.at("id") != id)
                    continue;
                auto lo = vec(zone.at("bounds").at("min")), hi = vec(zone.at("bounds").at("max"));
                float margin = zone.at("clearance");
                for (int fan = 0; fan < 7; ++fan) {
                    auto candidate = desired;
                    int column = fan == 0 ? 0 : ((fan + 1) / 2) * (fan % 2 ? 1 : -1);
                    candidate.center += candidate.right() * (column * (desired.width + .15f));
                    auto r = candidate.right(), u = candidate.up(), n = candidate.normal();
                    Vec3 half;
                    half.x = std::abs(r.x) * candidate.width * .5f + std::abs(u.x) * candidate.height * .5f +
                             std::abs(n.x) * .04f + margin;
                    half.y = std::abs(r.y) * candidate.width * .5f + std::abs(u.y) * candidate.height * .5f +
                             std::abs(n.y) * .04f + margin;
                    half.z = std::abs(r.z) * candidate.width * .5f + std::abs(u.z) * candidate.height * .5f +
                             std::abs(n.z) * .04f + margin;
                    if (hi.x - lo.x < 2 * half.x || hi.y - lo.y < 2 * half.y || hi.z - lo.z < 2 * half.z)
                        continue;
                    candidate.center.x = std::clamp(candidate.center.x, lo.x + half.x, hi.x - half.x);
                    candidate.center.z = std::clamp(candidate.center.z, lo.z + half.z, hi.z - half.z);
                    auto floor = interaction::raycast({candidate.center.x, hi.y, candidate.center.z}, {0, -1, 0},
                                                      w->collision, &w->collisionIndex);
                    float base = floor ? floor->point.y : lo.y;
                    float lift = desired.center.y - (camera.position.y - camera.eyeHeight);
                    candidate.center.y = std::clamp(base + lift, lo.y + half.y, hi.y - half.y);
                    if (!vacant(candidate, i.id) || !pushOut(candidate, w->collision))
                        continue;
                    if (candidate.center.x < lo.x + half.x || candidate.center.x > hi.x - half.x ||
                        candidate.center.y < lo.y + half.y || candidate.center.y > hi.y - half.y ||
                        candidate.center.z < lo.z + half.z || candidate.center.z > hi.z - half.z)
                        continue;
                    if (inView(candidate) && vacant(candidate, i.id) && reachable(candidate, *w))
                        return accept(candidate);
                }
            }
    freePoses.erase(i.id);
    unplaced.insert(i.id);
    return false;
}
std::optional<Surface> Layout::locate(const Instance &i, const world::World *w, const sim::Camera &camera, int pw,
                                      int ph) {
    if (unplaced.contains(i.id))
        return {};
    auto b = binding(i.workspaceId, w);
    if (!b && recalled.contains(i.id))
        b = binding(i.workspaceId, nullptr);
    // Recalled room clients have no home binding: they come to the player like the rest.
    if (!b && !freePoses.contains(i.id) && !saved(i, w) && !recalled.contains(i.id))
        return {};
    Surface s;
    s.id = i.id;
    s.title = i.title;
    s.pixelWidth = pw;
    s.pixelHeight = ph;
    auto p = saved(i, w);
    std::string anchorId;
    if (freePoses.contains(i.id)) {
        s = freePoses.at(i.id);
        auto old = s;
        if (auto c = contentFor(i, w)) {
            s.width = (*c)["worldSize"][0];
            s.height = (*c)["worldSize"][1];
        } else
            s.logicalSize(pw, ph, windowScale);
        if (auto f = fittedScale.find(i.id); f != fittedScale.end() && !contentFor(i, w) &&
            std::get<0>(f->second) == pw && std::get<1>(f->second) == ph) {
            s.width *= std::get<2>(f->second); // the fit found earlier for this client size
            s.height *= std::get<2>(f->second);
        } else if ((s.width != old.width || s.height != old.height) && !pushOut(s, w->collision)) {
            // Never publish a resized quad through geometry, and never lose the window either:
            // shrink it in place (same aspect, same content) until it fits, else bring it in
            // front of the player. Hiding it made a resize look like the window vanished.
            bool fitted = false;
            for (int k = 1; k <= 12 && !fitted; ++k) {
                auto smaller = s;
                smaller.width = s.width * std::pow(.85f, float(k));
                smaller.height = s.height * std::pow(.85f, float(k));
                if ((fitted = pushOut(smaller, w->collision))) {
                    s = smaller;
                    fittedScale[i.id] = {pw, ph, std::pow(.85f, float(k))};
                }
            }
            if (fitted)
                if (auto c = contentFor(i, w)) { // keep the fitted size: no refit every frame
                    auto sizing = *c;
                    sizing["worldSize"] = {s.width, s.height};
                    setContent(i, w, sizing);
                }
            if (!fitted) {
                auto front = carryPlane(camera, s.width, s.height, w->collision);
                front.id = s.id;
                front.title = s.title;
                front.pixelWidth = s.pixelWidth;
                front.pixelHeight = s.pixelHeight;
                s = front;
            }
        }
        freePoses[i.id] = s;
    } else if (carried == i.id || recalled.contains(i.id))
        s = carryPlane(camera, pw * windowScale, ph * windowScale, w->collision,
                       recalled.contains(i.id) ? recalled[i.id] : 0);
    else if (p && p->contains("pose")) {
        s.center = vec(p->at("pose").at("position"));
        s.rotation = p->at("pose").at("rotation").get<std::array<float, 4>>();
        if (auto c = contentFor(i, w)) {
            s.width = (*c)["worldSize"][0];
            s.height = (*c)["worldSize"][1];
        } else
            s.logicalSize(pw, ph, windowScale);
        auto found = recovered.find(i.id);
        if (found != recovered.end() && found->second.world == w && found->second.pose == p->at("pose") &&
            found->second.width == s.width && found->second.height == s.height) {
            s.center = found->second.surface.center;
            s.rotation = found->second.surface.rotation;
        } else if (pushOut(s, w->collision)) {
            if (recovered.size() > 1024)
                recovered.clear();
            recovered[i.id] = {w, p->at("pose"), s.width, s.height, s};
        } else {
            recalled[i.id] = 0;
            s = carryPlane(camera, pw * windowScale, ph * windowScale, w->collision);
        }
    } else if (p) {
        // Room clients have no binding: their tool-mount placement names its own area.
        const Json home = b ? b->at("areaId") : p->at("areaId");
        auto fit = [&](const Json &mount) {
            auto fitted = fitToMount(mount, pw, ph, w->collision);
            if (fitted)
                s = *fitted;
            return fitted.has_value();
        };
        anchorId = temporaryAnchors.contains(i.id) ? temporaryAnchors[i.id] : p->at("anchorId").get<std::string>();
        auto a = anchor(anchorId, w);
        bool valid = false;
        auto area =
            std::find_if(w->areas.begin(), w->areas.end(), [&](auto &a) { return a.at("id") == home; });
        if (area != w->areas.end())
            for (auto &id : area->value("freePlacementZones", Json::array()))
                for (auto &zone : w->placementZones)
                    if (id == anchorId && zone.at("id") == id) {
                        auto lo = vec(zone["bounds"]["min"]), hi = vec(zone["bounds"]["max"]);
                        float margin = zone.at("clearance");
                        s.width = std::min(1.6f, hi.x - lo.x - 2 * margin);
                        s.height = s.width * ph / pw;
                        s.center = zoneRest(*w, lo, hi, margin, s.height);
                        valid =
                            s.width > 0 && s.height < hi.y - lo.y - 2 * margin && !overlaps(s, w->collision, margin);
                    }
        bool occupied = std::any_of(instances.begin(), instances.end(), [&](auto &v) {
            return v.value("anchorId", "") == anchorId && v.at("surfaceId") != i.id;
        });
        if (occupied)
            valid = false;
        if (a && !occupied && a->at("areaId") == home && a->at("mount").at("role") == "tool-mount")
            valid = fit(*a);
        if (!valid) {
            const Json *free = nullptr;
            for (const auto &candidate : w->anchors) {
                if (candidate.at("areaId") != home || candidate.at("mount").at("role") != "tool-mount")
                    continue;
                if (std::any_of(instances.begin(), instances.end(), [&](auto &v) {
                        return candidate.at("id") == v.value("anchorId", "") && v.at("surfaceId") != i.id;
                    }))
                    continue;
                if (fit(candidate)) {
                    free = &candidate;
                    break;
                }
            }
            if (free) {
                auto old = anchorId;
                anchorId = free->at("id");
                if (occupied)
                    temporaryAnchors[i.id] = anchorId;
                else
                    for (auto &entry : placements)
                        if (&entry == p)
                            entry["anchorId"] = anchorId;
                events.push_back({"runtime.notice",
                                  {{"level", "warning"},
                                   {"code", "placement.relocated"},
                                   {"message", "Relocated " + i.title + " in " + home.get<std::string>() +
                                                   " from " + old}}});
            } else {
                bool zoned = false;
                auto area = std::find_if(w->areas.begin(), w->areas.end(),
                                         [&](auto &a) { return a.at("id") == home; });
                if (area != w->areas.end())
                    for (auto &id : area->value("freePlacementZones", Json::array()))
                        for (auto &zone : w->placementZones)
                            if (zone.at("id") == id && !std::any_of(instances.begin(), instances.end(), [&](auto &v) {
                                    return v.value("anchorId", "") == id.get<std::string>() &&
                                           v.at("surfaceId") != i.id;
                                })) {
                                auto lo = vec(zone["bounds"]["min"]), hi = vec(zone["bounds"]["max"]);
                                float margin = zone.at("clearance");
                                s.rotation = {0, 0, 0, 1};
                                s.yaw = 0;
                                s.width = std::min(1.6f, hi.x - lo.x - 2 * margin);
                                s.height = s.width * ph / pw;
                                s.center = zoneRest(*w, lo, hi, margin, s.height);
                                if (s.width > 0 && s.height < hi.y - lo.y - 2 * margin &&
                                    !overlaps(s, w->collision, margin)) {
                                    zoned = true;
                                    anchorId = zone.at("id");
                                    break;
                                }
                            }
                if (zoned && occupied)
                    temporaryAnchors[i.id] = anchorId;
                else if (zoned) {
                    for (auto &entry : placements)
                        if (&entry == p)
                            entry["anchorId"] = anchorId;
                }
                if (!zoned) {
                    recalled[i.id] = 0;
                    s = carryPlane(camera, pw * windowScale, ph * windowScale, w->collision);
                }
                if (!zoned)
                    anchorId.clear();
                events.push_back(
                    {"runtime.notice",
                     {{"level", "warning"},
                      {"code", zoned ? "placement.relocated" : "placement.recalled"},
                      {"message", zoned ? "Relocated " + i.title + " into its area free zone"
                                        : "No safe free mount; recalled " + i.title + " in front of player"}}});
            }
        }
    } else
        return {};
    s.id = i.id;
    s.title = i.title;
    s.pixelWidth = pw;
    s.pixelHeight = ph;
    occupiedPoses[i.id] = s;
    instances.push_back({{"surfaceId", i.id},
                         {"workspaceId", i.workspaceId},
                         {"areaId", b                    ? b->at("areaId")
                                    : p && !anchorId.empty() ? p->at("areaId")
                                    : Json(spawnAreas.contains(i.id) ? spawnAreas.at(i.id) : "")},
                         {"anchorId", anchorId},
                         {"status", carried == i.id           ? "carried"
                                    : recalled.contains(i.id) ? "recalled"
                                                              : "placed"}});
    if (instances.back()["areaId"] == "") instances.back().erase("areaId");
    return s;
}
std::optional<Surface> fitToMount(const Json &mount, int pw, int ph, std::span<const Triangle> collision) {
    Surface s;
    try {
        anchorPlane(s, mount, pw, ph); // aspect-fit and centred in the authored frame
    } catch (const ipc::Error &) {
        return {}; // aspect outside the mount's limits
    }
    // Flush with the face, just past the collision margin, same orientation. A frame
    // bezel may not intersect: shrink a little rather than float off the surface.
    auto base = s;
    for (float scale : {1.f, .96f, .92f, .88f}) {
        s.width = base.width * scale;
        s.height = base.height * scale;
        s.center = base.center + base.normal() * .05f;
        if (!overlaps(s, collision))
            return s;
    }
    // Frames resting on furniture may need the normal collision recovery, but only a
    // short way: a far push (a bezel lip ahead) leaves the client floating off the mount.
    s = base;
    if (pushOut(s, collision) && length(s.center - base.center) <= .3f)
        return s;
    return {};
}
void Layout::placeOnMount(const Instance &i, const Json &mount, const world::World *w) {
    need(w && mount.at("mount").at("role") == "tool-mount", -32003, "not a tool mount");
    auto b = binding(i.workspaceId, w);
    need(!b || b->at("areaId") == mount.at("areaId"), -32004, "tool mount belongs to another area");
    for (auto &live : instances)
        need(live.value("anchorId", "") != mount.at("id").get<std::string>() || live.at("surfaceId") == i.id, -32004,
             "tool mount occupied");
    // One placement per client and world (room clients) or per home area (homed clients).
    std::erase_if(placements.get_ref<Json::array_t &>(), [&](auto &p) {
        return p.at("worldId") == w->id && (!b || p.at("areaId") == b->at("areaId")) && matches(p, i);
    });
    need(placements.size() < 128, -32008, "placement budget");
    placements.push_back({{"worldId", w->id}, {"areaId", mount.at("areaId")}, {"anchorId", mount.at("id")}, {"rule", rule(i)}});
    if (content.contains(i.id))
        placements.back()["content"] = content.at(i.id);
    freePoses.erase(i.id);
    occupiedPoses.erase(i.id);
    unplaced.erase(i.id);
    spawnAreas[i.id] = mount.at("areaId");
    if (carried == i.id)
        carried.clear();
    recalled.erase(i.id);
    temporaryAnchors.erase(i.id);
}
void Layout::mount(const Instance &i, const world::World *w, Surface s) {
    s.rotation = quaternionProduct(s.rotation, {0, std::sin(s.yaw / 2), 0, std::cos(s.yaw / 2)});
    s.yaw = 0;
    need(w && !overlaps(s, w->collision), -32005, "mount intersects geometry");
    auto b = binding(i.workspaceId, w);
    Json area = b ? b->at("areaId") : Json(spawnAreas.contains(i.id) ? spawnAreas.at(i.id) : "");
    if (area == "" && !w->areas.empty())
        area = w->areas.front().at("id");
    std::erase_if(placements.get_ref<Json::array_t &>(),
                  [&](auto &p) { return p.at("worldId") == w->id && p.at("areaId") == area && matches(p, i); });
    need(placements.size() < 128, -32008, "placement budget");
    placements.push_back({{"worldId", w->id},
                          {"areaId", area},
                          {"rule", rule(i)},
                          {"anchorId", ""},
                          {"pose", {{"position", {s.center.x, s.center.y, s.center.z}}, {"rotation", s.rotation}}},
                          {"size", {s.width, s.height}}});
    if (content.contains(i.id))
        placements.back()["content"] = content.at(i.id);
    unplaced.erase(i.id);
    freePoses[i.id] = s;
    spawnAreas[i.id] = area;
    recalled.erase(i.id);
    temporaryAnchors.erase(i.id);
    if (carried == i.id)
        carried.clear();
}
Json Layout::state(const world::World *w) const {
    auto areas = w ? w->areas : Json::array();
    // Authoring-only gameplay geometry is not part of the IPC area contract.
    for (auto &area : areas) area.erase("playable");
    return {{"areas", areas},
            {"anchors", w ? w->anchors : Json::array()},
            {"objects", w ? w->objects : Json::array()},
            {"bindings", effectiveBindings()},
            {"placements", placements},
            {"placementZones", w ? w->placementZones : Json::array()},
            {"instances", instances},
            {"carriedSurfaceId", carried.empty() ? Json(nullptr) : Json(carried)},
            {"desktopWorkspaceId", desktop}};
}
const Json *roomAnchor(const world::World &w, size_t index) {
    auto anchors = w.manifest.find("anchors");
    if (!w.areas.empty() || anchors == w.manifest.end() || !anchors->is_array() || index >= anchors->size())
        return nullptr;
    auto &a = (*anchors)[index];
    return a.value("screenType", "") == "com.hyprune.window" ? &a : nullptr;
}
void roomAnchorPose(Surface &s, const Json &a) {
    auto pos = a.at("pose").at("position"), q = a.at("pose").at("rotation");
    s.center = {pos[0], pos[1], pos[2]};
    s.yaw = std::atan2(2 * (q[3].get<float>() * q[1].get<float>() + q[0].get<float>() * q[2].get<float>()),
                       1 - 2 * (q[1].get<float>() * q[1].get<float>() + q[2].get<float>() * q[2].get<float>()));
    float ratio = float(std::max(1, s.pixelWidth)) / std::max(1, s.pixelHeight);
    s.width = std::min(a.at("size")[0].get<float>(), a.at("size")[1].get<float>() * ratio);
    s.height = s.width / ratio;
}
RoomAdoption adoptRoomClient(const world::World &w, size_t index, bool existing, bool pocketed) {
    if (pocketed)
        return RoomAdoption::Pocket;
    if (!existing)
        return RoomAdoption::Spawn;
    return roomAnchor(w, index) ? RoomAdoption::Anchor : RoomAdoption::Pocket;
}
} // namespace hyprune::surfaces
