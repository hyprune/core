// NPCs (RFC-0010 runtime rules, RFC-0011 P2: world format 0.9, IPC 0.23). Character
// instances are world object instances (authored objects[] with the character trait, user
// characters in the overlay); each active one has an agent (sim/Npc.hpp) on its route or
// standing spot. Agents update and characters are posed in the tick, never in the render
// pass; the renderer draws the poses it is handed (and the offload worker the same poses).
// Included by Plugin.cpp after Objects.inc.
namespace {
constexpr float npcPublishSeconds = .5f; // moving characters' positions: at most twice a second
constexpr const char *companionId = "companion"; // the companion's reserved object id (Companion.inc)
float yawOf(const std::array<float, 4> &q) {
    auto f = rotate(q, {0, 0, 1});
    return std::atan2(f.x, f.z);
}
std::string qualifiedPath(const std::string &ref) {
    return ref.starts_with("package/") || ref.starts_with("user/") ? ref : "package/" + ref;
}
Json vecJson(Vec3 v) { return Json::array({v.x, v.y, v.z}); }
} // namespace

// ---- paths ------------------------------------------------------------------------
// The path record a qualified ref names in this world (package paths, then this world's
// user paths), or null.
Json Runtime::pathRecord(const std::string &ref) const {
    if (!world)
        return nullptr;
    auto q = qualifiedPath(ref);
    if (q.starts_with("package/")) {
        for (auto &p : world->paths)
            if (p["id"] == q.substr(8))
                return p;
        return nullptr;
    }
    for (auto &p : editor.paths)
        if (p["id"] == q.substr(5) && p.value("worldId", "") == world->id)
            return p;
    return nullptr;
}
// The walkability scene: collision, the playable union, approach zones and keepClear
// volumes (placementScene without windows or the player), and user-placed objects without a
// collision proxy as their bounds (characters walk round them too).
interaction::PlacementScene Runtime::npcScene() {
    auto s = placementScene();
    s.ignore = {};
    s.windows.clear();
    s.player = false;
    if (world)
        for (auto &info : objectInfos) {
            if (info.source != "user" || info.character || info.index < 0 || size_t(info.index) >= world->objectInstances.size())
                continue;
            auto &inst = world->objectInstances[info.index];
            if (inst.hidden || inst.collisionCount > 0 || inst.asset >= world->objectAssets.size())
                continue;
            auto &asset = world->objectAssets[inst.asset];
            interaction::Box b;
            b.centre = inst.model.point((asset.low + asset.high) * .5f);
            Vec3 half = (asset.high - asset.low) * .5f;
            for (int k = 0; k < 3; ++k) {
                Vec3 axis{inst.model.v[k * 4], inst.model.v[k * 4 + 1], inst.model.v[k * 4 + 2]};
                float scale = length(axis);
                b.axes[k] = scale > 1e-6f ? axis / scale : Vec3{float(k == 0), float(k == 1), float(k == 2)};
                (k == 0 ? half.x : k == 1 ? half.y : half.z) *= scale;
            }
            b.half = half;
            s.obstacles.push_back(b);
        }
    return s;
}
// Route problems, cached by the path record and the collision they were checked against. A
// stretch where the body hits collision (a placed object, a pillar) gets a detour: planned on
// the standing-spot grid from 0.6 m before it to 0.6 m after it. Other problems (no floor, a
// ledge, outside the playable volume, a screen approach), or a stretch with no way round,
// leave the route unwalkable.
const Runtime::Walkability &Runtime::routeWalkability(const Json &path, const interaction::Body &body) {
    auto key = path.dump() + "\n" + std::to_string(builtObjects) + "\n" + std::to_string(body.radius) + std::to_string(body.height);
    auto it = walkCache.find(key);
    if (it != walkCache.end())
        return it->second;
    if (walkCache.size() > 256)
        walkCache.clear();
    Walkability w;
    auto shape = world::pathProblems(path);
    for (auto &p : shape)
        w.problems.push_back(p);
    if (shape.empty()) {
        auto scene = npcScene();
        auto route = world::routeOf(path);
        std::vector<float> blocked;
        for (auto &p : interaction::checkRoute(scene, route, body, 256)) {
            if (p.rule == "walls" && p.along >= 0)
                blocked.push_back(p.along);
            else if (w.problems.size() < 8)
                w.problems.push_back(p.message);
        }
        // Runs of blocked samples (0.25 m apart); a closed route's last run joins its first.
        float l = route.length();
        std::vector<std::pair<float, float>> spans;
        for (float d : blocked)
            if (!spans.empty() && d - spans.back().second <= interaction::walkStep * 1.6f)
                spans.back().second = d;
            else
                spans.push_back({d, d});
        if (route.closed && spans.size() > 1 && spans.front().first < interaction::walkStep * 1.6f &&
            l - spans.back().second < interaction::walkStep * 1.6f) {
            spans.front().first = spans.back().first - l;
            spans.pop_back();
        }
        sim::GridPlanner planner;
        planner.range = 12;
        planner.budget = 800;
        planner.stand = [&](Vec3 p) -> std::optional<Vec3> {
            auto floor = interaction::floorAt(scene, p);
            if (!floor)
                return std::nullopt;
            Vec3 root{p.x, *floor, p.z};
            if (interaction::checkSpot(scene, root, body))
                return std::nullopt;
            return root;
        };
        planner.leg = [&](Vec3 a, Vec3 b) { return interaction::checkLeg(scene, a, b, body, 1).empty(); };
        auto at = [&](float d) { return route.at(route.closed ? route.wrap(d) : d); };
        auto clear = [&](float d) { return !interaction::checkSpot(scene, at(d), body); };
        for (auto [first, last] : spans) {
            float enter = first - .6f, exit = last + .6f;
            while (!clear(enter) && enter > first - 2.5f)
                enter -= interaction::walkStep;
            while (!clear(exit) && exit < last + 2.5f)
                exit += interaction::walkStep;
            std::optional<std::vector<Vec3>> legs;
            if ((route.closed || (enter >= 0 && exit <= l)) && clear(enter) && clear(exit))
                legs = planner.plan(at(enter), at(exit), .1f);
            if (legs && !legs->empty() && std::hypot(legs->back().x - at(exit).x, legs->back().z - at(exit).z) < .05f) {
                if (route.closed && enter < 0)
                    enter += l, exit += l;
                w.detours.push_back({enter, exit, *legs});
            } else if (w.problems.size() < 8)
                w.problems.push_back("blocked with no way round at " +
                                     std::format("({:.2f}, {:.2f}, {:.2f})", at(first).x, at(first).y, at(first).z));
        }
        std::sort(w.detours.begin(), w.detours.end(), [](auto &a, auto &b) { return a.enter < b.enter; });
    }
    w.walkable = w.problems.empty();
    return walkCache[key] = std::move(w);
}
// paths.list entries: package paths, then this world's user paths.
Json Runtime::pathsList() {
    Json out = Json::array();
    if (!world)
        return out;
    auto entry = [&](const Json &p, const std::string &source) {
        std::string ref = source + "/" + p["id"].get<std::string>();
        Json bound = Json::array();
        for (auto &n : npcs)
            if (n.path == ref && bound.size() < 8 && (!n.companion || ipc::protocol::atLeast(methodProtocol, "0.25")))
                bound.push_back(n.id);
        bool route = world::pathUse(p) == "route";
        Json walkable = nullptr, problems = Json::array();
        if (route) {
            auto &w = routeWalkability(p, {});
            walkable = w.walkable;
            for (auto &m : w.problems)
                if (problems.size() < 8)
                    problems.push_back(ipc::boundedText(m, 160));
        }
        Json points = Json::array();
        for (auto &pt : p.value("points", Json::array()))
            points.push_back({{"position", pt["position"]}, {"type", pt.value("type", "line")}});
        return Json{{"ref", ref},
                    {"source", source},
                    {"id", p["id"]},
                    {"name", p.contains("name") && p["name"].is_string() ? p["name"] : Json(nullptr)},
                    {"use", world::pathUse(p)},
                    {"closed", p.value("closed", false)},
                    {"points", points},
                    {"speed", p.contains("speed") && p["speed"].is_number() ? p["speed"] : Json(nullptr)},
                    {"stops", p.value("stops", Json::array())},
                    {"walkable", walkable},
                    {"problems", problems},
                    {"bound", bound}};
    };
    for (auto &p : world->paths)
        if (out.size() < 192)
            out.push_back(entry(p, "package"));
    for (auto &p : editor.paths)
        if (p.value("worldId", "") == world->id && out.size() < 192)
            out.push_back(entry(p, "user"));
    return out;
}

