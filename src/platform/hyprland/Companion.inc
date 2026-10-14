// The companion (RFC-0010 companions amendment, IPC 0.25): one character the user picks from
// their catalog follows them through every world. The choice is user state (companion.json);
// a stay spot or route binding is per world, in the editor overlay (editor.companions). In a
// loaded world the companion is a character instance with the reserved id `companion` (not
// published as an object instance): it is posed, drawn, lit and offloaded like residents, and
// sim::Companion drives its agent in follow and stay modes; on a route it walks like one.
// Included by Plugin.cpp after Npcs.inc.
namespace {
constexpr float carrySnap = .75f; // setting a character down: a route within this distance takes it
} // namespace

// ---- capability, choice and per-world mode -------------------------------------------
// The companion verdict for a catalog entry (null: not a character).
Json Runtime::companionCapability(const world::CatalogEntry &e) const {
    if (!e.has("character"))
        return nullptr;
    Json manifest = e.manifest.is_object() ? e.manifest : Json::object();
    Json traits = manifest.value("traits", Json::object()).value("character", Json::object());
    Json block = manifest.contains("companion") && manifest["companion"].is_object() ? manifest["companion"] : Json::object();
    std::string name = block.value("name", e.name);
    if (name.empty())
        name = e.id;
    std::string reason;
    auto walk = traits.value("clips", Json::object()).value("walk", Json::object());
    if (e.source == "package")
        reason = "world objects can't be companions";
    else if (e.state != "ready")
        reason = e.reason.empty() ? "not a valid object" : e.reason;
    else if (traits.value("height", 1.75f) > 2.0f)
        reason = "taller than 2.0 m";
    else if (traits.value("radius", .35f) > .45f)
        reason = "wider than a 0.45 m radius";
    else if (e.triangles.is_number() && e.triangles.get<uint64_t>() > 16000)
        reason = "more than 16,000 triangles";
    else if (e.gpuBytes.is_number() && e.gpuBytes.get<uint64_t>() > (32ull << 20))
        reason = "more than 32 MiB of GPU memory";
    else if (walk.contains("speed") && walk["speed"].is_number() && (walk["speed"] < .7 || walk["speed"] > 1.4))
        reason = "walk speed outside 0.7-1.4 m/s";
    return {{"capable", reason.empty()}, {"name", ipc::boundedText(name, 32)}, {"reason", reason.empty() ? Json(nullptr) : Json(ipc::boundedText(reason, 64))}};
}
std::string Runtime::companionRef() const {
    auto &c = companionFile.current;
    return c.is_object() && c.contains("ref") && c["ref"].is_string() ? c["ref"].get<std::string>() : "";
}
// This world's companion record resolved: {path, phase, speed?, reverse?} for a route that
// still exists and is walkable, {pose} for a stay spot, else null (follow). A record whose
// route went away is dropped (it follows again).
Json Runtime::companionConfig() {
    if (!world)
        return nullptr;
    auto rec = editor.companion(world->id);
    if (!rec.is_object())
        return nullptr;
    if (rec.contains("path")) {
        auto path = pathRecord(rec["path"]);
        if (!path.is_null() && world::pathUse(path) == "route" && routeWalkability(path, companionBody).walkable)
            return rec;
        editor.setCompanion(world->id, nullptr);
        saveEditor();
        objectNotice("companion.following", companionName + " follows you again: its route is gone or not walkable");
        return nullptr;
    }
    return rec;
}
std::string Runtime::companionMode() const {
    if (!world)
        return "follow";
    auto rec = editor.companion(world->id);
    return !rec.is_object() ? "follow" : rec.contains("path") ? "route" : "stay";
}
// Adds the companion's instance to the loaded world (objectsSync), when it can be shown.
void Runtime::companionInject(std::vector<ObjectInfo> &infos) {
    auto ref = companionRef();
    companionBlock = nullptr;
    if (ref.empty()) {
        companionStatus = "none", companionReason.clear(), companionName.clear();
        return;
    }
    if (!world) {
        companionStatus = "hidden", companionReason = "no world";
        return;
    }
    std::string substitute;
    auto *e = catalogEntry(ref, &substitute);
    if (!e) {
        companionStatus = "unavailable", companionReason = "not installed";
        if (catalogRevision > 0)
            objectNotice("companion.unavailable", "Your companion " + ref + " is not installed", "companion-missing:" + ref);
        return;
    }
    auto verdict = companionCapability(*e);
    companionName = verdict.is_object() ? verdict["name"].get<std::string>() : e->name;
    if (!verdict.is_object() || !verdict["capable"].get<bool>()) {
        companionStatus = "unavailable";
        companionReason = verdict.is_object() ? verdict["reason"].get<std::string>() : "not a character";
        objectNotice("companion.unavailable", companionName + " can't be your companion: " + companionReason, "companion-capable:" + ref);
        return;
    }
    int asset = assetFor(*e);
    if (asset < 0) {
        companionStatus = asset == -1 ? "loading" : "unavailable";
        companionReason = asset == -1 ? "" : "failed to load";
        return;
    }
    auto &a = world->objectAssets[asset];
    if (!a.character) {
        companionStatus = "unavailable", companionReason = "not a character";
        return;
    }
    // Core's own character check: errors make it unavailable (once per loaded character).
    auto checked = companionChecked.find(a.character.get());
    if (checked == companionChecked.end()) {
        std::string problem;
        for (auto &p : world::checkCharacter(*a.character))
            if (p.level == world::CharacterProblem::Error && problem.empty())
                problem = p.message;
        if (problem.empty() && a.character->hi.y - a.character->lo.y > 2.05f)
            problem = "taller than 2.0 m";
        checked = companionChecked.emplace(a.character.get(), problem).first;
    }
    if (!checked->second.empty()) {
        companionStatus = "unavailable", companionReason = ipc::boundedText(checked->second, 64);
        objectNotice("companion.unavailable", companionName + " can't be your companion: " + checked->second, "companion-check:" + ref);
        return;
    }
    companionBlock = e->manifest.contains("companion") && e->manifest["companion"].is_object() ? e->manifest["companion"] : Json::object();
    auto traits = e->manifest.value("traits", Json::object()).value("character", Json::object());
    companionBody = {traits.value("radius", .35f), traits.value("height", 1.75f)};
    ObjectInfo info;
    info.id = companionId;
    info.source = "companion";
    info.ref = e->ref;
    info.name = companionName;
    info.entry = e;
    info.character = true;
    info.movable = false;
    Vec3 at = camera.position - Vec3{0, camera.eyeHeight, 0};
    float yaw = camera.yaw;
    if (auto *n = npcRun(companionId))
        at = n->agent.position, yaw = n->agent.heading;
    info.position = at;
    info.rotation = {0, std::sin(yaw * .5f), 0, std::cos(yaw * .5f)};
    info.areaId = areaAt(at);
    world::ObjectInstance inst;
    inst.id = info.id;
    inst.asset = unsigned(asset);
    inst.position = at;
    inst.rotation = info.rotation;
    inst.model = world::instanceModel(at, info.rotation, 1);
    inst.center = inst.model.point(a.center);
    inst.radius = a.radius;
    inst.character = true;
    inst.bound = companionMode() == "route";
    info.index = int(world->objectInstances.size());
    world->objectInstances.push_back(inst);
    companionStatus = "active", companionReason.clear();
    infos.push_back(info);
}
// The companion's interactions: Talk (one line, the next each time), then Stay or Follow,
// then (following) its pace: "Run with me" or "Walk with me", whichever it isn't keeping.
Json Runtime::companionInteractions() const {
    Json out = Json::array();
    if (!companionLines().empty())
        out.push_back({{"id", "talk"}, {"label", "Talk"}, {"action", {{"type", "say"}, {"lines", Json::array({companionLines()[0]})}}}});
    if (companionMode() == "follow") {
        out.push_back({{"id", "stay"}, {"label", "Stay"}, {"action", {{"type", "companion"}, {"mode", "stay"}}}});
        if (companionPace() == "run")
            out.push_back({{"id", "walk"}, {"label", "Walk with me"}, {"action", {{"type", "companion"}, {"pace", "walk"}}}});
        else
            out.push_back({{"id", "run"}, {"label", "Run with me"}, {"action", {{"type", "companion"}, {"pace", "run"}}}});
    } else
        out.push_back({{"id", "follow"}, {"label", "Follow"}, {"action", {{"type", "companion"}, {"mode", "follow"}}}});
    return out;
}
std::string Runtime::companionPace() const { return companionFile.current.value("pace", std::string("walk")); }
// The pace it keeps while following (companion.json; "walk" is the default and not written).
void Runtime::companionSetPace(const std::string &pace) {
    if (companionRef().empty())
        throw ipc::Error(-32005, "no companion chosen");
    Json next = {{"ref", companionRef()}};
    if (pace == "run")
        next["pace"] = "run";
    companionFile.save(next);
    companionFile.current = next;
    ++companionRevision; // its interactions change
    objectNotice("companion.following", (companionName.empty() ? std::string("Your companion") : companionName) +
                                            (pace == "run" ? " runs with you" : " walks with you"));
}
std::vector<std::string> Runtime::companionLines() const {
    std::vector<std::string> lines;
    if (!companionBlock.is_object())
        return lines;
    for (auto &l : companionBlock.value("say", Json::array()))
        if (l.is_string() && lines.size() < 8)
            lines.push_back(l);
    return lines;
}
// Sets this world's companion record (null: follow) and rebuilds its agent.
void Runtime::companionRecord(Json record) {
    if (!world)
        throw ipc::Error(-32005, "no world loaded");
    if (!record.is_null())
        record["worldId"] = world->id;
    editor.setCompanion(world->id, record);
    saveEditor();
    ++companionRevision;
    objectsDirty = true;
}
// npcs.bind {id: "companion", path | pose, phase?, speed?, reverse?}: a route of this world
// (mode route) or a standing spot (mode stay).
void Runtime::companionBind(const Json &patch) {
    auto *n = npcRun(companionId);
    if (!n)
        throw ipc::Error(-32005, "no companion here");
    if (patch.contains("accent"))
        throw ipc::Error(-32602, "accent is not for the companion");
    auto previous = editor.companion(world->id);
    Json record = Json::object();
    if (patch.contains("path")) {
        auto ref = qualifiedPath(patch["path"]);
        requireRoute(ref, companionBody, companionId);
        record["path"] = ref;
        auto route = world::routeOf(pathRecord(ref));
        float phase = patch.contains("phase") ? patch["phase"].get<float>() : route.nearest(n->agent.position) / std::max(1e-3f, route.length());
        record["phase"] = std::clamp(phase, 0.f, .999f);
        for (auto key : {"speed", "reverse"})
            if (patch.contains(key) && !patch[key].is_null())
                record[key] = patch[key];
    } else if (patch.contains("pose")) {
        interaction::WalkProblem why{{}, "floor", ""};
        auto spot = standingSpot(companionBody, patch["pose"], &why);
        if (!spot)
            throw spotRefusal(why, false);
        record["pose"] = {{"position", vecJson(spot->first)}, {"rotation", yawRotation(spot->second)}};
    } else {
        // Settings alone adjust the current route binding.
        if (!previous.is_object() || !previous.contains("path"))
            throw ipc::Error(-32005, "the companion is not on a route");
        record = previous;
        for (auto key : {"phase", "speed", "reverse"})
            if (patch.contains(key)) {
                if (patch[key].is_null())
                    record.erase(key);
                else
                    record[key] = patch[key];
            }
    }
    companionRecord(record);
    objectNotice(record.contains("path") ? "companion.bound" : "companion.staying",
                 companionName + (record.contains("path") ? " walks this route" : " stays here"));
}
void Runtime::companionSetMode(const std::string &mode) {
    if (companionRef().empty())
        throw ipc::Error(-32005, "no companion chosen");
    if (mode == "follow") {
        if (world && editor.companion(world->id).is_object())
            companionRecord(nullptr);
        objectNotice("companion.following", (companionName.empty() ? std::string("Your companion") : companionName) + " follows you");
        return;
    }
    auto *n = npcRun(companionId);
    if (!n)
        throw ipc::Error(-32005, "no companion here");
    companionRecord({{"pose", {{"position", vecJson(n->agent.position)}, {"rotation", yawRotation(n->agent.heading)}}}});
    objectNotice("companion.staying", companionName + " stays here");
}

