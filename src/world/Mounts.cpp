#include "Mounts.hpp"
#include "Schema.hpp"
#include <set>
namespace hyprune::world {
namespace {
void need(bool yes, const char *why) {
    if (!yes)
        throw std::runtime_error(why);
}
void pose(const Json &p) {
    need(p.is_object() && p.size() == 2, "invalid mount pose");
    auto pos = p.at("position"), q = p.at("rotation");
    need(pos.is_array() && pos.size() == 3 && q.is_array() && q.size() == 4, "invalid mount pose vectors");
    double norm = 0;
    for (auto v : pos)
        need(v.is_number() && std::isfinite(v.get<double>()), "invalid mount position");
    for (auto v : q) {
        need(v.is_number() && std::isfinite(v.get<double>()), "invalid mount quaternion");
        norm += v.get<double>() * v.get<double>();
    }
    need(std::abs(norm - 1) < .002, "mount rotation must be normalized");
}
const std::set<std::string> types{"facade-wall",       "hanging-wall",   "wall-inset-console", "banner", "lectern",
                                  "operator-desk",     "field-rig",      "kiosk-pillar",       "globe",  "crate",
                                  "blast-door-screen", "terminal-bench", "radar-dish"};
} // namespace
Json defaultAtmosphere() {
    return {
        {"fog",
         {{"color", {.028, .045, .065}},
          {"density", .008},
          {"heightFalloff", 0},
          {"baseHeight", 0},
          {"excludeUnlit", true}}},
        {"sky",
         {{"zenith", {.18, .3, .52}},
          {"horizon", {.55, .61, .65}},
          {"ground", {.055, .07, .085}},
          {"sun", {{"direction", {.3, 1, .5}}, {"color", {1, .83, .6}}, {"intensity", 3}, {"angularRadius", .025}}}}},
        {"exposure", 1},
        {"toneMapping", "aces"},
        {"bloom", {{"strength", .12}, {"threshold", 1}, {"radius", 3}}}};
}
void validateAtmosphere(const Json &profile) {
    // Reuse the exact offline world schema, including closed fields and numeric bounds.
    Json manifest = {
        {"formatVersion", "0.3"},
        {"id", "com.hyprune.validation"},
        {"version", "0.1.0"},
        {"name", "Validation"},
        {"license", "CC0-1.0"},
        {"scene", "scene.gltf"},
        {"defaultSpawn", "arrival"},
        {"spawns", Json::array({{{"id", "arrival"}, {"pose", {{"position", {0, 0, 0}}, {"rotation", {0, 0, 0, 1}}}}}})},
        {"attribution", Json::array({{{"path", "."}, {"author", "Hyprune"}, {"license", "CC0-1.0"}}})},
        {"atmosphere", profile}};
    validateManifest(manifest);
    auto d = profile.at("sky").at("sun").at("direction");
    need(std::hypot(d[0].get<double>(), d[1].get<double>(), d[2].get<double>()) > .001, "sun direction is zero");
    for (auto group : {profile.at("fog").at("color"), profile.at("sky").at("zenith"), profile.at("sky").at("horizon"),
                       profile.at("sky").at("ground"), profile.at("sky").at("sun").at("color")})
        for (auto v : group)
            need(v.get<double>() >= 0 && v.get<double>() <= 10000, "invalid radiance");
}
Spawn areaSpawn(const World &w, const std::string &id) {
    auto area = std::find_if(w.areas.begin(), w.areas.end(), [&](auto &a) { return a.at("id") == id; });
    need(area != w.areas.end(), "bound area missing after world update");
    if (area->contains("spawnId"))
        return w.spawn(area->at("spawnId"));
    auto anchor = std::find_if(w.anchors.begin(), w.anchors.end(),
                               [&](auto &a) { return a.at("id") == area->at("primaryAnchor"); });
    need(anchor != w.anchors.end(), "primary mount missing");
    auto q = anchor->at("pose").at("rotation").get<std::array<float, 4>>();
    auto normal = rotate(q, {0, 0, 1});
    auto p = anchor->at("pose").at("position");
    Vec3 eye{p[0], p[1], p[2]};
    eye += normal * std::max(16.f, anchor->at("size")[0].get<float>() * .55f);
    auto forward = normal * -1;
    return {id, eye - Vec3{0, 1.7f, 0}, std::atan2(forward.x, -forward.z), std::asin(std::clamp(forward.y, -1.f, 1.f))};
}
void loadMounts(World &w, size_t nodes) {
    auto &j = w.manifest;
    w.areas = j.value("areas", Json::array());
    w.placementZones = j.value("placementZones", Json::array());
    auto originals = j.value("anchors", Json::array());
    std::map<std::string, Json> anchors;
    for (auto a : originals) {
        need(anchors.emplace(a.at("id"), a).second, "duplicate anchor");
        pose(a.at("pose"));
    }
    for (auto o : j.value("dataObjects", Json::array())) {
        pose(o.at("pose"));
        need(o.at("mount").at("role") == "data-object" && o.at("mount").at("interaction") == "none",
             "data object role/interaction mismatch");
        if (o.contains("node"))
            need(o.at("node").get<size_t>() < nodes, "unknown data object node");
        if (o.at("mount").contains("frameNode"))
            need(o.at("mount").at("frameNode").get<size_t>() < nodes, "unknown object frame node");
        if (o.contains("node"))
            w.objectNodes[o.at("id")] = o.at("node");
        w.objects.push_back({{"id", o.at("id")},
                             {"areaId", o.at("areaId")},
                             {"pose", o.at("pose")},
                             {"mountType", o.at("mount").at("type")},
                             {"sourceId", nullptr}});
    }
    auto ext = j.value("extensions", Json::object());
    if (ext.contains("com.hyprune.lumen-reach.placement")) {
        auto p = ext.at("com.hyprune.lumen-reach.placement");
        need(p.at("version") == 1, "placement extension version");
        w.placementZones = p.at("zones");
    }
    if (ext.contains("com.hyprune.lumen-reach.mounts")) {
        need(w.areas.empty(), "cannot mix canonical areas and legacy mounts extension");
        auto e = ext.at("com.hyprune.lumen-reach.mounts");
        need(e.at("version") == 1, "unsupported mounts extension version");
        w.areas = e.at("areas");
        for (auto &a : w.areas) {
            a.erase("name");
            a.erase("approachDepth");
            a.erase("primaryApproachDepth");
            a.erase("toolApproachDepth");
        }
        for (auto m : e.at("mounts")) {
            auto id = m.at("anchorId").get<std::string>();
            need(anchors.contains(id), "unknown extension anchor");
            std::string r = m.at("role");
            r = r == "tool" ? "tool-mount" : r == "data" ? "data-object" : r;
            Json mount = {{"type", m.at("mountType")},
                          {"role", r},
                          {"interaction", r == "mounted-workspace" ? "focus-in"
                                          : r == "tool-mount"      ? "in-place"
                                                                   : "none"}};
            if (m.contains("frameNode"))
                mount["frameNode"] = m["frameNode"];
            if (m.contains("limits"))
                mount["limits"] = m["limits"];
            anchors[id]["mount"] = mount;
            anchors[id]["areaId"] = m.at("areaId");
            auto area =
                std::find_if(w.areas.begin(), w.areas.end(), [&](auto &a) { return a.at("id") == m.at("areaId"); });
            need(area != w.areas.end() && area->at("workspaceSlot") == m.at("workspaceSlot"),
                 "mount slot/area mismatch");
        }
        for (auto o : e.value("objects", Json::array())) {
            pose(o.at("pose"));
            need(types.contains(o.at("mountType")), "unknown object mount type");
            if (o.contains("node"))
                need(o.at("node").get<size_t>() < nodes, "unknown object node");
            auto area =
                std::find_if(w.areas.begin(), w.areas.end(), [&](auto &a) { return a.at("id") == o.at("areaId"); });
            need(area != w.areas.end() && area->at("workspaceSlot") == o.at("workspaceSlot"),
                 "object slot/area mismatch");
            if (o.contains("node"))
                w.objectNodes[o.at("id")] = o.at("node");
            w.objects.push_back({{"id", o.at("id")},
                                 {"areaId", o.at("areaId")},
                                 {"pose", o.at("pose")},
                                 {"mountType", o.at("mountType")},
                                 {"sourceId", nullptr}});
        }
    }
    need(w.areas.is_array() && w.areas.size() <= 32 && anchors.size() <= 128 && w.objects.size() <= 128,
         "mount metadata budget");
    std::set<std::string> areaIds, slots, owned, objectIds;
    for (auto &o : w.objects)
        need(objectIds.insert(o.at("id")).second && !anchors.contains(o.at("id")), "duplicate data identity");
    for (auto a : w.areas) {
        need(areaIds.insert(a.at("id")).second && slots.insert(a.at("workspaceSlot")).second, "duplicate area/slot");
        auto check = [&](const Json &id, const std::string &role) {
            need(owned.insert(id).second, "anchor belongs to multiple areas");
            if (role == "data-object" && objectIds.contains(id)) {
                auto it = std::find_if(w.objects.begin(), w.objects.end(), [&](auto &o) { return o.at("id") == id; });
                need(it->at("areaId") == a.at("id"), "object area mismatch");
                return;
            }
            need(anchors.contains(id), "area references missing anchor");
            auto &anchor = anchors.at(id);
            need(anchor.contains("mount") && anchor.at("mount").at("role") == role, "area anchor role mismatch");
            if (anchor.contains("areaId"))
                need(anchor["areaId"] == a["id"], "anchor area mismatch");
            anchor["areaId"] = a["id"];
        };
        check(a.at("primaryAnchor"), "mounted-workspace");
        for (auto id : a.at("toolAnchors"))
            check(id, "tool-mount");
        for (auto id : a.at("dataAnchors"))
            check(id, "data-object");
        if (a.contains("spawnId"))
            w.spawn(a.at("spawnId"));
    }
    // Validate the normalized extension with the canonical 0.3 structural contract.
    auto normalized = j;
    normalized["placementZones"] = w.placementZones;
    std::set<std::string> zoneIds;
    for (auto &zone : w.placementZones) {
        need(zoneIds.insert(zone.at("id")).second, "duplicate placement zone");
        for (int k = 0; k < 3; k++)
            need(zone["bounds"]["min"][k].get<double>() < zone["bounds"]["max"][k].get<double>(),
                 "inverted placement zone");
    }
    for (auto &a : w.areas)
        for (auto &id : a.value("freePlacementZones", Json::array())) {
            need(id.is_string(), "areas.freePlacementZones must contain zone IDs, not objects");
            need(zoneIds.contains(id), "area references unknown placement zone");
        }
    normalized["formatVersion"] = "0.3";
    normalized["areas"] = w.areas;
    normalized["anchors"] = Json::array();
    for (auto &[id, a] : anchors) {
        if (a.contains("mount")) {
            need(owned.contains(id), "typed anchor has no area owner");
            auto m = a["mount"];
            need(types.contains(m.at("type")), "unknown mount type");
            std::string role = m.at("role"), mode = m.at("interaction");
            need(mode == (role == "mounted-workspace" ? "focus-in"
                          : role == "tool-mount"      ? "in-place"
                                                      : "none"),
                 "mount interaction/role mismatch");
            if (m.contains("frameNode"))
                need(m.at("frameNode").get<size_t>() < nodes, "unknown physical frame node");
            if (m.contains("limits")) {
                auto l = m["limits"];
                need(l["minAspect"].get<double>() <= l["maxAspect"].get<double>(), "inverted aspect limits");
                for (int k = 0; k < 2; k++)
                    need(l["minSize"][k].get<double>() <= l["maxSize"][k].get<double>(), "inverted size limits");
            }
            auto wire = a;
            wire.erase("screenType");
            wire.erase("node");
            w.anchors.push_back(wire);
            if (role == "data-object" && a.contains("node"))
                w.objectNodes[id] = a.at("node");
            if (role == "data-object")
                w.objects.push_back({{"id", id},
                                     {"areaId", a["areaId"]},
                                     {"pose", a["pose"]},
                                     {"mountType", m["type"]},
                                     {"sourceId", nullptr}});
        }
        a.erase("areaId");
        normalized["anchors"].push_back(a);
    }
    validateManifest(normalized);
    for (const auto &area : w.areas) {
        auto spawn = areaSpawn(w, area.at("id"));
        sim::Camera camera;
        camera.position = spawn.position + Vec3{0, 1.7f, 0};
        auto before = camera.position;
        sim::resolveCapsule(camera, w.collision);
        need(length(camera.position - before) < .015f, "area arrival capsule obstructed");
    }
    need(w.areas.dump().size() + w.anchors.dump().size() + w.objects.dump().size() <= 65536,
         "mount metadata IPC budget");
}
} // namespace hyprune::world
