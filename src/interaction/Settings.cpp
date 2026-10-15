#include "Settings.hpp"
#include "Config.hpp"
#include "render/Graphics.hpp"
namespace hyprune::interaction {
Json settingsPresets() {
    // values: what differs from the base defaults (render::graphicsDefaults, the player
    // defaults). The rendering tier's own keys (msaa, bloom, ...) follow graphics.quality.
    return Json::array(
        {{{"id", "auto"},
          {"name", "Automatic"},
          {"description", "Low on integrated GPUs, Medium elsewhere."},
          {"values", {{"graphics.quality", "auto"}}}},
         {{"id", "low"},
          {"name", "Low"},
          {"description", "Integrated GPUs and battery: simple shading, FSR upscaling, no MSAA."},
          {"values", {{"graphics.quality", "low"}}}},
         {{"id", "medium"},
          {"name", "Medium"},
          {"description", "Most discrete GPUs: reflections, normal maps, light bloom."},
          {"values", {{"graphics.quality", "medium"}}}},
         {{"id", "high"},
          {"name", "High"},
          {"description", "Fast GPUs: 2x MSAA, full bloom, smooth outlines."},
          {"values", {{"graphics.quality", "high"}}}},
         {{"id", "ultra"},
          {"name", "Ultra"},
          {"description", "The fastest GPUs: 4x MSAA, 16x anisotropic filtering."},
          {"values", {{"graphics.quality", "ultra"}}}},
         {{"id", "usb-c"},
          {"name", "USB-C laptop (offload)"},
          {"description", "A laptop on USB-C power: renders on the discrete GPU (offload), even frame pacing."},
          {"values", {{"graphics.quality", "medium"}, {"graphics.offload", "on"}, {"graphics.frame_pacing", "even"}}}}});
}
bool validPreset(const std::string &id) {
    for (auto &p : settingsPresets())
        if (p["id"] == id)
            return true;
    return false;
}
Json governedKeys(const std::string &section) {
    Json keys = Json::array();
    if (section == "graphics")
        for (auto &[key, value] : render::graphicsDefaults().items())
            keys.push_back(key);
    else if (section == "player")
        keys = playerSettingsKeys();
    return keys;
}
Json presetSection(const std::string &preset, const std::string &section) {
    Json out = Json::object();
    for (auto &p : settingsPresets())
        if (p["id"] == preset)
            for (auto &[key, value] : p["values"].items())
                if (key.starts_with(section + "."))
                    out[key.substr(section.size() + 1)] = value;
    return out;
}
Json presetDefaults(const std::string &preset, const std::string &device, const Json &quality) {
    Json out = Json::object();
    auto config = defaultConfig();
    auto graphics = render::graphicsDefaults();
    graphics.update(presetSection(preset, "graphics"));
    // The tier's rendering keys: what a null (inherit) resolves to under this preset.
    auto tier = graphics;
    if (quality.is_string())
        tier["quality"] = quality;
    auto effective = render::effectiveGraphics(tier, device);
    for (auto &[key, value] : graphics.items())
        out["graphics." + key] = value.is_null() && effective.contains(key) ? effective[key] : value;
    auto player = config["player"];
    player.update(presetSection(preset, "player"));
    for (auto &key : playerSettingsKeys())
        out["player." + key.get<std::string>()] = player[key.get<std::string>()];
    return out;
}
std::string presetFromGraphics(const Json &graphicsOverrides) {
    auto q = graphicsOverrides.is_object() ? graphicsOverrides.value("quality", Json()) : Json();
    if (q == "intel")
        return "low";
    if (q.is_string() && validPreset(q.get<std::string>()) && q != "usb-c")
        return q;
    return "auto";
}
Json prunedOverrides(const std::string &section, const Json &overrides, const Json &defaults) {
    Json out = Json::object();
    if (!overrides.is_object())
        return out;
    for (auto &[key, value] : overrides.items()) {
        if (value.is_null()) {
            if (section == "graphics")
                out[key] = value; // inherit the tier: kept as it was
            continue;
        }
        auto d = defaults.find(section + "." + key);
        bool same = d != defaults.end() && (*d == value || (d->is_number() && value.is_number() &&
                                                            std::abs(d->get<double>() - value.get<double>()) < 1e-9));
        if (!same)
            out[key] = value;
    }
    return out;
}
} // namespace hyprune::interaction