// ---- per tick ------------------------------------------------------------------------
// Callbacks for sim::Companion: the walkable grid and legs (collision and the playable union;
// approach zones and keepClear are passable on the way), free standing spots (the strict
// rules, outside the view corridor and 1 m from the player) and what the player can see.
void Runtime::companionView(sim::CompanionView &v, const interaction::PlacementScene &walkScene,
                            const interaction::PlacementScene &standScene) {
    auto body = companionBody;
    float aspect = monitor ? float(monitor->m_pixelSize.x) / std::max(1.f, float(monitor->m_pixelSize.y)) : 16.f / 9;
    Vec3 eye = camera.position, f = camera.forward();
    Vec3 r = normalized(cross(f, Vec3{0, 1, 0})), u = cross(r, f);
    float margin = 10 * 3.14159265f / 180, half = camera.fov * 3.14159265f / 360;
    float tanV = std::tan(half + margin), tanH = std::tan(std::atan(std::tan(half) * aspect) + margin);
    auto w = world;
    v.inView = [=](Vec3 root) {
        bool any = false;
        for (float h : {.3f, body.height * .9f}) {
            Vec3 p = root + Vec3{0, h, 0}, d = p - eye;
            float z = dot(d, f), dist = length(d);
            if (z <= .05f || dist > 80 || std::abs(dot(d, r)) > z * tanH || std::abs(dot(d, u)) > z * tanV)
                continue;
            auto hit = interaction::raycast(eye, d / dist, w->collision, &w->collisionIndex);
            if (!hit || hit->distance > dist - .05f)
                any = true;
        }
        return any;
    };
    // The view corridor: towards an aimed window within 8 m.
    std::optional<std::pair<Vec3, Vec3>> corridor;
    if (auto hit = aim(); hit && hit->second.distance <= 8)
        corridor = {pointerCamera().position, pointerCamera().position + pointerRay() * hit->second.distance};
    Vec3 feet = v.feet;
    // A spot by `scene`'s rules (strict: keep-clear too), 1 m from the player and clear of the
    // view corridor (distance from the companion's vertical axis to the segment, sampled).
    auto spot = [=](const interaction::PlacementScene &scene, Vec3 p, bool strict) -> std::optional<Vec3> {
        auto floor = interaction::floorBelow(scene, p + Vec3{0, 1.2f, 0}, 2.5f);
        if (!floor)
            return std::nullopt;
        Vec3 root{p.x, *floor, p.z};
        if (std::hypot(root.x - feet.x, root.z - feet.z) < 1)
            return std::nullopt;
        if (corridor) {
            auto [a, b] = *corridor;
            for (float t = 0; t <= 1.001f; t += .1f) {
                Vec3 q = a + (b - a) * t;
                if (q.y >= root.y - .2f && q.y <= root.y + body.height + .2f && std::hypot(q.x - root.x, q.z - root.z) < .6f + body.radius)
                    return std::nullopt;
            }
        }
        if (interaction::checkSpot(scene, root, body, strict))
            return std::nullopt;
        return root;
    };
    v.free = [spot, scene = &standScene](Vec3 p) { return spot(*scene, p, true); };
    // The fallback: the walking rules alone (the walk scene has no approach or keep-clear
    // volumes).
    v.loose = [spot, scene = &walkScene](Vec3 p) { return spot(*scene, p, false); };
    companionPlanner(walkScene);
}
// Points the planner at a walk scene. The callbacks hold its address: the caller keeps the
// scene alive and clears them (CompanionPlannerScope) before it goes.
void Runtime::companionPlanner(const interaction::PlacementScene &walkScene) {
    auto body = companionBody;
    companionBrain.planner.stand = [body, scene = &walkScene](Vec3 p) -> std::optional<Vec3> {
        auto floor = interaction::floorAt(*scene, p);
        if (!floor)
            return std::nullopt;
        Vec3 root{p.x, *floor, p.z};
        if (interaction::checkSpot(*scene, root, body))
            return std::nullopt;
        return root;
    };
    companionBrain.planner.leg = [body, scene = &walkScene](Vec3 a, Vec3 b) { return interaction::checkLeg(*scene, a, b, body, 1).empty(); };
}
interaction::PlacementScene Runtime::companionWalkScene() {
    auto scene = npcScene();
    scene.approaches.clear(); // passable on the way; never a place to stand (the free spots)
    scene.keepClear.clear();
    return scene;
}
// One tick of the companion's agent (npcTick): carried, focus, route, follow and stay.
// Returns whether it moved (it keeps frames coming only then).
bool Runtime::companionStep(NpcRun &n, float dt, const sim::NpcWorld &w, const std::string &paused) {
    auto &a = n.agent;
    auto section = timed("companion");
    Vec3 before = a.position;
    float heading = a.heading;
    Vec3 feet = camera.position - Vec3{0, camera.eyeHeight, 0};
    Vec3 step = feet - companionFeet;
    bool warped = std::hypot(step.x, step.z) > 3 || std::abs(step.y) > 3;
    // The slot follows the direction of travel, never the view alone: turning round to look at
    // the companion leaves it where it is. After a warp or a rejoin the view is the best guess.
    if (dt > 0 && std::hypot(step.x, step.z) / dt > .3f && !warped)
        companionHeading = std::atan2(step.x, step.z);
    else if (warped || companionBrain.teleport) {
        Vec3 f = camera.forward();
        if (std::hypot(f.x, f.z) > .1f)
            companionHeading = std::atan2(f.x, f.z);
    }
    companionFeet = feet;
    if (companionCarry) { // hangs from the grab point (characterCarryTick moves it)
        companionLastStep = "carried";
        a.advance(dt, w);
        a.idle();
        return true;
    }
    bool focus = !layout.desktop.is_null() || fullscreen.phase;
    std::string mode = companionMode();
    auto walkScene = companionWalkScene();
    CompanionPlannerScope scope{companionBrain.planner};
    auto standScene = npcScene();
    auto placing = placementScene(); // by value: one copy for both ranges
    standScene.keepClear.insert(standScene.keepClear.end(), placing.keepClear.begin(), placing.keepClear.end());
    standScene.keepClear.insert(standScene.keepClear.end(), placing.zones.begin(), placing.zones.end());
    sim::CompanionView v;
    v.feet = feet;
    v.heading = companionHeading;
    v.warped = warped;
    v.airborne = noclip || movement != &walk || climbing.active();
    if (auto c = characterAim(); c && c->first == companionId && c->second < 8)
        v.lookedAt = true;
    companionView(v, walkScene, standScene);
    // Diagnostics: can the player see it, and did a teleport ever happen in view (a pop)?
    int teleportsBefore = companionBrain.teleports;
    bool seenBefore = !companionBrain.hidden() && v.inView(a.position);
    struct Seen {
        Runtime *r;
        NpcRun &n;
        sim::CompanionView &v;
        int before;
        bool seen;
        ~Seen() {
            r->companionInView = !r->companionBrain.hidden() && v.inView(n.agent.position);
            if (r->companionBrain.teleports > before && (seen || r->companionInView))
                ++r->companionPops;
        }
    } seen{this, n, v, teleportsBefore, seenBefore};
    // Focus: out of the focused view as the transition starts (tried twice a second while it is
    // still in view), then it waits there. The transition hides the cut: not a pop.
    if (focus) {
        if (!companionFocused)
            companionFocused = true, companionFocusRetry = 0;
        companionFocusRetry -= dt;
        if (mode != "route" && !companionBrain.hidden() && companionFocusRetry <= 0 && v.inView(a.position)) {
            companionFocusRetry = .5f;
            if (auto spot = companionBrain.outOfView(v)) {
                a.position = *spot;
                a.idle();
                companionBrain.slot.reset();
                ++companionFocusMoves;
            } else
                ++companionFocusMisses;
        }
        companionLastStep = "focus";
        return false;
    }
    companionFocused = false;
    if (world->collision.empty()) { // nothing to walk on: it stays where it is
        companionLastStep = "no collision";
        return false;
    }
    bool still = paused == "still" || paused == "reduced motion";
    if (!paused.empty() && !still) { // inactive: it stands where it is
        companionLastStep = "paused";
        return false;
    }
    companionLastStep = mode == "route" ? "route" : "brain";
    if (mode == "route") {
        if (!still)
            a.update(dt, w);
    } else {
        companionBrain.mode = mode == "stay" ? sim::Companion::Mode::Stay : sim::Companion::Mode::Follow;
        companionBrain.pace = companionPace() == "run" ? sim::Companion::Pace::Run : sim::Companion::Pace::Walk;
        if (mode == "stay")
            if (auto rec = editor.companion(world->id); rec.is_object() && rec.contains("pose"))
                companionBrain.stay = vec3(rec["pose"]["position"]);
        v.animate = !still;
        companionBrain.update(a, dt, v, w);
    }
    return companionBrain.moved || (mode == "route" && !still &&
                                    (std::hypot(a.position.x - before.x, a.position.z - before.z) > 1e-4f || std::abs(a.heading - heading) > 1e-4f));
}