// ---- seats --------------------------------------------------------------------------
// A seat reference resolved to world space: a world seats[] id, "<objectId>/<anchorId>",
// or "<objectId>" (the anchor nearest to `near`). Objects' anchors move with the instance.
std::optional<sim::NpcSeat> Runtime::resolveSeat(const std::string &ref, Vec3 near) {
    if (!world)
        return std::nullopt;
    for (auto &s : world->seats)
        if (s["id"] == ref) {
            sim::NpcSeat seat;
            seat.ref = ref;
            seat.position = vec3(s["pose"]["position"]);
            seat.yaw = yawOf(s["pose"]["rotation"].get<std::array<float, 4>>());
            seat.height = s["height"];
            seat.approach = s.value("approach", .6f);
            return seat;
        }
    auto slash = ref.find('/');
    auto *info = objectInfo(ref.substr(0, slash));
    if (!info || info->index < 0 || info->state != "active")
        return std::nullopt;
    auto &inst = world->objectInstances[info->index];
    auto &traits = world->objectAssets[inst.asset].traits;
    if (!traits.is_object() || !traits.contains("seat"))
        return std::nullopt;
    std::optional<sim::NpcSeat> best;
    float bestDistance = 1e30f;
    for (auto &a : traits["seat"].value("anchors", Json::array())) {
        if (slash != std::string::npos && a["id"] != ref.substr(slash + 1))
            continue;
        sim::NpcSeat seat;
        seat.ref = info->id + "/" + a["id"].get<std::string>();
        seat.position = inst.model.point(vec3(a["position"]));
        seat.yaw = yawOf(inst.rotation) + a["yaw"].get<float>() * 3.14159265f / 180;
        seat.height = a["height"].get<float>() * inst.scale;
        seat.approach = a.value("approach", .6f);
        float d = length(seat.position - near);
        if (d < bestDistance)
            best = seat, bestDistance = d;
    }
    return best;
}
bool Runtime::reserveSeat(const std::string &ref) {
    auto object = ref.substr(0, ref.find('/'));
    if (seatTaken.contains(ref))
        return false;
    // The seat object's capacity (default: every anchor).
    if (auto *info = objectInfo(object); info && info->index >= 0) {
        auto &traits = world->objectAssets[world->objectInstances[info->index].asset].traits;
        if (traits.is_object() && traits.contains("seat")) {
            size_t capacity = traits["seat"].value("capacity", traits["seat"].value("anchors", Json::array()).size());
            size_t used = 0;
            for (auto &taken : seatTaken)
                used += taken.starts_with(object + "/");
            if (used >= capacity)
                return false;
        }
    }
    seatTaken.insert(ref);
    return true;
}

