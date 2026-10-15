// Tool 3 (Path): hover, point drags, inserts and deletes with undo, and when paths draw.
// Included by Plugin.cpp after Gizmo.inc.

// Paths draw in path mode (tool 3), with "show all paths" on (period in tool 3) or in the
// debug view; points and handles only where paths are edited (tool 3, debug).
bool Runtime::pathsVisible() const {
    return editor.slot == 3 || showAllPaths || debug;
}
// The surface point a path point goes to under the reticle: on the aimed surface, 3 cm out;
// Shift (or the gizmo's snap) snaps to the gizmo's 0.1 m grid, then a point on a floor
// settles back onto it.
std::optional<Vec3> Runtime::pathTarget() {
    auto h = worldHit();
    if (!h || tooFar(h->distance, "reach_placement", "path"))
        return {};
    Vec3 p = h->point + h->normal * .03f;
    if (snapHeld()) {
        auto grid = [](float v) { return interaction::snapValue(v, interaction::snapMetres); };
        p = {grid(p.x), grid(p.y), grid(p.z)};
        if (h->normal.y > .7f)
            if (auto down =
                    interaction::raycast(p + Vec3{0, .5f, 0}, {0, -1, 0}, world->collision, &world->collisionIndex);
                down && down->distance < 1.5f)
                p.y = p.y + .5f - down->distance + .03f;
    }
    return p;
}
// Routes (use: route) take points on walkable floor only (RFC-0011 "Walkability").
bool Runtime::routePath(const std::string &id) const {
    auto p = editor.path(id);
    return p.is_object() && world::pathUse(p) == "route";
}
bool Runtime::walkablePoint(Vec3 p) {
    if (!world || world->collision.empty())
        return true;
    return !interaction::checkSpot(npcScene(), p, {});
}
void Runtime::pathTick() {
    pathTargetWalkable = true;
    if (!world || editor.slot != 3 || typing || layerCursor || freeCursor || control.uiVisible()) {
        pathHover.reset();
        if (pathDrag)
            pathRelease(true);
        return;
    }
    if (pathDrag) {
        if (auto p = pathTarget()) {
            editor.movePoint(pathDrag->path, pathDrag->point, *p);
            if (routePath(pathDrag->path))
                pathTargetWalkable = walkablePoint(*p);
        }
        return;
    }
    if (!editor.activePath.empty() && routePath(editor.activePath))
        if (auto p = pathTarget())
            pathTargetWalkable = walkablePoint(*p);
    auto h = worldHit();
    pathHover = interaction::pickPath(editor.paths, world->id, pointerCamera().position, pointerRay(),
                                      h ? h->distance : config["player"]["reach_placement"].get<float>());
}
void Runtime::pathEdit(const std::string &id, const Json &before) {
    // A route characters walk stays walkable: an edit that breaks it is refused (RFC-0011).
    auto after = editor.path(id);
    bool bound = std::any_of(npcs.begin(), npcs.end(), [&](auto &n) { return n.path == "user/" + id; });
    if (bound && after.is_object() && world::pathUse(after) == "route") {
        auto &w = routeWalkability(after, {});
        if (!w.walkable || after.value("points", Json::array()).size() < 2) {
            editor.setPath(id, before);
            ipc->notify("runtime.notice", {{"level", "info"},
                                           {"code", "path.unwalkable"},
                                           {"message", "Edit refused: characters walk this route (" +
                                                           (w.problems.empty() ? std::string("too short") : w.problems[0]) + ")"}});
            reticleFlash = {"invalid", "path", -1, -1};
            reticleFlashUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(700);
            saveEditor();
            return;
        }
    }
    editor.recordEdit("path", world->id, id, before, after);
    saveEditor();
}
void Runtime::refuseUnwalkable() {
    ipc->notify("runtime.notice", {{"level", "info"}, {"code", "path.unwalkable"}, {"message", "Routes need walkable floor here"}});
    reticleFlash = {"invalid", "path", -1, -1};
    reticleFlashUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(700);
}
// Primary in tool 3: press on a point starts its drag, on a segment inserts a point there,
// elsewhere adds a point to the active path (as before).
void Runtime::pathPress() {
    if (pathHover && pathHover->point >= 0) {
        pathDrag = PathDrag{pathHover->path, pathHover->point, editor.path(pathHover->path)};
        return;
    }
    if (pathHover && pathHover->segment >= 0) {
        if (routePath(pathHover->path) && !walkablePoint(pathHover->at)) {
            refuseUnwalkable();
            return;
        }
        auto before = editor.path(pathHover->path);
        editor.insertPoint(pathHover->path, pathHover->segment, pathHover->at);
        pathEdit(pathHover->path, before);
        pathHover.reset();
        return;
    }
    if (auto p = pathTarget()) {
        if (!editor.activePath.empty() && routePath(editor.activePath) && !walkablePoint(*p)) {
            refuseUnwalkable();
            return;
        }
        auto b = layout.binding(monitor->m_activeWorkspace ? int(monitor->m_activeWorkspace->m_id) : 0, world.get());
        auto id = editor.activePath;
        auto before = id.empty() ? Json(nullptr) : editor.path(id);
        editor.point(world->id, b ? b->at("areaId").get<std::string>() : "", *p);
        pathEdit(editor.activePath, before);
    }
}
// Release commits the drag (one undo step); cancel (Escape, leaving tool 3) puts it back.
void Runtime::pathRelease(bool cancel) {
    if (!pathDrag)
        return;
    auto drag = std::move(*pathDrag);
    pathDrag.reset();
    if (!cancel && routePath(drag.path) && !pathTargetWalkable) {
        refuseUnwalkable();
        cancel = true;
    }
    if (cancel) {
        editor.setPath(drag.path, drag.before);
        saveEditor();
    } else if (editor.path(drag.path) != drag.before)
        pathEdit(drag.path, drag.before);
}
// Secondary in tool 3: removes the hovered point, else the active path's last point.
void Runtime::pathRemove() {
    if (pathHover && pathHover->point >= 0) {
        auto before = editor.path(pathHover->path);
        editor.removePointAt(pathHover->path, pathHover->point);
        pathEdit(pathHover->path, before);
        pathHover.reset();
        return;
    }
    if (editor.activePath.empty())
        return;
    auto before = editor.path(editor.activePath);
    editor.removePoint();
    pathEdit(editor.activePath, before);
}
void Runtime::pathDraw() {
    if (!world || !pathsVisible())
        return;
    bool editing = editor.slot == 3 || debug;
    auto view = pointerCamera();
    Vec3 right = normalized(cross(pointerRay(), {0, 1, 0})), up = normalized(cross(right, pointerRay()));
    auto marker = [&](Vec3 at, float pixels, Vec3 colour, float width) {
        float s = std::max(.02f, length(at - view.position) * pixels * .0012f);
        std::array<Vec3, 4> d{at + up * s, at + right * s, at - up * s, at - right * s};
        for (int k = 0; k < 4; ++k)
            renderer.lines.push_back({d[k], d[(k + 1) % 4], colour, width});
    };
    Vec3 point{.98f, .86f, .42f}, hot{1, 1, 1}, handle{.55f, .45f, .2f};
    for (auto &path : editor.paths) {
        if (path["worldId"] != world->id)
            continue;
        auto lines = interaction::pathLines(path);
        renderer.lines.insert(renderer.lines.end(), lines.begin(), lines.end());
        if (!editing)
            continue;
        auto &points = path["points"];
        std::string id = path["id"];
        auto vec = [](const Json &j) { return Vec3{j[0], j[1], j[2]}; };
        for (size_t i = 0; i < points.size(); ++i) {
            Vec3 at = vec(points[i]["position"]);
            bool hovered = (pathHover && pathHover->path == id && pathHover->point == int(i)) ||
                           (pathDrag && pathDrag->path == id && pathDrag->point == int(i));
            marker(at, hovered ? 9 : 6, hovered ? hot : point, hovered ? .03f : .02f);
            // A curve point's tangent (pathLines' Hermite tangent) as a handle on both sides.
            if (points[i]["type"] == "curve" && i > 0) {
                Vec3 before = vec(points[i > 1 ? i - 2 : 0]["position"]), end = vec(points[i - 1]["position"]);
                Vec3 tangent = (at - before) * .5f;
                if (length(tangent) > 1e-3f) {
                    Vec3 h = normalized(tangent) * std::min(length(tangent) * .35f, length(at - end) * .5f + .2f);
                    renderer.lines.push_back({at - h, at + h, handle, .012f});
                    marker(at - h, 3, handle, .012f);
                    marker(at + h, 3, handle, .012f);
                }
            }
        }
    }
    if (editing && pathHover && pathHover->segment >= 0 && !pathDrag)
        marker(pathHover->at, 7, hot, .02f); // where a click inserts a point
}
Json Runtime::pathDiagnostics() const {
    auto pick = [](const std::optional<interaction::PathPick> &p) {
        return p ? Json{{"path", p->path},
                        {"point", p->point},
                        {"segment", p->segment},
                        {"at", {p->at.x, p->at.y, p->at.z}}}
                 : Json(nullptr);
    };
    return {{"visible", pathsVisible()},
            {"showAll", showAllPaths},
            {"hover", pick(pathHover)},
            {"dragging", pathDrag ? Json{{"path", pathDrag->path}, {"point", pathDrag->point}} : Json(nullptr)}};
}
