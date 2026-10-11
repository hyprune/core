#include "Markers.hpp"
#include "interaction/Hands.hpp"
#include "ipc/Text.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
namespace hyprune::quests {
namespace {
Vec3 xyz(const Json &p) {
    return {p[0].get<float>(), p[1].get<float>(), p[2].get<float>()};
}
const Json *findId(const Json &list, const std::string &id) {
    if (list.is_array())
        for (auto &v : list)
            if (v.is_object() && v.value("id", "") == id)
                return &v;
    return nullptr;
}
const Json &zones(const world::World &w) {
    static const Json none = Json::array();
    return w.manifest.is_object() && w.manifest.contains("zones") ? w.manifest["zones"] : none;
}
bool inBox(const Json &lo, const Json &hi, Vec3 p) {
    return p.x >= lo[0].get<float>() && p.x <= hi[0].get<float>() && p.y >= lo[1].get<float>() &&
           p.y <= hi[1].get<float>() && p.z >= lo[2].get<float>() && p.z <= hi[2].get<float>();
}
std::string pretty(const std::string &id) {
    std::string out;
    bool start = true;
    for (char c : id) {
        if (c == '-' || c == '_') {
            out += ' ';
            start = true;
        } else {
            out += start ? char(std::toupper((unsigned char)c)) : c;
            start = false;
        }
    }
    return out;
}
std::string areaName(const world::World &w, const std::string &areaId) {
    if (auto *z = findId(zones(w), areaId))
        if (z->contains("name") && (*z)["name"].is_string() && !(*z)["name"].get<std::string>().empty())
            return (*z)["name"];
    return pretty(areaId);
}
std::optional<Vec3> areaHome(const world::World &w, const std::string &areaId) {
    auto *area = findId(w.areas, areaId);
    if (!area)
        return std::nullopt;
    if (auto *anchor = findId(w.anchors, area->value("primaryAnchor", ""))) {
        // The top centre of the area's primary mounted-workspace wall, plus 0.3 m.
        auto &pose = (*anchor)["pose"];
        Vec3 p = xyz(pose["position"]);
        std::array<float, 4> q{0, 0, 0, 1};
        if (pose.contains("rotation"))
            for (int i = 0; i < 4; ++i)
                q[i] = pose["rotation"][i].get<float>();
        float height = anchor->contains("size") ? (*anchor)["size"][1].get<float>() : 0.f;
        return p + rotate(q, {0, 1, 0}) * (height / 2) + Vec3{0, .3f, 0};
    }
    if (area->contains("spawnId"))
        for (auto &s : w.spawns)
            if (Json(s.id) == (*area)["spawnId"])
                return s.position + Vec3{0, 2.2f, 0};
    return std::nullopt;
}
std::string ascii(std::string s) {
    for (size_t at; (at = s.find("\xc2\xb7")) != std::string::npos;)
        s.replace(at, 2, "-"); // the overlay font is ASCII: a middle dot draws as a dash
    std::string out;
    for (unsigned char c : s)
        if (c >= 32 && c < 127)
            out += char(c);
        else if ((c & 0xc0) != 0x80)
            out += '?';
    return out;
}
std::string distanceText(float d) {
    char text[32];
    if (d >= 1000)
        std::snprintf(text, sizeof text, "%.1f km", d / 1000);
    else
        std::snprintf(text, sizeof text, "%d m", int(std::lround(d)));
    return text;
}
} // namespace
Json Marker::json() const {
    Json p = position ? Json{position->x, position->y, position->z} : Json(nullptr);
    return {{"id", id},
            {"kind", kind},
            {"tier", tier},
            {"label", ipc::boundedText(label, 48)},
            {"icon", icon},
            {"target", target},
            {"position", p},
            {"areaId", areaId.empty() ? Json(nullptr) : Json(areaId)},
            {"range", {rangeMin, rangeMax}},
            {"labelRange", labelRange},
            {"occlusion", occlusion},
            {"tracked", tracked},
            {"source", source}};
}
std::optional<Vec3> resolveTarget(const world::World &w, const Json &t, const Json &bindings) {
    if (!t.is_object())
        return std::nullopt;
    if (t.contains("position")) {
        if (t.contains("worldId") && t["worldId"] != w.id)
            return std::nullopt;
        return xyz(t["position"]);
    }
    if (t.contains("zoneId"))
        if (auto *z = findId(zones(w), t["zoneId"])) {
            auto &lo = (*z)["bounds"]["min"], &hi = (*z)["bounds"]["max"];
            return Vec3{(lo[0].get<float>() + hi[0].get<float>()) / 2, lo[1].get<float>() + 1.7f,
                        (lo[2].get<float>() + hi[2].get<float>()) / 2};
        }
    if (t.contains("placeId"))
        if (auto *p = findId(w.places, t["placeId"]))
            return xyz((*p)["position"]);
    if (t.contains("areaId"))
        return areaHome(w, t["areaId"]);
    if (t.contains("workspaceId") && bindings.is_array())
        for (auto &b : bindings)
            if (b.value("id", Json(-1)) == t["workspaceId"] && b.value("worldId", "") == w.id && b.contains("areaId") &&
                b["areaId"].is_string())
                return areaHome(w, b["areaId"]);
    return std::nullopt;
}
std::string areaAt(const world::World &w, Vec3 p) {
    for (auto &a : w.areas)
        if (a.contains("playable") && a["playable"].contains("volumes"))
            for (auto &v : a["playable"]["volumes"])
                if (v.value("type", "") == "box" && inBox(v["min"], v["max"], p))
                    return a.value("id", "");
    for (auto &z : zones(w))
        if (findId(w.areas, z.value("id", "")) && inBox(z["bounds"]["min"], z["bounds"]["max"], p))
            return z["id"];
    return "";
}
bool reaches(const world::World &w, const Json &t, std::optional<float> radius, Vec3 eye, const Json &bindings) {
    if (!t.is_object())
        return false;
    if (!radius) {
        if (t.contains("areaId"))
            return Json(areaAt(w, eye)) == t["areaId"];
        if (t.contains("zoneId")) {
            auto *z = findId(zones(w), t["zoneId"]);
            return z && inBox((*z)["bounds"]["min"], (*z)["bounds"]["max"], eye);
        }
        if (t.contains("workspaceId") && bindings.is_array()) // "do this at workspace 21": its home area
            for (auto &b : bindings)
                if (b.value("id", Json(-1)) == t["workspaceId"] && b.value("worldId", "") == w.id &&
                    b.contains("areaId") && b["areaId"].is_string())
                    return Json(areaAt(w, eye)) == b["areaId"];
    }
    auto p = resolveTarget(w, t, bindings);
    return p && length(*p - eye) <= radius.value_or(4.f);
}
std::vector<Marker> buildMarkers(const world::World *w, const Json &waypoint, const Json &route, const Json &bindings,
                                 const Json &requested) {
    std::vector<Marker> out;
    auto area = [&](std::optional<Vec3> p) { return w && p ? areaAt(*w, *p) : std::string(); };
    std::optional<Vec3> waypointAt;
    if (waypoint.is_object() && waypoint.contains("position") && waypoint["position"].is_array()) {
        Marker m;
        m.id = "waypoint:" + waypoint.value("id", "waypoint");
        m.kind = m.icon = "waypoint";
        m.tier = 1;
        m.label = waypoint.value("label", "Waypoint");
        waypointAt = m.position = xyz(waypoint["position"]);
        m.target = {{"position", waypoint["position"]}};
        m.source = {{"waypoint", true}};
        m.rangeMin = 2;
        m.rangeMax = 10000;
        m.areaId = area(m.position);
        out.push_back(m);
    }
    if (route.is_array() && !route.empty() && route.back().contains("position")) {
        auto end = xyz(route.back()["position"]);
        if (!waypointAt || length(end - *waypointAt) > 1) {
            Marker m;
            m.id = "route:destination";
            m.kind = m.icon = "route";
            m.tier = 1;
            m.label = route.back().value("label", "Destination");
            m.position = end;
            m.target = {{"position", route.back()["position"]}};
            m.source = {{"route", true}};
            m.rangeMax = 10000;
            m.areaId = area(m.position);
            out.push_back(m);
        }
    }
    if (requested.is_array())
        for (auto &r : requested) {
            Marker m;
            m.id = r.value("id", "");
            m.kind = r.value("kind", "quest");
            m.tier = std::clamp(r.value("tier", 2), 1, 4);
            m.label = r.value("label", "");
            m.icon = r.value("icon", m.kind);
            m.target = r.value("target", Json(nullptr));
            m.source = r.value("source", Json::object());
            m.tracked = r.value("tracked", false);
            m.rangeMin = m.kind == "break" ? 3 : 2;
            if (w)
                m.position = resolveTarget(*w, m.target, bindings);
            m.areaId = area(m.position);
            if (!m.id.empty() && m.target.is_object())
                out.push_back(m);
        }
    if (w) {
        std::set<std::string> homed;
        for (auto &b : bindings)
            if (b.value("worldId", "") == w->id && b.contains("areaId") && b["areaId"].is_string() &&
                b.contains("id") && b["id"].is_number_integer()) {
                std::string areaId = b["areaId"];
                int workspace = b["id"];
                Marker m;
                bool home = homed.insert(areaId).second;
                m.kind = m.icon = home ? "home" : "workspace";
                m.id = home ? "home:" + areaId : "workspace:" + std::to_string(workspace);
                m.tier = 3;
                m.label = areaName(*w, areaId) + " \u00b7 " + std::to_string(workspace);
                m.target = home ? Json{{"areaId", areaId}} : Json{{"workspaceId", workspace}};
                m.source = {{"areaId", areaId}, {"workspaceId", workspace}};
                m.position = areaHome(*w, areaId);
                m.areaId = areaId;
                m.rangeMin = 3;
                m.labelRange = 150;
                if (m.position)
                    out.push_back(m);
            }
        for (auto &p : w->places) {
            Marker m;
            m.kind = m.icon = "place";
            m.id = "place:" + p.value("id", "");
            m.tier = 4;
            m.label = p.value("name", "");
            m.target = {{"placeId", p.value("id", "")}};
            m.source = {{"placeId", p.value("id", "")}};
            m.position = xyz(p["position"]);
            m.areaId = area(m.position);
            m.rangeMin = 3;
            m.rangeMax = 400;
            m.labelRange = 80;
            m.occlusion = "visible";
            out.push_back(m);
        }
        for (auto &z : zones(*w)) {
            std::string id = z.value("id", "");
            if (homed.contains(id))
                continue; // the area's home marker already names it
            Marker m;
            m.kind = m.icon = "place";
            m.id = "place:zone-" + id;
            m.tier = 4;
            m.label = z.value("name", pretty(id));
            m.target = {{"zoneId", id}};
            m.source = {{"zoneId", id}};
            m.position = resolveTarget(*w, m.target, bindings);
            m.areaId = area(m.position);
            m.rangeMin = 3;
            m.rangeMax = 400;
            m.labelRange = 80;
            m.occlusion = "visible";
            if (m.position)
                out.push_back(m);
        }
    }
    std::stable_sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.tier != b.tier ? a.tier < b.tier : a.id < b.id; });
    // Unique IDs; the first (most important) wins.
    std::set<std::string> seen;
    std::erase_if(out, [&](auto &m) { return !seen.insert(m.id).second; });
    if (out.size() > 64)
        out.resize(64);
    return out;
}
void Reveal::start(int64_t now, int64_t hold, bool toggleMode, bool reducedMotion, const std::string &why) {
    if (active && toggleMode && why == "tap") {
        active = false;
        return;
    }
    bool shown = active;
    active = true;
    toggle = toggleMode;
    reduced = reducedMotion;
    holdMs = hold;
    reason = why;
    // Again while shown or fading: restart the hold with everything fully in.
    started = shown && !reduced ? now - FadeIn - 3 * Stagger : now;
}
float Reveal::alpha(int64_t now, int tier) const {
    if (!active)
        return 0;
    auto t = now - started;
    if (reduced)
        return toggle || t < holdMs ? 1.f : 0.f;
    float in = std::clamp(float(t - (tier - 1) * Stagger) / FadeIn, 0.f, 1.f);
    if (toggle)
        return in;
    float out = std::clamp(float(t - FadeIn - 3 * Stagger - holdMs) / FadeOut, 0.f, 1.f);
    return in * (1 - out);
}
bool Reveal::expired(int64_t now) const {
    if (!active || toggle)
        return false;
    return now - started >= (reduced ? holdMs : FadeIn + 3 * Stagger + holdMs + FadeOut);
}
bool Reveal::fading(int64_t now) const {
    if (!active || reduced)
        return false;
    auto t = now - started;
    return t < FadeIn + 3 * Stagger || (!toggle && t >= FadeIn + 3 * Stagger + holdMs);
}
Json Reveal::json() const {
    return {{"startedAtMs", started}, {"holdMs", toggle ? 0 : holdMs}, {"fadeMs", reduced ? 0 : FadeOut}, {"reason", reason}};
}
Json MarkerLayout::json() const {
    Json list = Json::array();
    for (auto &i : items)
        list.push_back({{"id", i.id},       {"kind", i.kind},     {"tier", i.tier},     {"x", i.x},
                        {"y", i.y},         {"alpha", i.alpha},   {"label", i.label},   {"distance", i.distance},
                        {"stack", i.stack}, {"arrow", i.arrow},   {"angle", i.angle},   {"occluded", i.occluded},
                        {"small", i.small}, {"dimmed", i.dimmed}, {"chevron", i.chevron}, {"tracked", i.tracked}});
    return {{"items", list}, {"inView", inView}, {"arrows", arrows}, {"labels", labels}, {"stacks", stacks}};
}
void MarkerRuntime::sync(const Json &j) {
    if (!j.is_object())
        return;
    requested = j.value("markers", Json::array());
    watch = j.value("watch", Json::array());
    calm = j.value("calm", false);
    auto g = j.value("guidance", Json::object());
    Settings next = settings;
    next.revealMs = g.value("revealMs", next.revealMs);
    next.toggle = g.value("revealMode", std::string("timer")) == "toggle";
    next.maxRevealed = std::clamp(g.value("maxRevealed", next.maxRevealed), 4, 16);
    next.labels = std::clamp(g.value("labels", next.labels), 2, 8);
    next.persistent = g.value("persistent", next.persistent);
    next.coreDraw = g.value("coreDraw", next.coreDraw);
    settings = next;
    listInputs_ = nullptr; // rebuild on the next tick
    ++version;
}
void MarkerRuntime::update(const Context &c) {
    reducedMotion = c.reducedMotion;
    mode = c.active ? c.mode : "hidden";
    // Opening the Director, entering use or focus mode, or a world change ends a reveal at once.
    if (reveal.active && (mode != "world" || c.world != world_))
        reveal.end();
    if (reveal.expired(c.now))
        reveal.end();
    if (mode != lastMode_) {
        lastMode_ = mode;
        ++version;
    }
    Json inputs = {{"world", reinterpret_cast<uintptr_t>(c.world)}, {"waypoint", c.waypoint}, {"route", c.route},
                   {"bindings", c.bindings}, {"requested", requested}};
    if (inputs != listInputs_ || c.world != world_) {
        world_ = c.world;
        listInputs_ = inputs;
        list = buildMarkers(c.world, c.waypoint, c.route, c.bindings, requested);
        occluded.clear();
        nextOcclusion_ = 0;
        ++version;
    }
    if (!c.world) {
        inside.clear();
        eyeArea.clear();
        return;
    }
    auto eye = c.camera.position;
    auto area = areaAt(*c.world, eye);
    if (area != eyeArea) {
        eyeArea = area;
        ++version;
    }
    // Reach facts: only while the world view is active (any movement counts).
    inside.clear();
    if (c.active)
        for (auto &w : watch)
            if (w.contains("key") && w.contains("target") &&
                reaches(*c.world, w["target"],
                        w.contains("radius") ? std::optional<float>(w["radius"].get<float>()) : std::nullopt, eye,
                        c.bindings))
                inside.insert(w["key"].get<std::string>());
    // Through-wall state at 10 Hz, only while something can draw.
    bool drawable = reveal.active || std::any_of(list.begin(), list.end(), [](auto &m) { return m.tracked; });
    if (drawable && c.now >= nextOcclusion_ && mode != "hidden") {
        nextOcclusion_ = c.now + 100;
        std::set<std::string> next;
        for (auto &m : list) {
            if (!m.position)
                continue;
            auto ray = *m.position - eye;
            float d = length(ray);
            if (d < .5f || d > 400)
                continue;
            if (auto hit = interaction::raycast(eye, normalized(ray), c.world->collision, &c.world->collisionIndex);
                hit && hit->distance < d - .3f)
                next.insert(m.id);
        }
        if (next != occluded) {
            occluded = std::move(next);
            ++version;
        }
    }
}
void MarkerRuntime::tap(int64_t now) {
    if (mode != "world")
        return;
    reveal.start(now, settings.revealMs, settings.toggle, reducedMotion, "tap");
    ++version;
}
void MarkerRuntime::request(int64_t now, std::optional<int64_t> durationMs) {
    reveal.start(now, durationMs.value_or(settings.revealMs), settings.toggle && !durationMs, reducedMotion, "request");
    if (mode != "world")
        reveal.end(); // shown only in world mode; the request still succeeds
    ++version;
}
Json MarkerRuntime::stateKey() const {
    Json items = Json::array();
    for (auto &m : list)
        items.push_back(m.json());
    return {{"reveal", reveal.active ? reveal.json() : Json(nullptr)},
            {"persistent", settings.persistent},
            {"calm", calm},
            {"coreDraw", settings.coreDraw},
            {"list", items}};
}
uint64_t MarkerRuntime::frameHash(int64_t now) const {
    uint64_t h = std::hash<uint64_t>{}(version);
    auto mix = [&](uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
    mix(reveal.active);
    mix(settings.coreDraw);
    if (reveal.active)
        for (int tier = 1; tier <= 4; ++tier)
            mix(uint64_t(reveal.alpha(now, tier) * 255));
    return h;
}
Json MarkerRuntime::projection(const sim::Camera &camera, float aspect, int64_t now) const {
    Json out = Json::array();
    if (mode == "hidden")
        return out;
    for (auto &m : list) {
        if (!m.position || out.size() >= 32)
            continue;
        bool persistentShown = m.tracked && settings.persistent != "off";
        if (!persistentShown && reveal.alpha(now, m.tier) <= 0)
            continue;
        auto p = camera.project(*m.position, aspect);
        if (!p.visible)
            continue;
        out.push_back({{"id", m.id},
                       {"markerId", m.id},
                       {"label", ipc::boundedText(m.label, 256)},
                       {"x", p.x},
                       {"y", p.y},
                       {"distance", length(*m.position - camera.position)},
                       {"occluded", occluded.contains(m.id)}});
    }
    return out;
}
MarkerLayout MarkerRuntime::layout(const MarkerView &v) const {
    MarkerLayout out;
    if (mode == "hidden" || !settings.coreDraw)
        return out;
    const bool revealing = reveal.active && mode == "world";
    const float W = float(v.width), H = float(v.height), aspect = W / std::max(1.f, H);
    const auto eye = v.camera.position;
    const auto view = v.camera.view();
    const float tangent = std::tan(v.camera.fov * .01745329252f / 2);
    struct Candidate {
        const Marker *m;
        float d, x, y, alpha;
        bool onScreen, persistentOnly, occluded;
        Vec3 local;
    };
    std::vector<Candidate> candidates;
    for (auto &m : list) {
        if (!m.position)
            continue;
        float d = length(*m.position - eye);
        if (d < m.rangeMin || d > m.rangeMax)
            continue; // arrived, or out of range
        bool persistent = m.tracked && settings.persistent != "off";
        bool tierOk = m.tier == 1 || (m.tier == 2 && d <= 60) ||
                      (m.tier == 3 && ((!m.areaId.empty() && m.areaId == eyeArea) || d <= 150)) ||
                      (m.tier == 4 && d <= 80);
        bool revealed = revealing && tierOk && (!calm || m.tier == 1);
        float ra = revealed ? reveal.alpha(v.now, m.tier) : 0.f;
        float alpha = std::max(ra, persistent ? 1.f : 0.f);
        if (alpha <= .002f)
            continue;
        bool isOccluded = occluded.contains(m.id);
        if (m.occlusion == "visible" && isOccluded)
            continue;
        Candidate c{&m, d, 0, 0, alpha, false, ra <= .002f, isOccluded, view.point(*m.position)};
        float depth = -c.local.z;
        if (depth > .01f) {
            c.x = (1 + v.camera.shiftX + c.local.x / depth / tangent / aspect) / 2 * W;
            c.y = (1 - v.camera.shiftY - c.local.y / depth / tangent) / 2 * H;
            c.onScreen = c.x >= 0 && c.x <= W && c.y >= 0 && c.y <= H;
        }
        // Persistent tracked marker: a small diamond only beyond 25 m.
        if (c.persistentOnly && c.onScreen && d <= 25)
            continue;
        candidates.push_back(c);
    }
    auto priority = [](const Candidate &a, const Candidate &b) {
        return a.m->tier != b.m->tier ? a.m->tier < b.m->tier : a.d < b.d;
    };
    std::stable_sort(candidates.begin(), candidates.end(), priority);
    auto aimedAt = [&](float x, float y) {
        for (auto &r : v.windows)
            if (r.aimed && x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1)
                return true;
        return false;
    };
    auto overWindow = [&](float x, float y) {
        for (auto &r : v.windows)
            if (!r.aimed && x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1)
                return true;
        return false;
    };
    // Budget: at most max_revealed in view and 6 edge arrows, by tier then distance.
    std::vector<Candidate> inView, arrows;
    for (auto &c : candidates) {
        if (c.onScreen) {
            if (c.m->kind == "waypoint")
                continue; // the waypoint keeps its own world-locked marker (RFC-0005)
            if (int(inView.size()) < settings.maxRevealed && !aimedAt(c.x, c.y))
                inView.push_back(c);
        } else if (c.m->tier <= 2 && c.m->occlusion != "visible")
            arrows.push_back(c);
    }
    out.inView = int(inView.size());
    const float s = std::max(1.f, v.scale);
    // Stacking: icons within 32 px merge under the most important; tier-1 markers stay heads.
    std::vector<DrawItem> heads;
    std::vector<const Candidate *> headSource;
    for (auto &c : inView) {
        DrawItem *joined = nullptr;
        if (c.m->tier != 1)
            for (auto &h : heads)
                if (std::hypot(h.x - c.x, h.y - c.y) < 32 * s) {
                    joined = &h;
                    break;
                }
        if (joined) {
            ++joined->stack;
            continue;
        }
        DrawItem i;
        i.id = c.m->id;
        i.kind = c.m->kind;
        i.icon = c.m->icon;
        i.tier = c.m->tier;
        i.x = c.x;
        i.y = c.y;
        i.alpha = c.alpha * (overWindow(c.x, c.y) ? .5f : 1.f);
        i.dimmed = overWindow(c.x, c.y);
        i.occluded = c.occluded;
        i.small = c.persistentOnly;
        i.tracked = c.m->tracked;
        i.distance = distanceText(c.d);
        float dy = c.m->position->y - eye.y;
        i.chevron = dy > 4 ? 1 : dy < -4 ? -1 : 0;
        heads.push_back(i);
        headSource.push_back(&c);
    }

    // Edge arrows: 48 logical px inside an ellipse fitted to the output.
    const float inset = 48 * s, rx = std::max(8.f, W / 2 - inset), ry = std::max(8.f, H / 2 - inset);
    std::vector<DrawItem> arrowItems;
    std::vector<const Candidate *> arrowSource;
    for (auto &c : arrows) {
        float dx, dy;
        if (-c.local.z > .01f) {
            dx = c.x - W / 2;
            dy = c.y - H / 2;
        } else if (std::abs(c.local.x) < .2f * std::abs(c.local.z)) {
            dx = 0;
            dy = 1; // about straight behind (within ~11 degrees): bottom centre, "turn around"
        } else {
            dx = c.local.x; // behind and to one side: that side's edge, height kept small
            dy = -c.local.y * .25f;
        }
        float t = 1 / std::sqrt((dx / rx) * (dx / rx) + (dy / ry) * (dy / ry));
        DrawItem i;
        i.id = c.m->id;
        i.kind = c.m->kind;
        i.icon = c.m->icon;
        i.tier = c.m->tier;
        i.arrow = true;
        i.x = W / 2 + dx * t;
        i.y = H / 2 + dy * t;
        i.angle = std::atan2(dy, dx);
        i.alpha = c.alpha;
        i.small = c.persistentOnly;
        i.tracked = c.m->tracked;
        i.occluded = c.occluded;
        i.distance = distanceText(c.d);
        if (aimedAt(i.x, i.y))
            continue;
        // Arrows for targets in one direction stack like icons (a tier-1 arrow keeps its own).
        auto same = std::find_if(arrowItems.begin(), arrowItems.end(), [&](const DrawItem &o) {
            return i.tier != 1 && std::hypot(o.x - i.x, o.y - i.y) < 28 * s;
        });
        if (same != arrowItems.end()) {
            ++same->stack;
            continue;
        }
        if (arrowItems.size() >= 6)
            continue;
        arrowItems.push_back(i);
        arrowSource.push_back(&c);
    }
    out.arrows = int(arrowItems.size());
    for (auto *list : {&heads, &arrowItems})
        for (auto &h : *list)
            out.stacks += h.stack > 1;
    // Labels: at most `labels`, tier 1 first, then nearest the screen centre. Near the
    // reticle labels collapse to icons; arrival, label range and tier rules drop others.
    struct LabelCandidate {
        DrawItem *item;
        const Candidate *c;
        float centre;
    };
    std::vector<LabelCandidate> wanted;
    const float rxp = v.reticleX * W, ryp = v.reticleY * H;
    auto eligible = [&](const DrawItem &i, const Candidate &c) {
        if (i.small && settings.persistent != "always")
            return false;
        if (c.d > c.m->labelRange || (c.m->tier == 4 && c.d > 30) || (c.m->tier == 1 && !i.arrow && c.d <= 4))
            return false;
        if (i.arrow && c.m->tier != 1)
            return false;
        if (!i.arrow && std::hypot(i.x - rxp, i.y - ryp) < 120 * s)
            return false;
        return !c.m->label.empty();
    };
    for (size_t k = 0; k < heads.size(); ++k)
        if (eligible(heads[k], *headSource[k]))
            wanted.push_back({&heads[k], headSource[k], std::hypot(heads[k].x - W / 2, heads[k].y - H / 2)});
    for (size_t k = 0; k < arrowItems.size(); ++k)
        if (eligible(arrowItems[k], *arrowSource[k]))
            wanted.push_back({&arrowItems[k], arrowSource[k], std::hypot(arrowItems[k].x - W / 2, arrowItems[k].y - H / 2)});
    std::stable_sort(wanted.begin(), wanted.end(), [](auto &a, auto &b) {
        bool ta = a.c->m->tier == 1, tb = b.c->m->tier == 1;
        return ta != tb ? ta : a.centre < b.centre;
    });
    for (auto &w : wanted) {
        if (out.labels >= settings.labels)
            break;
        auto text = ascii(w.c->m->label);
        if (text.size() > 32)
            text = text.substr(0, 29) + "...";
        w.item->label = text;
        ++out.labels;
    }
    out.items = std::move(heads);
    out.items.insert(out.items.end(), arrowItems.begin(), arrowItems.end());
    return out;
}
} // namespace hyprune::quests