// ---- carrying and setting down ---------------------------------------------------------
// The companion and the user's own characters can be carried (authored residents stay
// protected): Super+LMB picks one up to set it down on a route or a standing spot.
bool Runtime::carriable(const std::string &id) {
    if (id == companionId)
        return !companionBrain.hidden();
    auto *n = npcRun(id);
    return n && n->user && n->state == "active";
}
interaction::Body Runtime::carryBody(const std::string &id) {
    if (id == companionId)
        return companionBody;
    auto *info = objectInfo(id);
    auto *e = info ? catalogEntry(info->ref) : nullptr;
    return e ? bodyOf(*e) : interaction::Body{};
}
bool Runtime::characterPickUp() {
    auto c = characterAim();
    if (!c || !carried.empty() || !carriable(c->first))
        return false;
    auto *n = npcRun(c->first);
    if (!n)
        return false;
    if (tooFar(c->second, "reach_grab", "grab"))
        return true;
    carried = c->first;
    companionCarry = carried == companionId;
    carryFrom = n->agent.position;
    carryHeading = n->agent.heading;
    carryDistance = c->second;
    carryDrop = {};
    if (npcInteraction && npcInteraction->objectId == carried)
        endInteraction();
    if (!companionCarry)
        n->agent.idle();
    ++npcRevision;
    return true;
}
// Where a release would put it: the floor under the aim, snapped to a route of this world
// within 0.75 m (highlighted), else a free standing spot, else invalid.
void Runtime::characterCarryTick() {
    if (carried.empty() || !world)
        return;
    auto *n = npcRun(carried);
    if (!n) {
        carried.clear(), companionCarry = false;
        return;
    }
    auto body = carryBody(carried);
    auto o = pointerCamera().position, d = pointerRay();
    float reach = config["player"]["reach_grab"];
    auto hit = interaction::raycast(o, d, world->collision, &world->collisionIndex);
    float along = hit ? std::min(hit->distance, reach) : std::min(carryDistance, reach);
    Vec3 aimAt = o + d * along;
    auto scene = npcScene();
    auto floor = interaction::floorBelow(scene, aimAt + Vec3{0, .3f, 0}, 4);
    CarryDrop drop;
    if (floor) {
        drop.at = {aimAt.x, *floor, aimAt.z};
        // Routes of this world, and paths the user drew (they become routes when it is set down
        // on one): the nearest within the snap distance, walkable for its body.
        float best = carrySnap;
        auto consider = [&](Json p, const std::string &ref) {
            if (drawnPath(ref))
                p["use"] = "route";
            if (world::pathUse(p) != "route")
                return;
            auto route = world::routeOf(p);
            if (route.empty())
                return;
            float s = route.nearest(drop.at);
            Vec3 q = route.at(s);
            float dist = std::hypot(q.x - drop.at.x, q.z - drop.at.z);
            if (dist > best || std::abs(q.y - drop.at.y) > .5f || !routeWalkability(p, body).walkable)
                return;
            size_t bound = 0;
            for (auto &m : npcs)
                bound += m.path == ref && m.id != carried;
            if (bound >= world::charactersPerPath)
                return;
            best = dist;
            drop.route = ref, drop.s = s, drop.length = route.length(), drop.at = q;
            drop.samples = route.samples;
        };
        for (auto &p : world->paths)
            consider(p, "package/" + p["id"].get<std::string>());
        for (auto &p : editor.paths)
            if (p.value("worldId", "") == world->id)
                consider(p, "user/" + p["id"].get<std::string>());
        if (!drop.route.empty())
            drop.valid = true;
        else {
            auto strict = npcScene();
            for (auto &k : placementScene().keepClear)
                strict.keepClear.push_back(k);
            drop.valid = !interaction::checkSpot(strict, drop.at, body, true);
        }
    } else
        drop.at = aimAt - Vec3{0, .1f, 0};
    carryDrop = drop;
    // It hangs with its feet 0.1 m above the target, facing the player.
    n->agent.position = drop.at + Vec3{0, .1f, 0};
    Vec3 back = o - drop.at;
    if (std::hypot(back.x, back.z) > .05f)
        n->agent.heading = std::atan2(back.x, back.z);
}
// Highlights while carrying: the snapped route and the spot, or a red ring where it can't go.
void Runtime::characterCarryDraw() {
    if (carried.empty())
        return;
    auto decode = [](float v) { return v <= .04045f ? v / 12.92f : std::pow((v + .055f) / 1.055f, 2.4f); };
    auto c = renderer.overlayStyle["promptColor"];
    Vec3 glow{decode(c[0]), decode(c[1]), decode(c[2])}, red{1, .12f, .08f};
    auto &d = carryDrop;
    Vec3 lift{0, .04f, 0};
    for (size_t i = 1; i < d.samples.size(); ++i) {
        renderer.lines.push_back({d.samples[i - 1] + lift, d.samples[i] + lift, glow * .35f, .05f});
        renderer.lines.push_back({d.samples[i - 1] + lift, d.samples[i] + lift, glow, .015f});
    }
    Vec3 colour = d.valid ? glow : red;
    float r = carryBody(carried).radius + .1f;
    for (int k = 0; k < 24; ++k) {
        float a0 = k * .2618f, a1 = (k + 1) * .2618f;
        renderer.lines.push_back({d.at + lift + Vec3{std::sin(a0) * r, 0, std::cos(a0) * r},
                                  d.at + lift + Vec3{std::sin(a1) * r, 0, std::cos(a1) * r}, colour, .02f});
    }
}
// Release: binds to the snapped route, stays on a free spot, else goes back (never thrown).
void Runtime::characterPutDown() {
    auto id = std::exchange(carried, std::string());
    companionCarry = false;
    ++npcRevision;
    auto *n = npcRun(id);
    if (!n)
        return;
    auto drop = carryDrop;
    auto name = id == companionId ? companionName : n->name;
    try {
        if (drop.valid && !drop.route.empty()) {
            n->agent.position = drop.at;
            bindCharacter(id, {{"path", drop.route}, {"phase", std::clamp(drop.s / std::max(1e-3f, drop.length), 0.f, .999f)}});
            return;
        }
        if (drop.valid) {
            n->agent.position = drop.at;
            Json pose{{"position", vecJson(drop.at)}, {"rotation", yawRotation(n->agent.heading)}};
            if (id == companionId) {
                companionRecord({{"pose", pose}});
                objectNotice("companion.staying", companionName + " stays here");
            } else
                bindCharacter(id, {{"pose", pose}});
            return;
        }
    } catch (const ipc::Error &e) {
        objectNotice("placement.blocked", e.what());
    }
    n->agent.position = carryFrom;
    n->agent.heading = carryHeading;
    reticleFlash = {"invalid", "drop", -1, -1};
    reticleFlashUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(700);
    objectNotice("placement.blocked", name + " can't stand there");
}

