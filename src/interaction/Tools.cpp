#include "Tools.hpp"
#include "world/Motion.hpp"
#include "DebugTool.hpp"
#include "sim/Route.hpp"
#include <chrono>
#include <set>
namespace hyprune::interaction {
Mat4 Transform::matrix(Vec3 origin) const {
    auto m = Mat4::identity();
    auto x = rotate(rotation, {scale.x, 0, 0}), y = rotate(rotation, {0, scale.y, 0}),
         z = rotate(rotation, {0, 0, scale.z});
    m.v[0] = x.x;
    m.v[1] = x.y;
    m.v[2] = x.z;
    m.v[4] = y.x;
    m.v[5] = y.y;
    m.v[6] = y.z;
    m.v[8] = z.x;
    m.v[9] = z.y;
    m.v[10] = z.z;
    auto delta = position - m.point(origin);
    m.v[12] = delta.x;
    m.v[13] = delta.y;
    m.v[14] = delta.z;
    return m;
}
std::vector<Handle> gizmo(Vec3 c, const std::string &mode, float size) {
    std::vector<Handle> out;
    std::array<Vec3, 3> axis = {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};
    for (int i = 0; i < 3; i++) {
        if (mode == "rotate") {
            auto a = axis[(i + 1) % 3], b = axis[(i + 2) % 3];
            for (int j = 0; j < 40; j++) {
                float p = j * 6.283185f / 40, q = (j + 1) * 6.283185f / 40;
                out.push_back({i, false, c + (a * std::cos(p) + b * std::sin(p)) * size,
                               c + (a * std::cos(q) + b * std::sin(q)) * size, "arc"});
            }
        } else
            out.push_back({i, false, c + axis[i] * .15f * size, c + axis[i] * size, mode == "move" ? "arrow" : "cube"});
    }
    if (mode == "scale")
        out.push_back({3, true, c - Vec3{.08f, .08f, 0} * size, c + Vec3{.08f, .08f, 0} * size, "cube"});
    return out;
}
std::optional<int> pickHandle(std::span<const Handle> handles, Vec3 o, Vec3 ray, float tolerance) {
    for (size_t i = 0; i < handles.size(); i++)
        if (handles[i].uniform) {
            auto p = (handles[i].start + handles[i].end) * .5f;
            float d = dot(p - o, ray);
            if (d > 0 && length(o + ray * d - p) < tolerance)
                return int(i);
        }
    std::optional<int> result;
    float best = tolerance;
    for (size_t i = 0; i < handles.size(); i++) {
        auto h = handles[i];
        for (int j = 0; j <= 24; j++) {
            auto p = h.start + (h.end - h.start) * (j / 24.f);
            float d = dot(p - o, ray);
            if (d < 0)
                continue;
            float distance = length(o + ray * d - p);
            if (distance < best) {
                best = distance;
                result = i;
            }
        }
    }
    return result;
}
std::vector<Line> pathLines(const Json &p) {
    std::vector<Line> out;
    auto points = p.at("points");
    if (points.size() < 2)
        return out;
    auto vec = [](const Json &j) { return Vec3{j[0], j[1], j[2]}; };
    size_t count = points.size() + (p.at("closed").get<bool>() ? 1 : 0);
    for (size_t i = 1; i < count; i++) {
        auto a = vec(points[(i - 1) % points.size()]["position"]), b = vec(points[i % points.size()]["position"]);
        auto before = vec(points[(i > 1 ? i - 2 : 0) % points.size()]["position"]);
        auto type = sim::segmentType(points[i % points.size()]["type"]);
        Vec3 prev = a;
        for (int k = 1; k <= sim::samplesPerSegment; k++) {
            Vec3 next = sim::segmentPoint(before, a, b, type, k / float(sim::samplesPerSegment));
            out.push_back({prev, next, {.85f, .65f, .18f}, .025f, int(i % points.size())});
            prev = next;
        }
    }
    return out;
}
std::optional<PathPick> pickPath(const Json &paths, const std::string &worldId, Vec3 origin, Vec3 ray,
                                 float maxDistance) {
    std::optional<PathPick> best;
    auto along = [&](Vec3 p) { return dot(p - origin, ray); };
    auto off = [&](Vec3 p) { return length(p - (origin + ray * along(p))); };
    for (auto &p : paths) {
        if (p.value("worldId", "") != worldId)
            continue;
        auto &points = p.at("points");
        for (size_t i = 0; i < points.size(); ++i) {
            auto &v = points[i]["position"];
            Vec3 q{v[0], v[1], v[2]};
            float t = along(q);
            if (t < .1f || t > maxDistance + .25f || off(q) > std::max(.07f, .014f * t))
                continue;
            if (!best || best->point < 0 || t < best->distance)
                best = PathPick{p.at("id"), int(i), -1, q, t};
        }
    }
    if (best)
        return best;
    for (auto &p : paths) {
        if (p.value("worldId", "") != worldId)
            continue;
        for (auto &line : pathLines(p)) {
            // Closest points of the ray and the sampled segment.
            Vec3 d = line.b - line.a, w = origin - line.a;
            float a = dot(d, d), b = dot(d, ray), e = dot(d, w), c = dot(ray, w), den = a - b * b;
            float s = den > 1e-9f ? std::clamp((b * c - e) / den, 0.f, 1.f) : 0.f;
            Vec3 q = line.a + d * s;
            float t = along(q);
            if (t < .1f || t > maxDistance + .25f || off(q) > std::max(.05f, .01f * t))
                continue;
            if (!best || t < best->distance)
                best = PathPick{p.at("id"), -1, line.segment, q, t};
        }
    }
    return best;
}
Json Tools::definitions() const {
    auto binding = [](const char *action, const char *label) { return Json{{"action", action}, {"label", label}}; };
    auto def = [&](int n, const char *name, Json primary, Json secondary, Json alternate, Json cycle) {
        return Json{
            {"slot", n},
            {"name", name},
            {"slots", {{"primary", primary}, {"secondary", secondary}, {"alternate", alternate}, {"cycle", cycle}}}};
    };
    return Json::array(
        {def(1, "Normal", binding("client.primary", "Click / interact"), binding("client.secondary", "Right-click"),
             binding("client.alternate", "Middle-click"), binding("client.scroll", "Scroll")),
         def(2, "Window",
             binding(windowMode ? "window.lock" : "window.mount", windowMode ? "Lock / unlock" : "Tag / mount"),
             binding("window.lock", "Lock / unlock"), nullptr, binding("tool.cycle", "Place / lock mode")),
         def(3, "Path", binding("path.add", "Add / drag point"), binding("path.remove", "Remove point"),
             binding("path.closed", pathMode >= 3 ? "Open path" : "Close path"), binding("tool.cycle", "Segment type")),
         def(4, "Position", binding("gizmo.drag", "Select / drag"),
             binding("gizmo.space", localAxes ? "Use world axes" : "Use local axes"),
             binding("gizmo.snap", snap ? "Disable snapping" : "Enable snapping"),
             binding("tool.cycle", universal ? "Move / rotate / scale / all" : "Move / rotate / scale")),
         def(5, "Placement", binding("placement.ahead", "Place ahead"),
             binding("placement.surface", "Place on surface"), binding("placement.pocket", "Pocket window"),
             binding("placement.cycle", "Choose pocketed window"))});
}
Json Tools::debugDefinition() const {
    auto binding = [](const char *action, const char *label) { return Json{{"action", action}, {"label", label}}; };
    return {{"slot", debugSlot},
            {"name", "Debug"},
            {"slots",
             {{"primary", binding("debug.inspect", "Inspect")},
              {"secondary", binding("debug.clear", "Close info card")},
              {"alternate", nullptr},
              {"cycle", binding("tool.cycle", "Debug layer")}}}};
}
Json Tools::slots() const {
    if (slot == debugSlot)
        return debugDefinition().at("slots");
    return definitions().at(std::clamp(slot, 1, 5) - 1).at("slots");
}
std::string Tools::slotAction(const std::string &name) const {
    auto value = slots().value(name, Json(nullptr));
    return value.is_object() ? value.at("action").get<std::string>() : "";
}
void Tools::removePoint() {
    for (auto &path : paths)
        if (path["id"] == activePath && !path["points"].empty()) {
            path["points"].erase(path["points"].end() - 1);
            break;
        }
}
Json Tools::path(const std::string &id) const {
    for (auto &p : paths)
        if (p["id"] == id)
            return p;
    return nullptr;
}
void Tools::setPath(const std::string &id, Json record) {
    auto it = std::find_if(paths.begin(), paths.end(), [&](auto &p) { return p["id"] == id; });
    if (record.is_null()) {
        if (it != paths.end())
            paths.erase(it);
        if (activePath == id)
            activePath.clear();
    } else if (it != paths.end())
        *it = std::move(record);
    else
        paths.push_back(std::move(record));
}
void Tools::movePoint(const std::string &id, int index, Vec3 v) {
    for (auto &p : paths)
        if (p["id"] == id && index >= 0 && index < int(p["points"].size()))
            p["points"][index]["position"] = {v.x, v.y, v.z};
}
void Tools::insertPoint(const std::string &id, int index, Vec3 v) {
    for (auto &p : paths)
        if (p["id"] == id && index >= 0 && index <= int(p["points"].size())) {
            if (p["points"].size() >= 256)
                throw std::runtime_error("path point budget");
            // The new point keeps the segment's shape on both halves.
            std::string type = index < int(p["points"].size()) ? p["points"][index]["type"].get<std::string>() : "line";
            p["points"].insert(p["points"].begin() + index, Json{{"position", {v.x, v.y, v.z}}, {"type", type}});
        }
}
void Tools::removePointAt(const std::string &id, int index) {
    for (auto &p : paths)
        if (p["id"] == id && index >= 0 && index < int(p["points"].size()))
            p["points"].erase(p["points"].begin() + index);
}
void Tools::toggleClosed() {
    pathMode = (pathMode + 3) % 6;
    for (auto &path : paths)
        if (path["id"] == activePath)
            path["closed"] = pathMode >= 3;
}
std::vector<Handle> Tools::handles(Vec3 center, const std::array<float, 4> &rotation) const {
    auto result = gizmo(center, submode());
    if (localAxes)
        for (auto &handle : result) {
            handle.start = center + rotate(rotation, handle.start - center);
            handle.end = center + rotate(rotation, handle.end - center);
        }
    return result;
}
std::string Tools::submode() const {
    if (slot == 2)
        return windowMode ? "lock" : "place";
    if (slot == 3)
        return std::string(pathMode >= 3 ? "closed " : "open ") + std::array{"line", "curve", "arc"}[pathMode % 3];
    if (slot == 4)
        return std::array{"move", "rotate", "scale", "universal"}[std::clamp(positionMode, 0, 3)];
    if (slot == debugSlot)
        return debugLayerInfo(debugLayer).id;
    return slot == 1 ? "normal" : "placement";
}
void Tools::wheel(int steps) {
    auto cycle = [&](int &v, int n) { v = (v + steps % n + n) % n; };
    if (slot == 2)
        cycle(windowMode, 2);
    if (slot == 3) {
        int closed = pathMode / 3;
        pathMode = (pathMode % 3 + steps % 3 + 3) % 3 + closed * 3;
    }
    if (slot == 4) {
        if (!universal && positionMode > 2)
            positionMode = 0;
        cycle(positionMode, universal ? 4 : 3);
    }
    if (slot == debugSlot)
        debugLayer = debugLayerStep(debugLayer, steps);
}
std::string Tools::gizmoMode() const {
    return std::array{"move", "rotate", "scale", "universal"}[std::clamp(positionMode, 0, universal ? 3 : 2)];
}
void Tools::recordEdit(const std::string &kind, const std::string &worldId, const std::string &id, Json before, Json after) {
    if (before == after)
        return;
    history.resize(history.size() - undone);
    undone = 0;
    history.push_back({kind, worldId, id, std::move(before), std::move(after)});
    if (history.size() > undoLimit)
        history.erase(history.begin());
}
std::optional<Tools::Edit> Tools::undo() {
    if (!canUndo())
        return {};
    return history[history.size() - ++undone];
}
std::optional<Tools::Edit> Tools::redo() {
    if (!canRedo())
        return {};
    return history[history.size() - undone--];
}
void Tools::point(const std::string &world, const std::string &area, Vec3 p) {
    size_t total = 0;
    for (auto &path : paths)
        total += path["points"].size();
    if (total >= 2048)
        throw std::runtime_error("total path point budget");
    if (paths.size() >= 128 && activePath.empty())
        throw std::runtime_error("path budget");
    if (activePath.empty()) {
        activePath = "path-" + std::to_string(deterministicId ? (*deterministicId)++ : std::chrono::steady_clock::now().time_since_epoch().count());
        paths.push_back({{"id", activePath},
                         {"worldId", world},
                         {"areaId", area},
                         {"closed", pathMode >= 3},
                         {"points", Json::array()}});
    }
    auto it = std::find_if(paths.begin(), paths.end(), [&](auto &v) { return v["id"] == activePath; });
    if (it == paths.end() || it->at("points").size() >= 256)
        throw std::runtime_error("path point budget");
    (*it)["closed"] = pathMode >= 3;
    (*it)["points"].push_back(
        {{"position", {p.x, p.y, p.z}}, {"type", std::array{"line", "curve", "arc"}[pathMode % 3]}});
}
void Tools::finishPath() {
    // A path left without points (every point removed again) is dropped: nothing shows it.
    auto it = std::find_if(paths.begin(), paths.end(), [&](const Json &p) { return p["id"] == activePath; });
    if (it != paths.end() && it->value("points", Json::array()).empty())
        paths.erase(it);
    activePath.clear();
}
bool Tools::locked(const std::string &world, const surfaces::Instance &i) const {
    auto key = world + ":" + i.id;
    if (liveLocks.contains(key))
        return true;
    for (auto &l : locks)
        if (l["worldId"] == world && l["appClass"] == i.appClass && l["title"] == i.title) {
            if (liveLocks.size() >= 1024)
                liveLocks.erase(liveLocks.begin());
            liveLocks[key] = l;
            return true;
        }
    return false;
}
void Tools::toggleLock(const std::string &world, const surfaces::Instance &i) {
    auto key = world + ":" + i.id;
    if (locked(world, i)) {
        auto rule = liveLocks.at(key);
        std::erase_if(liveLocks, [&](auto &entry) { return entry.second == rule; });
        std::erase_if(locks.get_ref<Json::array_t &>(), [&](auto &l) { return l == rule; });
    } else {
        if (locks.size() >= 128)
            throw std::runtime_error("lock budget");
        Json rule = {{"worldId", world}, {"appClass", i.appClass}, {"title", i.title}};
        locks.push_back(rule);
        liveLocks[key] = rule;
    }
}
Json Tools::saved() const {
    Json j{{"version", 2}, {"paths", paths}, {"objects", objects}, {"locks", locks}, {"placements", placements},
           {"overrides", overrides}};
    if (!companions.empty())
        j["companions"] = companions;
    return j;
}
namespace {
Json find(const Json &list, const char *key, const std::string &world, const std::string &id) {
    for (auto &r : list)
        if (r.value("worldId", Json()) == world && r.value(key, Json()) == id)
            return r;
    return nullptr;
}
void put(Json &list, const char *key, const std::string &world, const std::string &id, Json record) {
    auto &a = list.get_ref<Json::array_t &>();
    auto it = std::find_if(a.begin(), a.end(), [&](auto &r) { return r["worldId"] == world && r[key] == id; });
    if (record.is_null()) {
        if (it != a.end())
            a.erase(it);
    } else if (it != a.end())
        *it = std::move(record);
    else
        a.push_back(std::move(record));
}
} // namespace
Json Tools::placement(const std::string &world, const std::string &id) const { return find(placements, "id", world, id); }
Json Tools::override(const std::string &world, const std::string &objectId) const { return find(overrides, "objectId", world, objectId); }
void Tools::setPlacement(const std::string &world, const std::string &id, Json record) { put(placements, "id", world, id, std::move(record)); }
void Tools::setOverride(const std::string &world, const std::string &objectId, Json record) {
    put(overrides, "objectId", world, objectId, std::move(record));
}
Json Tools::companion(const std::string &world) const {
    for (auto &r : companions)
        if (r.value("worldId", Json()) == world)
            return r;
    return nullptr;
}
void Tools::setCompanion(const std::string &world, Json record) {
    auto &a = companions.get_ref<Json::array_t &>();
    std::erase_if(a, [&](auto &r) { return r["worldId"] == world; });
    if (!record.is_null()) {
        if (a.size() >= companionLimit)
            a.erase(a.begin()); // the oldest world's record goes
        a.push_back(std::move(record));
    }
}
void Tools::restore(const Json &input) {
    bool companionKey = input.is_object() && input.value("version", Json()) == 2 && input.contains("companions");
    if (!input.is_object() || !input.contains("version") || (input["version"] != 1 && input["version"] != 2) ||
        input.size() != (input["version"] == 1 ? 4u : companionKey ? 7u : 6u))
        throw std::runtime_error("invalid editor state");
    Json j = input;
    bool upgrade = j["version"] == 1;
    if (upgrade) {
        // Version 1 kept Gizmo 2.0's world-object transforms as "object:<id>" records
        // in objects; they become overrides keyed by the plain instance id.
        j["placements"] = Json::array();
        j["overrides"] = Json::array();
        if (j["objects"].is_array()) {
            Json nodes = Json::array();
            for (auto &o : j["objects"]) {
                std::string id = o.is_object() && o.contains("objectId") && o["objectId"].is_string() ? o["objectId"].get<std::string>() : "";
                if (!id.starts_with("object:")) {
                    nodes.push_back(o);
                    continue;
                }
                if (o.size() != 6 || !o.contains("scale") || !o["scale"].is_array() || o["scale"].empty())
                    throw std::runtime_error("invalid object transform");
                j["overrides"].push_back({{"worldId", o.at("worldId")}, {"objectId", id.substr(7)}, {"areaId", o.at("areaId")},
                                          {"pose", {{"position", o.at("position")}, {"rotation", o.at("rotation")}}},
                                          {"scale", o["scale"][0]}});
            }
            j["objects"] = nodes;
        }
    }
    for (auto key : {"paths", "objects", "locks"})
        if (!j.at(key).is_array() || j.at(key).size() > 128)
            throw std::runtime_error("editor state budget");
    if (!j.at("placements").is_array() || j["placements"].size() > placementLimit || !j.at("overrides").is_array() ||
        j["overrides"].size() > overrideLimit)
        throw std::runtime_error("editor state budget");
    auto text = [](const Json &j) {
        if (!j.is_string() || j.get<std::string>().size() > 256)
            throw std::runtime_error("invalid editor ID");
    };
    auto vector = [](const Json &j, int count) {
        if (!j.is_array() || j.size() != size_t(count))
            throw std::runtime_error("invalid editor vector");
        for (auto &v : j)
            if (!v.is_number() || !std::isfinite(v.get<double>()) || std::abs(v.get<double>()) > 100000)
                throw std::runtime_error("invalid editor coordinate");
    };
    for (auto &p : j["paths"]) {
        if (p.size() != 5 || !p["closed"].is_boolean() || !p["points"].is_array() || p["points"].size() > 256)
            throw std::runtime_error("invalid path");
        for (auto key : {"id", "worldId", "areaId"})
            text(p.at(key));
        for (auto &v : p["points"]) {
            if (v.size() != 2 || (v.at("type") != "line" && v.at("type") != "curve" && v.at("type") != "arc"))
                throw std::runtime_error("invalid path segment");
            vector(v.at("position"), 3);
        }
    }
    for (auto &o : j["objects"]) {
        if (o.size() != 6)
            throw std::runtime_error("invalid object transform");
        for (auto k : {"worldId", "areaId", "objectId"})
            text(o.at(k));
        vector(o.at("position"), 3);
        vector(o.at("rotation"), 4);
        vector(o.at("scale"), 3);
        float norm = 0;
        for (auto v : o["rotation"])
            norm += v.get<float>() * v.get<float>();
        if (std::abs(norm - 1) > .01f)
            throw std::runtime_error("invalid object quaternion");
        for (auto v : o["scale"])
            if (v <= .01 || v > 100)
                throw std::runtime_error("invalid object scale");
    }
    for (auto &l : j["locks"]) {
        if (l.size() != 3)
            throw std::runtime_error("invalid lock");
        for (auto k : {"worldId", "appClass", "title"})
            text(l.at(k));
    }
    auto pose = [&](const Json &p) {
        if (!p.is_object() || p.size() != 2)
            throw std::runtime_error("invalid object pose");
        vector(p.at("position"), 3);
        vector(p.at("rotation"), 4);
        float norm = 0;
        for (auto v : p["rotation"])
            norm += v.get<float>() * v.get<float>();
        if (std::abs(norm - 1) > .01f)
            throw std::runtime_error("invalid object quaternion");
    };
    auto scale = [](const Json &s) {
        if (!s.is_number() || !std::isfinite(s.get<double>()) || s.get<double>() < .01 || s.get<double>() > 100)
            throw std::runtime_error("invalid object scale");
    };
    std::map<std::string, size_t> perWorld;
    std::set<std::pair<std::string, std::string>> placementIds, overrideIds, placementKeys;
    for (auto &r : j["placements"]) {
        // {worldId, id, areaId, ref, pose, scale} plus the optional lock (IPC 0.22), a user
        // character's route and settings (0.23) and a workspace screen's workspace (0.27).
        if (!r.is_object())
            throw std::runtime_error("invalid placement");
        for (auto k : {"worldId", "id", "areaId", "ref", "pose", "scale"})
            if (!r.contains(k))
                throw std::runtime_error("invalid placement");
        for (auto k : {"worldId", "id", "areaId", "ref"})
            text(r.at(k));
        pose(r.at("pose"));
        scale(r.at("scale"));
        for (auto &[k, v] : r.items()) {
            if (k == "worldId" || k == "id" || k == "areaId" || k == "ref" || k == "pose" || k == "scale")
                continue;
            bool ok = k == "locked" || k == "reverse" || k == "disabled"     ? v.is_boolean()
                      : k == "path"                                            ? v.is_string() && v.get<std::string>().size() <= 256
                      : k == "phase"                                           ? v.is_number() && v.get<double>() >= 0 && v.get<double>() < 1
                      : k == "speed"                                           ? v.is_number() && v.get<double>() >= .25 && v.get<double>() <= 2
                      : k == "workspace"                                       ? v.is_number_integer() && v.get<int64_t>() >= 1 && v.get<int64_t>() <= 9999
                      : k == "key" ? v.is_string() && validPlacementKey(v.get<std::string>()) // IPC 0.31: a placement's stable name
                      : k == "accent" ? v.is_array() && v.size() == 3 &&
                                            std::all_of(v.begin(), v.end(), [](auto &c) { return c.is_number() && c >= 0 && c <= 1; })
                      : k == "style"  ? world::validCharacterStyle(v) // IPC 0.28: a character's walk, run and idle styles
                                      : false;
            if (!ok)
                throw std::runtime_error("invalid placement field " + k);
        }
        if (!placementIds.insert({r["worldId"].get<std::string>(), r["id"].get<std::string>()}).second)
            throw std::runtime_error("duplicate placement id");
        if (r.contains("key") && !placementKeys.insert({r["worldId"].get<std::string>(), r["key"].get<std::string>()}).second)
            throw std::runtime_error("duplicate placement key");
        if (++perWorld[r["worldId"].get<std::string>()] > placementsPerWorld)
            throw std::runtime_error("placement budget");
    }
    for (auto &r : j["overrides"]) {
        if (!r.is_object() || !r.contains("worldId") || !r.contains("objectId"))
            throw std::runtime_error("invalid override");
        for (auto &[k, v] : r.items()) {
            if (k == "worldId" || k == "objectId" || k == "areaId")
                text(v);
            else if (k == "pose")
                pose(v);
            else if (k == "scale")
                scale(v);
            else if (k == "hidden" || k == "locked") {
                if (!v.is_boolean())
                    throw std::runtime_error("invalid override flag");
            } else
                throw std::runtime_error("invalid override field");
        }
        if (!overrideIds.insert({r["worldId"].get<std::string>(), r["objectId"].get<std::string>()}).second)
            throw std::runtime_error("duplicate override");
    }
    Json companionRecords = companionKey ? j["companions"] : Json::array();
    if (!companionRecords.is_array() || companionRecords.size() > companionLimit)
        throw std::runtime_error("editor state budget");
    std::set<std::string> companionWorlds;
    for (auto &r : companionRecords) {
        if (!r.is_object() || !r.contains("worldId") || (r.contains("path") == r.contains("pose")))
            throw std::runtime_error("invalid companion record");
        for (auto &[k, v] : r.items()) {
            if (k == "worldId" || k == "path")
                text(v);
            else if (k == "pose")
                pose(v);
            else if (k == "phase" || k == "speed") {
                if (!v.is_number() || !std::isfinite(v.get<double>()) || v.get<double>() < 0 || v.get<double>() > 10)
                    throw std::runtime_error("invalid companion record");
            } else if (k == "reverse") {
                if (!v.is_boolean())
                    throw std::runtime_error("invalid companion record");
            } else
                throw std::runtime_error("invalid companion field");
        }
        if (!companionWorlds.insert(r["worldId"].get<std::string>()).second)
            throw std::runtime_error("duplicate companion record");
    }
    size_t total = 0;
    for (auto &path : j["paths"])
        total += path["points"].size();
    if (total > 2048)
        throw std::runtime_error("total path point budget");
    paths = j["paths"];
    objects = j["objects"];
    locks = j["locks"];
    placements = j["placements"];
    overrides = j["overrides"];
    companions = companionRecords;
    migrated = upgrade;
    liveLocks.clear();
    selected.clear();
    activePath.clear();
    activeHandle = -1;
}
} // namespace hyprune::interaction
