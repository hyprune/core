// World folders in editor mode (RFC-0014 phase 1 as amended; IPC 0.27 `builder` state, world
// format 0.11). world.load of local.<slug> with no installed package of that id opens
// $HYPRUNE_PROJECTS_DIR/<slug>/ (default $XDG_DATA_HOME/hyprune/projects): core loads
// builder/base.json (the world without placements) and turns builder/project.json's
// placements into user placements of the loaded world. Placing a user or library object
// copies its directory into <folder>/objects/<id>/ first, so the world stays self-contained
// (ref world/<id>); every edit is written back to builder/project.json instead of
// editor.json. Core never runs git: Save and Release are the hyprune CLI's (the shell drives
// it). Included by Plugin.cpp after Screens.inc; runs in the tick and in method handlers,
// never in the render pass.
namespace {
bool projectSlug(const std::string &slug) {
    static const std::regex re("^[a-z][a-z0-9-]{0,63}$");
    return std::regex_match(slug, re);
}
} // namespace
// The folder of a local.<slug> world id, when it holds a project (never a symlink).
std::optional<std::filesystem::path> Runtime::projectFolder(const std::string &id) const {
    if (!id.starts_with("local.") || projectsDir.empty())
        return std::nullopt;
    auto slug = id.substr(6);
    if (!projectSlug(slug))
        return std::nullopt;
    auto folder = projectsDir / slug;
    std::error_code ec;
    for (auto &p : {folder, folder / "builder", folder / "builder/base.json", folder / "builder/project.json"})
        if (fs::is_symlink(p, ec))
            return std::nullopt;
    if (!fs::is_regular_file(folder / "builder/base.json", ec) || !fs::is_regular_file(folder / "builder/project.json", ec))
        return std::nullopt;
    return folder;
}
// Every object directory in <folder>/objects as world/<name> entries (at most 512).
void Runtime::builderScan() {
    worldCatalog.clear();
    worldMisses.clear();
    if (!builder)
        return;
    std::error_code ec;
    std::vector<fs::path> dirs;
    for (auto &d : fs::directory_iterator(builder->folder / "objects", ec))
        if (d.is_directory() && !d.is_symlink() && projectSlug(d.path().filename().string()))
            dirs.push_back(d.path());
    std::sort(dirs.begin(), dirs.end());
    for (auto &dir : dirs) {
        if (worldCatalog.size() >= 512)
            break;
        auto e = world::scanObject(dir, "world/" + dir.filename().string(), "world", &scanCache);
        world::decidePlaceable(e);
        worldCatalog.push_back(std::move(e));
    }
}
// A world/<id> entry that appeared after the folder loaded (a primitive the CLI just generated,
// RFC-0014 amendment: primitives and materials): scanned on its first use, so objects.place,
// objects.pocket and objects.configure {ref} can name it at once. A miss is retried after 2 s.
const world::CatalogEntry *Runtime::builderScanOne(const std::string &id) {
    if (!builder || !projectSlug(id) || worldCatalog.size() >= 512)
        return nullptr;
    auto now = std::chrono::steady_clock::now();
    if (auto miss = worldMisses.find(id); miss != worldMisses.end() && now - miss->second < std::chrono::seconds(2))
        return nullptr;
    auto dir = builder->folder / "objects" / id;
    std::error_code ec;
    if (fs::is_symlink(dir, ec) || !fs::is_regular_file(dir / "object.json", ec)) {
        worldMisses[id] = now;
        return nullptr;
    }
    worldMisses.erase(id);
    auto e = world::scanObject(dir, "world/" + id, "world", &scanCache);
    world::decidePlaceable(e);
    worldCatalog.push_back(std::move(e));
    ++catalogRevision;
    return &worldCatalog.back();
}
// Called by finishLoad before the new world's objects resolve: the old folder's placements
// are written and dropped, the new folder's are read.
void Runtime::builderSwitch(std::optional<std::filesystem::path> folder) {
    if (builder) {
        saveEditor(); // flushes builder/project.json
        auto &list = editor.placements.get_ref<Json::array_t &>();
        std::erase_if(list, [&](auto &r) { return r["worldId"] == builder->worldId; });
        auto &overrides = editor.overrides.get_ref<Json::array_t &>();
        std::erase_if(overrides, [&](auto &r) { return r["worldId"] == builder->worldId; });
        builder.reset();
        worldCatalog.clear();
    }
    if (!folder || !world)
        return;
    BuilderProject b;
    b.folder = *folder;
    b.projectId = folder->filename().string();
    b.worldId = world->id;
    Json project;
    try {
        project = interaction::readJSON(*folder / "builder/project.json");
        world::validateObjectDocument(project, "project", "0.1");
    } catch (const std::exception &e) {
        objectNotice("builder.invalid", std::string("builder/project.json: ") + e.what());
        b.error = std::string("builder/project.json: ") + e.what();
        b.name = world->name.substr(0, 64);
        b.readOnly = true; // never overwrite a file we couldn't read
        builder = std::move(b);
        builderScan();
        return;
    }
    b.name = project["name"];
    b.sky = project["sky"]["image"];
    builder = std::move(b);
    builderScan();
    for (auto &p : project["placements"]) {
        Json rec{{"worldId", world->id}, {"id", p["id"]}, {"areaId", areaAt(vec3(p["pose"]["position"]))}, {"ref", p["ref"]},
                 {"pose", p["pose"]}, {"scale", p["scale"]}};
        for (auto k : {"workspace", "locked", "key"})
            if (p.contains(k))
                rec[k] = p[k];
        editor.setPlacement(world->id, p["id"], rec);
    }
    builder->written = project["placements"];
}
// The placements of the loaded folder as builder/project.json stores them.
void Runtime::builderWrite(const Json &records) {
    if (!builder || builder->readOnly)
        return;
    Json placements = Json::array();
    for (auto &r : records) {
        if (!r["ref"].get<std::string>().starts_with("world/"))
            continue; // only copies inside the folder (builderCopy runs before every place)
        // Poses come from floats: 0.1 mm and 1e-6 in the quaternion keep the file readable.
        auto round = [](const Json &v, double scale) {
            Json out = Json::array();
            for (auto &x : v)
                out.push_back(std::round(x.get<double>() * scale) / scale + 0.0);
            return out;
        };
        Json p{{"id", r["id"]},
               {"ref", r["ref"]},
               {"pose", {{"position", round(r["pose"]["position"], 1e4)}, {"rotation", round(r["pose"]["rotation"], 1e6)}}},
               {"scale", std::round(r["scale"].get<double>() * 1e4) / 1e4}};
        for (auto k : {"workspace", "locked", "key"})
            if (r.contains(k))
                p[k] = r[k];
        placements.push_back(p);
    }
    if (placements == builder->written)
        return;
    auto path = builder->folder / "builder/project.json";
    try {
        // The CLI owns the other fields (name, sky): read them back, replace placements only.
        auto project = interaction::readJSON(path);
        project["placements"] = placements;
        world::validateObjectDocument(project, "project", "0.1");
        interaction::atomicJSON(path, project);
        builder->written = placements;
        builder->name = project["name"];
        builder->sky = project["sky"]["image"];
        builder->error.clear();
        ++builder->revision;
    } catch (const std::exception &e) {
        builder->error = std::string("builder/project.json not written: ") + e.what();
        objectNotice("builder.write", builder->error, "write:" + builder->error);
    }
}
// Copies a user or library object into the folder (once per content) and returns its
// world/<id> entry; the copy shares the original's decoded asset.
const world::CatalogEntry *Runtime::builderCopy(const world::CatalogEntry &e) {
    for (auto &w : worldCatalog)
        if (w.digest == e.digest && w.state == "ready")
            return &w;
    if (worldCatalog.size() >= 512)
        throw ipc::Error(-32008, "This world folder holds 512 objects", Json{{"retryable", false}, {"rule", "budget"}});
    auto base = e.id.substr(0, 60);
    if (!projectSlug(base))
        base = "object";
    std::string id = base;
    std::error_code ec;
    for (int n = 2; fs::exists(builder->folder / "objects" / id, ec); ++n)
        id = base + "-" + std::to_string(n);
    auto staging = builder->folder / "objects" / ("." + id + ".copy");
    fs::remove_all(staging, ec);
    fs::create_directories(staging.parent_path(), ec);
    fs::copy(e.directory, staging, fs::copy_options::recursive, ec);
    if (!ec)
        fs::rename(staging, builder->folder / "objects" / id, ec);
    if (ec) {
        fs::remove_all(staging, ec);
        throw ipc::Error(-32603, "Couldn't copy " + e.name + " into the world folder");
    }
    auto copy = world::scanObject(builder->folder / "objects" / id, "world/" + id, "world", &scanCache);
    world::decidePlaceable(copy);
    worldCatalog.push_back(std::move(copy));
    auto &w = worldCatalog.back();
    if (auto slot = assetSlots.find(e.ref); slot != assetSlots.end() && slot->second.index >= 0 && w.digest == e.digest) {
        auto &alias = assetSlots[w.ref];
        alias.index = slot->second.index;
        alias.entry = w;
    }
    objectNotice("builder.copied", e.name + " was copied into this world's folder");
    return &w;
}
Json Runtime::builderState() const {
    if (!builder)
        return nullptr;
    size_t placed = 0;
    for (auto &r : editor.placements)
        placed += r["worldId"] == builder->worldId;
    return {{"projectId", builder->projectId},
            {"worldId", builder->worldId},
            {"name", builder->name},
            {"folder", builder->folder.string()},
            {"mode", "edit"},
            {"revision", builder->revision},
            {"placements", std::min<size_t>(placed, 512)},
            {"sky", builder->sky.empty() ? Json(nullptr) : Json(builder->sky)},
            {"error", builder->error.empty() ? Json(nullptr) : Json(builder->error.substr(0, 512))}};
}
