#include "Tools.hpp"
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
        std::string type = points[i % points.size()]["type"];
        Vec3 prev = a;
        for (int k = 1; k <= 24; k++) {
            float t = k / 24.f;
            Vec3 next = a + (b - a) * t;
            if (type == "curve") {
                auto before = vec(points[(i > 1 ? i - 2 : 0) % points.size()]["position"]);
                auto tangent = (b - before) * .5f;
                next = a * (2 * t * t * t - 3 * t * t + 1) + tangent * (t * t * t - 2 * t * t + t) +
                       b * (-2 * t * t * t + 3 * t * t) + (b - a) * (t * t * t - t * t);
            }
            if (type == "arc")
                next += normalized(cross(b - a, {0, 1, 0})) * std::sin(t * 3.141593f) * length(b - a) * .3f;
            out.push_back({prev, next, {.85f, .65f, .18f}, .025f});
            prev = next;
        }
    }
    return out;
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
         def(3, "Path", binding("path.add", "Add point"), binding("path.remove", "Remove point"),
             binding("path.closed", pathMode >= 3 ? "Open path" : "Close path"), binding("tool.cycle", "Segment type")),
         def(4, "Position", binding("gizmo.drag", "Select / drag"),
             binding("gizmo.space", localAxes ? "Use world axes" : "Use local axes"),
             binding("gizmo.snap", snap ? "Disable snapping" : "Enable snapping"),
             binding("tool.cycle", universal ? "Move / rotate / scale / all" : "Move / rotate / scale")),
         def(5, "Placement", binding("placement.ahead", "Place ahead"),
             binding("placement.surface", "Place on surface"), binding("placement.pocket", "Pocket window"),
             binding("placement.cycle", "Choose pocketed window"))});
}
Json Tools::slots() const {
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
    return {{"version", 2}, {"paths", paths}, {"objects", objects}, {"locks", locks}, {"placements", placements},
            {"overrides", overrides}};
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
void Tools::restore(const Json &input) {
    if (!input.is_object() || !input.contains("version") || (input["version"] != 1 && input["version"] != 2) ||
        input.size() != (input["version"] == 1 ? 4u : 6u))
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
    std::set<std::pair<std::string, std::string>> placementIds, overrideIds;
    for (auto &r : j["placements"]) {
        if (!r.is_object() || r.size() < 6 || r.size() > 7 || (r.size() == 7 && !r.contains("locked")))
            throw std::runtime_error("invalid placement");
        for (auto k : {"worldId", "id", "areaId", "ref"})
            text(r.at(k));
        pose(r.at("pose"));
        scale(r.at("scale"));
        if (r.contains("locked") && !r["locked"].is_boolean())
            throw std::runtime_error("invalid placement lock");
        if (!placementIds.insert({r["worldId"].get<std::string>(), r["id"].get<std::string>()}).second)
            throw std::runtime_error("duplicate placement id");
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
    migrated = upgrade;
    liveLocks.clear();
    selected.clear();
    activePath.clear();
    activeHandle = -1;
}
} // namespace hyprune::interaction
