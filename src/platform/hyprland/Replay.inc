// Included in the adapter namespace, after input helpers and before the deferred tick.
Json Runtime::sessionState() const {
    return {{"camera", replay::pose(camera)},
            {"mode", inputMode()},
            {"tool", editor.slot},
            {"subtype", editor.submode()},
            {"windowMode", editor.windowMode},
            {"pathMode", editor.pathMode},
            {"positionMode", editor.positionMode},
            {"movement", movement == &walk ? "walk" : "fly"},
            {"noclip", noclip},
            {"scope", zoomHeld}};
}
Json Runtime::sessionInitial() const {
    auto state = sessionState();
    state["look"] = {lookX, lookY};
    state["baseFov"] = baseFov;
    state["zoom"] = zoom;
    state["editor"] = editor.saved();
    return state;
}
void Runtime::restoreSession(const Json &j) {
    climbing.reset(camera);
    camera = replay::camera(j.at("camera"));
    movement = j.at("movement") == "walk" ? static_cast<sim::MotionStrategy *>(&walk) : &fly;
    noclip = j.at("noclip");
    zoomHeld = j.at("scope");
    editor.slot = tool = j.at("tool");
    editor.windowMode = j.at("windowMode");
    editor.pathMode = j.at("pathMode");
    editor.positionMode = j.at("positionMode");
    editor.restore(j.at("editor"));
    editor.selected.clear();
    editor.activeHandle = -1;
    lookX = j.at("look").at(0);
    lookY = j.at("look").at(1);
    baseFov = j.at("baseFov");
    zoom = j.at("zoom");
    smoothIntent = {};
    accumulator = 0;
    keys.clear();
    input.clear();
    modifiers = 0;
    superHeld = altHeld = false;
}
uint64_t Runtime::inputClock() const {
    return session.recording || session.playing ? session.logicalMillis() : replay::clockNs() / 1000000;
}
std::vector<interaction::Action> Runtime::inputEvent(const std::string &mode, const std::string &key, unsigned mods,
                                                     bool down, uint64_t now, float value, bool gesture) {
    auto actions =
        input.event(mode, key, mods, down, session.recording || session.playing ? inputClock() : now, value, gesture);
    if (key != "MouseMove" &&
        std::none_of(actions.begin(), actions.end(), [](auto &a) { return a.name == "record.toggle"; })) {
        bool axis = key.starts_with("Wheel"), button = key == "LMB" || key == "RMB" || key == "MMB" || key == "Back" ||
                                                       key == "Forward" || key.starts_with("Button");
        bool modifier = key.starts_with("Super_") || key.starts_with("Shift_") || key.starts_with("Control_") ||
                        key.starts_with("Alt_");
        session.event({{"type", axis     ? "axis"
                                : button ? "button"
                                         : "key"},
                       {"key", key},
                       {"mods", mods},
                       {"down", down},
                       {"value", value},
                       {"mode", mode}},
                      !axis && !button && !modifier && actions.empty());
    }
    return actions;
}
Json Runtime::sessionMethod(const std::string &method, const Json &params) {
    if (renderDepth)
        throw ipc::Error(-32004, "record/replay inside frame");
    auto allowed = [&](std::initializer_list<const char *> names) {
        for (auto &[key, _] : params.items())
            if (std::find(names.begin(), names.end(), key) == names.end())
                throw ipc::Error(-32602, "unexpected recording parameter");
    };
    auto idle = [&] {
        if (!active || !world || loading || inputMode() != "world" || hands.gesture != interaction::Gesture::None ||
            objectCarry || climbing.active() || !keys.empty() || zoomHeld || control.driving())
            throw ipc::Error(-32004, "start in idle world control with no held movement/gesture");
    };
    if (method == "recording.status" || method == "replay.status") {
        allowed({});
        return session.status();
    }
    if (method == "recording.stop") {
        allowed({});
        session.stopRecording();
        editor.deterministicId.reset();
        ipc->notify("runtime.notice",
                    {{"level", "info"}, {"code", "recording.saved"}, {"message", "Recording: " + session.path}});
        return session.status();
    }
    if (method == "recording.mark") {
        allowed({"name"});
        session.mark(params.at("name"), sessionState());
        return session.status();
    }
    if (method == "replay.stop") {
        allowed({});
        stopReplay();
        return session.status();
    }
    if (method == "recording.start") {
        allowed({"includeAppKeys"});
        idle();
        if (params.contains("includeAppKeys") && !params.at("includeAppKeys").is_boolean())
            throw ipc::Error(-32602, "includeAppKeys must be boolean");
        uint64_t seed = replay::clockNs() / 1000;
        session.startRecording({{"worldId", world->id},
                                {"worldDigest", replay::worldDigest(*world)},
                                {"seed", seed},
                                {"initial", sessionInitial()},
                                {"player", config["player"]},
                                {"movementConfig", config["movement"]},
                                {"input", input.config}},
                               params.value("includeAppKeys", false));
        accumulator = 0;
        smoothIntent = {};
        editor.deterministicId = seed;
        ipc->notify(
            "runtime.notice",
            {{"level", "info"}, {"code", "recording.started"}, {"message", "Recording world input: " + session.path}});
        return session.status();
    }
    if (method == "replay.start") {
        allowed({"path", "mode", "smoothingMs", "assertions"});
        idle();
        session.load(params.at("path"), params.value("mode", "input"), params.value("smoothingMs", 0.),
                     params.value("assertions", Json::object()));
        if (session.header.at("worldId") != world->id ||
            session.header.at("worldDigest") != replay::worldDigest(*world))
            throw ipc::Error(-32004, "recording requires the same installed world manifest");
        auto initial = session.header.at("initial");
        replay::validateState(initial);
        if (initial.at("mode") != "world" || initial.at("look").size() != 2 || !initial.at("baseFov").is_number() ||
            initial.at("baseFov") < 3 || initial.at("baseFov") > 175 || initial.at("zoom") < 1 ||
            initial.at("zoom") > 20)
            throw ipc::Error(-32602, "invalid recording initial state");
        interaction::Tools checkedEditor;
        checkedEditor.restore(initial.at("editor"));
        auto recordedInput = interaction::validateInput(session.header.at("input"));
        auto checked = config;
        checked["player"] = session.header.at("player");
        checked = interaction::configPatch(config, {{"player", session.header.at("player")},
            {"movement", session.header.value("movementConfig", interaction::defaultConfig()["movement"])}});
        replayInput = input.config;
        replayConfig = {{"player", config["player"]}, {"movement", config["movement"]}};
        replayEditor = editor.saved();
        input.apply(recordedInput);
        config["player"] = checked["player"];
        config["movement"] = checked["movement"];
        restoreSession(session.header.at("initial"));
        editor.deterministicId = session.header.at("seed").get<uint64_t>();
        session.playing = true;
        session.frame = 0;
        return session.status();
    }
    throw ipc::Error(-32601, "unknown recording method");
}
void Runtime::stopReplay() {
    if (!session.playing)
        return;
    if (session.frame < session.frames.size() && session.failures.empty())
        session.failures.push_back("Replay stopped before the final frame");
    session.playing = false;
    replayDispatch = false;
    cancelGrab();
    keys.clear();
    input.apply(replayInput);
    config["player"] = replayConfig["player"];
    config["movement"] = replayConfig["movement"];
    climbing.reset(camera);
    editor.restore(replayEditor);
    editor.deterministicId.reset();
    editor.activeHandle = -1;
    objectCarry = false;
    smoothIntent = {};
    lookX = lookY = 0;
    accumulator = 0;
    zoomHeld = false;
    modifiers = 0;
    superHeld = altHeld = false;
    if (inputMode() != "world")
        leaveInteraction();
    ipc->notify("runtime.notice",
                {{"level", session.failures.empty() ? "info" : "warning"},
                 {"code", "replay.finished"},
                 {"message", session.failures.empty() ? "Replay complete" : "Replay assertions failed"}});
}
void Runtime::replayBeforeTick() {
    if (!session.playing)
        return;
    replayDispatch = true;
    try {
        if (session.mode == "input")
            for (auto &e : session.events[session.frame]) {
                if (e.value("redacted", false))
                    continue;
                std::string type = e.at("type");
                if (type == "motion") {
                    motion(nullptr, {e.at("dx").get<double>(), e.at("dy").get<double>()});
                    continue;
                }
                modifiers = e.at("mods");
                superHeld = modifiers & interaction::Super;
                altHeld = modifiers & interaction::Alt;
                bool gesture = hands.gesture != interaction::Gesture::None || objectCarry || input.gestureHeld() ||
                               (editor.activeHandle >= 0 && superHeld);
                auto actions =
                    inputEvent(inputMode(), e.at("key"), modifiers, e.at("down"), inputClock(), e.at("value"), gesture);
                for (auto a : actions)
                    action(a);
                // Unbound keys never enter Hyprland's bind manager or application keyboard.
                if (actions.empty() && type == "button" && typing && layout.desktop.is_null()) {
                    std::string key = e.at("key");
                    uint32_t code = key == "LMB"       ? 272
                                    : key == "RMB"     ? 273
                                    : key == "MMB"     ? 274
                                    : key == "Back"    ? 275
                                    : key == "Forward" ? 276
                                                       : std::stoul(key.substr(6));
                    button({.timeMs = uint32_t(inputClock()),
                            .button = code,
                            .state = e.at("down").get<bool>() ? WL_POINTER_BUTTON_STATE_PRESSED
                                                              : WL_POINTER_BUTTON_STATE_RELEASED},
                           false);
                }
                if (actions.empty() && type == "axis" && typing && layout.desktop.is_null())
                    axis({.timeMs = uint32_t(inputClock()),
                          .axis = WL_POINTER_AXIS_VERTICAL_SCROLL,
                          .delta = -e.at("value").get<double>() * 20.,
                          .deltaDiscrete = int32_t(-e.at("value").get<float>() * 120)});
            }
    } catch (const std::exception &e) {
        session.failures.push_back(std::string("Replay event failed: ")+e.what());
        stopReplay();
        throw;
    }
    replayDispatch = false;
}
void Runtime::replayAfterTick() {
    if (session.recording) {
        session.recordFrame(sessionState());
        if (session.frame >= 36000 || !session.error().empty())
            sessionMethod("recording.stop", Json::object());
    }
    if (session.playing) {
        if (session.mode == "camera") {
            auto frame = session.cameraFrame();
            camera = replay::camera(frame.at("camera"));
            editor.slot = tool = frame.at("tool");
            editor.windowMode = frame.at("windowMode");
            editor.pathMode = frame.at("pathMode");
            editor.positionMode = frame.at("positionMode");
        }
        if (session.finishFrame(sessionState()))
            stopReplay();
    }
}