// ---- IPC 0.25 --------------------------------------------------------------------------
std::optional<Json> Runtime::companionMethod(const std::string &method, const Json &params) {
    auto only = [&](std::initializer_list<const char *> required) {
        if (!params.is_object())
            throw ipc::Error(-32602, "params must be an object");
        for (auto k : required)
            if (!params.contains(k))
                throw ipc::Error(-32602, std::string("missing ") + k);
        if (params.size() != required.size())
            throw ipc::Error(-32602, "unknown parameter");
    };
    auto revision = [&] {
        publish();
        return Json{{"revision", ipc->revision()}};
    };
    if (method == "companion.set") {
        only({"ref"});
        if (!params["ref"].is_string())
            throw ipc::Error(-32602, "ref must be a string");
        std::string ref = params["ref"];
        auto *e = catalogEntry(ref);
        if (!e)
            throw ipc::Error(-32004, "unknown ref " + ref);
        auto verdict = companionCapability(*e);
        if (!verdict.is_object())
            throw ipc::Error(-32005, e->name + " is not a character");
        if (!verdict["capable"].get<bool>())
            throw ipc::Error(-32005, e->name + " can't be a companion: " + verdict["reason"].get<std::string>());
        if (ref != companionRef()) {
            Json next = {{"ref", ref}};
            if (companionPace() == "run")
                next["pace"] = "run"; // the pace stays with the next companion
            companionFile.save(next);
            companionFile.current = next;
            companionBrain = {};
            companionBrain.rejoin(true);
            ++companionRevision;
            objectsDirty = true;
        }
        objectNotice("companion.set", verdict["name"].get<std::string>() + " is your companion");
        return revision();
    }
    if (method == "companion.clear") {
        only({});
        if (!companionRef().empty()) {
            Json next = Json::object();
            if (companionPace() == "run")
                next["pace"] = "run"; // the pace stays for the next companion
            companionFile.save(next);
            companionFile.current = next;
            if (companionCarry)
                carried.clear(), companionCarry = false;
            ++companionRevision;
            objectsDirty = true;
            objectNotice("companion.cleared", "No companion");
        }
        return revision();
    }
    if (method == "companion.mode") {
        // IPC 0.26: an optional pace next to the mode (set first; it stays across modes).
        bool paced = params.is_object() && params.contains("pace") && ipc::protocol::atLeast(methodProtocol, "0.26");
        if (paced)
            only({"mode", "pace"});
        else
            only({"mode"});
        if (params["mode"] != "follow" && params["mode"] != "stay")
            throw ipc::Error(-32602, "mode must be follow or stay");
        if (paced && params["pace"] != "walk" && params["pace"] != "run")
            throw ipc::Error(-32602, "pace must be walk or run");
        if (paced && params["pace"] != companionPace())
            companionSetPace(params["pace"]);
        companionSetMode(params["mode"]);
        return revision();
    }
    throw ipc::Error(-32601, "unknown method");
}
Json Runtime::companionState() {
    auto ref = companionRef();
    std::string status = companionStatus;
    std::string reason = companionReason;
    if (ref.empty())
        status = "none", reason.clear();
    else if (!world || loading)
        status = "hidden", reason = "no world";
    else if (config["npc"]["motion"] == "off" && status == "active")
        status = "hidden", reason = "characters are off";
    auto *n = status == "active" ? npcRun(companionId) : nullptr;
    if (n && companionBrain.hidden() && companionMode() != "route")
        status = "waiting", reason = "no spot out of view";
    std::string mode = companionMode();
    Json activity = nullptr, position = nullptr;
    if (n) {
        std::string act = companionCarry                     ? "carried"
                          : status == "waiting"              ? "waiting"
                          : mode == "route"                  ? n->agent.activity()
                                                             : companionBrain.activity(n->agent);
        if (act == "interacting" || act == "talking")
            act = "talking";
        else if (act == "stopped" || act == "suspended" || act == "guiding")
            act = "idle";
        activity = act;
        position = vecJson(companionPublished);
    }
    return {{"ref", ref.empty() ? Json(nullptr) : Json(ref)},
            {"name", ref.empty() || companionName.empty() ? Json(nullptr) : Json(ipc::boundedText(companionName, 32))},
            {"state", status},
            {"reason", reason.empty() || status == "active" ? Json(nullptr) : Json(ipc::boundedText(reason, 64))},
            {"mode", mode},
            {"path", n && mode == "route" && !n->path.empty() ? Json(n->path) : Json(nullptr)},
            {"activity", activity},
            {"position", position},
            {"carried", companionCarry},
            {"pace", companionPace()}}; // IPC 0.26 (older peers: erased)
}
// Diagnostics (camera()["companion"]).
Json Runtime::companionDiagnostics() {
    auto *n = npcRun(companionId);
    auto &b = companionBrain;
    // Why the spot behind the player is not a follow slot (strict rules), when there is none.
    Json slotMiss = nullptr;
    if (world && n && !b.slot && companionMode() == "follow") {
        auto standScene = npcScene();
        auto placing = placementScene();
        standScene.keepClear.insert(standScene.keepClear.end(), placing.keepClear.begin(), placing.keepClear.end());
        standScene.keepClear.insert(standScene.keepClear.end(), placing.zones.begin(), placing.zones.end());
        Vec3 back = companionFeet - Vec3{std::sin(companionHeading), 0, std::cos(companionHeading)} * b.followDistance;
        auto floor = interaction::floorBelow(standScene, back + Vec3{0, 1.2f, 0}, 2.5f);
        auto why = floor ? interaction::checkSpot(standScene, {back.x, *floor, back.z}, companionBody, true) : std::nullopt;
        slotMiss = !floor ? Json("floor: none below") : why ? Json(why->rule + ": " + why->message) : Json("free");
    }
    return {{"state", companionState()},
            {"slotMiss", slotMiss},
            {"brain", b.state == sim::Companion::State::Idle ? "idle" : b.state == sim::Companion::State::Moving ? "moving" : "waiting"},
            {"teleports", b.teleports},
            {"slot", b.slot ? vecJson(*b.slot) : Json(nullptr)},
            {"slotLoose", b.slotLoose},
            {"stranded", b.stranded},
            {"waypoints", b.waypoints.size()},
            {"plannerExpanded", b.planner.expanded},
            {"running", b.running},
            {"pace", companionPace()},
            {"madeWay", b.madeWay},
            {"makingWay", b.wayFor >= 0},
            {"speed", n ? n->agent.speedNow : 0.f},
            {"stride", n ? n->agent.strideAt(n->agent.speedNow) : 0.f},
            {"gait", n ? Json{{"phase", n->agent.gaitPhase}, {"clip", n->agent.pose.clip}, {"previous", n->agent.pose.previous},
                              {"fade", n->agent.pose.fade}} : Json(nullptr)},
            {"run", n ? Json(n->state) : Json(nullptr)},
            {"step", companionLastStep},
            {"teleportPending", b.teleport},
            {"focusMoves", companionFocusMoves},
            {"focusMisses", companionFocusMisses},
            {"position", n ? vecJson(n->agent.position) : Json(nullptr)},
            {"heading", n ? n->agent.heading : 0},
            {"clip", n ? Json(n->agent.pose.clip) : Json(nullptr)},
            {"moving", companionMoving},
            {"inView", companionInView},
            {"pops", companionPops},
            {"feet", vecJson(companionFeet)},
            {"carry", companionCarry ? Json{{"valid", carryDrop.valid}, {"route", carryDrop.route}, {"at", vecJson(carryDrop.at)}} : Json(nullptr)}};
}
