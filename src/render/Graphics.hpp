#pragma once
#include "world/World.hpp"
namespace hyprune::render {
using world::Json;
// Null means inherit the selected preset; explicit values survive preset changes.
inline Json graphicsDefaults() {
    return {{"quality", "auto"},
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
            {"fov", 60.},
            {"exposure", 1.},
            {"bloom_strength", .15},
            {"reduced_motion", false}};
}
inline Json graphicsPresets() {
    return {{"low",
             {{"msaa", 1},
              {"bloom", "off"},
              {"fog", "linear"},
              {"sky", "gradient"},
              {"reflections", false},
              {"normal_maps", false},
              {"specular", false},
              {"ambient_occlusion", true},
              {"alpha_haze", false},
              {"anisotropy", 1}}},
            {"medium",
             {{"msaa", 1},
              {"bloom", "low"},
              {"fog", "height"},
              {"sky", "environment"},
              {"reflections", true},
              {"normal_maps", true},
              {"specular", true},
              {"ambient_occlusion", true},
              {"alpha_haze", true},
              {"anisotropy", 2}}},
            {"high",
             {{"msaa", 2},
              {"bloom", "high"},
              {"fog", "height"},
              {"sky", "environment"},
              {"reflections", true},
              {"normal_maps", true},
              {"specular", true},
              {"ambient_occlusion", true},
              {"alpha_haze", true},
              {"anisotropy", 8}}},
            {"ultra",
             {{"msaa", 4},
              {"bloom", "high"},
              {"fog", "height"},
              {"sky", "environment"},
              {"reflections", true},
              {"normal_maps", true},
              {"specular", true},
              {"ambient_occlusion", true},
              {"alpha_haze", true},
              {"anisotropy", 16}}}};
}
inline Json graphicsOptions() {
    return {{"quality", {"auto", "low", "medium", "high", "ultra"}},
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
            {"dynamic_resolution", {false, true}}};
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
