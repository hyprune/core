// IPC 0.30 global settings presets: defaults per preset, overrides and the migration.
#include "interaction/Config.hpp"
#include "interaction/Settings.hpp"
#include "render/Graphics.hpp"
#include <cassert>
#include <iostream>
using namespace hyprune;
using Json = world::Json;
int main() {
    auto presets = interaction::settingsPresets();
    std::vector<std::string> ids;
    for (auto &p : presets)
        ids.push_back(p["id"]);
    assert((ids == std::vector<std::string>{"auto", "low", "medium", "high", "ultra", "usb-c"}));
    assert(interaction::validPreset("ultra") && !interaction::validPreset("custom"));
    // Every governed key has a default in every preset, and a non-null one (tiers resolved).
    for (auto &id : ids) {
        auto d = interaction::presetDefaults(id, "NVIDIA GeForce RTX 3090");
        for (auto section : {"graphics", "player"})
            for (auto &k : interaction::governedKeys(section)) {
                auto key = std::string(section) + "." + k.get<std::string>();
                assert(d.contains(key));
                bool nullable = key == "graphics.offload_power_cap_w" || key == "graphics.outline_width_px" ||
                                key == "graphics.outline_colour" || key == "graphics.npc_motion" || key == "graphics.upscaler";
                assert(nullable || !d[key].is_null());
            }
    }
    auto low = interaction::presetDefaults("low", "x"), ultra = interaction::presetDefaults("ultra", "x");
    assert(low["graphics.quality"] == "low" && low["graphics.msaa"] == 1 && ultra["graphics.msaa"] == 4);
    assert(ultra["graphics.anisotropy"] == 16 && low["player.walk_speed"] == 4.0);
    // A tier chosen by the user sets the tier keys' defaults.
    assert(interaction::presetDefaults("low", "x", "ultra")["graphics.msaa"] == 4);
    // auto: integrated GPUs get the low tier, others medium.
    assert(interaction::presetDefaults("auto", "Mesa Intel(R) Graphics")["graphics.msaa"] == 1);
    assert(interaction::presetDefaults("auto", "Mesa Intel(R) Graphics")["graphics.bloom"] == "off");
    assert(interaction::presetDefaults("auto", "NVIDIA")["graphics.bloom"] == "low");
    auto usb = interaction::presetDefaults("usb-c", "x");
    assert(usb["graphics.offload"] == "on" && usb["graphics.frame_pacing"] == "even" && usb["graphics.quality"] == "medium");
    assert(interaction::presetSection("usb-c", "graphics") == Json({{"quality", "medium"}, {"offload", "on"}, {"frame_pacing", "even"}}));
    assert(interaction::presetSection("usb-c", "player").empty());
    // Migration: the preset an existing graphics.json implies; equal values stop being overrides.
    assert(interaction::presetFromGraphics(Json{{"quality", "medium"}}) == "medium");
    assert(interaction::presetFromGraphics(Json{{"quality", "intel"}}) == "low");
    assert(interaction::presetFromGraphics(Json::object()) == "auto");
    auto defaults = interaction::presetDefaults("medium", "x");
    Json existing = {{"quality", "medium"}, {"dynamic_resolution", true}, {"offload", "on"}, {"target_fps", 110},
                     {"sharpen", 1}, {"scale_min", .625}, {"render_scale", "auto"}, {"msaa", 1}, {"bloom", nullptr}};
    auto kept = interaction::prunedOverrides("graphics", existing, defaults);
    assert((kept == Json{{"offload", "on"}, {"target_fps", 110}, {"sharpen", 1}, {"scale_min", .625}, {"bloom", nullptr}}));
    auto player = interaction::prunedOverrides("player", Json{{"walk_speed", 4.0}, {"run_speed", 12.0}}, defaults);
    assert((player == Json{{"run_speed", 12.0}}));
    // The pruned overrides validate as config patches.
    interaction::configPatch(interaction::defaultConfig(), {{"graphics", kept}, {"player", player}});
    std::cout << "PASS settings presets, defaults, migration\n";
}
