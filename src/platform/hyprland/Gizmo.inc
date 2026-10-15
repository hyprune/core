// Position tool (tool 4) runtime: Gizmo 2.0 handles, previews, placement rules,
// undo/redo, drop/align and world 0.8 object targets. Included by Plugin.cpp after
// the editor helpers. Everything here runs in the tick or in deferred input: no
// cursor, focus or workspace change and no GL work happens in the render pass.
namespace {
Vec3 vec3(const Json &j) { return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()}; }
Json array3(Vec3 v) { return {v.x, v.y, v.z}; }
std::array<float, 4> shortestArc(Vec3 from, Vec3 to) {
    from = normalized(from), to = normalized(to);
    float w = 1 + dot(from, to);
    if (w < 1e-5f) { // opposite: half a turn about any perpendicular
        auto axis = std::abs(from.x) < .9f ? normalized(cross(from, {1, 0, 0})) : normalized(cross(from, {0, 1, 0}));
        return {axis.x, axis.y, axis.z, 0};
    }
    auto c = cross(from, to);
    std::array<float, 4> q{c.x, c.y, c.z, w};
    float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (auto &v : q)
        v /= l;
    return q;
}
// Rest-pose bounds of a selection, in the space its Transform maps from: world space
// for scene nodes (Transform::matrix about the bounds centre), asset space for instances.
struct Rest {
    Vec3 low{}, high{};
    bool instance = false;
    size_t index = 0;
};
std::optional<Rest> restBounds(const world::World &w, const std::string &id) {
    if (auto k = instanceIndex(w, id)) {
        auto &a = w.objectAssets[w.objectInstances[*k].asset];
        return Rest{a.low, a.high, true, *k};
    }
    auto node = editorNode(w, id);
    if (!node)
        return {};
    Rest r{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
    bool found = false;
    for (auto &m : w.meshes)
        if (belongs(w, m.node, *node))
            for (auto &v : m.vertices) {
                found = true;
                r.low = {std::min(r.low.x, v.position.x), std::min(r.low.y, v.position.y), std::min(r.low.z, v.position.z)};
                r.high = {std::max(r.high.x, v.position.x), std::max(r.high.y, v.position.y), std::max(r.high.z, v.position.z)};
            }
    if (!found)
        return {};
    return r;
}
// Rest space -> world at a candidate transform.
Mat4 selectionMatrix(const world::World &w, const std::string &id, const interaction::Transform &t) {
    if (instanceIndex(w, id))
        return world::instanceModel(t.position, t.rotation, t.scale.x);
    return t.matrix(objectOrigin(w, id));
}
} // namespace

