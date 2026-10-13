// RFC-0011 P1 runtime (IPC 0.22): the object catalog (world package, installed
// libraries, the user folder), user placements and overrides of authored objects as
// world object instances, object pocket items for tool 5, and the objects.* and
// libraries.* methods. Included by Plugin.cpp after Gizmo.inc. Scans, installs and
// asset decoding run on worker threads; merging, uploads and collision happen in the
// tick, never in the render pass.
namespace {
namespace fs = std::filesystem;
constexpr size_t userPlacementLimit = 128, userAssetLimit = 32, userTriangleLimit = 400000, userCollisionLimit = 10000,
                 pocketObjectLimit = 16;
constexpr uint64_t userGpuLimit = 192ull << 20;
std::string poseKey(const Json &record) { return record.value("pose", Json()).dump() + record.value("scale", Json()).dump(); }
Json poseJson(const interaction::Transform &t) { return {{"position", array3(t.position)}, {"rotation", t.rotation}}; }
std::array<float, 4> yawRotation(float yaw) { return {0, std::sin(yaw * .5f), 0, std::cos(yaw * .5f)}; }
// Upright: only the turn about +Y is kept.
std::array<float, 4> upright(const std::array<float, 4> &q) {
    auto f = rotate(q, {0, 0, 1});
    return yawRotation(std::atan2(f.x, f.z));
}
std::array<float, 4> normalizedRotation(std::array<float, 4> q) {
    float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (!(l > 1e-6f))
        return {0, 0, 0, 1};
    for (auto &v : q)
        v = std::clamp(v / l, -1.f, 1.f);
    return q;
}
// movable.surfaces as the placement scene's support rule ("any", or a comma list).
std::string supportOf(const world::CatalogEntry &e) {
    if (!e.surfaces.is_array())
        return "";
    std::string out;
    for (auto &s : e.surfaces) {
        if (s == "any")
            return "any";
        out += (out.empty() ? "" : ",") + s.get<std::string>();
    }
    return out;
}
bool supports(const std::string &support, const char *surface) {
    return support == "any" || support.empty() || ("," + support + ",").find(std::string(",") + surface + ",") != std::string::npos;
}
// Rest bounds of an asset at a transform (uniform scale) as an oriented box.
interaction::Box assetBox(const world::ObjectAsset &a, const interaction::Transform &t) {
    interaction::Box b;
    auto m = world::instanceModel(t.position, t.rotation, t.scale.x);
    b.centre = m.point((a.low + a.high) * .5f);
    b.axes = {rotate(t.rotation, {1, 0, 0}), rotate(t.rotation, {0, 1, 0}), rotate(t.rotation, {0, 0, 1})};
    Vec3 half = (a.high - a.low) * .5f;
    b.half = half * t.scale.x;
    return b;
}
const char *ruleError(const std::string &rule) {
    return rule == "support" ? "support" : rule.empty() ? "walls" : rule.c_str();
}
ipc::Error refusal(const std::string &what, const interaction::PlacementVerdict &v) {
    return ipc::Error(-32007, what + ": " + (v.message.empty() ? v.rule : v.message),
                      Json{{"retryable", false}, {"rule", v.rule.empty() ? "walls" : v.rule}});
}
ipc::Error budgetError(const std::string &limit) {
    return ipc::Error(-32008, "Object budget: " + limit, Json{{"retryable", false}, {"rule", "budget"}});
}
} // namespace