// ---- agents ---------------------------------------------------------------------------
// Rebuilds agents whose configuration changed (instance, overlay record, path, seats,
// collision); unchanged ones keep their state. Characters that are hidden, missing or
// blocked have no agent.
// Where a resident stands to look at a screen (a stop facing an anchor): on the screen's
// normal through its centre, just outside its approach zone (1.5 m deep, kept free), on the
// floor; walkable, with straight legs from the route point and back. None: it stops on the
// route point and turns to the screen from there.
std::optional<Vec3> Runtime::screenFront(const Json &anchor, Vec3 from, const interaction::Body &body,
                                         const interaction::PlacementScene &scene) {
    if (!anchor.contains("pose"))
        return std::nullopt;
    Vec3 centre = vec3(anchor["pose"]["position"]);
    auto q = anchor["pose"]["rotation"].get<std::array<float, 4>>();
    Vec3 n = rotate(q, {0, 0, 1});
    n.y = 0;
    if (length(n) < .5f)
        return std::nullopt; // a floor or ceiling screen has no front to stand at
    n = normalized(n);
    for (float d : {1.5f + body.radius + .15f, 2.3f, 2.8f}) {
        Vec3 p = centre + n * d;
        p.y = from.y;
        auto floor = interaction::floorAt(scene, p, 1.5f);
        if (!floor)
            continue;
        Vec3 root{p.x, *floor, p.z};
        if (!interaction::checkSpot(scene, root, body) && interaction::checkLeg(scene, from, root, body, 1).empty() &&
            interaction::checkLeg(scene, root, from, body, 1).empty())
            return root;
    }
    return std::nullopt;
}
void Runtime::npcSync() {
    auto section = timed("npcs.sync");
    npcSynced = {instanceRevision, pathRevision, builtObjects, companionRevision};
    if (!world) {
        npcs.clear();
        return;
    }
    std::vector<NpcRun> next;
    auto scene = npcScene();
    for (auto &info : objectInfos) {
        if (!info.character || info.index < 0 || info.state != "active" || size_t(info.index) >= world->objectInstances.size())
            continue;
        auto &inst = world->objectInstances[info.index];
        auto &asset = world->objectAssets[inst.asset];
        if (!asset.character)
            continue;
        // Configuration: the authored objects[] entry or the user record.
        Json config = Json::object();
        bool companion = info.source == "companion";
        if (info.source == "authored") {
            for (auto &o : world->manifest.value("objects", Json::array()))
                if (o["id"] == info.id)
                    config = o;
        } else if (companion) {
            // A route, a stay spot or (follow) where it is now.
            config = companionConfig();
            if (!config.is_object())
                config = Json{{"mode", "follow"}};
            config["interactions"] = companionInteractions();
        } else
            config = editor.placement(world->id, info.id);
        std::string path = config.contains("path") && config["path"].is_string() ? qualifiedPath(config["path"]) : "";
        Json pathJson = path.empty() ? Json(nullptr) : pathRecord(path);
        Json traits = asset.traits.contains("character") ? asset.traits["character"] : Json::object();
        interaction::Body body{traits.value("radius", .35f), traits.value("height", 1.75f)};
        auto signature = config.dump() + "\n" + pathJson.dump() + "\n" + std::to_string(builtObjects) + "\n" +
                         std::to_string(inst.asset) + "\n" + (companion ? "companion" : std::to_string(instanceRevision));
        auto old = std::find_if(npcs.begin(), npcs.end(), [&](auto &n) { return n.id == info.id; });
        if (old != npcs.end() && old->signature == signature) {
            next.push_back(std::move(*old));
            continue;
        }
        if (old != npcs.end() && old->agent.seat >= 0 && old->agent.seat < int(old->agent.seats.size()))
            seatTaken.erase(old->agent.seats[old->agent.seat].ref);
        NpcRun n;
        n.id = info.id;
        n.name = info.name;
        n.user = info.source == "user";
        n.companion = companion;
        n.asset = inst.asset;
        n.character = asset.character;
        n.signature = signature;
        n.path = path;
        auto &c = *asset.character;
        auto &a = n.agent;
        for (auto &clip : c.clips)
            a.clips[clip.name] = clip.duration;
        auto clips = traits.value("clips", Json::object());
        if (auto walk = c.clip("walk"); walk && walk->speed > 0)
            a.walkSpeed = walk->speed;
        if (clips.contains("walk") && clips["walk"].contains("speed"))
            a.walkSpeed = clips["walk"]["speed"];
        if (auto run = c.clip("run"); run && run->speed > 0)
            a.runSpeed = run->speed;
        // Seat fit: object.json's measured seat height, else core's measurement; the sink
        // (ankles below idle) always from the clip.
        auto fit = seatFits.find(asset.character.get());
        if (fit == seatFits.end())
            fit = seatFits.emplace(asset.character.get(), c.measureSeat()).first;
        a.seatKnown = fit->second.valid;
        a.seatHeight = clips.contains("sit") && clips["sit"].contains("seatHeight") ? clips["sit"]["seatHeight"].get<float>()
                                                                                    : fit->second.seatHeight;
        a.seatSink = fit->second.sink;
        if (auto sit = c.clip("sit"))
            a.sitTravel = std::clamp(sit->speed * sit->duration, 0.f, .8f);
        a.height = body.height, a.radius = body.radius;
        a.rate = std::clamp(config.value("speed", 1.f), .05f, 10.f);
        a.phase = config.value("phase", 0.f);
        a.reverse = config.value("reverse", false);
        a.reserveSeat = [this, id = n.id](int seat) {
            auto *run = npcRun(id);
            return run && seat < int(run->agent.seats.size()) && reserveSeat(run->agent.seats[seat].ref);
        };
        a.releaseSeat = [this, id = n.id](int seat) {
            if (auto *run = npcRun(id); run && seat < int(run->agent.seats.size()))
                seatTaken.erase(run->agent.seats[seat].ref);
        };
        if (config.contains("accent") && config["accent"].is_array())
            n.accent = vec3(config["accent"]);
        n.interactions = config.contains("interactions") ? config["interactions"] : traits.value("interactions", Json::array());
        bool suspended = false;
        if (!pathJson.is_null()) {
            a.route = world::routeOf(pathJson);
            a.speed = pathJson.value("speed", 0.f);
            auto &walk = routeWalkability(pathJson, body);
            suspended = !walk.walkable;
            a.detours = walk.detours;
            if (suspended)
                objectNotice("npc.suspended", info.name + " stands still: its route is not walkable (" +
                                                  (walk.problems.empty() ? std::string("unknown") : walk.problems[0]) + ")",
                             "suspended:" + info.id + signature);
            // Stops: their arc length, clip, facing and seat (with walkable approach legs).
            auto distances = a.route.points;
            for (auto &s : pathJson.value("stops", Json::array())) {
                int point = s["point"];
                if (point < 0 || size_t(point) >= distances.size())
                    continue;
                sim::NpcStop stop;
                stop.point = point;
                stop.at = distances[point];
                stop.seconds = s.value("seconds", 3.f);
                stop.clip = s.value("clip", std::string());
                if (a.detourAt(stop.at) >= 0)
                    continue; // inside a blocked stretch: walked round, not stopped at
                Vec3 at = a.route.at(stop.at);
                if (s.contains("face")) {
                    auto &f = s["face"];
                    if (f.contains("position"))
                        stop.face = vec3(f["position"]);
                    else if (f.contains("object")) {
                        if (auto *o = objectInfo(f["object"]))
                            stop.face = o->position;
                    } else if (f.contains("anchor"))
                        for (auto &an : world->anchors)
                            if (Json(an.value("id", "")) == f["anchor"] && an.contains("pose")) {
                                stop.face = vec3(an["pose"]["position"]);
                                stop.spot = screenFront(an, at, body, scene);
                                if (!stop.spot)
                                    objectNotice("npc.suspended", info.name + " stops beside " + f["anchor"].get<std::string>() +
                                                                      ": there is no walkable spot in front of it",
                                                 "front:" + info.id + f["anchor"].get<std::string>() + signature);
                            }
                }
                if (s.contains("seat"))
                    if (auto seat = resolveSeat(s["seat"], at)) {
                        Vec3 approach = seat->position + seat->forward() * seat->approach;
                        bool legs = interaction::checkLeg(scene, at, approach, body, 1).empty() &&
                                    interaction::checkLeg(scene, approach, seat->position, body, 1).empty();
                        if (legs) {
                            stop.seat = int(a.seats.size());
                            a.seats.push_back(*seat);
                        } else
                            objectNotice("npc.suspended", info.name + " won't sit at " + seat->ref + ": the way to it is blocked",
                                         "seatlegs:" + info.id + seat->ref + signature);
                    }
                a.stops.push_back(stop);
            }
        } else {
            bool posed = info.source == "user" || (companion && config.contains("pose"));
            a.home = posed ? vec3(config["pose"]["position"]) : inst.position;
            a.homeHeading = yawOf(posed ? config["pose"]["rotation"].get<std::array<float, 4>>() : inst.rotation);
            if (config.contains("seat") && config["seat"].is_string())
                if (auto seat = resolveSeat(config["seat"], a.home); seat && reserveSeat(seat->ref)) {
                    a.homeSeat = int(a.seats.size());
                    a.seats.push_back(*seat);
                }
        }
        a.start();
        if (a.homeSeat >= 0 && a.mode != sim::NpcAgent::Mode::Seated)
            seatTaken.erase(a.seats[a.homeSeat].ref); // didn't fit: stands at its spot
        if (suspended)
            a.suspend();
        // Carry over where a rebuilt character was, when it stays on the same route.
        if (old != npcs.end() && old->path == path && !path.empty() && !suspended) {
            a.s = a.route.wrap(old->agent.s);
            a.direction = old->agent.direction;
            a.heading = old->agent.heading;
            a.position = a.route.at(a.s);
        } else if (companion && old != npcs.end()) {
            // The companion keeps its place and pose across a mode change; put on a route, it
            // walks to its starting point first (a leg like a seat's return).
            a.position = old->agent.position, a.heading = old->agent.heading, a.pose = old->agent.pose;
            if (!a.route.empty() && !suspended) {
                Vec3 join = a.route.at(a.s);
                if (std::hypot(join.x - a.position.x, join.z - a.position.z) > .05f) {
                    auto walkScene = companionWalkScene();
                    CompanionPlannerScope scope{companionBrain.planner};
                    companionPlanner(walkScene);
                    auto legs = companionBrain.planner.plan(a.position, join);
                    a.legs = legs ? *legs : std::vector<Vec3>{join};
                    a.mode = sim::NpcAgent::Mode::FromSeat;
                }
            }
        }
        n.state = suspended ? "suspended" : "active";
        n.light = std::nullopt;
        next.push_back(std::move(n));
    }
    // Released seats of characters that went away.
    for (auto &n : npcs)
        if (std::none_of(next.begin(), next.end(), [&](auto &m) { return m.id == n.id; }) && n.agent.seat >= 0 &&
            n.agent.seat < int(n.agent.seats.size()))
            seatTaken.erase(n.agent.seats[n.agent.seat].ref);
    npcs = std::move(next);
    if (npcInteraction && !npcRun(npcInteraction->objectId))
        npcInteraction.reset();
    ++npcRevision;
}
Runtime::NpcRun *Runtime::npcRun(const std::string &id) {
    for (auto &n : npcs)
        if (n.id == id)
            return &n;
    return nullptr;
}
void Runtime::npcWorldLoaded() {
    npcs.clear();
    seatTaken.clear();
    seatFits.clear();
    walkCache.clear();
    npcInteraction.reset();
    npcDrawing.reset();
    npcAimed = nullptr;
    npcSynced = {};
    companionBrain.rejoin(true); // comes along: out of view, a wave when seen
    carried.clear(), companionCarry = false;
    companionFocused = false;
    companionFeet = camera.position - Vec3{0, camera.eyeHeight, 0};
    renderer.npcs.clear();
    renderer.releaseCharacters();
}
// Motion policy (RFC-0010 "Frames only when someone is looking", sim/NpcMotion.hpp).
sim::NpcMotionInput Runtime::npcMotion() {
    return {.motion = config["npc"]["motion"],
            .reducedMotion = config["graphics"]["reduced_motion"] == true,
            .shown = active && !loading,
            .focus = !layout.desktop.is_null() || fullscreen.phase};
}
// The reason agents are paused, or "" when they animate.
std::string Runtime::npcPaused() { return sim::npcPauseReason(npcMotion()); }
void Runtime::npcTick(float dt) {
    if (!world || loading) {
        renderer.npcs.clear();
        return;
    }
    if (npcSynced != std::array<uint64_t, 4>{instanceRevision, pathRevision, builtObjects, companionRevision})
        npcSync();
    if (npcs.empty()) {
        npcState = "none", npcReason.clear();
        renderer.npcs.clear();
        npcInteraction.reset();
        npcAimed = nullptr;
        return;
    }
    auto paused = npcPaused();
    std::string motion = config["npc"]["motion"];
    auto state = paused.empty() ? "animating" : paused == "off" || paused == "still" ? paused : std::string("paused");
    auto reason = paused == "off" || paused == "still" ? "" : paused;
    if (state != npcState || reason != npcReason)
        npcState = state, npcReason = reason, ++npcRevision;
    if (motion == "off") {
        renderer.npcs.clear();
        endInteraction(); // a guide is cancelled: it walks back when motion returns
        npcAimed = nullptr;
        return;
    }
    bool animate = paused.empty();
    bool still = motion == "still" || config["graphics"]["reduced_motion"] == true;
    if (still && npcInteraction && npcInteraction->type == "guide") // set to still mid-guide: it ends
        endInteraction();
    bool present = active && !typing && layout.desktop.is_null() && !freeCursor && !layerCursor;
    auto scene = npcScene();
    sim::NpcWorld w;
    w.player = camera.position;
    npcPlayer = w.player, npcPresent = present;
    w.present = present;
    w.glance = present;
    w.walkable = [&](Vec3 p) { return !interaction::checkSpot(scene, p, {}).has_value(); };
    // Steering (sim/Steer.hpp): every character as of now, so the update order changes
    // nothing. Priorities: residents and user characters in order, the companion last.
    std::vector<sim::Mover> movers;
    for (size_t i = 0; i < npcs.size(); ++i) {
        auto &n = npcs[i];
        auto &a = n.agent;
        a.id = int(i);
        a.priority = n.companion ? -1 : 1000 - int(i);
        if (n.state != "active" || n.id == carried || (n.companion && companionBrain.hidden()))
            continue;
        movers.push_back({a.id, a.position, animate ? a.velocity : Vec3{}, a.radius, a.priority,
                          Vec3{std::sin(a.heading), 0, std::cos(a.heading)}, animate && a.heldUp()});
    }
    w.movers = &movers;
    npcInteractionTick();
    renderer.npcs.resize(npcs.size());
    auto now = std::chrono::steady_clock::now();
    bool moved = false;
    for (size_t i = 0; i < npcs.size(); ++i) {
        auto &n = npcs[i];
        auto &a = n.agent;
        auto &c = *n.character;
        bool moving = animate && n.state == "active";
        if (n.companion) {
            moving = n.state == "active" && companionStep(n, dt, w, paused);
            companionMoving = moving;
        } else if (n.id == carried) { // hangs from the grab point (characterCarryTick), idling
            a.advance(dt, w);
            a.idle();
            moving = true;
        }
        else if (animate && n.state == "active")
            a.update(dt, w);
        if (animate || still || n.skin.empty() || n.stillPosed != still) {
            world::Character::PoseInput in;
            if (still) {
                // Posed still: seated at a seat, else the calm idle's first frame.
                in.to = c.clip(a.seated() && a.has("sit_idle") ? "sit_idle" : "idle");
            } else {
                in.from = a.pose.previous.empty() ? nullptr : c.clip(a.pose.previous);
                in.to = c.clip(a.pose.clip);
                in.fromTime = a.pose.previousTime, in.toTime = a.pose.clipTime;
                in.fromLoop = a.pose.previousLoop, in.toLoop = a.pose.loop;
                in.weight = in.from ? a.pose.fade : 1;
                in.lookYaw = a.pose.lookYaw, in.lookPitch = a.pose.lookPitch;
            }
            if (!in.to)
                in.to = c.clip("idle");
            c.evaluate(in, n.skin);
            n.posed = in;
            n.stillPosed = still;
        }
        auto model = a.model();
        // The instance follows: culling, regions (every metre), probe light (every 2 m, off-thread).
        if (auto *info = objectInfo(n.id); info && info->index >= 0 && size_t(info->index) < world->objectInstances.size()) {
            auto &inst = world->objectInstances[info->index];
            auto &asset = world->objectAssets[inst.asset];
            inst.model = model;
            inst.position = a.position;
            inst.center = model.point(asset.center);
            if (length(a.position - n.regionAt) > 1) {
                n.regionAt = a.position;
                world::objectRegions(*world, size_t(info->index));
            }
            if (n.light && n.light->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                world::applyProbeLight(inst, n.light->get());
                n.light.reset();
            }
            if (!n.light && (length(a.position - n.lightAt) > 2 || !n.lit)) {
                n.lightAt = a.position, n.lit = true;
                auto copy = inst;
                auto wp = world;
                n.light = std::async(std::launch::async, [wp, copy] { return world::probeLight(*wp, copy); });
            }
            n.probe = {inst.ambientUp.x, inst.ambientUp.y, inst.ambientUp.z, inst.sun,
                       inst.ambientDown.x, inst.ambientDown.y, inst.ambientDown.z, inst.sunVisibility};
            // Published positions: at most twice a second, when they changed.
            std::string activity = n.companion ? companionBrain.activity(a) + (companionCarry ? "c" : "") : a.activity();
            if (std::chrono::duration<float>(now - n.published).count() >= npcPublishSeconds &&
                (length(a.position - info->position) > .05f || n.activity != activity || n.seat != a.seatRef())) {
                info->position = a.position;
                if (n.companion)
                    companionPublished = a.position;
                info->rotation = {0, std::sin(a.heading * .5f), 0, std::cos(a.heading * .5f)};
                n.published = now;
                n.activity = activity;
                n.seat = a.seatRef();
                moved = true;
            }
        }
        // Occlusion: a few rays to its bounds against opaque scene geometry, only when the eye
        // or the character moved. With visibility culling off nothing is hidden.
        if (!renderer.graphics.value("visibility_culling", true))
            n.occluded = false, n.occludedEye = n.occludedAt = {1e9f, 1e9f, 1e9f};
        else if (length(camera.position - n.occludedEye) > .05f || length(a.position - n.occludedAt) > .05f) {
            n.occludedEye = camera.position, n.occludedAt = a.position;
            n.occluded = world::characterOccluded(*world, camera.position, model, c.lo, c.hi);
        }
        bool hidden = n.companion && companionBrain.hidden() && !companionCarry && companionMode() != "route";
        renderer.npcs[i] = {hidden ? nullptr : n.character, model, n.skin, n.accent, moving, n.probe, n.occluded};
    }
    // New characters upload here, never in the render pass.
    Render::GL::g_pHyprOpenGL->makeEGLCurrent();
    renderer.prepareCharacters();
    if (moved)
        publish();
}