interaction::GizmoView Runtime::gizmoView() {
    interaction::GizmoView v;
    v.camera = pointerCamera();
    if (auto m = monitor.lock()) {
        v.width = std::max(1., m->m_pixelSize.x);
        v.height = std::max(1., m->m_pixelSize.y);
    }
    v.dpi = std::max(1.f, renderer.logicalScale);
    return v;
}
std::pair<float, float> Runtime::gizmoPointer(const interaction::GizmoView &v) {
    bool pointer = layerCursor || typing || editor.activeHandle >= 0;
    return {(pointer ? pointerX : .5f) * v.width, (pointer ? pointerY : .5f) * v.height};
}
interaction::Box Runtime::selectionBox(const interaction::Transform &t) {
    interaction::Box b;
    if (editor.kind == "window") {
        auto q = t.rotation;
        b.centre = t.position;
        b.axes = {rotate(q, {1, 0, 0}), rotate(q, {0, 1, 0}), rotate(q, {0, 0, 1})};
        b.half = {t.scale.x * .5f, t.scale.y * .5f, .02f};
        return b;
    }
    auto rest = world ? restBounds(*world, editor.selected) : std::nullopt;
    if (!rest)
        return interaction::aabbBox(t.position - Vec3{.1f, .1f, .1f}, t.position + Vec3{.1f, .1f, .1f});
    auto m = selectionMatrix(*world, editor.selected, t);
    b.centre = m.point((rest->low + rest->high) * .5f);
    Vec3 half = (rest->high - rest->low) * .5f;
    b.axes = {rotate(t.rotation, {1, 0, 0}), rotate(t.rotation, {0, 1, 0}), rotate(t.rotation, {0, 0, 1})};
    Vec3 scale = rest->instance ? Vec3{t.scale.x, t.scale.x, t.scale.x} : t.scale;
    b.half = {half.x * scale.x, half.y * scale.y, half.z * scale.z};
    return b;
}
Vec3 Runtime::selectionPivot(const interaction::Transform &t) {
    auto b = selectionBox(t);
    if (!editor.pivotBase)
        return b.centre;
    // Bottom centre: under the centre along world up (objects on floors), or the
    // window's lower edge along its own up.
    if (editor.kind == "window")
        return b.centre - b.axes[1] * b.half.y;
    return b.centre - Vec3{0, b.extent({0, 1, 0}), 0};
}
std::vector<Triangle> Runtime::selectionTriangles(const interaction::Transform &t) {
    std::vector<Triangle> out;
    if (!world || editor.kind != "object")
        return out;
    auto m = selectionMatrix(*world, editor.selected, t);
    if (auto k = instanceIndex(*world, editor.selected)) {
        for (auto &tri : world->objectAssets[world->objectInstances[*k].asset].collision)
            out.push_back({m.point(tri.a), m.point(tri.b), m.point(tri.c)});
        return out;
    }
    auto node = editorNode(*world, editor.selected);
    if (!node)
        return out;
    for (size_t i = 0; i < originalCollision.size() && i < world->collisionNodes.size(); ++i)
        if (world->collisionNodes[i] != world::objectCollisionNode && belongs(*world, world->collisionNodes[i], *node)) {
            auto &tri = originalCollision[i];
            out.push_back({m.point(tri.a), m.point(tri.b), m.point(tri.c)});
        }
    return out;
}
interaction::PlacementScene Runtime::placementScene() {
    interaction::PlacementScene s;
    if (!world)
        return s;
    s.collision = world->collision;
    s.index = &world->collisionIndex;
    auto selected = editor.selected;
    auto instance = instanceIndex(*world, selected);
    auto node = instance ? std::nullopt : editorNode(*world, selected);
    if (instance) {
        auto &o = world->objectInstances[*instance];
        size_t first = o.collisionFirst, count = o.collisionCount;
        s.ignore = [first, count](unsigned id) { return id >= first && id < first + count; };
        if (auto *i = selectedInfo())
            s.support = i->support; // rule 3 per movable.surfaces (advisory for world 0.8 objects)
    } else if (node) {
        auto w = world;
        unsigned root = *node;
        s.ignore = [w, root](unsigned id) {
            return id < w->collisionNodes.size() && w->collisionNodes[id] != world::objectCollisionNode &&
                   belongs(*w, w->collisionNodes[id], root);
        };
    }
    s.playable = &world->playable;
    float below = config["player"]["respawn_below"].get<float>();
    if (below > 0)
        s.respawnBelow = world->spawn().position.y - below;
    // Placed windows (with the RFC-0005 margin) stay visible.
    for (auto &e : capture.entries) {
        if (!e.visible || !e.parentId.empty() || !e.wall.empty())
            continue;
        auto &f = e.surface;
        interaction::Box b;
        b.centre = f.center;
        b.axes = {f.right(), f.up(), f.normal()};
        b.half = {f.width * .5f + .04f, f.height * .5f + .04f, .06f};
        s.windows.push_back(b);
    }
    // Screen approaches: in front of every anchor, tool mount and workspace wall,
    // its width plus 0.3 m each side, from 0.1 m above its floor to its top edge, 1.5 m deep.
    // A workspace screen's own approach (IPC 0.27) never blocks moving that screen.
    std::string own = instance ? "screen-" + world->objectInstances[*instance].id : "";
    for (auto &[id, zone] : interaction::namedApproachZones(*world, s.ignore))
        if (id != own)
            s.approaches.push_back(zone);
    for (auto &z : world->placementZones)
        if (z.contains("bounds"))
            s.zones.push_back(interaction::aabbBox(vec3(z["bounds"]["min"]), vec3(z["bounds"]["max"])));
    // Ways: spawn capsules (0.3 m radius, 1.8 m, grown by 0.2 m) and climbables.
    for (auto &sp : world->spawns)
        s.keepClear.push_back(interaction::aabbBox(sp.position - Vec3{.5f, 0, .5f}, sp.position + Vec3{.5f, 2.f, .5f}));
    for (auto &c : world->climbables) {
        Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        for (auto p : {c.bottom, c.top, c.bottomExit, c.topExit})
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)}, hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
        s.keepClear.push_back(interaction::aabbBox(lo - Vec3{.4f, 0, .4f}, hi + Vec3{.4f, 1.9f, .4f}));
    }
    // keepClear volumes (world 0.9, RFC-0011), and a development file's
    // (HYPRUNE_GIZMO_KEEPCLEAR, [{id,min,max}]).
    for (auto &k : world->keepClear)
        s.keepClear.push_back(interaction::aabbBox(vec3(k["min"]), vec3(k["max"])));
    for (auto &k : gizmoKeepClear)
        s.keepClear.push_back(k);
    s.player = true;
    s.playerLow = camera.position + Vec3{0, -camera.eyeHeight + .3f, 0};
    s.playerHigh = camera.position + Vec3{0, .1f - .3f, 0};
    return s;
}
interaction::PlacementVerdict Runtime::placementCheck(const interaction::Transform &t, const std::set<std::string> &waived) {
    auto scene = placementScene();
    return interaction::checkPlacement(scene, selectionBox(t), selectionTriangles(t), waived);
}
std::set<std::string> Runtime::waivedRules(const interaction::Transform &t) {
    auto scene = placementScene();
    return interaction::brokenSoftRules(scene, selectionBox(t), selectionTriangles(t));
}
Json Runtime::objectRecord(const interaction::Transform &t) {
    if (auto *i = selectedInfo())
        return placementRecord(*i, t); // a world object instance: its override or placement record
    auto b = layout.binding(monitor && monitor->m_activeWorkspace ? int(monitor->m_activeWorkspace->m_id) : 0, world.get());
    auto scale = instanceIndex(*world, editor.selected) ? Vec3{t.scale.x, t.scale.x, t.scale.x} : t.scale;
    return {{"worldId", world->id},
            {"areaId", b ? b->at("areaId").get<std::string>() : ""},
            {"objectId", editor.selected},
            {"position", array3(t.position)},
            {"rotation", t.rotation},
            {"scale", array3(scale)}};
}
Json Runtime::currentRecord(const std::string &worldId, const std::string &id) {
    if (id.starts_with("object:")) {
        auto plain = id.substr(7);
        auto o = editor.override(worldId, plain);
        return o.is_null() ? editor.placement(worldId, plain) : o;
    }
    for (auto &o : editor.objects)
        if (o["worldId"] == worldId && o["objectId"] == id)
            return o;
    return nullptr;
}
void Runtime::writeObjectRecord(const std::string &worldId, const std::string &id, const Json &record) {
    if (id.starts_with("object:")) {
        // World object instances live in the RFC-0011 overlay: an authored instance's
        // override or a user placement record.
        auto plain = id.substr(7);
        bool authored = !editor.override(worldId, plain).is_null() || (record.is_object() && record.contains("objectId"));
        if (world && worldId == world->id)
            authored = std::any_of(world->objectInstances.begin(),
                                   world->objectInstances.begin() + std::min(authoredInstances, world->objectInstances.size()),
                                   [&](auto &i) { return i.id == plain; });
        if (authored)
            editor.setOverride(worldId, plain, record);
        else
            editor.setPlacement(worldId, plain, record);
        ++objectRevision;
        objectsSync();
        return;
    }
    if (id.empty())
        return;
    auto it = std::find_if(editor.objects.begin(), editor.objects.end(),
                           [&](auto &o) { return o["worldId"] == worldId && o["objectId"] == id; });
    if (record.is_null()) {
        if (it != editor.objects.end())
            editor.objects.erase(it);
    } else if (it != editor.objects.end())
        *it = record;
    else if (editor.objects.size() < 128)
        editor.objects.push_back(record);
    else
        ipc->notify("runtime.notice", {{"level", "warning"}, {"code", "editor.budget"}, {"message", "At most 128 object transforms per state file."}});
    ++objectRevision;
}
// Commits an object pose (outside a drag): refused when it breaks a placement rule.
bool Runtime::commitObject(const interaction::Transform &t, const std::string &what) {
    if (auto *i = selectedInfo(); i && (i->locked || !i->movable || i->character)) {
        objectNotice(i->character ? "object.protected" : "object.locked", what + " refused: " + i->name + (i->character ? " is protected" : " is locked"));
        return false;
    }
    auto verdict = placementCheck(t, waivedRules(objectPose(editor.selected)));
    if (!verdict.ok) {
        ipc->notify("runtime.notice", {{"level", "info"}, {"code", "placement.blocked"}, {"message", what + " refused: " + verdict.message}});
        return false;
    }
    auto id = editor.selected;
    auto before = currentRecord(world->id, id), after = objectRecord(t);
    editor.recordEdit("object", world->id, id, before, after);
    writeObjectRecord(world->id, id, after);
    selectedTransform = t;
    saveEditor();
    return true;
}
Json Runtime::windowPose(const Capture::Entry &e) {
    auto &s = e.surface;
    return {{"center", array3(s.center)}, {"rotation", s.rotation}, {"yaw", s.yaw}, {"width", s.width},
            {"height", s.height}, {"pixelWidth", s.pixelWidth}, {"pixelHeight", s.pixelHeight}};
}
void Runtime::resizeClient(Capture::Entry &e, surfaces::Surface &next, double width, double height) {
    auto w = e.window.lock();
    if (!w || !w->m_target)
        return;
    auto box = w->m_target->position();
    if (auto content = layout.contentFor(e.instance, world.get())) {
        auto sizing = *content;
        sizing["worldSize"] = {next.width, next.height};
        sizing["logicalSize"] = {int(width), int(height)};
        layout.setContent(e.instance, world.get(), sizing);
    }
    if (std::abs(box.w - width) > 1 || std::abs(box.h - height) > 1) {
        Fullscreen::controller()->setFullscreenMode(w, Fullscreen::FSMODE_NONE, Fullscreen::FSMODE_NONE);
        (void)Config::Actions::floatWindow(Config::Actions::TOGGLE_ACTION_ENABLE, w);
        (void)Config::Actions::resize({width, height}, false, w);
        w->sendWindowSize(true);
    }
}
void Runtime::applyWindowPose(Capture::Entry &e, const Json &pose) {
    if (!pose.is_object() || !world)
        return;
    auto next = e.surface;
    next.center = vec3(pose["center"]);
    next.rotation = pose["rotation"].get<std::array<float, 4>>();
    next.yaw = pose["yaw"].get<float>();
    next.width = pose["width"].get<float>();
    next.height = pose["height"].get<float>();
    layout.freePoses[e.surface.id] = next;
    layout.carried.clear();
    int pw = pose["pixelWidth"], ph = pose["pixelHeight"];
    if (pw != e.surface.pixelWidth || ph != e.surface.pixelHeight)
        resizeClient(e, next, pw, ph);
    if (layout.binding(e.instance.workspaceId, world.get()))
        layout.mount(e.instance, world.get(), next);
}
void Runtime::applyUndo(bool redo) {
    auto edit = redo ? editor.redo() : editor.undo();
    if (!edit) {
        ipc->notify("runtime.notice", {{"level", "info"}, {"code", redo ? "editor.redo" : "editor.undo"},
                                       {"message", redo ? "Nothing to redo" : "Nothing to undo"}});
        return;
    }
    auto &value = redo ? edit->after : edit->before;
    if ((edit->kind == "object" || edit->kind == "path") && (!world || edit->worldId != world->id)) {
        ipc->notify("runtime.notice", {{"level", "info"},
                                       {"code", redo ? "editor.redo" : "editor.undo"},
                                       {"message", "Skipped an edit in another world"}});
        return;
    }
    if (edit->kind == "object")
        writeObjectRecord(edit->worldId, edit->id, value);
    else if (edit->kind == "path") {
        pathDrag.reset();
        pathHover.reset();
        editor.setPath(edit->id, value);
    } else if (auto e = capture.find(edit->id))
        applyWindowPose(*e, value);
    if (editor.kind == "object" && editor.selected == edit->id && world)
        selectedTransform = objectPose(editor.selected);
    saveEditor();
    ipc->notify("runtime.notice", {{"level", "info"},
                                   {"code", redo ? "editor.redo" : "editor.undo"},
                                   {"message", std::string(redo ? "Redid " : "Undid ") +
                                                   (edit->kind == "path"     ? "path edit"
                                                    : edit->kind == "object" ? "object transform"
                                                                             : "window transform")}});
}
// End: drop the selected object onto the floor below it. Shift+End: align it to the
// aimed surface (base on the surface, up along its normal).
void Runtime::dropSelection(bool align) {
    if (!world || editor.kind != "object" || editor.selected.empty()) {
        ipc->notify("runtime.notice", {{"level", "info"}, {"code", "gizmo.drop"}, {"message", "Select an object first"}});
        return;
    }
    auto t = objectPose(editor.selected);
    auto scene = placementScene();
    auto box = selectionBox(t);
    if (!align) {
        float base = box.centre.y - box.extent({0, 1, 0});
        auto floor = interaction::floorBelow(scene, box.centre, box.extent({0, 1, 0}) + 60);
        if (!floor) {
            ipc->notify("runtime.notice", {{"level", "info"}, {"code", "gizmo.drop"}, {"message", "No floor below"}});
            return;
        }
        t.position.y += *floor - base;
        commitObject(t, "Drop to floor");
        return;
    }
    auto o = pointerCamera().position, d = pointerRay();
    std::optional<std::pair<float, Vec3>> hit;
    auto test = [&](unsigned id) {
        if (scene.ignore && scene.ignore(id))
            return;
        auto &tri = world->collision[id];
        auto h = interaction::raycast(o, d, std::span<const Triangle>(&tri, 1));
        if (h && (!hit || h->distance < hit->first))
            hit = std::pair{h->distance, h->normal};
    };
    for (auto id : world->collisionIndex.ray(o, d))
        test(id);
    if (!hit || tooFar(hit->first, "reach_placement")) {
        ipc->notify("runtime.notice", {{"level", "info"}, {"code", "gizmo.align"}, {"message", "Aim at a surface in reach"}});
        return;
    }
    auto point = o + d * hit->first, normal = dot(hit->second, d) > 0 ? hit->second * -1 : hit->second;
    t.rotation = quaternionProduct(shortestArc(rotate(t.rotation, {0, 1, 0}), normal), t.rotation);
    auto b = selectionBox(t);
    t.position += point - (b.centre - normal * b.extent(normal));
    commitObject(t, "Align to surface");
}
// Movable world object instances under the pointer: swept sphere, then the asset's
// rest-pose triangles at the instance pose (node animation is ignored).
std::optional<std::pair<std::string, interaction::WorldHit>> Runtime::instanceAim() {
    if (!world)
        return {};
    auto o = pointerCamera().position, d = pointerRay();
    std::optional<std::pair<std::string, interaction::WorldHit>> best;
    // Movable instances only (the movable trait; locked ones too, they show the lock);
    // characters are never targets (RFC-0011 "Characters are protected").
    for (auto &info : objectInfos) {
        if (info.index < 0 || info.state != "active" || !info.movable || info.character ||
            size_t(info.index) >= world->objectInstances.size())
            continue;
        auto &inst = world->objectInstances[info.index];
        auto &asset = world->objectAssets[inst.asset];
        auto to = inst.center - o;
        float along = dot(to, d);
        if (along < -inst.radius || length(to - d * along) > inst.radius)
            continue;
        // In asset space: the ray through the inverse instance model (rotation, uniform
        // scale), a slab test against the rest bounds, then the asset's triangles.
        const auto &m = inst.model.v;
        Vec3 c0{m[0], m[1], m[2]}, c1{m[4], m[5], m[6]}, c2{m[8], m[9], m[10]}, origin{m[12], m[13], m[14]};
        float s2 = dot(c0, c0);
        if (!(s2 > 1e-12f))
            continue;
        float scale = std::sqrt(s2);
        auto local = [&](Vec3 v) { return Vec3{dot(v, c0), dot(v, c1), dot(v, c2)} / s2; };
        Vec3 lo = local(o - origin), ld = local(d) * scale; // ld is unit: the asset-space distance is world / scale
        float enter = 0, leave = 1e30f;
        bool miss = false;
        for (int a = 0; a < 3 && !miss; ++a) {
            float oa = a == 0 ? lo.x : a == 1 ? lo.y : lo.z, da = a == 0 ? ld.x : a == 1 ? ld.y : ld.z;
            float low = a == 0 ? asset.low.x : a == 1 ? asset.low.y : asset.low.z, high = a == 0 ? asset.high.x : a == 1 ? asset.high.y : asset.high.z;
            if (std::abs(da) < 1e-9f) {
                miss = oa < low || oa > high;
                continue;
            }
            float t0 = (low - oa) / da, t1 = (high - oa) / da;
            if (t0 > t1)
                std::swap(t0, t1);
            enter = std::max(enter, t0), leave = std::min(leave, t1);
            miss = enter > leave;
        }
        if (miss)
            continue;
        float nearest = best ? best->second.distance / scale : 1e30f;
        std::optional<std::pair<float, Vec3>> hit;
        for (auto &mesh : asset.meshes)
            for (size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
                auto &a = mesh.vertices[i].position, &b = mesh.vertices[i + 1].position, &c = mesh.vertices[i + 2].position;
                auto e = b - a, f = c - a, p = cross(ld, f);
                float det = dot(e, p);
                if (std::abs(det) < 1e-12f)
                    continue;
                auto v = lo - a;
                float u = dot(v, p) / det;
                auto q = cross(v, e);
                float w = dot(ld, q) / det, t = dot(f, q) / det;
                if (u < 0 || w < 0 || u + w > 1 || t <= 0 || t >= nearest)
                    continue;
                nearest = t;
                hit = std::pair{t, cross(e, f)};
            }
        if (hit) {
            float distance = hit->first * scale;
            auto n = normalized(c0 * hit->second.x + c1 * hit->second.y + c2 * hit->second.z);
            if (dot(n, d) > 0)
                n = n * -1;
            best = {{"object:" + inst.id, interaction::WorldHit{distance, o + d * distance, n}}};
        }
    }
    return best;
}
// Instance models: committed poses, then the live preview (render only). Returns the
// committed models (collision and relighting follow their changes).
std::map<size_t, Mat4> Runtime::applyInstanceModels() {
    std::map<size_t, Mat4> committed;
    if (!world)
        return committed;
    for (size_t k = 0; k < world->objectInstances.size(); ++k) {
        auto &inst = world->objectInstances[k];
        if (inst.character)
            continue; // the NPC runtime moves characters (no proxy, never edited)
        auto &asset = world->objectAssets[inst.asset];
        auto id = "object:" + inst.id;
        auto pose = objectPose(id);
        auto model = world::instanceModel(pose.position, pose.rotation, pose.scale.x);
        committed[k] = model;
        float scale = pose.scale.x;
        if (preview.active && editor.kind == "object" && editor.selected == id) {
            model = world::instanceModel(preview.pose.position, preview.pose.rotation, preview.pose.scale.x);
            scale = preview.pose.scale.x;
        }
        if (!(model == inst.model)) {
            inst.model = model;
            inst.scale = scale;
            inst.center = model.point(asset.center);
            inst.radius = asset.radius * scale;
        }
    }
    return committed;
}
void Runtime::previewObject(const interaction::Transform &candidate, bool carry) {
    preview.candidate = candidate;
    preview.verdict = placementCheck(candidate, preview.waived);
    preview.valid = preview.verdict.ok;
    if (preview.valid)
        preview.pose = candidate;
    else if (!carry)
        preview.pose = dragStart; // a refused gizmo pose shows (and releases to) the start pose
    preview.active = true;
    selectedTransform = preview.pose;
}
// Releases an object preview: commits the shown pose with an undo record; a refused
// pose leaves the overlay unchanged and says why.
void Runtime::commitPreview() {
    if (!preview.active || !world)
        return;
    auto before = editBefore;
    auto start = dragStart;
    bool changed = length(preview.pose.position - start.position) > 1e-5f || preview.pose.rotation != start.rotation ||
                   length(preview.pose.scale - start.scale) > 1e-5f;
    if (changed) {
        auto after = objectRecord(preview.pose);
        auto id = editor.selected; // writing syncs the instances, which may change the selection
        editor.recordEdit("object", world->id, id, before, after);
        writeObjectRecord(world->id, id, after);
    }
    if (!preview.valid)
        ipc->notify("runtime.notice", {{"level", "info"}, {"code", "placement.blocked"},
                                       {"message", "Not placed: " + preview.verdict.message}});
    selectedTransform = preview.pose;
    preview = {};
    saveEditor();
}
void Runtime::gizmoConfigure() {
    using interaction::GizmoMode;
    auto mode = editor.gizmoMode();
    gizmo.options.mode = mode == "rotate" ? GizmoMode::Rotate : mode == "scale" ? GizmoMode::Scale
                         : mode == "universal"                       ? GizmoMode::Universal
                                                                     : GizmoMode::Move;
    bool instance = world && instanceIndex(*world, editor.selected).has_value();
    // Scale is always along the selection's own axes (a transform has no shear).
    gizmo.options.local = editor.localAxes || mode == "scale";
    gizmo.options.uniformScaleOnly = instance;
    gizmo.options.scaleAxes = editor.kind == "window" ? std::array<bool, 3>{true, true, false} : std::array<bool, 3>{true, true, true};
    if (mode == "universal" && !editor.localAxes) {
        auto q = selectedTransform.rotation;
        if (std::abs(std::abs(q[3]) - 1) > 1e-4f)
            gizmo.options.scaleAxes = {false, false, false}; // world axes differ from the scale axes
    }
}
bool Runtime::gizmoBegin() {
    if (!world || editor.selected.empty())
        return false;
    gizmoConfigure();
    auto view = gizmoView();
    gizmo.layout(view, selectionPivot(selectedTransform), selectedTransform.rotation);
    auto [x, y] = gizmoPointer(view);
    auto part = gizmo.pick(x, y);
    if (part == interaction::Part::None)
        return false;
    if (auto *i = selectedInfo(); i && (i->locked || !i->movable)) {
        objectNotice("object.locked", i->name + " is locked");
        return true; // handles shown, edits refused
    }
    dragStart = selectedTransform;
    dragPivot = selectionPivot(dragStart);
    if (!gizmo.begin(part, x, y, {dragPivot, dragStart.rotation, dragStart.scale}, view))
        return false;
    editor.activeHandle = int(part);
    if (editor.kind == "object") {
        editBefore = currentRecord(world->id, editor.selected);
        preview = {};
        preview.active = true;
        preview.pose = dragStart;
        preview.startSupported = placementCheck(dragStart).supported;
        preview.waived = waivedRules(dragStart);
        // Resting on a floor (base within 8 cm of it): a scale keeps it there (gizmoTransform).
        dragFloor.reset();
        auto box = selectionBox(dragStart);
        float base = box.centre.y - box.extent({0, 1, 0});
        if (auto f = interaction::floorBelow(placementScene(), Vec3{box.centre.x, base + .1f, box.centre.z}, .25f);
            f && std::abs(*f - base) < .08f)
            dragFloor = *f;
    } else if (auto e = capture.find(editor.selected)) {
        editBefore = windowPose(*e);
        dragPixelWidth = e->surface.pixelWidth;
        dragPixelHeight = e->surface.pixelHeight;
    }
    windowBlocked.reset();
    pointerX = pointerY = .5f;
    keys.clear();
    smoothIntent = {};
    return true;
}
interaction::Transform Runtime::gizmoTransform(const interaction::Gizmo::Result &r) {
    auto t = dragStart;
    auto part = gizmo.active();
    if (interaction::isMove(part))
        t.position = dragStart.position + (r.pose.position - dragPivot);
    else if (interaction::isRotate(part)) {
        auto delta = quaternionProduct(r.pose.rotation, conjugate(dragStart.rotation));
        t.rotation = r.pose.rotation;
        t.position = dragPivot + rotate(delta, dragStart.position - dragPivot);
    } else if (interaction::isScale(part)) {
        t.scale = r.pose.scale;
        auto local = rotate(conjugate(dragStart.rotation), dragStart.position - dragPivot);
        local = {local.x * r.factor.x, local.y * r.factor.y, local.z * r.factor.z};
        t.position = dragPivot + rotate(dragStart.rotation, local);
    }
    t.scale = {std::clamp(t.scale.x, .01f, 100.f), std::clamp(t.scale.y, .01f, 100.f), std::clamp(t.scale.z, .01f, 100.f)};
    if (auto *i = selectedInfo()) { // uniform, within the asset's movable.scale range
        float s = std::clamp(t.scale.x, i->scaleMin, i->scaleMax);
        t.scale = {s, s, s};
    }
    // An object resting on a floor stays on it while it is scaled: its base settles on that
    // floor (scaled up it rises out of it, scaled down it drops onto it), so a size is never
    // refused for "Needs a floor under it". The other placement rules still apply.
    if (interaction::isScale(part) && editor.kind == "object" && dragFloor) {
        auto box = selectionBox(t);
        t.position.y += *dragFloor - (box.centre.y - box.extent({0, 1, 0}));
    }
    return t;
}
void Runtime::gizmoUpdate() {
    if (!gizmo.dragging())
        return;
    auto view = gizmoView();
    bool snap = snapHeld();
    auto [x, y] = gizmoPointer(view);
    auto r = gizmo.numeric() ? gizmo.applyTyped() : gizmo.drag(x, y, snap, view);
    auto t = gizmoTransform(r);
    if (editor.kind == "object")
        previewObject(t, false);
    else
        selectedTransform = t;
}
void Runtime::gizmoRelease(bool cancel) {
    if (!gizmo.dragging())
        return;
    if (editor.kind == "object") {
        if (cancel) {
            preview.pose = dragStart;
            preview.valid = true;
        }
        commitPreview();
    } else if (auto e = capture.find(editor.selected)) {
        if (cancel)
            applyWindowPose(*e, editBefore);
        else
            editor.recordEdit("window", world ? world->id : "", editor.selected, editBefore, windowPose(*e));
    }
    gizmo.end();
    editor.activeHandle = -1;
    windowBlocked.reset();
    saveEditor();
}
// Tool 4 keys outside the configurable keymap (IPC 0.20 has no actions for them):
// while dragging, digits . - Backspace type a value, Enter applies it, Escape cancels;
// otherwise Ctrl+Z undo, Ctrl+Shift+Z / Ctrl+Y redo, End drop to floor, Shift+End
// align to the aimed surface, period toggles the pivot. Keys an action binds with the
// same modifiers keep their binding. Decides synchronously; effects are deferred.
bool Runtime::gizmoKey(const std::string &key, unsigned mods, bool down, uint32_t code) {
    if (!down)
        return gizmoKeys.erase(code) > 0;
    if (!active || (editor.slot != 4 && editor.slot != 3) || typing || layerCursor || freeCursor ||
        control.uiVisible() || !world)
        return false;
    auto bound = [&](const std::string &k, unsigned m) {
        for (auto &[name, list] : input.config["modes"]["world"].items())
            for (auto &v : list)
                try {
                    auto t = interaction::parseBinding(v.get<std::string>());
                    if (t.key == k && t.mods == m)
                        return true;
                } catch (...) {
                }
        return false;
    };
    std::function<void()> effect;
    if (editor.slot == 3) {
        // Tool 3: Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y undo and redo path edits, Escape cancels a
        // point drag, period shows every path outside path mode too.
        if ((mods & 4) && (key == "z" || key == "y")) {
            bool redo = key == "y" || (mods & 1);
            consumePendingCtrl();
            effect = [redo] { runtime->applyUndo(redo); };
        } else if (key == "Escape" && pathDrag)
            effect = [] { runtime->pathRelease(true); };
        else if (key == "period" && !mods && !bound("period", 0))
            effect = [] {
                runtime->showAllPaths = !runtime->showAllPaths;
                runtime->ipc->notify("runtime.notice", {{"level", "info"},
                                                        {"code", "path.visibility"},
                                                        {"message", runtime->showAllPaths ? "Paths: always shown"
                                                                                          : "Paths: path mode only"}});
            };
    } else if (gizmo.dragging()) {
        char ch = 0;
        if (key.size() == 1 && key[0] >= '0' && key[0] <= '9')
            ch = key[0];
        else if (key.size() == 4 && key.starts_with("KP_") && key[3] >= '0' && key[3] <= '9')
            ch = key[3];
        else if (key == "period" || key == "KP_Decimal")
            ch = '.';
        else if (key == "minus" || key == "KP_Subtract")
            ch = '-';
        if (ch && gizmo.numericAllowed())
            effect = [ch] { runtime->gizmo.type(ch); };
        else if (key == "BackSpace" && gizmo.numeric())
            effect = [] { runtime->gizmo.backspace(); };
        else if (key == "Return" || key == "KP_Enter")
            effect = [] {
                runtime->gizmoUpdate();
                runtime->gizmoRelease(false);
            };
        else if (key == "Escape")
            effect = [] { runtime->gizmoRelease(true); };
        else if (ch || gizmo.numeric())
            effect = [] {}; // swallow tool switches and stray keys mid-entry
    } else if ((mods & 4) && (key == "z" || key == "y")) {
        bool redo = key == "y" || (mods & 1);
        consumePendingCtrl(); // the chord owns Ctrl: no crouch, no fly down
        effect = [redo] { runtime->applyUndo(redo); };
    } else if (key == "End" && !bound("End", mods & 1)) {
        consumePendingCtrl();
        effect = [shift = bool(mods & 1)] { runtime->dropSelection(shift); };
    }
    else if (key == "period" && !mods && !bound("period", 0))
        effect = [] {
            runtime->editor.pivotBase = !runtime->editor.pivotBase;
            runtime->ipc->notify("runtime.notice", {{"level", "info"}, {"code", "gizmo.pivot"},
                                                    {"message", runtime->editor.pivotBase ? "Pivot: base" : "Pivot: centre"}});
        };
    if (!effect)
        return false;
    gizmoKeys.insert(code);
    deferInput([effect] {
        effect();
        runtime->publish();
    });
    return true;
}
// Ghost: the selection's bounds (and outline edges) at the candidate pose, red when
// the pose breaks a placement rule.
void Runtime::ghostEdges() {
    if (ghostFor == editor.selected)
        return;
    ghostFor = editor.selected;
    ghostLines.clear();
    if (!world || editor.kind != "object")
        return;
    std::vector<Triangle> rest;
    auto instance = instanceIndex(*world, editor.selected);
    if (instance) {
        auto &a = world->objectAssets[world->objectInstances[*instance].asset];
        rest = a.collision;
        if (rest.empty())
            for (auto &m : a.meshes)
                for (size_t i = 0; i + 2 < m.vertices.size() && rest.size() < 20000; i += 3)
                    rest.push_back({m.vertices[i].position, m.vertices[i + 1].position, m.vertices[i + 2].position});
    } else if (auto node = editorNode(*world, editor.selected)) {
        for (size_t i = 0; i < originalCollision.size() && i < world->collisionNodes.size(); ++i)
            if (world->collisionNodes[i] != world::objectCollisionNode && belongs(*world, world->collisionNodes[i], *node))
                rest.push_back(originalCollision[i]);
    }
    // Feature edges: boundaries and creases over 40 degrees, at most 400.
    std::map<std::array<int64_t, 6>, std::vector<Vec3>> edges;
    auto q = [](Vec3 p) { return std::array<int64_t, 3>{std::llround(p.x * 1000), std::llround(p.y * 1000), std::llround(p.z * 1000)}; };
    std::map<std::array<int64_t, 6>, std::pair<Vec3, Vec3>> ends;
    for (auto &t : rest) {
        auto n = normalized(cross(t.b - t.a, t.c - t.a));
        for (auto [a, b] : {std::pair{t.a, t.b}, {t.b, t.c}, {t.c, t.a}}) {
            auto ka = q(a), kb = q(b);
            if (kb < ka)
                std::swap(ka, kb), std::swap(a, b);
            std::array<int64_t, 6> key{ka[0], ka[1], ka[2], kb[0], kb[1], kb[2]};
            edges[key].push_back(n);
            ends[key] = {a, b};
        }
    }
    for (auto &[key, normals] : edges) {
        bool feature = normals.size() == 1;
        for (size_t i = 1; i < normals.size() && !feature; ++i)
            feature = std::abs(dot(normals[0], normals[i])) < .766f;
        if (feature && ghostLines.size() < 400)
            ghostLines.push_back(ends[key]);
    }
}
void Runtime::gizmoDraw() {
    renderer.gizmo = {};
    renderer.readouts.clear();
    gizmoHover = interaction::Part::None;
    debugToolDraw(); // tool 9: its layer, banner and info card (nothing for the other tools)
    if (world && editor.slot == 5 && objectGhost.active && !freeCursor && !control.uiVisible()) {
        // Tool 5 with an object in the pocket: its placement ghost and what it says.
        auto view = gizmoView();
        interaction::GizmoFrame frame;
        objectGhostDraw(frame, view);
        renderer.gizmo = std::move(frame);
        auto item = std::find_if(pocketObjects.begin(), pocketObjects.end(), [&](auto &p) { return p.itemId == unplacedSelected; });
        std::string text = item == pocketObjects.end() ? "" : item->name;
        if (!objectGhost.valid && !objectGhost.message.empty())
            text += "   BLOCKED: " + objectGhost.message;
        if (!text.empty())
            renderer.readouts.push_back({.5f + 22.f / view.width, .5f + 26.f / view.height, text,
                                         objectGhost.valid ? Vec3{.875f, .74f, .41f} : Vec3{1, .45f, .4f}});
    }
    if (!world || editor.slot != 4 || editor.selected.empty() || freeCursor || control.uiVisible()) {
        if (gizmo.dragging())
            gizmoRelease(true);
        return;
    }
    auto view = gizmoView();
    gizmoConfigure();
    auto current = selectedTransform;
    gizmo.layout(view, selectionPivot(current), current.rotation);
    auto [px, py] = gizmoPointer(view);
    if (!gizmo.dragging())
        gizmoHover = gizmo.pick(px, py);
    bool snap = snapHeld();
    interaction::GizmoFrame frame;
    using interaction::Rgba;
    auto ghostColor = preview.valid ? Rgba{.43f, .95f, .69f, .55f} : interaction::palette::invalid;
    auto addBox = [&](const interaction::Box &b, Rgba edge, float fillAlpha) {
        std::array<Vec3, 8> p;
        for (int i = 0; i < 8; ++i)
            p[i] = b.centre + b.axes[0] * ((i & 1) ? b.half.x : -b.half.x) + b.axes[1] * ((i & 2) ? b.half.y : -b.half.y) +
                   b.axes[2] * ((i & 4) ? b.half.z : -b.half.z);
        unsigned sequence = frame.next();
        if (fillAlpha > 0)
            for (int axis = 0; axis < 3; ++axis)
                for (int side = 0; side < 2; ++side) {
                    int bit = 1 << axis, u = 1 << ((axis + 1) % 3), w = 1 << ((axis + 2) % 3), base = side ? bit : 0;
                    interaction::GizmoFill f{{}, {edge.r, edge.g, edge.b, fillAlpha}, false, sequence++};
                    for (int c : {base, base | u, base | u | w, base | w})
                        if (auto s = view.project(p[c]))
                            f.points.push_back(*s);
                    if (f.points.size() == 4)
                        frame.fills.push_back(f);
                }
        for (int i = 0; i < 8; ++i)
            for (int bit : {1, 2, 4})
                if (!(i & bit)) {
                    auto a = view.project(p[i]), c = view.project(p[i | bit]);
                    if (a && c)
                        frame.strokes.push_back({*a, *c, edge, 2.2f * view.dpi, false, false, true, 0, sequence++});
                }
    };
    // A valid preview is the object itself; only a refused pose gets the red ghost.
    if (preview.active && editor.kind == "object" && !preview.valid) {
        ghostEdges();
        auto m = selectionMatrix(*world, editor.selected, preview.candidate);
        unsigned sequence = frame.next();
        Rgba edge = preview.valid ? Rgba{ghostColor.r, ghostColor.g, ghostColor.b, .35f} : Rgba{1, .27f, .24f, .9f};
        for (auto &[a, b] : ghostLines) {
            auto pa = view.project(m.point(a)), pb = view.project(m.point(b));
            if (pa && pb)
                frame.strokes.push_back({*pa, *pb, edge, 2.f * view.dpi, false, false, true, 0, sequence++});
        }
        addBox(selectionBox(preview.candidate), interaction::palette::invalid, .22f);
    } else if (preview.active && editor.kind == "object" && (gizmo.dragging() || objectCarry))
        addBox(selectionBox(preview.candidate), Rgba{.43f, .95f, .69f, .3f}, 0.f); // faint bounds while dragging
    if (windowBlocked)
        addBox(*windowBlocked, interaction::palette::invalid, .18f);
    frame.append(gizmo.draw(gizmoHover, snap, view));
    // The selection never hides its own handles: world depth counts as in front
    // only beyond the selection's extent around the pivot.
    auto box = selectionBox(current);
    frame.occlusionSlack = std::max(frame.occlusionSlack, length(box.half) + length(box.centre - selectionPivot(current)));
    renderer.gizmo = std::move(frame);
    float ox = 22.f / view.width, oy = 26.f / view.height;
    auto [rx, ry] = std::pair{px / view.width, py / view.height};
    std::string where = std::string(editor.localAxes || editor.gizmoMode() == "scale" ? "local" : "world") + " / " +
                        (editor.pivotBase ? "base" : "centre");
    if (gizmo.dragging()) {
        // Value first; the placement verdict on its own line below.
        renderer.readouts.push_back({rx + ox, ry + oy, gizmo.readout(), {.965f, .96f, .92f}});
        std::string status;
        if (editor.kind == "object" && !preview.valid)
            status = "BLOCKED: " + preview.verdict.message;
        else if (windowBlocked)
            status = "BLOCKED: collision";
        else if (editor.kind == "object" && preview.startSupported && !preview.verdict.supported)
            status = "Floating (End drops to the floor)";
        if (!status.empty())
            renderer.readouts.push_back({rx + ox, ry + oy + 30.f * std::clamp(view.width / 1920.f, 1.f, 2.f) / view.height, status,
                                         status.starts_with("BLOCKED") ? Vec3{1, .45f, .4f} : Vec3{.875f, .74f, .41f}});
    } else if (gizmoHover != interaction::Part::None) {
        std::string name = interaction::partName(gizmoHover);
        for (auto &c : name)
            c = c == '-' ? ' ' : char(std::toupper(c));
        renderer.readouts.push_back({rx + ox, ry + oy, name + "   " + where, {.875f, .74f, .41f}});
    }
}
Json Runtime::gizmoDiagnostics() {
    Json handles = Json::object();
    for (int k = 0; k < int(interaction::Part::Count); ++k)
        if (auto a = gizmo.anchor(interaction::Part(k))) {
            auto v = gizmoView();
            auto s = v.project(*a);
            handles[interaction::partName(interaction::Part(k))] = {
                {"world", array3(*a)}, {"screen", s ? Json{s->x / v.width, s->y / v.height} : Json(nullptr)}, {"alpha", gizmo.alpha(interaction::Part(k))}};
        }
    auto &g = gizmo.geometry();
    return {{"mode", editor.gizmoMode()},
            {"universal", editor.universal},
            {"local", editor.localAxes},
            {"pivot", editor.pivotBase ? "base" : "centre"},
            {"visible", gizmo.visible() && editor.slot == 4 && !editor.selected.empty()},
            {"hover", interaction::partName(gizmoHover)},
            {"active", interaction::partName(gizmo.active())},
            {"typed", gizmo.typed()},
            {"readout", [&] {
                 std::string text;
                 for (auto &r : renderer.readouts)
                     text += (text.empty() ? "" : "  ") + r.text;
                 return text;
             }()},
            {"preview", preview.active},
            {"valid", preview.valid && !windowBlocked},
            {"rule", preview.verdict.rule},
            {"message", preview.verdict.message},
            {"supported", preview.verdict.supported},
            {"canUndo", editor.canUndo()},
            {"canRedo", editor.canRedo()},
            {"objectsEnabled", gizmoObjects},
            {"size", g.size},
            {"pivotPoint", array3(g.centre)},
            {"bounds", array3(selectionBox(selectedTransform).half)},
            {"pivotScreen", {g.centrePx.x / std::max(1.f, gizmoView().width), g.centrePx.y / std::max(1.f, gizmoView().height)}},
            {"sign", {g.sign[0], g.sign[1], g.sign[2]}},
            {"primitives", renderer.gizmo.strokes.size() + renderer.gizmo.fills.size()},
            {"handles", handles},
            {"transform", {{"position", array3(selectedTransform.position)}, {"rotation", selectedTransform.rotation},
                           {"scale", array3(selectedTransform.scale)}}}};
}
// Ends any gizmo gesture without a commit (tool switch, world change, input reload).
// A window keeps its live pose and gets its undo record; an object preview is dropped.
void Runtime::cancelGizmo() {
    if (gizmo.dragging() && editor.kind == "window" && world)
        if (auto e = capture.find(editor.selected))
            editor.recordEdit("window", world->id, editor.selected, editBefore, windowPose(*e));
    gizmo.end();
    preview = {};
    editor.activeHandle = -1;
    windowBlocked.reset();
}
// Ctrl's own actions (crouch / fly down by default) wait briefly in world mode wherever a
// Ctrl chord can follow: tools 3 and 4 (Ctrl+Z, Ctrl+Shift+Z, Ctrl+Y) and any Ctrl binding of
// the mode (Ctrl+R records), so the chord never dips the camera first.
bool Runtime::deferCtrl(const std::string &key, bool down, uint32_t code, const std::vector<interaction::Action> &actions) {
    if (!down) {
        auto it = std::find_if(pendingCtrl.begin(), pendingCtrl.end(), [&](auto &p) { return p.code == code; });
        if (it == pendingCtrl.end())
            return false;
        bool dispatched = it->fired && !it->released;
        pendingCtrl.erase(it);
        return !dispatched; // never ran (or a chord already ended it): drop the release too
    }
    if (!key.starts_with("Control_")) {
        // A key bound as a Ctrl chord while Ctrl is held: the chord owns Ctrl.
        if (!pendingCtrl.empty() && ctrlChord(key))
            consumePendingCtrl();
        return false;
    }
    if (actions.empty() || !active || typing || layerCursor || freeCursor || control.uiVisible() || inputMode() != "world" ||
        (editor.slot != 4 && editor.slot != 3 && !ctrlChord("")))
        return false;
    std::erase_if(pendingCtrl, [&](auto &p) { return p.code == code; });
    pendingCtrl.push_back(PendingCtrl{code, actions, std::chrono::steady_clock::now()});
    return true;
}
// Whether the world mode binds key (any key: "") with Ctrl.
bool Runtime::ctrlChord(const std::string &key) const {
    auto &world = input.config["modes"]["world"];
    for (auto &[action, bindings] : world.items())
        if (bindings.is_array())
            for (auto &b : bindings) {
                if (!b.is_string())
                    continue;
                auto chord = interaction::parseBinding(b.get<std::string>());
                if ((chord.mods & interaction::Ctrl) && (key.empty() || chord.key == key))
                    return true;
            }
    return false;
}
// A chord took Ctrl: its actions never run, or, held past the chord window, they end now.
void Runtime::consumePendingCtrl() {
    for (auto &p : pendingCtrl)
        if (!p.fired)
            p.consumed = true;
        else if (!p.released) {
            p.released = true;
            deferInput([actions = p.actions] {
                for (auto a : actions) {
                    a.pressed = false;
                    runtime->action(a);
                }
            });
        }
}
// Ctrl held alone past the chord window: its actions run, late by at most 180 ms.
void Runtime::ctrlTick() {
    for (auto &p : pendingCtrl) {
        if (p.fired || p.consumed || std::chrono::steady_clock::now() - p.since < std::chrono::milliseconds(180))
            continue;
        p.fired = true;
        deferInput([actions = p.actions] {
            for (auto a : actions)
                runtime->action(a);
        });
    }
}
// Snapping (gizmo drags, path points): Shift, the sprint key, as before IPC 0.29's swap.
bool Runtime::snapHeld() const {
    return editor.snap || (modifiers & interaction::Shift) || keys.contains(KEY_LEFTSHIFT); // KEY_LEFTSHIFT: sprint held
}
