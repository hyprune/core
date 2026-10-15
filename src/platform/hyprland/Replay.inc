// Included in the adapter namespace, after input helpers and before the deferred tick.
Json Runtime::sessionState() const {
    return {{"camera", replay::pose(camera)},
            {"mode", inputMode()},
            {"tool", editor.slot},
            {"subtype", editor.submode()},
            {"windowMode", editor.windowMode},
            {"pathMode", editor.pathMode},
            {"positionMode", editor.positionMode},
            {"debugLayer", editor.debugLayer},
            {"movement", movement == &walk ? "walk" : "fly"},
            {"noclip", noclip},
            {"scope", zoomHeld}};
}
Json Runtime::sessionInitial() const {
    auto state = sessionState();
    state["look"] = {lookX, lookY};
    // Format 2 starts wherever the player is: the movement inertia in flight continues.
    state["smoothIntent"] = {smoothIntent.forward, smoothIntent.right, smoothIntent.up};
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
    editor.debugLayer = std::clamp(j.value("debugLayer", 0), 0, interaction::debugLayerCount - 1);
    editor.restore(j.at("editor"));
    editor.selected.clear();
    editor.activeHandle = -1;
    lookX = j.at("look").at(0);
    lookY = j.at("look").at(1);
    baseFov = j.at("baseFov");
    zoom = j.at("zoom");
    smoothIntent = {};
    if (auto s = j.value("smoothIntent", Json()); s.is_array() && s.size() == 3)
        smoothIntent.forward = s[0], smoothIntent.right = s[1], smoothIntent.up = s[2];
    accumulator = 0;
    keys.clear();
    input.clear();
    modifiers = 0;
    superHeld = altHeld = false;
}
// Playback runs input on the recorded clock; recording (format 2) uses the live one.
uint64_t Runtime::inputClock() const {
    return session.playing ? session.logicalMillis() : replay::clockNs() / 1000000;
}
bool Runtime::timing() const { return frameLog.file || session.recording; }
// One frame of the debug recording, at the end of each tick: the dt it moved by, its timing
// (CPU sections, presents since the last frame, refresh, GPU, offload) and state.
void Runtime::recordTick(const Json &sections, std::chrono::steady_clock::time_point started) {
    auto recordStarted = std::chrono::steady_clock::now();
    auto m = monitor.lock();
    double refreshMs = refreshInterval() * 1000;
    Json timing = {{"sections", sections},
                   {"tickMs", std::round(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() * 1000) / 1000},
                   {"presents", recordPresents},
                   {"refreshMs", refreshMs},
                   {"vrr", vrrActive()},
                   {"renderedFrames", renderer.frames},
                   {"recordMs", recordMs},
                   {"gpuMs", renderer.gpuFrameMs() >= 0 ? Json(renderer.gpuFrameMs()) : Json(nullptr)}};
    if (offload.connected())
        timing["offload"] = {{"front", offload.frontSequence()},
                             {"frontAgeMs", offload.frontAgeMs()},
                             {"readyP90Ms", offload.readyP90Ms()},
                             {"latencyRefreshes", offloadLatency.ticks},
                             {"divisor", offloadDivisor},
                             {"shown", renderedOffload}};
    auto &w = recordWindow;
    for (double p : recordPresents) {
        if (w.lastPresentNs >= 0) {
            double interval = (p - w.lastPresentNs) / 1e6;
            w.presentMs.push_back(interval);
            w.presentAt.push_back(p / 1e9);
            w.missed += std::max(0, int(std::lround(interval / refreshMs)) - 1);
        }
        w.lastPresentNs = p;
    }
    recordPresents.clear();
    if (tickDtSet)
        w.dtMs.push_back(tickDt * 1000.);
    session.recordFrame(sessionState(), tickDtSet ? tickDt : 0., session.since(started), timing);
    auto elapsed = session.elapsedNs();
    if (elapsed - w.startNs >= 5000000000ull)
        recordStats(false);
    // A gentle reminder: at 5 minutes, then every 10.
    if (elapsed >= (5ull + 10ull * recordingReminders) * 60 * 1000000000) {
        ++recordingReminders;
        ipc->notify("runtime.notice", {{"level", "info"}, {"code", "recording.reminder"},
                                       {"message", "Still recording (" + std::to_string(elapsed / 60000000000ull) +
                                                       " min) · Ctrl+R stops it"}});
    }
    recordMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - recordStarted).count();
    if (session.frame >= replay::Session::FrameLimitV2 || elapsed >= replay::Session::DurationLimitNs ||
        !session.error().empty())
        sessionMethod("recording.stop", Json::object());
}
// The statistics of the window since the last ones: written into the recording, kept for
// recording.status and the HUD.
void Runtime::recordStats(bool final) {
    auto &w = recordWindow;
    auto elapsed = session.elapsedNs();
    double window = (elapsed - w.startNs) / 1e9;
    if (window <= 0)
        return;
    auto pct = [](std::vector<double> v, double q) -> Json {
        if (v.empty())
            return nullptr;
        std::sort(v.begin(), v.end());
        return std::round(v[size_t(q * double(v.size() - 1))] * 100) / 100;
    };
    auto mean = [](const std::vector<double> &v) { return v.empty() ? 0. : std::accumulate(v.begin(), v.end(), 0.) / double(v.size()); };
    double dtMean = mean(w.dtMs), dtVar = 0;
    for (double d : w.dtMs)
        dtVar += (d - dtMean) * (d - dtMean);
    double dtStd = w.dtMs.empty() ? 0 : std::sqrt(dtVar / double(w.dtMs.size()));
    Json hitches = Json::array();
    auto median = pct(w.presentMs, .5);
    double refreshMs = refreshInterval() * 1000;
    // A hitch: a present interval over 2.5x the window's median and over two refreshes.
    if (median.is_number())
        for (size_t i = 0; i < w.presentMs.size() && hitches.size() < 32; ++i)
            if (w.presentMs[i] > 2.5 * median.get<double>() && w.presentMs[i] > 2 * refreshMs)
                hitches.push_back({{"t", std::round(w.presentAt[i] * 1000) / 1000}, {"ms", std::round(w.presentMs[i] * 10) / 10}});
    // New frames: the renderer's, or with the offload renderer shown the worker's fronts.
    uint64_t newFrames = renderedOffload && offload.connected() ? offload.frontSequence() - std::min(w.front0, offload.frontSequence())
                                                                : renderer.frames - w.frames0;
    recordingStats = {{"t", std::round(elapsed / 1e6) / 1000},
                      {"windowS", std::round(window * 1000) / 1000},
                      {"final", final},
                      {"presents", w.presentMs.size()},
                      {"presentHz", std::round(double(w.presentMs.size()) / window * 10) / 10},
                      {"fps", std::round(double(newFrames) / window * 10) / 10},
                      {"frameMs", {{"p50", median}, {"p95", pct(w.presentMs, .95)}, {"p99", pct(w.presentMs, .99)}, {"max", pct(w.presentMs, 1)}}},
                      {"dtMs", {{"mean", std::round(dtMean * 100) / 100}, {"std", std::round(dtStd * 100) / 100}, {"max", pct(w.dtMs, 1)}}},
                      {"refreshMs", std::round(refreshMs * 1000) / 1000},
                      {"missedVblanks", w.missed},
                      {"hitches", hitches}};
    session.stats(recordingStats);
    w = RecordWindow{elapsed, renderer.frames, offload.frontSequence(), w.lastPresentNs, 0, {}, {}, {}};
}
Json Runtime::recordingHeader() {
    auto m = monitor.lock();
    Render::GL::g_pHyprOpenGL->makeEGLCurrent();
    auto gl = [](GLenum name) {
        auto s = glGetString(name);
        return s ? std::string(reinterpret_cast<const char *>(s)) : std::string();
    };
    Json manifest = world ? world->manifest : Json::object();
    return {{"monitor", m ? Json{{"name", m->m_name}, {"width", m->m_pixelSize.x}, {"height", m->m_pixelSize.y},
                                 {"refreshHz", m->m_refreshRate}, {"vrr", vrrActive()}, {"vrrCapable", m->m_output && m->m_output->vrrCapable},
                                 {"scale", m->m_scale}}
                          : Json(nullptr)},
            {"gpu", {{"vendor", gl(GL_VENDOR)}, {"renderer", gl(GL_RENDERER)}, {"version", gl(GL_VERSION)}}},
            {"profile", {{"graphics", renderer.graphics},
                         {"renderer", renderedOffload ? "offload" : "in-process"},
                         {"offload", offloadPolicy},
                         {"worker", offload.connected() ? offload.capabilities : Json(nullptr)}}},
            {"versions", {{"core", HYPRUNE_CORE_VERSION},
                          {"protocol", std::string(ipc::protocol::Latest)},
                          {"world", {{"id", world ? world->id : ""}, {"version", manifest.value("version", Json(nullptr))}}}}}};
}
// state.recording (IPC 0.29): whether a recording runs, since when (core steady clock ms,
// the keymap holds' clock), the latest statistics, and the last finished one with its label.
Json Runtime::recordingState() const {
    return {{"active", session.recording},
            {"startedAt", session.recording ? Json(session.startedNs() / 1000000) : Json(nullptr)},
            {"path", session.recording ? Json(ipc::boundedText(session.path, ipc::PathLimit)) : Json(nullptr)},
            {"stats", session.recording ? recordingStats : Json(nullptr)},
            {"last", lastRecording.empty() ? Json(nullptr)
                                           : Json{{"path", ipc::boundedText(lastRecording, ipc::PathLimit)},
                                                  {"label", lastRecordingLabel.empty() ? Json(nullptr) : Json(lastRecordingLabel)}}}};
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
    if (renderPass.active())
        throw ipc::Error(-32004, "record/replay inside frame");
    auto allowed = [&](std::initializer_list<const char *> names) {
        for (auto &[key, _] : params.items())
            if (std::find(names.begin(), names.end(), key) == names.end())
                throw ipc::Error(-32602, "unexpected recording parameter");
    };
    // Replay starts from idle world control; a recording (format 2) may start while moving:
    // keys already held go into its first frame as presses.
    auto idle = [&](bool heldKeys = false) {
        if (!active || !world || loading || inputMode() != "world" || hands.gesture != interaction::Gesture::None ||
            objectCarry || climbing.active() || (!heldKeys && !keys.empty()) || zoomHeld || control.driving())
            throw ipc::Error(-32004, heldKeys ? "start in world control with no gesture, carry, climb or scope"
                                              : "start in idle world control with no held movement/gesture");
    };
    // IPC 0.29: the debug recording's timing, statistics and last finished recording.
    auto status = [&] {
        auto s = session.status();
        if (!ipc::protocol::atLeast(methodProtocol, "0.29"))
            s["frame"] = std::min<uint64_t>(session.frame, replay::Session::FrameLimitV1 + 1); // older schemas' bound
        else {
            s["elapsedMs"] = session.recording ? Json(session.elapsedNs() / 1000000) : Json(nullptr);
            s["stats"] = session.recording ? recordingStats : Json(nullptr);
            s["last"] = recordingState()["last"];
        }
        return s;
    };
    if (method == "recording.status" || method == "replay.status") {
        allowed({});
        return status();
    }
    if (method == "recording.stop") {
        allowed({});
        bool was = session.recording;
        if (was)
            recordStats(true);
        session.stopRecording();
        editor.deterministicId.reset();
        if (was) {
            lastRecording = session.path;
            lastRecordingLabel.clear();
        }
        ipc->notify("runtime.notice",
                    {{"level", "info"}, {"code", "recording.saved"}, {"message", "Recording: " + session.path}});
        publish();
        return status();
    }
    if (method == "recording.label") {
        // IPC 0.29: the last finished recording's label (the shell asks for one on stop): the
        // file is renamed rec-<time>-<slug>.jsonl, a sidecar keeps the label and meta.
        allowed({"label", "meta"});
        if (!ipc::protocol::atLeast(methodProtocol, "0.29"))
            throw ipc::Error(-32601, "unknown recording method");
        if (!params.contains("label") || !params["label"].is_string() || params["label"].get<std::string>().empty() ||
            params["label"].get<std::string>().size() > 120)
            throw ipc::Error(-32602, "label must be 1-120 characters");
        if (params.contains("meta") && (!params["meta"].is_object() || params["meta"].dump().size() > 4096))
            throw ipc::Error(-32602, "meta must be an object under 4 KiB");
        if (session.recording || lastRecording.empty())
            throw ipc::Error(-32004, "no finished recording to label");
        try {
            lastRecording = replay::labelRecording(lastRecording, params["label"], params.value("meta", Json::object()));
        } catch (const std::exception &e) {
            throw ipc::Error(-32004, e.what());
        }
        lastRecordingLabel = params["label"];
        ipc->notify("runtime.notice", {{"level", "info"}, {"code", "recording.labelled"}, {"message", "Recording: " + lastRecording}});
        publish();
        return status();
    }
    if (method == "recording.mark") {
        allowed({"name"});
        session.mark(params.at("name"), sessionState());
        return status();
    }
    if (method == "replay.stop") {
        allowed({});
        stopReplay();
        return status();
    }
    if (method == "recording.start") {
        allowed({"includeAppKeys"});
        idle(true);
        if (params.contains("includeAppKeys") && !params.at("includeAppKeys").is_boolean())
            throw ipc::Error(-32602, "includeAppKeys must be boolean");
        uint64_t seed = replay::clockNs() / 1000;
        session.startRecording({{"worldId", world->id},
                                {"worldDigest", replay::worldDigest(*world)},
                                {"seed", seed},
                                {"initial", sessionInitial()},
                                {"player", config["player"]},
                                {"movementConfig", config["movement"]},
                                {"input", input.config},
                                {"system", recordingHeader()}},
                               params.value("includeAppKeys", false));
        // Nothing about the motion is reset: the recording must not change the feel.
        editor.deterministicId = seed;
        recordWindow = RecordWindow{0, renderer.frames, offload.frontSequence(), -1, 0, {}, {}, {}};
        recordPresents.clear();
        recordingStats = nullptr;
        recordingReminders = 0;
        // Keys already held (Ctrl's sprint while pressing Ctrl+R, a held W) are pressed again in
        // the first frame of a replay; the record.toggle key itself is not.
        // A held key that is neither a modifier nor bound in the world is redacted like any
        // application key.
        std::set<std::string> toggleKeys, worldKeys;
        for (auto &[action, bindings] : input.config["modes"]["world"].items())
            for (auto &b : bindings)
                (action == "record.toggle" ? toggleKeys : worldKeys).insert(interaction::parseBinding(b.get<std::string>()).key);
        for (auto &[code, key] : inputNames) {
            if (toggleKeys.contains(key))
                continue;
            bool modifier = key.starts_with("Super_") || key.starts_with("Shift_") || key.starts_with("Control_") ||
                            key.starts_with("Alt_");
            session.event({{"type", "key"}, {"key", key}, {"mods", modifiers}, {"down", true}, {"value", 0},
                           {"mode", inputMode()}, {"held", true}},
                          !modifier && !worldKeys.contains(key));
        }
        ipc->notify(
            "runtime.notice",
            {{"level", "info"}, {"code", "recording.started"}, {"message", "Recording world input: " + session.path}});
        publish();
        return status();
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
        auto recordedInput = interaction::validateInput(session.header.at("input"), false);
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
        return status();
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
                if (interaction::freeCursorToggle(e.at("key"), modifiers)) {
                    if (e.at("down").get<bool>()) toggleFreeCursor();
                    continue;
                }
                if (layerCursor) continue;
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
    // Recording: the frame is written when the tick ends (recordTick, with its timing).
    if (session.playing) {
        if (session.mode == "camera") {
            auto frame = session.cameraFrame();
            camera = replay::camera(frame.at("camera"));
            editor.slot = tool = frame.at("tool");
            editor.windowMode = frame.at("windowMode");
            editor.pathMode = frame.at("pathMode");
            editor.positionMode = frame.at("positionMode");
            editor.debugLayer = std::clamp(frame.value("debugLayer", 0), 0, interaction::debugLayerCount - 1);
        }
        if (session.finishFrame(sessionState()))
            stopReplay();
    }
}
