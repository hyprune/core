// RFC-0012 markers and quest facts (IPC 0.21). Runs in the plugin tick, never in
// a render pass: it only computes data. The renderer draws the marker layout in
// its present pass under the existing GL guard; nothing here renders, moves the
// cursor, switches workspaces or changes focus.
std::string Runtime::markerMode() const {
    if (!active || loading || freeCursor || layerCursor)
        return "hidden";
    auto mode = inputMode();
    return mode == "world" || mode == "interactive" ? mode : "hidden";
}
void Runtime::questsTick() {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                   .count();
    quests::MarkerRuntime::Context context;
    context.world = active ? world.get() : nullptr;
    context.waypoint = control.location.value("waypoint", Json(nullptr));
    context.route = control.location.value("route", Json::array());
    context.bindings = layout.effectiveBindings();
    context.camera = camera;
    context.mode = markerMode();
    context.active = active && !loading;
    context.reducedMotion = config["graphics"].value("reduced_motion", false) == true;
    context.now = now;
    markers.update(context);
    renderer.markers = &markers;
    if (active && monitor && markers.fading(now)) {
        g_pHyprRenderer->damageMonitor(monitor.lock());
        monitor->scheduleFrame();
    }
    if (now < nextFacts)
        return;
    nextFacts = now + 250;
    // Focus facts carry the app class and workspace only, never a title. While the
    // world view holds the keyboard (walking, the Director) nothing has focus.
    Json focusFact = nullptr;
    bool worldKeyboard = active && (control.uiVisible() || (ownsInput() && !typing));
    if (!worldKeyboard && !locked())
        if (auto window = Desktop::focusState()->window(); window && window->m_workspace && window->m_workspace->m_id > 0)
            focusFact = {{"workspaceId", int(window->m_workspace->m_id)},
                         {"appClass", ipc::boundedText(window->m_class, 128)}};
    Json inside = Json::array();
    for (auto &key : markers.inside)
        inside.push_back(key);
    ipc->facts({{"worldId", world && active ? Json(world->id) : Json(nullptr)},
                {"focus", focusFact},
                {"inputAtMs", markers.lastInput / 1000 * 1000},
                {"locked", locked()},
                {"inside", inside}});
}
