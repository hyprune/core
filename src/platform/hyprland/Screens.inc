// Workspace screens (IPC 0.27, object format 0.11; RFC-0011 amendment "Workspace screens"):
// a user placement of an object whose mount trait has a mounted-workspace anchor becomes a
// workspace home while it is bound to a workspace. The home is injected into the loaded
// world after the package's own: an area "screen-<placement id>", its primary anchor (the
// object's anchor at the instance pose and scale) and an arrival spawn in front of it. The
// layout binds it like an authored default home, so the wall, the tiled home windows, focus
// and workspace travel work as they do for authored walls. Included by Plugin.cpp after
// Objects.inc; runs in the tick and in method handlers, never in the render pass.
namespace {
// The mounted-workspace anchor of an object manifest, or null.
const Json *screenAnchor(const Json &manifest) {
    if (!manifest.is_object() || !manifest.contains("traits") || !manifest["traits"].is_object() ||
        !manifest["traits"].contains("mount") || !manifest["traits"]["mount"].is_object())
        return nullptr;
    auto &mount = manifest["traits"]["mount"];
    if (!mount.contains("anchors") || !mount["anchors"].is_array())
        return nullptr;
    for (auto &a : mount["anchors"])
        if (a.is_object() && a.value("role", "") == "mounted-workspace" && a.contains("size") && a.contains("pose"))
            return &a;
    return nullptr;
}
std::string screenAreaId(const std::string &placementId) { return "screen-" + placementId; }
std::string workspaceSlot(int workspace) {
    auto s = std::to_string(workspace);
    return s.size() < 2 ? "0" + s : s;
}
} // namespace
Json Runtime::catalogScreen(const world::CatalogEntry &e) const {
    auto *a = screenAnchor(e.manifest);
    if (!a)
        return nullptr;
    double w = (*a)["size"][0], h = (*a)["size"][1];
    auto aspect = world::objects::screenAspect(w, h);
    if (aspect.empty())
        return nullptr;
    return {{"aspect", aspect}, {"size", {w, h}}, {"mountType", (*a)["mountType"]}};
}
Json Runtime::instanceScreen(const ObjectInfo &i) const {
    if (!world || i.source != "user" || !i.entry)
        return nullptr;
    auto screen = catalogScreen(*i.entry);
    if (screen.is_null())
        return nullptr;
    auto record = editor.placement(world->id, i.id);
    Json workspace = record.is_object() && record.contains("workspace") ? record["workspace"] : Json(nullptr);
    auto area = screenAreaId(i.id);
    bool home = !workspace.is_null() && layout.screenBound.contains(area);
    auto mm = [](double v) { return std::round(v * 10000) / 10000; }; // the scale is a float
    screen["size"] = {mm(screen["size"][0].get<double>() * i.scale), mm(screen["size"][1].get<double>() * i.scale)};
    return {{"workspace", workspace}, {"home", home ? Json(area) : Json(nullptr)}, {"aspect", screen["aspect"]}, {"size", screen["size"]}};
}
// Workspaces a screen may not take: the room, configured locations, explicit homes (any
// world), this world's authored homes and its other screens.
std::string Runtime::workspaceHolder(int id, const std::string &exceptPlacement) const {
    if (layout.reservedWorkspaces.contains(id))
        return id == config["windows"]["world_workspace"].get<int>() ? "the room workspace" : "a configured location";
    for (auto &b : layout.bindings)
        if (b.at("id") == id)
            return "the home of " + b.at("areaId").get<std::string>() + " in " + b.at("worldId").get<std::string>();
    if (world)
        for (auto &b : layout.defaults)
            if (b.at("id") == id && b.at("worldId") == world->id && b.at("areaId") != screenAreaId(exceptPlacement)) {
                std::string area = b.at("areaId");
                return area.starts_with("screen-u-") ? "another screen" : "the home of " + area;
            }
    if (world)
        for (auto &r : editor.placements)
            if (r["worldId"] == world->id && r["id"] != exceptPlacement && r.value("workspace", 0) == id)
                return "another screen";
    return "";
}
int Runtime::nextFreeWorkspace(const std::string &exceptPlacement) const {
    int base = std::max(0, config["windows"]["workspace_base"].get<int>());
    for (int id = base + 1; id <= 9999; ++id)
        if (workspaceHolder(id, exceptPlacement).empty())
            return id;
    return 0;
}
// A workspace param of objects.place or objects.configure: a number, "next-free" or null
// (0: none). Refuses a workspace another home holds.
int Runtime::screenWorkspace(const Json &value, const std::string &placementId) const {
    if (value.is_null())
        return 0;
    if (value == "next-free") {
        int id = nextFreeWorkspace(placementId);
        if (!id)
            throw ipc::Error(-32008, "no free workspace up to 9999", Json{{"retryable", false}, {"rule", "budget"}});
        return id;
    }
    if (!value.is_number_integer() || value.get<int64_t>() < 1 || value.get<int64_t>() > 9999)
        throw ipc::Error(-32602, "workspace must be 1-9999, \"next-free\" or null");
    int id = value.get<int>();
    if (auto holder = workspaceHolder(id, placementId); !holder.empty())
        throw ipc::Error(-32005, "Workspace " + std::to_string(id) + " is " + holder,
                         Json{{"retryable", false},
                              {"rule", layout.reservedWorkspaces.contains(id) ? "workspace.reserved" : "workspace.taken"}});
    return id;
}
// Rebuilds the injected homes from this world's screen placements. Placed screens that are
// drawn or still loading are homes; missing or blocked ones are not.
void Runtime::screensSync() {
    if (!world)
        return;
    std::vector<std::pair<int, std::string>> homes;
    Json areas = Json::array(), anchors = Json::array();
    std::vector<world::Spawn> spawns;
    for (auto &i : objectInfos) {
        if (i.source != "user" || !i.entry || (i.state != "active" && i.state != "loading"))
            continue;
        auto *a = screenAnchor(i.entry->manifest);
        auto record = editor.placement(world->id, i.id);
        if (!a || !record.is_object() || !record.contains("workspace") || !record["workspace"].is_number_integer())
            continue;
        int workspace = record["workspace"];
        auto area = screenAreaId(i.id);
        Vec3 local = vec3((*a)["pose"]["position"]);
        auto q = quaternionProduct(i.rotation, (*a)["pose"]["rotation"].get<std::array<float, 4>>());
        Vec3 centre = i.position + rotate(i.rotation, local * i.scale);
        auto mm = [](double v) { return std::round(v * 10000) / 10000; }; // the scale is a float
        double w = mm((*a)["size"][0].get<double>() * i.scale), h = mm((*a)["size"][1].get<double>() * i.scale);
        anchors.push_back({{"id", area},
                           {"pose", {{"position", array3(centre)}, {"rotation", q}}},
                           {"size", {w, h}},
                           {"mount", {{"type", (*a)["mountType"]}, {"role", "mounted-workspace"}, {"interaction", "focus-in"}}},
                           {"areaId", area}});
        // Arrival: the standback in front of the screen (max(2.5 m, 0.87 w), where it fills about
        // 60 degrees of view; RFC-0014), on the floor, with a clear standing capsule; nearer or
        // further spots when that one is blocked.
        Vec3 normal = rotate(q, {0, 0, 1}), flat = normalized(Vec3{normal.x, 0, normal.z});
        world::Spawn spawn{area, centre - Vec3{0, 1.7f, 0}, 0, 0};
        if (length(Vec3{normal.x, 0, normal.z}) > .3f) {
            auto scene = placementScene();
            scene.ignore = {};
            float d = std::max(2.5f, .87f * float(w));
            bool found = false;
            for (float k : {1.f, .8f, 1.25f, .6f, 1.5f}) {
                Vec3 at = centre + flat * (d * k);
                Vec3 feet = at - Vec3{0, 1.7f, 0};
                if (!world->collision.empty()) {
                    auto floor = interaction::floorBelow(scene, Vec3{at.x, centre.y, at.z}, float(h) * .5f + 4);
                    if (!floor)
                        continue;
                    feet = {at.x, *floor, at.z};
                    sim::Camera test = camera;
                    test.position = feet + Vec3{0, 1.7f, 0};
                    sim::resolveCapsule(test, world->collision, &world->collisionIndex);
                    if (length(test.position - (feet + Vec3{0, 1.7f, 0})) > .02f)
                        continue;
                }
                Vec3 look = centre - (feet + Vec3{0, 1.7f, 0});
                spawn.position = feet;
                spawn.yaw = std::atan2(-flat.x, flat.z);
                spawn.pitch = std::asin(std::clamp(look.y / std::max(length(look), 1e-3f), -1.f, 1.f));
                found = true;
                break;
            }
            if (!found) {
                Vec3 at = centre + flat * d;
                spawn.position = at - Vec3{0, 1.7f, 0};
                spawn.yaw = std::atan2(-flat.x, flat.z);
            }
        }
        spawns.push_back(spawn);
        areas.push_back({{"id", area},
                         {"workspaceSlot", workspaceSlot(workspace)},
                         {"primaryAnchor", area},
                         {"toolAnchors", Json::array()},
                         {"dataAnchors", Json::array()},
                         {"theme", i.name.substr(0, 256)},
                         {"spawnId", area}});
        homes.push_back({workspace, area});
    }
    Json signature = {areas, anchors};
    for (auto &s : spawns)
        signature.push_back({s.position.x, s.position.y, s.position.z, s.yaw, s.pitch});
    auto key = std::hash<std::string>{}(signature.dump());
    if (key == screenSignature && layout.screenHomes == homes)
        return;
    screenSignature = key;
    size_t old = std::min({world->screenHomes, world->areas.size(), world->anchors.size(), world->spawns.size()});
    world->areas.erase(world->areas.end() - old, world->areas.end());
    world->anchors.erase(world->anchors.end() - old, world->anchors.end());
    world->spawns.resize(world->spawns.size() - old);
    world->screenHomes = areas.size();
    for (auto &a : areas)
        world->areas.push_back(a);
    for (auto &a : anchors)
        world->anchors.push_back(a);
    for (auto &s : spawns)
        world->spawns.push_back(s);
    layout.screenHomes = homes;
    layout.invalidateCollision();
    deriveHomes();
    for (auto &[id, area] : homes)
        if (!layout.screenBound.contains(area))
            objectNotice("screen.unbound", "Workspace " + std::to_string(id) + " already has a home here: the screen stays empty",
                         "unbound:" + area + std::to_string(id));
}
