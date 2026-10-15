#pragma once
#include "world/World.hpp"
namespace hyprune::render {
using world::Json;
// Null means inherit the selected preset; explicit values survive preset changes.
inline Json graphicsDefaults() {
    return {{"quality", "auto"},
            {"offload", "off"}, {"offload_power_cap_w", nullptr}, {"offload_device", "auto"},
            {"scale_min", .375}, {"scale_max", 1.0},
            {"outline_width_px", nullptr}, {"outline_colour", nullptr}, {"outline_quality", nullptr}, {"outline", nullptr}, {"visibility_culling", true},
            {"upscaler", nullptr},
            {"msaa", nullptr},
            {"bloom", nullptr},
            {"fog", nullptr},
            {"sky", nullptr},
            {"reflections", nullptr},
            {"normal_maps", nullptr},
            {"specular", nullptr},
            {"ambient_occlusion", nullptr},
            {"alpha_haze", nullptr},
            {"anisotropy", nullptr},
            {"dynamic_resolution", true},
            {"render_scale", "auto"},
            {"target_fps", 60},
            {"sharpen", .35},
            {"fov", 65.},
            {"exposure", 1.},
            {"bloom_strength", .15},
            {"reduced_motion", false},
            // IPC 0.29 feel settings (null: the preset's value). frame_pacing: "even" shows a
            // rate-capped renderer's new frames on a steady refresh divisor at a constant
            // latency; "max_rate" sends as many as the cap allows, unevenly spaced.
            {"frame_pacing", nullptr},
            // IPC 0.23: characters' motion (null: Lua npc.motion, default auto).
            {"npc_motion", nullptr}};
}
inline Json graphicsPresets() {
    return {{"low",
             {{"outline_quality", "fast"}, {"outline", true}, {"upscaler", "fsr1"}, {"msaa", 1},
              {"bloom", "off"},
              {"fog", "linear"},
              {"sky", "gradient"},
              {"reflections", false},
              {"normal_maps", false},
              {"specular", false},
              {"ambient_occlusion", true},
              {"alpha_haze", false},
              {"anisotropy", 1},
              {"frame_pacing", "even"}}},
            {"medium",
             {{"outline_quality", "fast"}, {"outline", true}, {"upscaler", "fsr1"}, {"msaa", 1},
              {"bloom", "low"},
              {"fog", "height"},
              {"sky", "environment"},
              {"reflections", true},
              {"normal_maps", true},
              {"specular", true},
              {"ambient_occlusion", true},
              {"alpha_haze", true},
              {"anisotropy", 2},
              {"frame_pacing", "even"}}},
            {"high",
             {{"outline_quality", "smooth"}, {"outline", true}, {"upscaler", "fsr1"}, {"msaa", 2},
              {"bloom", "high"},
              {"fog", "height"},
              {"sky", "environment"},
              {"reflections", true},
              {"normal_maps", true},
              {"specular", true},
              {"ambient_occlusion", true},
              {"alpha_haze", true},
              {"anisotropy", 8},
              {"frame_pacing", "even"}}},
            {"ultra",
             {{"outline_quality", "smooth"}, {"outline", true}, {"upscaler", "fsr1"}, {"msaa", 4},
              {"bloom", "high"},
              {"fog", "height"},
              {"sky", "environment"},
              {"reflections", true},
              {"normal_maps", true},
              {"specular", true},
              {"ambient_occlusion", true},
              {"alpha_haze", true},
              {"anisotropy", 16},
              {"frame_pacing", "even"}}}};
}
inline Json graphicsOptions() {
    return {{"quality", {"auto", "low", "medium", "high", "ultra"}},
            {"offload", {"off", "auto", "on"}},
            {"npc_motion", {"auto", "always", "still", "off"}},
            {"outline_quality", {"fast", "smooth"}}, {"upscaler", {"fsr1", "bilinear"}}, {"outline", {false, true}}, {"visibility_culling", {false,true}},
            {"msaa", {1, 2, 4}},
            {"bloom", {"off", "low", "high"}},
            {"fog", {"off", "linear", "height"}},
            {"sky", {"off", "gradient", "environment"}},
            {"reflections", {false, true}},
            {"normal_maps", {false, true}},
            {"specular", {false, true}},
            {"ambient_occlusion", {false, true}},
            {"alpha_haze", {false, true}},
            {"anisotropy", {1, 2, 4, 8, 16}},
            {"dynamic_resolution", {false, true}},
            {"frame_pacing", {"even", "max_rate"}}};
}
// Graphics settings added with IPC 0.29 (the feel settings): older peers never see them.
inline const std::vector<std::string> &feelGraphicsKeys() {
    static const std::vector<std::string> keys{"frame_pacing"};
    return keys;
}
// User presentation overrides are independent of the current world's defaults.
inline Json effectiveOutline(const Json &profile, const Json &graphics) {
    auto ink = profile.value("outline", Json::object());
    if (ink.empty()) return ink; // a width/colour override does not invent a style
    for (auto [key, field] : {std::pair{"outline_width_px", "widthPx"}, std::pair{"outline_colour", "colour"}})
        if (graphics.contains(key) && !graphics.at(key).is_null()) ink[field] = graphics.at(key);
    return ink;
}
inline Json effectiveGraphics(const Json &config, const std::string &device) {
    auto result = config;
    std::string quality = config.at("quality");
    if (quality == "intel")
        quality = "low";
    if (quality == "auto")
        quality = device.find("Intel") != std::string::npos ? "low" : "medium";
    result["quality"] = quality;
    auto presets = graphicsPresets();
    for (auto &[key, value] : presets.at(quality).items())
        if (result.at(key).is_null())
            result[key] = value;
    return result;
}
} // namespace hyprune::render
