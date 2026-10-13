// Included in the adapter namespace after Gizmo.inc. Tool 9, the debug tool
// (interaction/DebugTool.hpp): the shown layer's items are rebuilt in the tick (collision
// only when the eye moves on or the collision changes), projected into the gizmo pass and
// labelled by a banner; Primary inspects, Secondary closes the card. Nothing here runs
// while another tool is out, and nothing forces frames: a still view keeps the frame key.
bool Runtime::debugToolShown() const {
    return world && editor.slot == editor.debugSlot && !freeCursor && !control.uiVisible();
}
interaction::DebugInputs Runtime::debugInputs() {
    interaction::DebugInputs in;
    in.world = world.get();
    in.eye = camera.position;
    auto layer = interaction::DebugLayer(editor.debugLayer);
    using L = interaction::DebugLayer;
    if (layer == L::Windows || layer == L::Placement)
        for (auto &e : capture.entries) {
            if (!e.visible || !e.parentId.empty())
                continue;
            auto &f = e.surface;
            interaction::DebugWindow w;
            w.id = f.id, w.title = e.instance.title, w.appClass = e.instance.appClass;
            w.box.centre = f.center;
            w.box.axes = {f.right(), f.up(), f.normal()};
            w.box.half = {f.width * .5f + .04f, f.height * .5f + .04f, .06f};
            w.free = e.wall.empty();
            if (!e.wall.empty())
                w.mount = "workspace wall " + e.wall;
            in.windows.push_back(std::move(w));
        }
    if (layer == L::Paths)
        in.paths = editor.paths;
    if (layer == L::Steering)
        for (size_t i = 0; i < npcs.size(); ++i) {
            auto &n = npcs[i];
            auto &a = n.agent;
            if (n.state != "active" || n.id == carried || (n.companion && companionBrain.hidden()))
                continue;
            interaction::DebugCharacter c;
            c.id = n.id, c.name = n.name, c.mode = a.activity(), c.companion = n.companion;
            c.position = a.position, c.velocity = a.velocity, c.heading = a.heading, c.radius = a.radius, c.height = a.height;
            c.lateral = a.lateral, c.steerLateral = a.steerLateral, c.steerFactor = a.steerFactor, c.speed = a.speedNow;
            c.blockedFor = a.blockedFor;
            if (a.route.length() > 0) {
                float from = a.route.closed ? a.s - 10 : std::max(0.f, a.s - 10),
                      to = a.route.closed ? a.s + 10 : std::min(a.route.length(), a.s + 10);
                for (float s = from; s < to; s += .5f)
                    c.route.push_back(a.route.at(s) + Vec3{0, .05f, 0});
                c.route.push_back(a.route.at(to) + Vec3{0, .05f, 0});
                c.routePoint = a.route.at(a.s);
            }
            for (auto &d : a.detours) {
                std::vector<Vec3> legs{a.route.at(d.enter)};
                legs.insert(legs.end(), d.legs.begin(), d.legs.end());
                for (auto &p : legs)
                    p = p + Vec3{0, .05f, 0};
                c.detours.push_back(std::move(legs));
            }
            c.activeDetour = a.detour;
            if (!a.legs.empty()) {
                c.legs = {a.position};
                c.legs.insert(c.legs.end(), a.legs.begin(), a.legs.end());
            }
            if (a.blockedBy >= 0 && size_t(a.blockedBy) < npcs.size()) {
                c.blockedBy = npcs[a.blockedBy].name.empty() ? npcs[a.blockedBy].id : npcs[a.blockedBy].name;
                c.blocker = npcs[a.blockedBy].agent.position;
            }
            in.characters.push_back(std::move(c));
        }
    if (layer == L::Seats) {
        for (auto &s : world->seats) {
            interaction::DebugSeat seat;
            seat.id = s.value("id", "");
            seat.position = vec3(s["pose"]["position"]);
            seat.yaw = yawOf(s["pose"]["rotation"].get<std::array<float, 4>>());
            seat.height = s.value("height", .45f);
            seat.approach = s.value("approach", .6f);
            if (seatTaken.contains(seat.id))
                seat.takenBy = "a character";
            in.seats.push_back(std::move(seat));
        }
        for (auto &inst : world->objectInstances) {
            auto &traits = world->objectAssets[inst.asset].traits;
            if (inst.hidden || !traits.is_object() || !traits.contains("seat"))
                continue;
            for (auto &a : traits["seat"].value("anchors", Json::array())) {
                interaction::DebugSeat seat;
                seat.owner = inst.id;
                seat.id = inst.id + "/" + a.value("id", "");
                seat.position = inst.model.point(vec3(a["position"]));
                seat.yaw = yawOf(inst.rotation) + a.value("yaw", 0.f) * 3.14159265f / 180;
                seat.height = a.value("height", .45f) * inst.scale;
                seat.approach = a.value("approach", .6f);
                if (seatTaken.contains(seat.id))
                    seat.takenBy = "a character";
                in.seats.push_back(std::move(seat));
            }
        }
    }
    if (layer == L::KeepClear || layer == L::Placement)
        for (size_t i = 0; i < gizmoKeepClear.size(); ++i)
            in.keepClear.push_back({"development keepClear " + std::to_string(i + 1), gizmoKeepClear[i]});
    if (layer == L::Collision) {
        if (debugKindsWorld != world.get() || debugKinds.size() != world->collision.size() || debugKindsRevision != objectRevision) {
            debugKinds = interaction::classifyCollision(*world);
            debugKindsWorld = world.get();
            debugKindsRevision = objectRevision;
        }
        in.collisionKinds = debugKinds;
    }
    return in;
}
void Runtime::debugToolDraw() {
    renderer.debugBanner = {};
    if (!debugToolShown()) {
        debugItems.clear();
        debugBuilt = {};
        return;
    }
    auto started = std::chrono::steady_clock::now();
    auto layer = interaction::DebugLayer(editor.debugLayer);
    // Collision is the costly layer: rebuilt when the eye has moved 3 m or the collision changed.
    bool collision = layer == interaction::DebugLayer::Collision;
    bool stale = !collision || debugBuilt.layer != editor.debugLayer || debugBuilt.world != world.get() ||
                 debugBuilt.revision != objectRevision || length(camera.position - debugBuilt.eye) > 3;
    if (stale) {
        debugItems = interaction::debugItems(layer, debugInputs());
        debugBuilt = {editor.debugLayer, world.get(), objectRevision, camera.position};
        debugBuildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    }
    if (debugCardLayer != editor.debugLayer) {
        debugCard.clear();
        debugSelected.clear();
        debugCardLayer = editor.debugLayer;
    }
    int selected = -1;
    for (size_t i = 0; i < debugItems.size() && !debugSelected.empty(); ++i)
        if (debugItems[i].id + "\n" + debugItems[i].type == debugSelected)
            selected = int(i);
    auto view = gizmoView();
    renderer.gizmo = interaction::debugFrame(debugItems, view, selected);
    auto &info = interaction::debugLayerInfo(editor.debugLayer);
    renderer.debugBanner.title = std::format("DEBUG {}/{}  {}", editor.debugLayer + 1, interaction::debugLayerCount, info.label);
    for (auto &l : info.legend)
        renderer.debugBanner.legend.push_back({l.label, Vec3{l.color.r, l.color.g, l.color.b}});
    if (!debugCard.empty()) {
        float scale = std::clamp(view.width / 1920.f, 1.f, 2.f);
        int n = int(debugCard.size());
        std::string text;
        for (auto &line : debugCard)
            text += (text.empty() ? "" : "\n") + line;
        render::Renderer::Label card{.5f + 28.f * scale / view.width, .5f + (44.f + (n - 1) * 26.f) * scale / view.height, text,
                                     {.965f, .96f, .92f}, debugCard};
        renderer.readouts.push_back(std::move(card));
    }
}
void Runtime::debugInspect() {
    if (!debugToolShown())
        return;
    auto view = gizmoView();
    auto [px, py] = gizmoPointer(view);
    debugCard.clear();
    debugSelected.clear();
    debugCardLayer = editor.debugLayer;
    if (auto pick = interaction::pickDebugItem(debugItems, view, px, py)) {
        auto &item = debugItems[pick->item];
        debugCard = interaction::debugCard(item);
        debugSelected = item.id + "\n" + item.type;
        debugCardAt = pick->point;
    } else if (interaction::DebugLayer(editor.debugLayer) == interaction::DebugLayer::Placement) {
        // Nothing drawn under the reticle: what the rules say about a 0.5 m box on the aimed surface.
        if (auto hit = worldHit()) {
            interaction::Box probe;
            probe.centre = hit->point + hit->normal * .26f;
            probe.half = {.25f, .25f, .25f};
            auto scene = placementScene();
            scene.player = false;
            auto verdict = interaction::checkPlacement(scene, probe, {});
            debugCard = {"Placement probe", std::format("A 0.5 m box at ({:.2f}, {:.2f}, {:.2f})", probe.centre.x, probe.centre.y, probe.centre.z),
                         verdict.ok ? "Allowed here" : "Refused: " + verdict.message + " (rule " + verdict.rule + ")"};
            debugCardAt = probe.centre;
        }
    }
    if (debugCard.empty())
        debugCard = {"Nothing of this layer under the reticle"};
}
// IPC 0.26 editor.debugTool: null while another tool is out, else the shown layer, its
// legend (sRGB 0-1) and the open info card (null: none). Core draws all of it itself.
Json Runtime::debugToolState() const {
    if (editor.slot != editor.debugSlot)
        return nullptr;
    auto &info = interaction::debugLayerInfo(editor.debugLayer);
    auto unit = [](float v) { return std::round(std::clamp(v, 0.f, 1.f) * 1000.f) / 1000.f; };
    Json legend = Json::array();
    for (auto &l : info.legend)
        if (legend.size() < 8)
            legend.push_back({{"label", ipc::boundedText(l.label, 64)}, {"colour", {unit(l.color.r), unit(l.color.g), unit(l.color.b)}}});
    Json card = nullptr;
    for (auto &line : debugCard)
        if (!line.empty() && (card.is_null() || card.size() < 8)) {
            if (card.is_null())
                card = Json::array();
            card.push_back(ipc::boundedText(line, 256));
        }
    return {{"layer", info.id},
            {"label", ipc::boundedText(info.label, 64)},
            {"index", std::clamp(editor.debugLayer, 0, interaction::debugLayerCount - 1)},
            {"count", interaction::debugLayerCount},
            {"legend", legend},
            {"card", card}};
}
Json Runtime::debugToolDiagnostics() {
    auto &info = interaction::debugLayerInfo(editor.debugLayer);
    Json legend = Json::array();
    for (auto &l : info.legend)
        legend.push_back(l.label);
    Json items = Json::array();
    for (auto &item : debugItems)
        if (items.size() < 64)
            items.push_back({{"id", item.id}, {"type", item.type}});
    return {{"shown", debugToolShown()},
            {"layer", info.id},
            {"label", info.label},
            {"index", editor.debugLayer},
            {"count", interaction::debugLayerCount},
            {"legend", legend},
            {"banner", renderer.debugBanner.title},
            {"items", debugItems.size()},
            {"itemSample", items},
            {"strokes", renderer.gizmo.strokes.size()},
            {"fills", renderer.gizmo.fills.size()},
            {"lineWidth", debugToolShown() ? interaction::debugLineWidth(gizmoView()) : 0.f},
            {"buildMs", debugBuildMs},
            {"selected", debugSelected},
            {"card", debugCard},
            {"cardAt", array3(debugCardAt)}};
}