// ---- catalog -----------------------------------------------------------------------
void Runtime::catalogInit() {
    catalogRoots = world::CatalogRoots::fromEnvironment();
    catalogWatch = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    catalogWatchDirs();
    catalogDirty = true;
    catalogQuiet = std::chrono::steady_clock::now();
}
// A parent-directory watch (like input.json's): the user folder and the user library
// root, or their nearest existing parent, plus every object directory and its
// subdirectories (an object's files).
void Runtime::catalogWatchDirs() {
    if (catalogWatch < 0)
        return;
    std::vector<fs::path> dirs;
    auto watch = [&](const fs::path &dir) {
        if (dirs.size() < 1024)
            dirs.push_back(dir);
    };
    for (auto &root : {catalogRoots.userObjects, catalogRoots.userLibraries}) {
        std::error_code ec;
        fs::path existing = root;
        while (existing.has_parent_path() && existing != existing.parent_path() && !fs::is_directory(existing, ec))
            existing = existing.parent_path();
        watch(existing);
        if (existing != root || root != catalogRoots.userObjects)
            continue;
        for (auto &d : fs::directory_iterator(root, ec))
            if (d.is_directory() && !d.is_symlink()) {
                watch(d.path());
                for (auto &sub : fs::directory_iterator(d.path(), ec))
                    if (sub.is_directory() && !sub.is_symlink())
                        watch(sub.path());
            }
    }
    // Watches change only with the directories: removing one queues IN_IGNORED, which
    // must not read as a change (a rescan after every scan, forever).
    std::vector<fs::path> current;
    for (auto &[wd, path] : catalogWatches)
        current.push_back(path);
    std::sort(dirs.begin(), dirs.end());
    std::sort(current.begin(), current.end());
    if (dirs == current)
        return;
    for (auto &[wd, path] : catalogWatches)
        inotify_rm_watch(catalogWatch, wd);
    catalogWatches.clear();
    constexpr uint32_t mask = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ATTRIB |
                              IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR;
    for (auto &dir : dirs)
        if (int wd = inotify_add_watch(catalogWatch, dir.c_str(), mask); wd >= 0)
            catalogWatches[wd] = dir;
}
void Runtime::catalogTick() {
    auto section = timed("catalog");
    auto now = std::chrono::steady_clock::now();
    if (catalogWatch >= 0) {
        alignas(inotify_event) char buffer[4096];
        bool changed = false;
        ssize_t n;
        while ((n = read(catalogWatch, buffer, sizeof buffer)) > 0)
            for (ssize_t at = 0; at + ssize_t(sizeof(inotify_event)) <= n;) {
                auto *e = reinterpret_cast<const inotify_event *>(buffer + at);
                changed = changed || !(e->mask & IN_IGNORED);
                at += sizeof(inotify_event) + e->len;
            }
        if (changed) {
            // Validation waits for a 1 s quiet period (an object being copied in).
            catalogDirty = true;
            catalogQuiet = now + std::chrono::seconds(1);
        }
    }
    if (catalogScan.valid() && catalogScan.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto snap = catalogScan.get();
            for (auto &e : snap.entries)
                if (e.source == "user" && e.state == "invalid" && !e.errors.empty())
                    objectNotice("catalog.invalid", "Object " + e.id + " is invalid: " + e.errors[0], "invalid:" + e.ref + e.digest + e.errors[0]);
            catalog = std::make_shared<const world::CatalogSnapshot>(std::move(snap));
            ++catalogRevision;
            objectsSync(); // instances point into the snapshot: never publish across a swap
            catalogWatchDirs();
        } catch (const std::exception &e) {
            std::fprintf(stderr, "[hyprune] catalog scan failed: %s\n", e.what());
        }
    }
    if (libraryInstall.valid() && libraryInstall.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto job = libraryInstall.get();
            catalog = std::make_shared<const world::CatalogSnapshot>(std::move(job.snapshot));
            ++catalogRevision;
            objectsSync();
            catalogWatchDirs();
            objectNotice("library.installed", "Library " + job.installed.id + " " + job.installed.version +
                                                  (job.installed.already ? " is already installed" : " installed"));
            publish();
            ipc->complete({{"revision", ipc->revision()}, {"id", job.installed.id}, {"version", job.installed.version}});
        } catch (const std::domain_error &e) {
            objectNotice("library.conflict", e.what());
            ipc->fail(-32007, e.what(), Json{{"retryable", false}, {"rule", "library.conflict"}});
        } catch (const std::invalid_argument &e) {
            objectNotice("library.invalid", e.what());
            ipc->fail(-32602, e.what());
        } catch (const std::exception &e) {
            ipc->fail(-32603, std::string("library install failed: ") + e.what());
        }
    }
    if (catalogDirty && !catalogScan.valid() && !libraryInstall.valid() && now >= catalogQuiet) {
        catalogDirty = false;
        auto roots = catalogRoots;
        auto *cache = &scanCache;
        catalogScan = std::async(std::launch::async, [roots, cache] { return world::scanCatalog(roots, *cache); });
    }
}
// Resolution (RFC-0011 "Catalog, resolution and id collisions"): package refs in the
// loaded world only; library refs exactly, or the highest installed version with the same
// major that has the object (substitute names it); user refs while ready.
const world::CatalogEntry *Runtime::catalogEntry(const std::string &ref, std::string *substitute) {
    auto r = world::parseRef(ref);
    if (!r)
        return nullptr;
    if (r->source == "package") {
        for (auto &e : packageCatalog)
            if (e.ref == ref)
                return &e;
        return nullptr;
    }
    if (auto it = catalog->refs.find(ref); it != catalog->refs.end())
        return &it->second;
    if (r->source != "library" || !substitute)
        return nullptr;
    auto majorOf = [](const std::string &v) { return v.substr(0, v.find('.')); };
    const world::CatalogEntry *best = nullptr;
    std::string bestVersion;
    for (auto &[key, e] : catalog->refs) {
        auto o = world::parseRef(key);
        if (!o || o->source != "library" || o->library != r->library || o->id != r->id || majorOf(o->version) != majorOf(r->version) ||
            e.state != "ready")
            continue;
        if (!best || world::versionLess(bestVersion, o->version))
            best = &e, bestVersion = o->version;
    }
    if (best)
        *substitute = best->ref;
    return best;
}
Json Runtime::catalogEntries() {
    Json out = Json::array();
    for (auto &e : packageCatalog)
        if (out.size() < 1024)
            out.push_back(e.json());
    for (auto &e : catalog->entries)
        if (out.size() < 1024)
            out.push_back(e.json());
    return out;
}
// ---- assets and instances ----------------------------------------------------------
// The world asset for a catalog entry: package assets are loaded with the world; library
// and user objects decode on a worker (-1 while loading, -2 when they failed).
int Runtime::assetFor(const world::CatalogEntry &e) {
    if (!world)
        return -2;
    if (e.source == "package") {
        auto assets = world->manifest.value("objectAssets", Json::array());
        for (size_t k = 0; k < world->objectAssets.size() && k < assets.size(); ++k)
            if (world->objectAssets[k].id == e.id)
                return int(k);
        return -2;
    }
    auto &slot = assetSlots[e.ref];
    if (slot.index >= 0 && slot.entry.digest == e.digest)
        return slot.index;
    if (slot.index >= 0 || !slot.error.empty()) {
        if (slot.entry.digest == e.digest)
            return slot.index >= 0 ? slot.index : -2;
        slot = {}; // the object changed on disk: load it again
    }
    if (!slot.load.valid()) {
        if (!compressedKnown)
            return -1;
        slot.entry = e;
        auto dir = e.directory;
        auto manifest = e.manifest;
        auto formats = compressedFormats;
        slot.load = std::async(std::launch::async, [dir, manifest, formats] { return world::loadObjectAsset(dir, manifest, {formats}); });
    }
    return -1;
}
Runtime::ObjectInfo *Runtime::objectInfo(const std::string &id) {
    for (auto &i : objectInfos)
        if (i.id == id)
            return &i;
    return nullptr;
}
Runtime::ObjectInfo *Runtime::selectedInfo() {
    if (editor.kind != "object" || !editor.selected.starts_with("object:"))
        return nullptr;
    return objectInfo(editor.selected.substr(7));
}
interaction::Transform Runtime::objectPose(const std::string &id) {
    interaction::Transform t;
    if (!world)
        return t;
    if (id.starts_with("object:")) {
        if (auto *i = objectInfo(id.substr(7))) {
            t.position = i->position;
            t.rotation = i->rotation;
            t.scale = {i->scale, i->scale, i->scale};
        }
        return t;
    }
    t.position = objectOrigin(*world, id);
    for (auto &o : editor.objects)
        if (o["worldId"] == world->id && o["objectId"] == id) {
            t.position = {o["position"][0], o["position"][1], o["position"][2]};
            t.rotation = o["rotation"].get<std::array<float, 4>>();
            t.scale = {o["scale"][0], o["scale"][1], o["scale"][2]};
            break;
        }
    return t;
}
std::string Runtime::areaAt(Vec3 p) const {
    auto zone = hud::zoneAt(world.get(), p);
    return zone.is_object() ? zone.at("id").get<std::string>() : "";
}
std::string Runtime::newPlacementId() const {
    static const char *digits = "0123456789abcdef";
    for (int attempt = 0; attempt < 64; ++attempt) {
        unsigned char bytes[3];
        if (getrandom(bytes, sizeof bytes, 0) != sizeof bytes)
            throw std::runtime_error("getrandom failed");
        std::string id = "u-";
        for (auto b : bytes)
            id += digits[b >> 4], id += digits[b & 15];
        bool taken = std::any_of(objectInfos.begin(), objectInfos.end(), [&](auto &i) { return i.id == id; }) ||
                     (world && !editor.placement(world->id, id).is_null());
        if (!taken)
            return id;
    }
    throw std::runtime_error("no free placement id");
}
void Runtime::objectNotice(const std::string &code, const std::string &message, const std::string &once) {
    if (!once.empty() && !objectNotices.insert(once).second)
        return;
    ipc->notify("runtime.notice", {{"level", code == "object.blocked" || code == "object.missing" || code == "library.conflict" ||
                                                     code == "library.invalid" || code == "catalog.invalid" || code == "machine.invalid"
                                                 ? "warning"
                                                 : "info"},
                                   {"code", code},
                                   {"message", message}});
}
void Runtime::objectRefused(const std::string &what, const interaction::PlacementVerdict &v) {
    ipc->notify("runtime.notice", {{"level", "info"}, {"code", "placement.blocked"}, {"message", what + ": " + v.message}});
    reticleFlash = {"invalid", "place", -1, -1};
    reticleFlashUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(700);
}
// Rebuilds the instance list from the authored objects and this world's overlay: poses,
// hidden and lock flags, user placements resolved against the catalog (missing, loading
// or blocked when they can't be drawn). Collision and lighting follow in toolsTick.
void Runtime::objectsSync() {
    auto section = timed("objects.sync");
    objectsDirty = false;
    if (!world) {
        objectInfos.clear();
        return;
    }
    // Each user instance keeps its committed proxy range until collision is rebuilt, so the
    // recheck below ignores the object's own proxy at its previous pose.
    std::map<std::string, std::pair<size_t, size_t>> ranges;
    std::map<std::string, world::ObjectInstance> previous; // their lighting, when the pose is unchanged
    for (size_t k = authoredInstances; k < world->objectInstances.size(); ++k) {
        ranges[world->objectInstances[k].id] = {world->objectInstances[k].collisionFirst, world->objectInstances[k].collisionCount};
        previous[world->objectInstances[k].id] = world->objectInstances[k];
    }
    world->objectInstances.resize(authoredInstances);
    std::vector<ObjectInfo> infos;
    for (size_t k = 0; k < authoredInstances; ++k) {
        auto &inst = world->objectInstances[k];
        ObjectInfo info;
        info.id = inst.id;
        info.source = "authored";
        info.ref = "package/" + world->objectAssets[inst.asset].id;
        info.name = world->objectAssets[inst.asset].id.substr(0, 64);
        info.index = int(k);
        info.position = inst.position;
        info.rotation = inst.rotation;
        info.scale = inst.authoredScale;
        info.areaId = areaAt(inst.position);
        // World 0.8 assets carry no traits, so authored objects stay render-only; the
        // development switch HYPRUNE_GIZMO_OBJECTS stands in for the movable trait. World 0.9
        // assets say so themselves; characters are never movable (RFC-0011).
        auto &traits = world->objectAssets[inst.asset].traits;
        info.character = inst.character;
        info.movable = !info.character && (gizmoObjects || (traits.is_object() && traits.contains("movable")));
        if (traits.is_object() && traits.contains("movable") && traits["movable"].contains("scale"))
            info.scaleMin = traits["movable"]["scale"][0], info.scaleMax = traits["movable"]["scale"][1];
        else if (traits.is_object() && traits.contains("movable"))
            info.scaleMin = info.scaleMax = inst.authoredScale;
        for (auto &o : world->manifest.value("objects", Json::array()))
            if (o["id"] == inst.id && o.value("locked", false))
                info.locked = true;
        auto o = editor.override(world->id, inst.id);
        if (o.is_object()) {
            if (o.contains("pose")) {
                info.position = vec3(o["pose"]["position"]);
                info.rotation = o["pose"]["rotation"].get<std::array<float, 4>>();
            }
            if (o.contains("scale"))
                info.scale = o["scale"];
            if (o.contains("areaId"))
                info.areaId = o["areaId"];
            info.locked = info.locked || o.value("locked", false);
            if (o.value("hidden", false))
                info.state = "hidden";
        }
        inst.hidden = info.state == "hidden";
        infos.push_back(info);
    }
    for (auto &r : editor.placements) {
        if (r["worldId"] != world->id)
            continue;
        ObjectInfo info;
        info.id = r["id"];
        info.source = "user";
        info.ref = r["ref"];
        info.areaId = r["areaId"];
        info.position = vec3(r["pose"]["position"]);
        info.rotation = r["pose"]["rotation"].get<std::array<float, 4>>();
        info.scale = r["scale"];
        info.locked = r.value("locked", false);
        std::string substitute;
        auto *e = catalogEntry(info.ref, &substitute);
        info.name = e ? e->name : info.ref.substr(info.ref.rfind('/') + 1).substr(0, 64);
        if (info.name.empty())
            info.name = info.id;
        if (e && !substitute.empty())
            objectNotice("object.substituted", info.ref + " is not installed; " + info.id + " uses " + substitute, "sub:" + info.id + substitute);
        if (!e || e->state != "ready" || !e->placeable) {
            info.state = "missing";
            if (catalogRevision > 0)
                objectNotice("object.missing", info.name + " (" + info.ref + ") is not available", "missing:" + info.id + info.ref);
            infos.push_back(info);
            continue;
        }
        info.entry = e;
        info.character = e->has("character");
        info.movable = !info.character;
        info.support = supportOf(*e);
        if (e->scale.is_array())
            info.scaleMin = e->scale[0], info.scaleMax = e->scale[1];
        else
            info.scaleMin = info.scaleMax = 1;
        int asset = assetFor(*e);
        if (asset < 0) {
            info.state = asset == -1 ? "loading" : "missing";
            infos.push_back(info);
            continue;
        }
        world::ObjectInstance inst;
        inst.id = info.id;
        inst.asset = unsigned(asset);
        inst.position = info.position;
        inst.rotation = info.rotation;
        inst.authoredScale = inst.scale = info.scale;
        inst.model = world::instanceModel(info.position, info.rotation, info.scale);
        auto &a = world->objectAssets[asset];
        inst.center = inst.model.point(a.center);
        inst.radius = a.radius * info.scale;
        info.index = int(world->objectInstances.size());
        if (auto range = ranges.find(info.id); range != ranges.end())
            inst.collisionFirst = range->second.first, inst.collisionCount = range->second.second;
        if (auto old = previous.find(info.id); old != previous.end() && old->second.asset == inst.asset && old->second.model == inst.model) {
            inst.ambientUp = old->second.ambientUp, inst.ambientDown = old->second.ambientDown;
            inst.sun = old->second.sun, inst.sunVisibility = old->second.sunVisibility, inst.regions = old->second.regions;
        }
        inst.character = info.character;
        inst.bound = info.character && r.contains("path");
        if (inst.character && r.value("disabled", false))
            info.state = "hidden", inst.hidden = true;
        world->objectInstances.push_back(inst);
        // Characters are checked by their route or standing spot (Npcs.inc), not as boxes.
        if (info.character) {
            infos.push_back(info);
            continue;
        }
        // Rechecked against the current geometry once per pose: at load, after a package
        // update or a catalog change. Never relocated: a failing one is blocked.
        auto key = poseKey(r);
        auto verified = objectVerified.find(info.id);
        if (verified == objectVerified.end() || verified->second.substr(0, key.size()) != key) {
            interaction::Transform t{info.position, {info.scale, info.scale, info.scale}, info.rotation};
            auto v = objectVerdict(asset, t, info.support, size_t(info.index), {}, false);
            objectVerified[info.id] = key + "\n" + (v.ok ? "" : v.rule);
            if (!v.ok)
                objectNotice("object.blocked", info.name + " breaks a placement rule here (" + v.rule + "): move or remove it",
                             "blocked:" + info.id + key);
        }
        auto rule = objectVerified[info.id].substr(key.size() + 1);
        if (!rule.empty()) {
            info.state = "blocked";
            info.rule = rule;
            world->objectInstances.back().hidden = true;
        }
        infos.push_back(info);
    }
    companionInject(infos);
    objectInfos = std::move(infos);
    // Collision, visibility and frames follow only a real change (a catalog rescan that
    // resolves the same objects changes nothing).
    uint64_t signature = 1469598103934665603ull;
    auto mix = [&](const void *data, size_t size) {
        for (size_t i = 0; i < size; ++i)
            signature = (signature ^ static_cast<const unsigned char *>(data)[i]) * 1099511628211ull;
    };
    for (auto &inst : world->objectInstances) {
        mix(inst.id.data(), inst.id.size());
        mix(&inst.asset, sizeof inst.asset);
        mix(inst.model.v.data(), sizeof inst.model.v);
        mix(&inst.hidden, sizeof inst.hidden);
    }
    for (auto &i : objectInfos)
        mix(i.state.data(), i.state.size()), mix(&i.locked, sizeof i.locked);
    if (signature == instanceSignature)
        return;
    instanceSignature = signature;
    ++instanceRevision;
    if (editor.kind == "object" && editor.selected.starts_with("object:")) {
        auto *i = selectedInfo();
        if (!i || i->index < 0 || i->state != "active") {
            cancelGizmo();
            editor.selected.clear();
            editor.kind.clear();
            objectCarry = false;
        }
    }
    ++objectRevision;
}
// Per-world state when a world becomes current: authored instances and the collision
// they start from, the package catalog; pocketed objects are dropped (RFC-0011).
void Runtime::objectsWorldLoaded() {
    authoredInstances = world->objectInstances.size();
    packageAssets = world->objectAssets.size();
    mergedAssets.clear();
    ++worldLoads;
    sceneCollision = world->collision.size();
    for (auto &inst : world->objectInstances)
        if (inst.collisionCount)
            sceneCollision = std::min(sceneCollision, inst.collisionFirst);
    originalCollisionNodes = world->collisionNodes;
    assetSlots.clear();
    objectVerified.clear();
    instanceSignature = 0;
    pocketObjects.clear();
    objectGhost = {};
    // Authored instances were lit at load; user ones get their probe when collision is built.
    instanceOverrides.clear();
    for (size_t k = 0; k < authoredInstances; ++k)
        instanceOverrides[k] = world->objectInstances[k].model;
    packageCatalog = world::packageEntries(*world, installed / world->id, gizmoObjects);
    ++catalogRevision;
    objectsSync(); // before anything publishes: instances point into the package catalog
    builtObjects = ~0ull;
}
void Runtime::relight(size_t k) {
    if (!world || k >= world->objectInstances.size())
        return;
    world::objectRegions(*world, k); // culling needs them now
    auto &inst = world->objectInstances[k];
    if (world->objectAssets[inst.asset].unlit)
        return;
    std::erase_if(relights, [&](auto &j) { return j.id == inst.id && !j.light.valid(); });
    auto w = world;
    auto copy = inst;
    relights.push_back({world.get(), inst.id, inst.model,
                        std::async(std::launch::async, [w, copy] { return world::probeLight(*w, copy); })});
}
void Runtime::relightTick() {
    for (auto it = relights.begin(); it != relights.end();) {
        if (it->light.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }
        auto light = it->light.get();
        if (world && it->world == world.get())
            for (auto &inst : world->objectInstances)
                if (inst.id == it->id && inst.model == it->model) {
                    world::applyProbeLight(inst, light);
                    ++objectRevision; // a frame with the new light
                }
        it = relights.erase(it);
    }
}
void Runtime::joinRelights() {
    for (auto &j : relights)
        if (j.light.valid())
            j.light.wait();
    relightTick();
}
void Runtime::objectsTick() {
    relightTick();
    catalogTick();
    if (!world || loading)
        return;
    if (!compressedKnown) {
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        compressedFormats = render::Renderer::compressedFormats();
        compressedKnown = true;
        objectsDirty = true;
    }
    // A finished decode merges into the world while no other object upload is pending.
    for (auto &[ref, slot] : assetSlots)
        if (slot.load.valid() && slot.load.wait_for(std::chrono::seconds(0)) == std::future_status::ready &&
            !renderer.objectAssetsPending()) {
            joinRelights(); // merging appends materials and images a probe reads
            try {
                auto section = timed("objects.merge");
                auto scratch = slot.load.get();
                slot.index = int(world::mergeObjectAsset(*world, *scratch));
                mergedAssets.push_back({slot.entry.directory, slot.entry.manifest});
            } catch (const std::exception &e) {
                slot.error = e.what();
                objectNotice("object.missing", "Can't load " + slot.entry.name + ": " + e.what(), "load:" + ref + slot.entry.digest);
            }
            objectsDirty = true;
            break;
        }
    Render::GL::g_pHyprOpenGL->makeEGLCurrent();
    {
        auto section = timed("objects.upload");
        if (!renderer.objectAssetsPending())
            renderer.stageObjectAssets();
        renderer.continueObjectAssets();
    }
    if (objectsDirty)
        objectsSync();
}
// The placement rules (RFC-0011) for an asset at a transform: its bounds box and its
// collision proxy at that pose; self excludes an instance's own committed proxy.
interaction::PlacementVerdict Runtime::objectVerdict(int asset, const interaction::Transform &t, const std::string &support,
                                                     std::optional<size_t> self, const std::set<std::string> &waived,
                                                     bool player) {
    if (!world || asset < 0 || size_t(asset) >= world->objectAssets.size())
        return {false, "walls", "Not loaded", false};
    auto scene = placementScene();
    scene.ignore = {};
    if (self && *self < world->objectInstances.size()) {
        size_t first = world->objectInstances[*self].collisionFirst, count = world->objectInstances[*self].collisionCount;
        scene.ignore = [first, count](unsigned id) { return id >= first && id < first + count; };
    }
    scene.support = support;
    scene.player = player;
    auto &a = world->objectAssets[asset];
    auto m = world::instanceModel(t.position, t.rotation, t.scale.x);
    std::vector<Triangle> own;
    for (auto &tri : a.collision)
        own.push_back({m.point(tri.a), m.point(tri.b), m.point(tri.c)});
    return interaction::checkPlacement(scene, assetBox(a, t), own, waived);
}
// RFC-0011 "Budgets and limits" for user placements in this world (exceptId: a placement
// being replaced, e.g. re-placed from the pocket). Returns the limit a new one would break.
std::optional<std::string> Runtime::budgetRefusal(const world::CatalogEntry &e, const std::string &exceptId) {
    if (!world)
        return std::nullopt;
    size_t count = 0;
    uint64_t triangles = e.triangles.is_number() ? e.triangles.get<uint64_t>() : 0,
             collision = e.collisionTriangles.is_number() ? e.collisionTriangles.get<uint64_t>() : 0;
    std::set<std::string> refs{e.ref};
    std::map<std::string, uint64_t> gpu{{e.ref, e.gpuBytes.is_number() ? e.gpuBytes.get<uint64_t>() : 0}};
    for (auto &i : objectInfos) {
        if (i.source != "user" || i.id == exceptId)
            continue;
        ++count;
        refs.insert(i.ref);
        if (i.entry) {
            triangles += i.entry->triangles.is_number() ? i.entry->triangles.get<uint64_t>() : 0;
            collision += i.entry->collisionTriangles.is_number() ? i.entry->collisionTriangles.get<uint64_t>() : 0;
            gpu[i.ref] = i.entry->gpuBytes.is_number() ? i.entry->gpuBytes.get<uint64_t>() : 0;
        }
    }
    uint64_t pool = 0;
    for (auto &[ref, bytes] : gpu)
        pool += bytes;
    if (count + 1 > userPlacementLimit)
        return "user placements per world (128)";
    if (refs.size() > userAssetLimit)
        return "distinct assets used by user placements (32)";
    if (triangles > userTriangleLimit)
        return "triangles drawn by user placements (400,000)";
    if (pool > userGpuLimit)
        return "GPU pool for user-placed assets (192 MiB)";
    if (collision > userCollisionLimit)
        return "collision proxy triangles of user placements (10,000)";
    return std::nullopt;
}
// Tool 5's pose for a catalog object under the aim. Ahead (Primary): front toward the
// player, upright; floor objects drop to the floor under the aim point. Surface
// (Secondary): on the aimed surface, if the object may sit on that kind of surface.
// Either way the bounds touch the surface (asset conventions put the origin there).
std::optional<interaction::Transform> Runtime::placeAhead(const world::CatalogEntry &e, int asset, bool surface,
                                                          float *distance, std::string *why) {
    if (!world || asset < 0)
        return std::nullopt;
    auto &a = world->objectAssets[asset];
    // Objects rest on collision: the indexed collision ray (every tick while tool 5 aims),
    // the rendered meshes only in worlds without collision.
    auto hit = world->collision.empty()
                   ? worldHit()
                   : interaction::raycast(pointerCamera().position, pointerRay(), world->collision, &world->collisionIndex);
    if (!hit) {
        *why = "Aim at a surface";
        return std::nullopt;
    }
    *distance = hit->distance;
    auto ray = pointerRay();
    auto support = supportOf(e);
    interaction::Transform t;
    t.rotation = yawRotation(std::atan2(-ray.x, -ray.z));
    auto n = dot(hit->normal, ray) > 0 ? hit->normal * -1.f : hit->normal;
    // Origin where the bounds touch a surface with normal n (5 mm off it).
    auto touching = [&](Vec3 point, Vec3 normal) {
        t.position = {};
        auto box = assetBox(a, t);
        return point + normal * (box.extent(normal) + .005f) - box.centre;
    };
    auto scene = placementScene();
    scene.ignore = {};
    if (surface || (!supports(support, "floor") && support != "any")) {
        if (n.y >= .65f) {
            if (!supports(support, "floor")) {
                *why = "Place this object on a wall";
                return std::nullopt;
            }
        } else if (n.y <= -.65f) {
            if (!supports(support, "ceiling")) {
                *why = "This object doesn't hang from ceilings";
                return std::nullopt;
            }
        } else {
            if (!supports(support, "wall")) {
                *why = "This object needs an upward surface";
                return std::nullopt;
            }
            t.rotation = yawRotation(std::atan2(n.x, n.z)); // its front (+Z) along the wall normal
        }
        t.position = touching(hit->point, n);
        return t;
    }
    // Ahead: back off the aimed surface so the object fits, then drop to the floor.
    Vec3 flat = normalized(Vec3{ray.x, 0, ray.z});
    if (length(flat) < .5f)
        flat = {0, 0, -1};
    Vec3 at = hit->point;
    if (n.y < .65f) {
        t.position = {};
        at = at - flat * (assetBox(a, t).extent(flat) + .02f);
    }
    if (auto floor = interaction::floorBelow(scene, at + Vec3{0, .35f, 0}, 2.35f)) {
        t.position = touching({at.x, *floor, at.z}, {0, 1, 0});
        return t;
    }
    if (support == "any") {
        t.position = touching(at, n);
        return t;
    }
    *why = "No floor under the aim";
    return std::nullopt;
}
bool Runtime::pocketSelected() const {
    return std::any_of(pocketObjects.begin(), pocketObjects.end(), [&](auto &p) { return p.itemId == unplacedSelected; });
}
// Ghost for the selected pocket object (tool 5), recomputed each tick from the aim.
void Runtime::objectGhostTick() {
    auto section = timed("tools.ghost");
    objectGhost = {};
    if (!world || editor.slot != 5 || freeCursor || !active || !pocketSelected())
        return;
    auto item = std::find_if(pocketObjects.begin(), pocketObjects.end(), [&](auto &p) { return p.itemId == unplacedSelected; });
    auto *e = catalogEntry(item->ref);
    if (!e)
        return;
    int asset = assetFor(*e);
    objectGhost.active = true;
    objectGhost.ref = item->ref;
    objectGhost.asset = asset;
    if (asset < 0) {
        objectGhost.message = asset == -1 ? "Loading " + e->name : "Can't load " + e->name;
        return;
    }
    std::string why;
    auto t = placeAhead(*e, asset, false, &objectGhost.distance, &why);
    if (!t) {
        objectGhost.message = why;
        objectGhost.rule = "support";
        return;
    }
    objectGhost.pose = *t;
    if (objectGhost.distance > config["player"]["reach_placement"].get<float>()) {
        objectGhost.rule = "reach";
        objectGhost.message = tooFarText(objectGhost.distance);
        return;
    }
    auto v = objectVerdict(asset, *t, supportOf(*e), std::nullopt);
    objectGhost.valid = v.ok;
    objectGhost.rule = v.rule;
    objectGhost.message = v.message;
}
// The ghost: the asset's bounds, translucent, green where it would be placed, red where a
// rule refuses it, with the asset's feature edges.
void Runtime::objectGhostDraw(interaction::GizmoFrame &frame, const interaction::GizmoView &view) {
    if (!objectGhost.active || objectGhost.asset < 0 || !world || (objectGhost.rule == "support" && length(objectGhost.pose.position) == 0))
        return;
    auto &a = world->objectAssets[objectGhost.asset];
    auto box = assetBox(a, objectGhost.pose);
    using interaction::Rgba;
    Rgba edge = objectGhost.valid ? Rgba{.43f, .95f, .69f, .9f} : Rgba{1, .27f, .24f, .9f};
    std::array<Vec3, 8> p;
    for (int i = 0; i < 8; ++i)
        p[i] = box.centre + box.axes[0] * ((i & 1) ? box.half.x : -box.half.x) + box.axes[1] * ((i & 2) ? box.half.y : -box.half.y) +
               box.axes[2] * ((i & 4) ? box.half.z : -box.half.z);
    unsigned sequence = frame.next();
    for (int axis = 0; axis < 3; ++axis)
        for (int side = 0; side < 2; ++side) {
            int bit = 1 << axis, u = 1 << ((axis + 1) % 3), w = 1 << ((axis + 2) % 3), base = side ? bit : 0;
            interaction::GizmoFill f{{}, {edge.r, edge.g, edge.b, .16f}, false, sequence++};
            for (int c : {base, base | u, base | u | w, base | w})
                if (auto s = view.project(p[c]))
                    f.points.push_back(*s);
            if (f.points.size() == 4)
                frame.fills.push_back(f);
        }
    for (int i = 0; i < 8; ++i)
        for (int bit : {1, 2, 4})
            if (!(i & bit))
                if (auto x = view.project(p[i]), y = view.project(p[i | bit]); x && y)
                    frame.strokes.push_back({*x, *y, edge, 1.6f * view.dpi, false, false, 0, sequence++});
    // Feature edges of the asset's meshes (boundaries and creases over 40 degrees), cached per asset.
    static std::map<std::pair<const world::World *, int>, std::vector<std::pair<Vec3, Vec3>>> cache;
    auto key = std::pair{world.get(), objectGhost.asset};
    if (!cache.contains(key)) {
        std::map<std::array<int64_t, 6>, std::vector<Vec3>> normals;
        std::map<std::array<int64_t, 6>, std::pair<Vec3, Vec3>> ends;
        auto q = [](Vec3 v) { return std::array<int64_t, 3>{std::llround(v.x * 1000), std::llround(v.y * 1000), std::llround(v.z * 1000)}; };
        size_t triangles = 0;
        for (auto &mesh : a.meshes)
            for (size_t i = 0; i + 2 < mesh.vertices.size() && triangles < 20000; i += 3, ++triangles) {
                Triangle tri{mesh.vertices[i].position, mesh.vertices[i + 1].position, mesh.vertices[i + 2].position};
                auto nn = normalized(cross(tri.b - tri.a, tri.c - tri.a));
                for (auto [x, y] : {std::pair{tri.a, tri.b}, {tri.b, tri.c}, {tri.c, tri.a}}) {
                    auto kx = q(x), ky = q(y);
                    if (ky < kx)
                        std::swap(kx, ky), std::swap(x, y);
                    std::array<int64_t, 6> k{kx[0], kx[1], kx[2], ky[0], ky[1], ky[2]};
                    normals[k].push_back(nn);
                    ends[k] = {x, y};
                }
            }
        auto &lines = cache[key];
        for (auto &[k, list] : normals) {
            bool feature = list.size() == 1;
            for (size_t i = 1; i < list.size() && !feature; ++i)
                feature = std::abs(dot(list[0], list[i])) < .766f;
            if (feature && lines.size() < 600)
                lines.push_back(ends[k]);
        }
    }
    auto m = world::instanceModel(objectGhost.pose.position, objectGhost.pose.rotation, objectGhost.pose.scale.x);
    Rgba line{edge.r, edge.g, edge.b, .55f};
    for (auto &[x, y] : cache[key])
        if (auto s = view.project(m.point(x)), t = view.project(m.point(y)); s && t)
            frame.strokes.push_back({*s, *t, line, 1.2f * view.dpi, false, false, 0, sequence++});
}
Json Runtime::placementRecord(const ObjectInfo &info, const interaction::Transform &t) {
    float scale = std::clamp(t.scale.x, info.scaleMin, info.scaleMax);
    if (info.source == "authored") {
        Json o = world ? editor.override(world->id, info.id) : Json(nullptr);
        if (!o.is_object())
            o = {{"worldId", world->id}, {"objectId", info.id}, {"areaId", info.areaId}};
        o["pose"] = poseJson({t.position, {}, normalizedRotation(t.rotation)});
        o["scale"] = scale;
        return o;
    }
    Json r = editor.placement(world->id, info.id);
    r["pose"] = poseJson({t.position, {}, normalizedRotation(t.rotation)});
    r["scale"] = scale;
    return r;
}
// One overlay edit (an authored override or a user placement record; null = none), with
// an undo record; the instances follow on the next sync.
void Runtime::objectEdit(const std::string &plainId, Json before, Json after) {
    if (!world)
        return;
    bool authored = std::any_of(world->objectInstances.begin(), world->objectInstances.begin() + std::min(authoredInstances, world->objectInstances.size()),
                                [&](auto &i) { return i.id == plainId; });
    if (authored)
        editor.setOverride(world->id, plainId, after);
    else
        editor.setPlacement(world->id, plainId, after);
    if (after.is_object() && !authored)
        objectVerified[plainId] = poseKey(after) + "\n"; // checked by the caller
    editor.recordEdit("object", world->id, "object:" + plainId, std::move(before), std::move(after));
    saveEditor();
    objectsSync();
}
void Runtime::scaleObject(const std::string &id, float steps) {
    auto *info = objectInfo(id);
    if (!info || info->index < 0 || !info->movable || info->character)
        return;
    if (info->locked) {
        objectNotice("object.locked", info->name + " is locked");
        return;
    }
    auto t = objectPose("object:" + id);
    float s = std::clamp(t.scale.x * std::pow(1.12f, steps), info->scaleMin, info->scaleMax);
    if (std::abs(s - t.scale.x) < 1e-5f)
        return;
    // Scale about the base: the object keeps standing where it stands.
    auto &a = world->objectAssets[world->objectInstances[info->index].asset];
    Vec3 base = t.position + rotate(t.rotation, Vec3{(a.low.x + a.high.x) * .5f, a.low.y, (a.low.z + a.high.z) * .5f}) * t.scale.x;
    Vec3 after = t.position + rotate(t.rotation, Vec3{(a.low.x + a.high.x) * .5f, a.low.y, (a.low.z + a.high.z) * .5f}) * s;
    t.position += base - after;
    t.scale = {s, s, s};
    auto start = objectPose("object:" + id);
    int asset = int(world->objectInstances[info->index].asset);
    auto v = objectVerdict(asset, t, info->support, size_t(info->index), objectWaived(asset, start, size_t(info->index)));
    if (!v.ok) {
        objectRefused("Scale refused", v);
        return;
    }
    auto before = info->source == "authored" ? editor.override(world->id, id) : editor.placement(world->id, id);
    objectEdit(id, before, placementRecord(*info, t));
}
// window.reset on an object: an authored one loses its override, a user one stands
// upright at scale 1 where it is.
void Runtime::resetObject(const std::string &id) {
    auto *info = objectInfo(id);
    if (!info || !world)
        return;
    if (info->source == "authored") {
        auto before = editor.override(world->id, id);
        if (!before.is_null())
            objectEdit(id, before, nullptr);
        return;
    }
    if (info->locked) {
        objectNotice("object.locked", info->name + " is locked");
        return;
    }
    if (info->index < 0)
        return;
    auto t = objectPose("object:" + id);
    t.rotation = upright(t.rotation);
    t.scale = {1, 1, 1};
    auto v = objectVerdict(int(world->objectInstances[info->index].asset), t, info->support, size_t(info->index));
    if (!v.ok) {
        objectRefused("Reset refused", v);
        return;
    }
    objectEdit(id, editor.placement(world->id, id), placementRecord(*info, t));
}
Json Runtime::pocketObjectsJson() const {
    Json out = Json::array();
    for (auto &p : pocketObjects)
        out.push_back({{"itemId", p.itemId}, {"ref", p.ref}, {"name", p.name}});
    return out;
}
// Tool 5 Primary/Secondary with an object selected in the pocket: place it (consumes it).
void Runtime::placeFromPocket(bool surface) {
    auto item = std::find_if(pocketObjects.begin(), pocketObjects.end(), [&](auto &p) { return p.itemId == unplacedSelected; });
    if (item == pocketObjects.end() || !world)
        return;
    auto *e = catalogEntry(item->ref);
    if (!e || !e->placeable) {
        objectNotice("object.missing", item->name + " is no longer available");
        return;
    }
    int asset = assetFor(*e);
    if (asset < 0) {
        objectNotice("placement.blocked", asset == -1 ? item->name + " is still loading" : "Can't load " + item->name);
        return;
    }
    float distance = -1;
    std::string why;
    auto t = placeAhead(*e, asset, surface, &distance, &why);
    if (!t) {
        objectRefused("Not placed", {false, "support", why, false});
        return;
    }
    if (tooFar(distance, "reach_placement"))
        return;
    if (auto limit = budgetRefusal(*e)) {
        objectNotice("object.budget", "Not placed: " + *limit);
        return;
    }
    auto v = objectVerdict(asset, *t, supportOf(*e), std::nullopt);
    if (!v.ok) {
        objectRefused("Not placed", v);
        return;
    }
    auto id = newPlacementId();
    Json record{{"worldId", world->id}, {"id", id}, {"areaId", areaAt(t->position)}, {"ref", e->ref},
                {"pose", poseJson(*t)}, {"scale", 1.0}};
    auto name = item->name;
    pocketObjects.erase(item);
    unplacedSelected = pocketObjects.empty() ? "" : pocketObjects.back().itemId;
    objectEdit(id, nullptr, record);
    objectNotice("object.placed", "Placed " + name);
}
// Tool 5 Alternate on a user placement: it leaves the world and becomes the selected
// pocket item (authored objects are hidden from the Director instead).
void Runtime::pocketAimedObject() {
    auto aimed = instanceAim();
    if (!aimed || !world)
        return;
    auto *info = objectInfo(aimed->first.substr(7));
    if (!info)
        return;
    if (info->character) {
        objectNotice("object.protected", "Characters can't be pocketed");
        return;
    }
    if (info->source != "user") {
        objectNotice("object.locked", "Authored objects stay in the world; hide them from the Director");
        return;
    }
    if (info->locked) {
        objectNotice("object.locked", info->name + " is locked");
        return;
    }
    if (tooFar(aimed->second.distance, "reach_placement", "pocket") || pocketObjects.size() >= pocketObjectLimit)
        return;
    auto ref = info->ref, name = info->name, id = info->id;
    pocketObjects.push_back({"pocket-" + std::to_string(++pocketSerial), ref, name});
    unplacedSelected = pocketObjects.back().itemId;
    objectEdit(id, editor.placement(world->id, id), nullptr);
    ipc->notify("runtime.notice", {{"level", "info"}, {"code", "placement.pocketed"}, {"message", "Pocketed " + name}});
}
Json Runtime::objectsState() {
    Json instances = Json::array();
    for (auto &i : objectInfos) {
        if (instances.size() >= 512)
            break;
        if (i.source == "companion") // its own state key (IPC 0.25)
            continue;
        auto q = normalizedRotation(i.rotation);
        auto clamp = [](float v) { return std::clamp(v, -100000.f, 100000.f); };
        instances.push_back({{"id", i.id},
                             {"source", i.source},
                             {"ref", i.ref.empty() ? Json(nullptr) : Json(i.ref)},
                             {"name", i.name.empty() ? i.id.substr(0, 64) : i.name.substr(0, 64)},
                             {"areaId", i.areaId.substr(0, 128)},
                             {"pose", {{"position", {clamp(i.position.x), clamp(i.position.y), clamp(i.position.z)}}, {"rotation", q}}},
                             {"scale", std::clamp(i.scale, .01f, 100.f)},
                             {"state", i.state},
                             {"movable", i.movable && !i.character},
                             {"locked", i.locked},
                             {"rule", i.rule.empty() ? Json(nullptr) : Json(i.rule)},
                             {"path", nullptr},
                             {"activity", nullptr},
                             {"seat", nullptr},
                             {"character", i.character},
                             {"accent", nullptr}});
        // IPC 0.23: a character's route, what it is doing and the seat it holds.
        if (!i.character || !world)
            continue;
        auto &rec = instances.back();
        if (auto *n = npcRun(i.id)) {
            rec["path"] = n->path.empty() ? Json(nullptr) : Json(n->path);
            rec["activity"] = n->state == "suspended" ? "suspended" : n->agent.activity();
            rec["seat"] = n->agent.seatRef().empty() ? Json(nullptr) : Json(n->agent.seatRef().substr(0, 192));
            if (n->state == "suspended")
                rec["state"] = "suspended";
            if (n->accent)
                rec["accent"] = {std::clamp(n->accent->x, 0.f, 1.f), std::clamp(n->accent->y, 0.f, 1.f), std::clamp(n->accent->z, 0.f, 1.f)};
        } else {
            Json config = i.source == "user" ? editor.placement(world->id, i.id) : Json(nullptr);
            if (config.is_object() && config.contains("path"))
                rec["path"] = config["path"];
        }
    }
    uint64_t placements = 0, triangles = 0, gpu = 0, collision = 0;
    std::set<std::string> refs;
    for (auto &i : objectInfos)
        if (i.source == "user") {
            ++placements;
            if (!i.entry)
                continue;
            triangles += i.entry->triangles.is_number() ? i.entry->triangles.get<uint64_t>() : 0;
            collision += i.entry->collisionTriangles.is_number() ? i.entry->collisionTriangles.get<uint64_t>() : 0;
            if (refs.insert(i.ref).second)
                gpu += i.entry->gpuBytes.is_number() ? i.entry->gpuBytes.get<uint64_t>() : 0;
        }
    for (auto &i : objectInfos)
        if (i.source == "user")
            refs.insert(i.ref);
    return {{"catalogRevision", catalogRevision},
            {"instances", instances},
            {"pocket", pocketObjectsJson()},
            {"usage", {{"placements", placements}, {"assets", refs.size()}, {"triangles", triangles}, {"gpuBytes", gpu}, {"collisionTriangles", collision}}},
            {"limits",
             {{"placements", userPlacementLimit},
              {"assets", userAssetLimit},
              {"triangles", userTriangleLimit},
              {"gpuBytes", userGpuLimit},
              {"collisionTriangles", userCollisionLimit}}}};
}
std::set<std::string> Runtime::objectWaived(int asset, const interaction::Transform &start, std::optional<size_t> self) {
    std::set<std::string> waived;
    for (int i = 0; i < 4; ++i) {
        auto v = objectVerdict(asset, start, "", self, waived, true);
        if (v.ok || v.rule == "walls" || v.rule == "playable" || v.rule == "player" || v.rule == "support")
            break;
        waived.insert(v.rule);
    }
    return waived;
}
// IPC 0.22 objects.* and libraries.* (RFC-0011 P1). Throws ipc::Error; nullopt defers the
// reply (libraries.install completes from catalogTick).
std::optional<Json> Runtime::objectsMethod(const std::string &method, const Json &params) {
    if (method.starts_with("npcs.") || method.starts_with("paths.") || method == "interaction.choose")
        return npcsMethod(method, params);
    if (method.starts_with("companion."))
        return companionMethod(method, params);
    auto only = [&](std::initializer_list<const char *> required, std::initializer_list<const char *> optional = {}) {
        if (!params.is_object())
            throw ipc::Error(-32602, "params must be an object");
        for (auto k : required)
            if (!params.contains(k))
                throw ipc::Error(-32602, std::string("missing ") + k);
        for (auto &[k, v] : params.items())
            if (std::none_of(required.begin(), required.end(), [&](auto r) { return k == r; }) &&
                std::none_of(optional.begin(), optional.end(), [&](auto o) { return k == o; }))
                throw ipc::Error(-32602, "unknown parameter " + k);
    };
    auto revision = [&](Json extra = Json::object()) {
        publish();
        extra["revision"] = ipc->revision();
        return extra;
    };
    auto pose = [&](const Json &p) {
        if (!p.is_object() || p.size() != 2 || !p.contains("position") || !p.contains("rotation") || !p["position"].is_array() ||
            p["position"].size() != 3 || !p["rotation"].is_array() || p["rotation"].size() != 4)
            throw ipc::Error(-32602, "pose must be {position, rotation}");
        interaction::Transform t;
        float norm = 0;
        for (int i = 0; i < 3; ++i) {
            if (!p["position"][i].is_number() || std::abs(p["position"][i].get<double>()) > 100000)
                throw ipc::Error(-32602, "invalid position");
        }
        for (int i = 0; i < 4; ++i) {
            if (!p["rotation"][i].is_number())
                throw ipc::Error(-32602, "invalid rotation");
            t.rotation[i] = p["rotation"][i].get<float>();
            norm += t.rotation[i] * t.rotation[i];
        }
        if (std::abs(std::sqrt(norm) - 1) > .002f)
            throw ipc::Error(-32602, "rotation must be a unit quaternion");
        t.position = vec3(p["position"]);
        return t;
    };
    auto requireWorld = [&] {
        if (!world || loading)
            throw ipc::Error(-32005, "no world loaded");
    };
    auto info = [&](const char *key = "id") -> ObjectInfo & {
        if (!params.contains(key) || !params[key].is_string())
            throw ipc::Error(-32602, "id required");
        auto *i = objectInfo(params[key].get<std::string>());
        if (!i)
            throw ipc::Error(-32004, "unknown object " + params[key].get<std::string>());
        if (i->source == "companion")
            throw ipc::Error(-32005, "the companion is protected; use companion.clear");
        return *i;
    };
    auto editable = [&](ObjectInfo &i) {
        if (i.character)
            throw ipc::Error(-32005, "characters are protected");
        if (!i.movable)
            throw ipc::Error(-32005, i.name + " is not movable");
        if (i.locked)
            throw ipc::Error(-32005, i.name + " is locked");
    };
    auto record = [&](const ObjectInfo &i) { return i.source == "authored" ? editor.override(world->id, i.id) : editor.placement(world->id, i.id); };
    if (method == "objects.catalog") {
        only({});
        auto entries = catalogEntries();
        // IPC 0.24 (RFC-0013): a machine's declared inputs, for its data source panel.
        if (ipc::protocol::atLeast(objectsProtocol, "0.24"))
            for (auto &e : entries) {
                auto *entry = catalogEntry(e["ref"]);
                e["machine"] = entry ? world::machineInputs(entry->manifest) : Json(nullptr);
            }
        // IPC 0.25: each entry's companion capability.
        if (ipc::protocol::atLeast(methodProtocol, "0.25"))
            for (auto &entry : entries)
                if (auto *e = catalogEntry(entry["ref"]))
                    entry["companion"] = companionCapability(*e);
                else
                    entry["companion"] = nullptr;
        return Json{{"revision", catalogRevision}, {"entries", entries}};
    }
    if (method == "libraries.list") {
        only({});
        Json list = Json::array();
        for (auto &l : catalog->libraries)
            if (list.size() < 256)
                list.push_back(l.json());
        return Json{{"libraries", list}};
    }
    if (method == "libraries.install") {
        only({"archive", "sha256"});
        if (!params["archive"].is_string() || !params["sha256"].is_string())
            throw ipc::Error(-32602, "archive and sha256 must be strings");
        std::string archive = params["archive"], sha = params["sha256"];
        static const std::regex hex("^[0-9a-f]{64}$");
        if (archive.size() > 4096 || archive.empty() || archive[0] != '/' || !archive.ends_with(".tar.zst") || !std::regex_match(sha, hex))
            throw ipc::Error(-32602, "archive must be an absolute .tar.zst path and sha256 64 lowercase hex digits");
        if (libraryInstall.valid())
            throw ipc::Error(-32004, "a library install is already running");
        auto roots = catalogRoots;
        auto *cache = &scanCache;
        libraryInstall = std::async(std::launch::async, [roots, cache, archive, sha] {
            LibraryJob job;
            job.installed = world::installLibrary(archive, sha, roots);
            job.snapshot = world::scanCatalog(roots, *cache);
            return job;
        });
        return std::nullopt;
    }
    if (method == "libraries.remove") {
        only({"id", "version"});
        if (!params["id"].is_string() || !params["version"].is_string())
            throw ipc::Error(-32602, "id and version must be strings");
        try {
            world::removeLibrary(params["id"], params["version"], catalogRoots);
        } catch (const std::domain_error &e) {
            throw ipc::Error(-32005, e.what());
        } catch (const std::invalid_argument &e) {
            throw ipc::Error(-32004, e.what());
        } catch (const std::exception &e) {
            throw ipc::Error(-32603, e.what());
        }
        catalogDirty = true;
        catalogQuiet = std::chrono::steady_clock::now();
        return revision();
    }
    requireWorld();
    if (method == "objects.pocket") {
        only({"ref"});
        if (!params["ref"].is_string())
            throw ipc::Error(-32602, "ref must be a string");
        if (!active || !layout.desktop.is_null())
            throw ipc::Error(-32005, "exploration inactive");
        auto *e = catalogEntry(params["ref"]);
        if (!e)
            throw ipc::Error(-32004, "unknown ref " + params["ref"].get<std::string>());
        if (e->has("character"))
            throw ipc::Error(-32005, "characters are protected: spawn them with npcs.spawn");
        if (!e->placeable)
            throw ipc::Error(-32005, e->reason.empty() ? "not placeable" : e->reason);
        if (pocketObjects.size() >= pocketObjectLimit)
            throw budgetError("pocket items (16)");
        pocketObjects.push_back({"pocket-" + std::to_string(++pocketSerial), e->ref, e->name});
        unplacedSelected = pocketObjects.back().itemId;
        cancelGrab();
        cancelGizmo();
        objectCarry = false;
        editor.slot = tool = 5;
        assetFor(*e); // start decoding now: the ghost appears as soon as it is ready
        ipc->notify("runtime.notice", {{"level", "info"}, {"code", "placement.pocketed"}, {"message", "Pocketed " + e->name}});
        return revision({{"itemId", pocketObjects.back().itemId}});
    }
    if (method == "objects.place") {
        only({"ref"}, {"pose", "scale"});
        if (!params["ref"].is_string())
            throw ipc::Error(-32602, "ref must be a string");
        auto *e = catalogEntry(params["ref"]);
        if (!e)
            throw ipc::Error(-32004, "unknown ref " + params["ref"].get<std::string>());
        if (e->has("character"))
            throw ipc::Error(-32005, "characters are spawned on a route or a standing spot: use npcs.spawn");
        if (!e->placeable)
            throw ipc::Error(-32005, e->reason.empty() ? "not placeable" : e->reason);
        float scale = 1;
        if (params.contains("scale")) {
            if (!params["scale"].is_number())
                throw ipc::Error(-32602, "scale must be a number");
            scale = params["scale"];
            float lo = e->scale.is_array() ? e->scale[0].get<float>() : 1, hi = e->scale.is_array() ? e->scale[1].get<float>() : 1;
            if (scale < lo - 1e-6f || scale > hi + 1e-6f)
                throw ipc::Error(-32602, "scale outside the object's range");
        }
        if (auto limit = budgetRefusal(*e))
            throw budgetError(*limit);
        int asset = assetFor(*e);
        if (asset == -1)
            throw ipc::Error(-32004, e->name + " is still loading; retry", Json{{"retryable", true}});
        if (asset < 0)
            throw ipc::Error(-32005, "can't load " + e->name);
        auto support = supportOf(*e);
        std::optional<interaction::Transform> placed;
        interaction::PlacementVerdict last{false, "walls", "No free spot in front of you", false};
        if (params.contains("pose")) {
            auto t = pose(params["pose"]);
            t.scale = {scale, scale, scale};
            last = objectVerdict(asset, t, support, std::nullopt);
            if (last.ok)
                placed = t;
        } else {
            // In front of the player by the reachable-spawn rules: short of walls, on the floor.
            if (!active)
                throw ipc::Error(-32005, "exploration inactive");
            Vec3 flat = normalized(Vec3{camera.forward().x, 0, camera.forward().z});
            float spawn = config["windows"]["spawn_distance"].get<float>();
            auto scene = placementScene();
            scene.ignore = {};
            for (float d : {spawn, 2.f, 1.5f, 3.f, 1.f}) {
                if (auto wall = interaction::raycast(camera.position, flat, world->collision, &world->collisionIndex))
                    d = std::min(d, interaction::limitSpawnDistance(d, wall->distance));
                interaction::Transform t;
                t.rotation = yawRotation(std::atan2(-flat.x, -flat.z));
                t.scale = {scale, scale, scale};
                Vec3 at = camera.position + flat * d;
                auto &a = world->objectAssets[asset];
                if (auto floor = interaction::floorBelow(scene, at, 4.f); floor && supports(support, "floor"))
                    t.position = Vec3{at.x, *floor, at.z} - rotate(t.rotation, Vec3{0, a.low.y * scale, 0});
                else if (support == "any")
                    t.position = at;
                else
                    continue;
                last = objectVerdict(asset, t, support, std::nullopt);
                if (last.ok) {
                    placed = t;
                    break;
                }
            }
        }
        if (!placed)
            throw refusal("Not placed", last);
        auto id = newPlacementId();
        Json rec{{"worldId", world->id}, {"id", id}, {"areaId", areaAt(placed->position)}, {"ref", e->ref},
                 {"pose", poseJson(*placed)}, {"scale", scale}};
        objectEdit(id, nullptr, rec);
        objectNotice("object.placed", "Placed " + e->name);
        return revision({{"id", id}});
    }
    if (method == "objects.transform") {
        only({"id", "pose"}, {"scale"});
        auto &i = info();
        editable(i);
        if (i.index < 0 || i.state != "active")
            throw ipc::Error(-32005, i.name + " is " + i.state);
        auto t = pose(params["pose"]);
        float scale = i.scale;
        if (params.contains("scale")) {
            if (!params["scale"].is_number())
                throw ipc::Error(-32602, "scale must be a number");
            scale = params["scale"];
            if (scale < i.scaleMin - 1e-6f || scale > i.scaleMax + 1e-6f)
                throw ipc::Error(-32602, "scale outside the object's range");
        }
        t.scale = {scale, scale, scale};
        int asset = int(world->objectInstances[i.index].asset);
        auto start = objectPose("object:" + i.id);
        auto v = objectVerdict(asset, t, i.support, size_t(i.index), objectWaived(asset, start, size_t(i.index)));
        if (!v.ok)
            throw refusal("Not moved", v);
        auto id = i.id;
        objectEdit(id, record(i), placementRecord(i, t));
        return revision();
    }
    if (method == "objects.remove") {
        if (params.contains("all")) {
            only({"all"});
            if (params["all"] != true)
                throw ipc::Error(-32602, "all must be true");
            std::vector<std::string> ids;
            for (auto &i : objectInfos)
                if (i.source == "user")
                    ids.push_back(i.id);
            for (auto &id : ids)
                objectEdit(id, editor.placement(world->id, id), nullptr);
            objectNotice("object.removed", "Removed " + std::to_string(ids.size()) + " object" + (ids.size() == 1 ? "" : "s"));
            return revision();
        }
        only({"id"});
        auto &i = info();
        if (i.source != "user")
            throw ipc::Error(-32005, "authored objects are never deleted; hide them instead");
        if (i.locked)
            throw ipc::Error(-32005, i.name + " is locked");
        auto id = i.id, name = i.name;
        objectEdit(id, record(i), nullptr);
        objectNotice("object.removed", "Removed " + name);
        return revision();
    }
    if (method == "objects.hide") {
        only({"id", "hidden"});
        if (!params["hidden"].is_boolean())
            throw ipc::Error(-32602, "hidden must be a boolean");
        auto &i = info();
        if (i.source != "authored")
            throw ipc::Error(-32005, "remove user objects instead of hiding them");
        if (!i.movable && !i.character)
            throw ipc::Error(-32005, i.name + " can't be hidden: the world may rely on it");
        auto before = record(i);
        Json after = before.is_object() ? before : Json{{"worldId", world->id}, {"objectId", i.id}, {"areaId", i.areaId}};
        if (params["hidden"].get<bool>())
            after["hidden"] = true;
        else
            after.erase("hidden");
        if (after.size() == 3 && !after.contains("pose") && !after.contains("scale") && !after.contains("locked"))
            after = nullptr; // nothing left to override
        objectEdit(i.id, before, after);
        return revision();
    }
    if (method == "objects.reset") {
        if (params.contains("all")) {
            only({"all"});
            if (params["all"] != true)
                throw ipc::Error(-32602, "all must be true");
            std::vector<std::string> ids;
            for (auto &i : objectInfos)
                if (i.source == "authored" && !editor.override(world->id, i.id).is_null())
                    ids.push_back(i.id);
            for (auto &id : ids)
                objectEdit(id, editor.override(world->id, id), nullptr);
            return revision();
        }
        only({"id"});
        auto &i = info();
        if (i.source == "authored") {
            auto before = record(i);
            if (!before.is_null())
                objectEdit(i.id, before, nullptr);
            return revision();
        }
        if (i.locked)
            throw ipc::Error(-32005, i.name + " is locked");
        if (i.index < 0)
            throw ipc::Error(-32005, i.name + " is " + i.state);
        auto t = objectPose("object:" + i.id);
        t.rotation = upright(t.rotation);
        t.scale = {1, 1, 1};
        auto v = objectVerdict(int(world->objectInstances[i.index].asset), t, i.support, size_t(i.index));
        if (!v.ok)
            throw refusal("Not reset", v);
        auto id = i.id;
        objectEdit(id, record(i), placementRecord(i, t));
        return revision();
    }
    if (method == "objects.configure") {
        only({"id", "patch"});
        auto &i = info();
        auto &patch = params["patch"];
        if (!patch.is_object() || patch.empty())
            throw ipc::Error(-32602, "patch must name at least one setting");
        for (auto &[k, v] : patch.items())
            if (k != "locked" || !v.is_boolean())
                throw ipc::Error(-32602, "IPC 0.22 configures only locked (a boolean)");
        if (i.character)
            throw ipc::Error(-32005, "characters are protected");
        if (!i.movable)
            throw ipc::Error(-32005, i.name + " is not movable");
        auto before = record(i);
        Json after = before.is_object() ? before : Json{{"worldId", world->id}, {"objectId", i.id}, {"areaId", i.areaId}};
        if (patch["locked"].get<bool>())
            after["locked"] = true;
        else
            after.erase("locked");
        if (i.source == "authored" && after.size() == 3 && !after.contains("pose") && !after.contains("scale") && !after.contains("hidden"))
            after = nullptr;
        objectEdit(i.id, before, after);
        return revision();
    }
    throw ipc::Error(-32601, "unknown method");
}
