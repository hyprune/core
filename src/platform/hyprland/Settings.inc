// IPC 0.30 global settings (settings.get / settings.set): one preset for every graphics and
// player setting, the user's explicit overrides on top (graphics.json values that are not
// null, player.json keys; both survive preset switches), and the one-time migration that turns
// existing files into a preset plus the values that differ from it. Interaction/Settings.cpp
// holds the presets and their defaults.
std::string Runtime::glRenderer() {
    Render::GL::g_pHyprOpenGL->makeEGLCurrent();
    auto s = glGetString(GL_RENDERER);
    return s ? std::string(reinterpret_cast<const char *>(s)) : std::string();
}
Json Runtime::settingsSnapshot() {
    auto preset = settingsPreset();
    auto device = glRenderer();
    auto quality = graphicsFile.current.value("quality", Json());
    auto defaults = interaction::presetDefaults(preset, device, quality.is_string() ? quality : Json());
    auto base = interaction::defaultConfig();
    Json settings = Json::object();
    int overrides = 0;
    for (auto section : {"graphics", "player"}) {
        auto &file = std::string(section) == "graphics" ? graphicsFile.current : playerFile.current;
        for (auto &k : interaction::governedKeys(section)) {
            std::string key = k;
            Json value = config[section].value(key, Json());
            if (std::string(section) == "graphics") {
                if (key == "npc_motion")
                    value = config["npc"]["motion"];
                else if (value.is_null() && renderer.graphics.contains(key))
                    value = renderer.graphics[key]; // inherited from the tier: the effective value
            }
            bool user = file.contains(key) && !file[key].is_null();
            bool lua = !user && configured[section].value(key, Json()) != base[section].value(key, Json());
            overrides += user;
            settings[std::string(section) + "." + key] = {{"value", value},
                                                          {"default", defaults.value(std::string(section) + "." + key, Json())},
                                                          {"source", user ? "user" : lua ? "lua" : "preset"}};
        }
    }
    Json presets = Json::array();
    for (auto p : interaction::settingsPresets()) {
        p["defaults"] = interaction::presetDefaults(p["id"], device);
        presets.push_back(std::move(p));
    }
    return {{"preset", preset},
            {"presets", presets},
            {"settings", settings},
            {"overrides", overrides},
            {"keymapDefaults", interaction::inputDefaults()},
            {"path", ipc::boundedText(settingsFile.path.string(), ipc::PathLimit)},
            {"error", ipc::boundedText(settingsFile.error, ipc::MessageLimit)}};
}
Json Runtime::settingsMethod(const std::string &method, const Json &params) {
    if (renderPass.active())
        throw ipc::Error(-32004, "settings update during render");
    if (method == "settings.get") {
        if (!params.empty())
            throw ipc::Error(-32602, "unexpected params");
        return settingsSnapshot();
    }
    for (auto &[key, value] : params.items())
        if (key != "preset" && key != "set" && key != "reset" && key != "resetAll")
            throw ipc::Error(-32602, "unexpected settings parameter: " + key);
    if (params.empty())
        throw ipc::Error(-32602, "settings.set needs preset, set, reset or resetAll");
    auto graphics = graphicsFile.current, player = playerFile.current;
    auto keymapNext = input.config;
    bool keymap = false;
    auto split = [](const std::string &key) -> std::pair<std::string, std::string> {
        auto dot = key.find('.');
        auto section = dot == std::string::npos ? "" : key.substr(0, dot), name = dot == std::string::npos ? "" : key.substr(dot + 1);
        auto keys = interaction::governedKeys(section);
        if ((section != "graphics" && section != "player") || std::find(keys.begin(), keys.end(), name) == keys.end())
            throw ipc::Error(-32602, "unknown setting: " + key);
        return {section, name};
    };
    // Reset all: every override goes (graphics, player and the keymap); the preset stays.
    if (params.contains("resetAll")) {
        if (!params["resetAll"].is_boolean())
            throw ipc::Error(-32602, "resetAll must be boolean");
        if (params["resetAll"] == true) {
            graphics = Json::object(), player = Json::object();
            keymapNext = interaction::inputDefaults();
            keymap = true;
        }
    }
    std::string preset = settingsPreset();
    if (params.contains("preset")) {
        if (!params["preset"].is_string() || !interaction::validPreset(params["preset"]))
            throw ipc::Error(-32602, "unknown preset");
        preset = params["preset"];
    }
    if (params.contains("reset")) {
        if (!params["reset"].is_array() || params["reset"].size() > 256)
            throw ipc::Error(-32602, "reset must be a list of settings");
        for (auto &k : params["reset"]) {
            if (!k.is_string())
                throw ipc::Error(-32602, "reset must be a list of settings");
            auto [section, name] = split(k);
            auto &file = section == "graphics" ? graphics : player;
            // Graphics keys a tier sets inherit with null; the rest drop their override.
            if (section == "graphics" && render::graphicsDefaults().at(name).is_null())
                file[name] = nullptr;
            else
                file.erase(name);
        }
    }
    if (params.contains("set")) {
        if (!params["set"].is_object() || params["set"].size() > 256)
            throw ipc::Error(-32602, "set must be an object of settings");
        for (auto &[k, value] : params["set"].items()) {
            auto [section, name] = split(k);
            if (value.is_null())
                throw ipc::Error(-32602, "set " + k + ": use reset to return to the default");
            if (name == "npc_motion" && !value.is_string())
                throw ipc::Error(-32602, "npc_motion must be auto, always, still or off");
            (section == "graphics" ? graphics : player)[name] = value;
        }
    }
    try {
        interaction::configPatch(configured, {{"graphics", graphics}, {"player", player}});
    } catch (const std::exception &e) {
        throw ipc::Error(-32602, e.what());
    }
    if (preset != settingsPreset() || settingsFile.current.value("preset", Json()).is_null())
        settingsFile.save({{"version", 1}, {"preset", preset}});
    if (graphics != graphicsFile.current)
        graphicsFile.save(graphics);
    if (player != playerFile.current)
        playerFile.save(player);
    if (keymap) {
        inputFile.save(keymapNext);
        applyInput(keymapNext);
    }
    applyGraphics(graphics);
    applyPlayer(player);
    publish();
    return settingsSnapshot();
}
// Once, when settings.json has no preset: the preset the existing graphics.json implies, and
// the graphics and player overrides that differ from it (equal values stop being overrides).
void Runtime::migrateSettings() {
    if (!settingsFile.current.value("preset", Json()).is_null())
        return;
    auto preset = interaction::presetFromGraphics(graphicsFile.current);
    auto defaults = interaction::presetDefaults(preset, glRenderer());
    auto graphics = interaction::prunedOverrides("graphics", graphicsFile.current, defaults);
    auto player = interaction::prunedOverrides("player", playerFile.current, defaults);
    if (graphics != graphicsFile.current)
        graphicsFile.save(graphics);
    if (player != playerFile.current)
        playerFile.save(player);
    settingsFile.save({{"version", 1}, {"preset", preset}});
    applyGraphics(graphics);
    applyPlayer(player);
}