// ---- picking, protection and interactions ---------------------------------------------
// The nearest active character under the reticle (a capsule of its radius and height),
// with its distance; nothing behind collision.
std::optional<std::pair<std::string, float>> Runtime::characterAim() {
    if (!world || npcs.empty() || config["npc"]["motion"] == "off")
        return std::nullopt;
    auto o = pointerCamera().position, d = pointerRay();
    std::optional<std::pair<std::string, float>> best;
    for (auto &n : npcs) {
        auto &a = n.agent;
        Vec3 base = a.position + Vec3{0, a.seatOffset, 0};
        float r = a.radius;
        // Ray against the vertical cylinder, then the height range.
        float ox = o.x - base.x, oz = o.z - base.z, dx = d.x, dz = d.z;
        float qa = dx * dx + dz * dz, qb = 2 * (ox * dx + oz * dz), qc = ox * ox + oz * oz - r * r;
        if (qa < 1e-9f)
            continue;
        float disc = qb * qb - 4 * qa * qc;
        if (disc < 0)
            continue;
        float t = (-qb - std::sqrt(disc)) / (2 * qa);
        if (t < 0)
            t = (-qb + std::sqrt(disc)) / (2 * qa);
        float y = o.y + d.y * t;
        float top = base.y + (a.seated() ? a.height * .75f : a.height);
        if (t < 0 || y < base.y || y > top)
            continue;
        if (!best || t < best->second)
            best = {{n.id, t}};
    }
    if (best)
        if (auto wall = interaction::raycast(o, d, world->collision, &world->collisionIndex); wall && wall->distance < best->second)
            return std::nullopt;
    return best;
}
// A gesture aimed at a character: refused, the reticle invalid for a moment and at most one
// object.protected notice every 10 s (RFC-0011 "Characters are protected").
bool Runtime::protectCharacter(const std::string &what, const char *action) {
    auto c = characterAim();
    if (!c)
        return false;
    reticleFlash = {"invalid", action, c->second, -1};
    reticleFlashUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(700);
    auto now = std::chrono::steady_clock::now();
    if (now - protectedNotice > std::chrono::seconds(10)) {
        protectedNotice = now;
        auto *n = npcRun(c->first);
        objectNotice("object.protected", what + " refused: " + (n ? n->name : c->first) + " is a character");
    }
    ++npcProtected;
    return true;
}
Json Runtime::interactionList(const NpcRun &n) const {
    Json out = Json::array();
    for (auto &i : n.interactions)
        if (out.size() < 8)
            out.push_back({{"id", i["id"]}, {"label", i["label"]}, {"type", i["action"]["type"]}});
    return out;
}
// Starts an interaction of a character (or the choice list when it has several and none is named).
void Runtime::beginInteraction(const std::string &objectId, const std::string &interactionId) {
    auto *n = npcRun(objectId);
    // Only the user's settings stop a request: refused, not accepted and never carried out.
    auto motion = npcMotion();
    if (auto refusal = sim::npcRefusal(motion, false); n && !refusal.empty())
        throw ipc::Error(-32005, refusal);
    if (!n || n->interactions.empty() || n->state != "active")
        throw ipc::Error(-32004, "no interaction for " + objectId);
    if (typing || !layout.desktop.is_null())
        throw ipc::Error(-32005, "interactions never run while a window is in use");
    const Json *chosen = nullptr;
    if (!interactionId.empty()) {
        for (auto &i : n->interactions)
            if (i["id"] == interactionId)
                chosen = &i;
        if (!chosen)
            throw ipc::Error(-32004, "unknown interaction " + interactionId);
    } else if (n->interactions.size() == 1)
        chosen = &n->interactions[0];
    if (auto refusal = sim::npcRefusal(motion, chosen && (*chosen)["action"]["type"] == "guide"); !refusal.empty())
        throw ipc::Error(-32005, refusal);
    if (npcInteraction && npcInteraction->objectId != objectId)
        endInteraction();
    NpcInteraction it;
    it.objectId = objectId;
    it.started = std::chrono::steady_clock::now();
    if (!n->agent.canInteract() && n->agent.mode != sim::NpcAgent::Mode::Interact)
        throw ipc::Error(-32005, n->name + " is busy");
    n->agent.beginInteract(camera.position);
    if (!chosen) {
        it.type = "choose";
        it.label = n->name;
        for (auto &c : interactionList(*n)) // choices are {id, label} on the wire
            it.choices.push_back({{"id", c["id"]}, {"label", c["label"]}});
        npcInteraction = it;
        ++npcRevision;
        return;
    }
    auto &action = (*chosen)["action"];
    if (action["type"] == "companion") { // Stay or Follow, or its pace: done at once
        n->agent.endInteract();
        if (npcInteraction && npcInteraction->objectId == objectId)
            npcInteraction.reset();
        if (action.contains("pace"))
            companionSetPace(action["pace"]);
        else
            companionSetMode(action["mode"]);
        ++npcRevision;
        return;
    }
    it.interactionId = (*chosen)["id"];
    it.label = (*chosen)["label"];
    it.type = action["type"];
    if (it.type == "say" && n->companion) { // the next of its lines, each time
        auto lines = companionLines();
        it.lines = lines.empty() ? action["lines"] : Json::array({lines[companionLine++ % lines.size()]});
    } else if (it.type == "say")
        it.lines = action["lines"];
    else if (it.type == "panel")
        it.panel = action["panel"];
    else if (it.type == "waypoint") {
        auto &t = action["target"];
        Json target = t;
        if (t.contains("anchor")) {
            for (auto &an : world->anchors)
                if (Json(an.value("id", "")) == t["anchor"] && an.contains("pose"))
                    target = {{"position", an["pose"]["position"]}};
        } else if (t.contains("object")) {
            if (auto *o = objectInfo(t["object"]))
                target = {{"position", vecJson(o->position)}};
        }
        if (!target.contains("position") && !target.contains("zoneId"))
            throw ipc::Error(-32004, "waypoint target not found");
        control.execute("waypoint.set",
                        {{"id", "npc-" + objectId + "-" + it.interactionId}, {"label", it.label.substr(0, 256)}, {"target", target}}, "",
                        "", world.get(), camera, {});
        ipc->notify("runtime.notice", {{"level", "info"}, {"code", "waypoint.set"}, {"message", n->name + " set a waypoint"}});
    } else if (it.type == "guide") {
        auto path = pathRecord(action["path"]);
        if (path.is_null() || world::pathUse(path) != "route")
            throw ipc::Error(-32004, "guide path not found");
        auto route = world::routeOf(path);
        auto scene = npcScene();
        interaction::Body body{n->agent.radius, n->agent.height};
        if (!routeWalkability(path, body).walkable || !routeWalkability(path, body).detours.empty() ||
            !interaction::checkLeg(scene, n->agent.guideStart(), route.at(0), body, 1).empty()) {
            n->agent.endInteract();
            throw ipc::Error(-32007, n->name + " can't walk there", Json{{"retryable", false}, {"rule", "walkable"}});
        }
        n->agent.beginGuide(std::move(route));
    }
    npcInteraction = it;
    ++npcRevision;
}
void Runtime::endInteraction() {
    if (!npcInteraction)
        return;
    if (auto *n = npcRun(npcInteraction->objectId)) {
        if (npcInteraction->type == "guide" && n->agent.mode == sim::NpcAgent::Mode::Guide)
            n->agent.cancelGuide();
        n->agent.endInteract();
    }
    npcInteraction.reset();
    ++npcRevision;
}
// E (use) on an aimed character: run its interaction, advance a `say`, or end it.
bool Runtime::interactUse() {
    if (npcInteraction && npcInteraction->type == "say") {
        if (++npcInteraction->line >= int(npcInteraction->lines.size()))
            endInteraction();
        else
            ++npcRevision;
        publish();
        return true;
    }
    if (npcInteraction && npcInteraction->type == "choose") {
        endInteraction();
        publish();
        return true;
    }
    if (npcAimed.is_null())
        return false;
    try {
        beginInteraction(npcAimed["objectId"], "");
    } catch (const ipc::Error &e) {
        ipc->notify("runtime.notice", {{"level", "info"}, {"code", "interaction.refused"}, {"message", e.what()}});
    }
    publish();
    return true;
}
void Runtime::npcInteractionTick() {
    // The aimed character (prompt, reticle): within the use distance, with interactions.
    Json aimed = nullptr;
    bool exploring = active && !typing && layout.desktop.is_null() && !freeCursor && !layerCursor;
    if (exploring)
        if (auto c = characterAim(); c && c->second <= input.config["useDistance"].get<float>())
            if (auto *n = npcRun(c->first); n && n->state == "active" && !n->interactions.empty()) {
                auto list = interactionList(*n);
                std::string prompt = n->interactions.size() == 1
                                         ? n->interactions[0].value("prompt", n->interactions[0]["label"].get<std::string>())
                                         : "Talk to " + n->name;
                aimed = {{"objectId", n->id}, {"name", n->name.substr(0, 64)}, {"prompt", prompt.substr(0, 64)}, {"interactions", list}};
            }
    if (aimed != npcAimed)
        npcAimed = aimed, ++npcRevision;
    if (!npcInteraction)
        return;
    auto *n = npcRun(npcInteraction->objectId);
    // Ends: the character went away, the player walked 4 m off (guides: 30 m, the agent's
    // rule) or started using a window, or the action finished.
    if (!n || typing || !layout.desktop.is_null()) {
        endInteraction();
        return;
    }
    Vec3 d = camera.position - n->agent.position;
    float away = std::hypot(d.x, d.z);
    auto since = std::chrono::steady_clock::now() - npcInteraction->started;
    if (npcInteraction->type == "guide") {
        if (n->agent.mode != sim::NpcAgent::Mode::Guide) {
            npcInteraction.reset();
            ++npcRevision;
        }
        return;
    }
    if (away > 4 || (npcInteraction->type == "waypoint" && since > std::chrono::milliseconds(1500)))
        endInteraction();
    else if (n->agent.mode == sim::NpcAgent::Mode::Interact)
        n->agent.interactWith = camera.position;
}
Json Runtime::npcsState() {
    size_t authored = 0, user = 0;
    for (auto &i : objectInfos)
        if (i.character && i.source != "companion")
            (i.source == "user" ? user : authored) += 1;
    Json interaction = nullptr;
    if (npcInteraction) {
        auto &it = *npcInteraction;
        bool say = it.type == "say" && it.line < int(it.lines.size());
        interaction = {{"objectId", it.objectId},
                       {"interactionId", it.interactionId.empty() ? Json(nullptr) : Json(it.interactionId)},
                       {"type", it.type},
                       {"label", it.label.substr(0, 64)},
                       {"choices", it.type == "choose" ? it.choices : Json::array()},
                       {"line", say ? Json(it.lines[it.line]) : Json(nullptr)},
                       {"lineIndex", say ? it.line : 0},
                       {"lineCount", it.type == "say" ? int(it.lines.size()) : 0},
                       {"panel", it.panel.empty() ? Json(nullptr) : Json(it.panel)}};
    }
    Json drawing = nullptr;
    if (npcDrawing)
        drawing = {{"pathId", npcDrawing->pathId},
                   {"use", npcDrawing->use},
                   {"spawn", npcDrawing->spawn.is_null() ? Json(nullptr) : Json{{"ref", npcDrawing->spawn["ref"]}}},
                   {"bind", npcDrawing->bind.empty() ? Json(nullptr) : Json(npcDrawing->bind)}};
    return {{"motion", config["npc"]["motion"]},
            {"state", npcState},
            {"reason", npcReason.empty() ? Json(nullptr) : Json(npcReason.substr(0, 64))},
            {"usage", {{"authored", authored}, {"user", user}}},
            {"limits", {{"authored", world::characterInstanceLimit}, {"user", world::userCharacterLimit}}},
            {"drawing", drawing},
            {"aimed", npcAimed},
            {"interaction", interaction}};
}
// Prompt and caption (core-drawn, world-locked): the aimed character's prompt, a `say` line.
void Runtime::npcPrompts() {
    if (!renderer.showInteractionHints)
        return;
    auto above = [&](const NpcRun &n) {
        return n.agent.position + Vec3{0, n.agent.seatOffset + (n.agent.seated() ? n.agent.height * .75f : n.agent.height) + .25f, 0};
    };
    if (npcInteraction && npcInteraction->type == "say" && npcInteraction->line < int(npcInteraction->lines.size())) {
        if (auto *n = npcRun(npcInteraction->objectId)) {
            auto line = npcInteraction->lines[npcInteraction->line].get<std::string>();
            bool more = npcInteraction->line + 1 < int(npcInteraction->lines.size());
            renderer.toolPrompt = {
                {"position", vecJson(above(*n))}, {"text", line + (more ? "   [E]" : "")}, {"wrap", true}};
        }
        return;
    }
    if (!npcInteraction && npcAimed.is_object())
        if (auto *n = npcRun(npcAimed["objectId"])) {
            std::string text = npcAimed["prompt"];
            std::transform(text.begin(), text.end(), text.begin(), ::toupper);
            renderer.toolPrompt = {{"position", vecJson(above(*n))}, {"text", "E " + text}};
        }
}

// ---- user characters and the route-drawing flow -------------------------------------
// A user character record: on a route (pose = where it starts) or a standing spot.
Json Runtime::characterRecord(const world::CatalogEntry &e, const std::string &id, const std::string &path, Vec3 at,
                              float yaw, const Json &settings) {
    Json r{{"worldId", world->id}, {"id", id}, {"areaId", areaAt(at)}, {"ref", e.ref},
           {"pose", {{"position", vecJson(at)}, {"rotation", yawRotation(yaw)}}}, {"scale", 1.0}};
    if (!path.empty())
        r["path"] = path;
    for (auto key : {"phase", "speed", "accent", "reverse"})
        if (settings.contains(key) && !settings[key].is_null())
            r[key] = settings[key];
    return r;
}
// Checks a route for a character: a walkable route path of this world with room for it.
void Runtime::requireRoute(const std::string &ref, const interaction::Body &body, const std::string &except) {
    auto path = pathRecord(ref);
    if (path.is_null())
        throw ipc::Error(-32004, "unknown path " + ref);
    if (world::pathUse(path) != "route")
        throw ipc::Error(-32007, ref + " is not a route", Json{{"retryable", false}, {"rule", "walkable"}});
    if (world->collision.empty())
        throw ipc::Error(-32007, "this world has no collision: characters can't be checked here",
                         Json{{"retryable", false}, {"rule", "walkable"}});
    auto &w = routeWalkability(path, body);
    if (!w.walkable)
        throw ipc::Error(-32007, "route not walkable: " + (w.problems.empty() ? std::string("unknown") : w.problems[0]),
                         Json{{"retryable", false}, {"rule", "walkable"}});
    size_t bound = 0;
    for (auto &n : npcs)
        bound += n.path == qualifiedPath(ref) && n.id != except;
    if (bound >= world::charactersPerPath)
        throw budgetError("characters per path (8)");
}
interaction::Body Runtime::bodyOf(const world::CatalogEntry &e) {
    auto traits = e.manifest.value("traits", Json::object()).value("character", Json::object());
    return {traits.value("radius", .35f), traits.value("height", 1.75f)};
}
// A standing spot for a body (RFC-0011 "User-spawned characters"): given, or in front of
// the player by the reachable-spawn rules.
std::optional<std::pair<Vec3, float>> Runtime::standingSpot(const interaction::Body &body, const Json &pose) {
    auto scene = npcScene();
    scene.keepClear.clear();
    auto s = placementScene();
    for (auto &k : s.keepClear)
        scene.keepClear.push_back(k);
    for (auto &z : s.zones)
        scene.keepClear.push_back(z);
    if (!pose.is_null()) {
        Vec3 at = vec3(pose["position"]);
        if (interaction::checkSpot(scene, at, body, true))
            return std::nullopt;
        auto floor = interaction::floorAt(scene, at);
        return std::pair{Vec3{at.x, floor.value_or(at.y), at.z}, yawOf(pose["rotation"].get<std::array<float, 4>>())};
    }
    Vec3 flat = normalized(Vec3{camera.forward().x, 0, camera.forward().z});
    for (float d : {2.f, 2.5f, 1.5f, 3.f}) {
        Vec3 at = camera.position + flat * d;
        auto floor = interaction::floorBelow(scene, at, 4);
        if (!floor)
            continue;
        Vec3 feet{at.x, *floor, at.z};
        if (auto wall = interaction::raycast(camera.position, flat, world->collision, &world->collisionIndex); wall && wall->distance < d + body.radius)
            continue;
        if (!interaction::checkSpot(scene, feet, body, true))
            return std::pair{feet, std::atan2(-flat.x, -flat.z)};
    }
    return std::nullopt;
}
std::string Runtime::spawnCharacter(const std::string &ref, const std::string &path, const Json &pose, const Json &settings) {
    auto *e = catalogEntry(ref);
    if (!e)
        throw ipc::Error(-32004, "unknown ref " + ref);
    if (!e->has("character"))
        throw ipc::Error(-32005, e->name + " is not a character");
    if (e->state != "ready" || !e->placeable)
        throw ipc::Error(-32005, e->reason.empty() ? "not available" : e->reason);
    size_t users = 0;
    for (auto &i : objectInfos)
        users += i.character && i.source == "user";
    if (users >= world::userCharacterLimit)
        throw budgetError("user characters per world (8)");
    if (auto limit = budgetRefusal(*e))
        throw budgetError(*limit);
    if (world->collision.empty())
        throw ipc::Error(-32007, "this world has no collision: characters can't be placed here",
                         Json{{"retryable", false}, {"rule", "support"}});
    auto body = bodyOf(*e);
    Vec3 at{};
    float yaw = 0;
    std::string qualified;
    if (!path.empty()) {
        qualified = qualifiedPath(path);
        requireRoute(qualified, body);
        auto route = world::routeOf(pathRecord(qualified));
        float s = route.wrap(settings.value("phase", 0.f) * route.length());
        at = route.at(s);
        auto t = route.tangent(s) * (settings.value("reverse", false) ? -1.f : 1.f);
        yaw = std::atan2(t.x, t.z);
    } else {
        auto spot = standingSpot(body, pose);
        if (!spot)
            throw ipc::Error(-32007, pose.is_null() ? "no free standing spot in front of you" : "not a free standing spot",
                             Json{{"retryable", false}, {"rule", "support"}});
        at = spot->first, yaw = spot->second;
    }
    assetFor(*e); // decode now
    auto id = newPlacementId();
    objectEdit(id, nullptr, characterRecord(*e, id, qualified, at, yaw, settings));
    objectNotice("npc.spawned", "Spawned " + e->name);
    return id;
}
// Finishing a drawn path (paths.finish, leaving tool 3): spawn or rebind on it.
Json Runtime::finishDrawing() {
    if (!npcDrawing)
        return {{"id", nullptr}, {"bound", nullptr}};
    auto drawing = *npcDrawing;
    npcDrawing.reset();
    ++npcRevision;
    if (editor.activePath == drawing.pathId)
        editor.finishPath();
    auto ref = "user/" + drawing.pathId;
    auto path = pathRecord(ref);
    Json bound = nullptr;
    if (path.is_null() || path.value("points", Json::array()).size() < 2) {
        objectNotice("path.unwalkable", "The route needs at least two points");
        return {{"id", path.is_null() ? Json(nullptr) : Json(drawing.pathId)}, {"bound", nullptr}};
    }
    try {
        if (!drawing.spawn.is_null())
            bound = spawnCharacter(drawing.spawn["ref"], ref, nullptr, drawing.spawn);
        else if (!drawing.bind.empty()) {
            bindCharacter(drawing.bind, {{"path", ref}});
            bound = drawing.bind;
        }
        objectNotice("path.saved", "Route saved");
    } catch (const ipc::Error &e) {
        objectNotice("path.unwalkable", e.what());
    }
    return {{"id", drawing.pathId}, {"bound", bound}};
}
void Runtime::bindCharacter(const std::string &id, const Json &patch) {
    if (id == companionId) {
        companionBind(patch);
        return;
    }
    auto *info = objectInfo(id);
    if (!info || !info->character)
        throw ipc::Error(-32004, "unknown character " + id);
    if (info->source != "user")
        throw ipc::Error(-32005, "authored characters can only be disabled");
    auto *e = catalogEntry(info->ref);
    auto before = editor.placement(world->id, id);
    auto after = before;
    auto body = e ? bodyOf(*e) : interaction::Body{};
    if (patch.contains("path")) {
        auto ref = qualifiedPath(patch["path"]);
        requireRoute(ref, body, id);
        after["path"] = ref;
        auto route = world::routeOf(pathRecord(ref));
        float s = route.wrap(patch.value("phase", after.value("phase", 0.f)) * route.length());
        after["pose"] = {{"position", vecJson(route.at(s))}, {"rotation", yawRotation(yawOf(after["pose"]["rotation"].get<std::array<float, 4>>()))}};
    } else if (patch.contains("pose")) {
        auto spot = standingSpot(body, patch["pose"]);
        if (!spot)
            throw ipc::Error(-32007, "not a free standing spot", Json{{"retryable", false}, {"rule", "support"}});
        after.erase("path");
        after["pose"] = {{"position", vecJson(spot->first)}, {"rotation", yawRotation(spot->second)}};
    }
    for (auto key : {"phase", "speed", "accent", "reverse"})
        if (patch.contains(key)) {
            if (patch[key].is_null())
                after.erase(key);
            else
                after[key] = patch[key];
        }
    if (after.value("reverse", false) && !after.contains("path"))
        throw ipc::Error(-32602, "reverse needs a path");
    objectEdit(id, before, after);
}
// IPC 0.23 npcs.*, paths.* and interaction.choose. Throws ipc::Error.
std::optional<Json> Runtime::npcsMethod(const std::string &method, const Json &params) {
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
    auto settings = [&](const Json &p) {
        if (p.contains("phase") && !(p["phase"].is_number() && p["phase"] >= 0 && p["phase"] < 1))
            throw ipc::Error(-32602, "phase must be in [0, 1)");
        if (p.contains("speed") && !(p["speed"].is_number() && p["speed"] >= .25 && p["speed"] <= 2))
            throw ipc::Error(-32602, "speed must be 0.25-2");
        if (p.contains("accent") && !p["accent"].is_null()) {
            bool ok = p["accent"].is_array() && p["accent"].size() == 3;
            for (auto &c : ok ? p["accent"] : Json::array())
                ok = ok && c.is_number() && c >= 0 && c <= 1;
            if (!ok)
                throw ipc::Error(-32602, "accent must be [r, g, b] in 0..1");
        }
        if (p.contains("reverse") && !p["reverse"].is_boolean())
            throw ipc::Error(-32602, "reverse must be a boolean");
        if (p.contains("pose") && !(p["pose"].is_object() && p["pose"].contains("position") && p["pose"].contains("rotation")))
            throw ipc::Error(-32602, "pose must be {position, rotation}");
        if (p.contains("path") && !p["path"].is_string())
            throw ipc::Error(-32602, "path must be a path ref");
    };
    if (!world || loading)
        throw ipc::Error(-32005, "no world loaded");
    if (method == "paths.list") {
        only({});
        return Json{{"paths", pathsList()}};
    }
    if (method == "interaction.choose") {
        only({"objectId", "interactionId"});
        if (!params["objectId"].is_string() || !(params["interactionId"].is_string() || params["interactionId"].is_null()))
            throw ipc::Error(-32602, "objectId and interactionId (string or null)");
        if (params["interactionId"].is_null()) {
            if (npcInteraction && Json(npcInteraction->objectId) == params["objectId"])
                endInteraction();
            return revision();
        }
        beginInteraction(params["objectId"], params["interactionId"]);
        return revision();
    }
    if (method == "npcs.spawn") {
        only({"ref"}, {"path", "pose", "phase", "speed", "accent", "reverse"});
        settings(params);
        if (!params["ref"].is_string())
            throw ipc::Error(-32602, "ref must be a string");
        if (params.contains("path") && params.contains("pose"))
            throw ipc::Error(-32602, "path or pose, not both");
        if (params.contains("reverse") && !params.contains("path"))
            throw ipc::Error(-32602, "reverse needs a path");
        auto id = spawnCharacter(params["ref"], params.value("path", ""), params.value("pose", Json(nullptr)), params);
        return revision({{"id", id}});
    }
    if (method == "npcs.bind") {
        only({"id"}, {"path", "pose", "phase", "speed", "accent", "reverse"});
        settings(params);
        if (params.size() < 2)
            throw ipc::Error(-32602, "name at least one setting");
        if (params.contains("path") && params.contains("pose"))
            throw ipc::Error(-32602, "path or pose, not both");
        auto patch = params;
        patch.erase("id");
        bindCharacter(params["id"], patch);
        return revision();
    }
    if (method == "npcs.disable") {
        only({"id", "disabled"});
        if (!params["id"].is_string() || !params["disabled"].is_boolean())
            throw ipc::Error(-32602, "id (string) and disabled (boolean)");
        auto *info = objectInfo(params["id"]);
        if (!info || !info->character)
            throw ipc::Error(-32004, "unknown character " + params["id"].get<std::string>());
        if (info->source == "companion")
            throw ipc::Error(-32005, "the companion is not disabled; use companion.clear");
        bool disabled = params["disabled"];
        auto id = info->id, name = info->name;
        if (info->source == "authored") {
            auto before = editor.override(world->id, id);
            Json after = before.is_object() ? before : Json{{"worldId", world->id}, {"objectId", id}, {"areaId", info->areaId}};
            if (disabled)
                after["hidden"] = true;
            else
                after.erase("hidden");
            if (after.size() == 3 && !after.contains("pose") && !after.contains("scale") && !after.contains("locked"))
                after = nullptr;
            objectEdit(id, before, after);
        } else {
            auto before = editor.placement(world->id, id);
            auto after = before;
            if (disabled)
                after["disabled"] = true;
            else
                after.erase("disabled");
            objectEdit(id, before, after);
        }
        objectNotice(disabled ? "npc.disabled" : "npc.enabled", (disabled ? "Disabled " : "Enabled ") + name);
        return revision();
    }
    if (method == "npcs.remove") {
        if (params.contains("all")) {
            only({"all"});
            if (params["all"] != true)
                throw ipc::Error(-32602, "all must be true");
            std::vector<std::string> ids;
            for (auto &i : objectInfos)
                if (i.source == "user" && i.character)
                    ids.push_back(i.id);
            for (auto &id : ids)
                objectEdit(id, editor.placement(world->id, id), nullptr);
            objectNotice("npc.removed", "Removed " + std::to_string(ids.size()) + " character" + (ids.size() == 1 ? "" : "s"));
            return revision();
        }
        only({"id"});
        if (!params["id"].is_string())
            throw ipc::Error(-32602, "id must be a string");
        auto *info = objectInfo(params["id"]);
        if (!info || !info->character)
            throw ipc::Error(-32004, "unknown character " + params["id"].get<std::string>());
        if (info->source == "companion")
            throw ipc::Error(-32005, "the companion is not removed; use companion.clear");
        if (info->source != "user")
            throw ipc::Error(-32005, "authored characters are never removed; disable them instead");
        auto id = info->id, name = info->name;
        objectEdit(id, editor.placement(world->id, id), nullptr);
        objectNotice("npc.removed", "Removed " + name);
        return revision();
    }
    if (method == "paths.set") {
        only({"path"});
        auto p = params["path"];
        if (!p.is_object())
            throw ipc::Error(-32602, "path must be an object");
        for (auto &[k, v] : p.items())
            if (k != "id" && k != "name" && k != "use" && k != "closed" && k != "points" && k != "speed" && k != "stops")
                throw ipc::Error(-32602, "unknown path field " + k);
        std::string id = p.value("id", "");
        if (!id.empty()) {
            for (auto &w : world->paths)
                if (w["id"] == id && pathRecord("user/" + id).is_null())
                    throw ipc::Error(-32005, "package paths are read-only");
        }
        if (auto problems = world::pathProblems(p); !problems.empty())
            throw ipc::Error(-32602, problems[0]);
        auto before = id.empty() ? Json(nullptr) : pathRecord("user/" + id);
        if (id.empty()) {
            size_t total = 0, paths = 0;
            for (auto &q : editor.paths)
                total += q["points"].size(), ++paths;
            if (paths >= 128 || total + p["points"].size() > 2048)
                throw budgetError("user paths (128 paths, 2,048 points)");
            id = "path-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        }
        Json record = before.is_object() ? before : Json{{"id", id}, {"worldId", world->id}, {"areaId", areaAt(vec3(p["points"][0]["position"]))}};
        record["id"] = id;
        record["use"] = p.value("use", "generic");
        record["closed"] = p.value("closed", false);
        record["points"] = p["points"];
        for (auto key : {"name", "speed", "stops"})
            if (p.contains(key))
                record[key] = p[key];
            else
                record.erase(key);
        // Routes must be walkable; one bound by characters must stay a walkable route.
        bool bound = std::any_of(npcs.begin(), npcs.end(), [&](auto &n) { return n.path == "user/" + id; });
        if (bound && world::pathUse(record) != "route")
            throw ipc::Error(-32005, "characters walk this route: it stays a route");
        if (world::pathUse(record) == "route") {
            auto &w = routeWalkability(record, {});
            if (!w.walkable)
                throw ipc::Error(-32007, "route not walkable: " + (w.problems.empty() ? std::string("unknown") : w.problems[0]),
                                 Json{{"retryable", false}, {"rule", "walkable"}});
        }
        editor.setPath(id, record);
        editor.recordEdit("path", world->id, id, before, record);
        saveEditor();
        objectNotice("path.saved", "Path saved");
        return revision({{"id", id}});
    }
    if (method == "paths.remove") {
        only({"id"}, {"unbind"});
        if (!params["id"].is_string() || (params.contains("unbind") && !params["unbind"].is_boolean()))
            throw ipc::Error(-32602, "id (string), unbind (boolean)");
        std::string id = params["id"];
        auto before = pathRecord("user/" + id);
        if (before.is_null())
            throw ipc::Error(-32004, "unknown user path " + id);
        // Removing the route being drawn cancels the draw: nothing spawns or binds.
        if (npcDrawing && npcDrawing->pathId == id) {
            npcDrawing.reset();
            ++npcRevision;
            if (editor.activePath == id)
                editor.finishPath();
        }
        std::vector<std::string> bound;
        for (auto &n : npcs)
            if (n.path == "user/" + id)
                bound.push_back(n.id);
        for (auto &i : objectInfos) // disabled characters keep their binding too
            if (i.character && i.source == "user" && editor.placement(world->id, i.id).value("path", "") == "user/" + id &&
                std::find(bound.begin(), bound.end(), i.id) == bound.end())
                bound.push_back(i.id);
        if (!bound.empty() && !params.value("unbind", false))
            throw ipc::Error(-32005, "characters walk this route; pass unbind to remove it anyway");
        for (auto &cid : bound) {
            auto *info = objectInfo(cid);
            if (!info || info->source != "user")
                continue; // authored characters can't bind user paths
            auto rec = editor.placement(world->id, cid);
            auto after = rec;
            after.erase("path");
            after.erase("reverse");
            if (auto *n = npcRun(cid))
                after["pose"] = {{"position", vecJson(n->agent.position)}, {"rotation", yawRotation(n->agent.heading)}};
            objectEdit(cid, rec, after);
        }
        editor.setPath(id, nullptr);
        editor.recordEdit("path", world->id, id, before, nullptr);
        saveEditor();
        objectNotice("path.removed", "Path removed");
        return revision();
    }
    if (method == "paths.draw") {
        only({}, {"use", "name", "spawn", "bind"});
        auto use = params.value("use", std::string("route"));
        if (use != "route" && use != "generic")
            throw ipc::Error(-32602, "use must be route or generic");
        if (params.contains("spawn") && params.contains("bind"))
            throw ipc::Error(-32602, "spawn or bind, not both");
        if (params.contains("name") && !(params["name"].is_string() && !params["name"].get<std::string>().empty() &&
                                         params["name"].get<std::string>().size() <= 64))
            throw ipc::Error(-32602, "name: 1-64 characters");
        if (!active || !layout.desktop.is_null())
            throw ipc::Error(-32005, "exploration inactive");
        Json spawn = nullptr;
        std::string bind;
        if (params.contains("spawn")) {
            auto &s = params["spawn"];
            if (!s.is_object() || !s.contains("ref") || !s["ref"].is_string())
                throw ipc::Error(-32602, "spawn needs a ref");
            for (auto &[k, v] : s.items())
                if (k != "ref" && k != "phase" && k != "speed" && k != "accent")
                    throw ipc::Error(-32602, "unknown spawn field " + k);
            settings(s);
            auto *e = catalogEntry(s["ref"]);
            if (!e)
                throw ipc::Error(-32004, "unknown ref " + s["ref"].get<std::string>());
            if (!e->has("character"))
                throw ipc::Error(-32005, e->name + " is not a character");
            spawn = s;
        }
        if (params.contains("bind")) {
            if (!params["bind"].is_string())
                throw ipc::Error(-32602, "bind must be a character id");
            auto *info = objectInfo(params["bind"]);
            if (!info || !info->character)
                throw ipc::Error(-32004, "unknown character " + params["bind"].get<std::string>());
            if (info->source != "user" && info->source != "companion")
                throw ipc::Error(-32005, "authored characters can only be disabled");
            bind = params["bind"];
        }
        size_t paths = 0;
        for (auto &q : editor.paths)
            paths += 1;
        if (paths >= 128)
            throw budgetError("user paths (128)");
        // Tool 3 with a fresh active path of that use.
        cancelGrab();
        cancelGizmo();
        objectCarry = false;
        editor.finishPath();
        editor.slot = tool = 3;
        auto id = "path-" + std::to_string(editor.deterministicId ? (*editor.deterministicId)++
                                                                  : std::chrono::steady_clock::now().time_since_epoch().count());
        auto b = layout.binding(monitor && monitor->m_activeWorkspace ? int(monitor->m_activeWorkspace->m_id) : 0, world.get());
        Json record{{"id", id}, {"worldId", world->id}, {"areaId", b ? b->at("areaId").get<std::string>() : areaAt(camera.position)},
                    {"closed", false}, {"use", use}, {"points", Json::array()}};
        if (params.contains("name"))
            record["name"] = params["name"];
        editor.pathMode = 0; // open line segments
        editor.setPath(id, record);
        editor.activePath = id;
        npcDrawing = NpcDrawing{id, use, spawn, bind};
        ++npcRevision;
        saveEditor();
        return revision({{"id", id}});
    }
    if (method == "paths.finish") {
        only({});
        auto result = finishDrawing();
        saveEditor();
        return revision(result);
    }
    throw ipc::Error(-32601, "unknown method");
}
// Diagnostics (camera()["npcs"]): agents, seats and frames.
Json Runtime::npcDiagnostics() {
    float aspect = monitor ? float(monitor->m_pixelSize.x) / std::max(1.f, float(monitor->m_pixelSize.y)) : 16.f / 9;
    Json agents = Json::array();
    for (auto &n : npcs) {
        auto &a = n.agent;
        agents.push_back({{"id", n.id},
                          {"user", n.user},
                          {"state", n.state},
                          {"path", n.path},
                          {"position", vecJson(a.position)},
                          {"heading", a.heading},
                          {"activity", a.activity()},
                          {"mode", int(a.mode)},
                          {"clip", a.pose.clip},
                          {"look", {a.pose.lookYaw, a.pose.lookPitch}},
                          {"lateral", a.lateral},
                          {"yielding", a.yielding},
                          {"occluded", n.occluded},
                          {"seat", a.seatRef()},
                          {"seatOffset", a.seatOffset},
                          {"seatFit", {{"known", a.seatKnown}, {"seatHeight", a.seatHeight}, {"sink", a.seatSink}}},
                          {"s", a.s},
                          {"routeLength", a.route.length()},
                          {"speed", a.speedNow},
                          {"radius", a.radius},
                          {"stride", a.strideAt(a.speedNow)},
                          {"gait", {{"phase", a.gaitPhase}, {"previous", a.pose.previous}, {"fade", a.pose.fade},
                                    {"clipTime", a.pose.clipTime}, {"previousTime", a.pose.previousTime}}},
                          {"detours", a.detours.size()},
                          {"stopSpots", [&] {
                               Json out = Json::array();
                               for (auto &st : a.stops)
                                   out.push_back({{"point", st.point},
                                                  {"face", st.face ? vecJson(*st.face) : Json(nullptr)},
                                                  {"spot", st.spot ? vecJson(*st.spot) : Json(nullptr)}});
                               return out;
                           }()},
                          {"stop", a.stop},
                          {"detour", a.detour},
                          {"backingOff", a.backoffLeft > 0},
                          {"blockedFor", a.blockedFor},
                          {"stops", a.stops.size()},
                          {"seats", a.seats.size()},
                          {"interactions", n.interactions.size()}});
    }
    return {{"state", npcState},
            {"reason", npcReason},
            {"agents", agents},
            {"animationVisible", renderer.npcAnimationVisible(camera, aspect)},
            {"drawn", renderer.npcsDrawn},
            {"protectedRefusals", npcProtected},
            {"player", vecJson(npcPlayer)},
            {"present", npcPresent},
            {"seatsTaken", Json(std::vector<std::string>(seatTaken.begin(), seatTaken.end()))},
            {"aimed", npcAimed},
            {"interaction", npcsState()["interaction"]}};
}
// The offload worker poses and draws characters itself (parity with the in-process draw):
// each frame carries their pose inputs, never skinning matrices (offload/Characters.hpp).
Json Runtime::offloadCharacters() const {
    Json out = Json::array();
    for (size_t i = 0; i < npcs.size() && i < renderer.npcs.size(); ++i) {
        auto &n = npcs[i];
        auto &draw = renderer.npcs[i];
        if (!draw.character)
            continue;
        out.push_back(offload::characterRecord(n.asset, draw.model, n.posed, draw.accent, draw.probe, draw.moving,
                                               draw.occluded));
    }
    return out;
}
