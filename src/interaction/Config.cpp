#include "Config.hpp"
#include "render/Graphics.hpp"
#include <algorithm>
#include <cmath>
#include <regex>
#include <set>
namespace hyprune::interaction {
using Json = world::Json;
Json defaultConfig() {
    return {
        {"input",
         {{"typing_button", 275},
          {"carry_button", 272},
          {"resize_button", 273},
          {"typing_cursor", true},
          {"bindings",
           {{"forward", "w"},
            {"back", "s"},
            {"left", "a"},
            {"right", "d"},
            {"jump", "space"},
            {"down", "Shift_L"},
            {"focus", "f"},
            {"noclip", "v"},
            {"fly", "F4"},
            {"zoom", "c"}}}}},
        {"movement", {{"climb_mode", "grab"}}},
        // NPCs (RFC-0010 prototype, Lua only): auto animates them only while the player is
        // present in the world on mains power; still poses them; off hides them.
        {"npc", {{"motion", "auto"}}},
        {"player",
         {{"walk_speed", 4.0},
          {"run_speed", 10.0},
          {"fly_speed", 5.0},
          {"fly_bounds_allowance", 0.5},
          {"jump_speed", 5.0},
          {"look_sensitivity", .0025},
          {"look_inertia", .03},
          {"move_inertia", .05},
          {"step_height", .35},
          {"respawn_below", 30.0},
          // Player settings on the wire (player.get/set, IPC 0.19); Lua can set them too.
          {"view_bob", "subtle"},
          {"reticle_style", "cross_dot"},
          {"reticle_size", 18.0},
          {"reticle_thickness", 2.0},
          {"reticle_gap", 4.0},
          {"reticle_opacity", .9},
          {"reticle_outline", true},
          {"reticle_colour", "theme"},
          {"reticle_dynamic", true},
          {"reach_grab", 15.0},
          {"reach_mount", 8.0},
          {"reach_placement", 12.0}}},
        {"windows",
         {{"world_workspace", 20},
          {"workspace_base", 20},
          {"carry_face_player", true},
          {"spawn_distance", 5.0},
          {"scale", .002},
          {"wheel_factor", 1.06},
          {"transition_seconds", .35}}},
        // Captures only (U, photo mode): never the live HUD or world rendering.
        {"capture", {{"watermark", false}}},
        {"tools", {{"enabled", true}, {"slots", Json::array({"pointer", "mount", "path", "position", "placement"})}}},
        {"graphics", render::graphicsDefaults()}};
}
Json playerOptions() {
    return {{"view_bob", {"off", "subtle", "normal"}},
            {"reticle_style", {"cross_dot", "t_dot", "circle_dot", "dot", "cross", "chevron"}}};
}
Json playerRanges() {
    return {{"walk_speed", {.5, 20}},       {"run_speed", {.5, 30}},        {"reticle_size", {6, 64}},
            {"reticle_thickness", {1, 6}},  {"reticle_gap", {0, 20}},       {"reticle_opacity", {.2, 1}},
            {"reach_grab", {1, 100}},       {"reach_mount", {1, 100}},      {"reach_placement", {1, 100}}};
}
Json playerSettingsKeys() {
    Json keys = Json::array();
    for (auto &[key, value] : playerRanges().items())
        keys.push_back(key);
    for (auto key : {"view_bob", "reticle_style", "reticle_outline", "reticle_colour", "reticle_dynamic"})
        keys.push_back(key);
    return keys;
}
Json configPatch(const Json &current, const Json &patch) {
    if (!patch.is_object())
        throw std::runtime_error("config must be a table");
    auto result = current;
    for (auto &[section, value] : patch.items()) {
        if (!result.contains(section) || !value.is_object())
            throw std::runtime_error("unknown config section: " + section);
        for (auto &[key, v] : value.items()) {
            if (section == "player" && key == "run_multiplier") {
                // Deprecated Lua alias from before run_speed: run = walk x multiplier.
                if (!v.is_number() || !std::isfinite(v.get<double>()) || v < 1 || v > 4)
                    throw std::runtime_error("run_multiplier outside supported range");
                double walk = value.contains("walk_speed") && value["walk_speed"].is_number() ? value["walk_speed"].get<double>()
                                                                                               : result["player"]["walk_speed"].get<double>();
                result["player"]["run_speed"] = std::clamp(walk * v.get<double>(), .5, 30.);
                continue;
            }
            if (section == "player" && playerOptions().contains(key)) {
                auto options = playerOptions().at(key);
                if (std::find(options.begin(), options.end(), v) == options.end())
                    throw std::runtime_error("unsupported player." + key);
                result[section][key] = v;
                continue;
            }
            if (section == "player" && key == "reticle_colour") {
                bool valid = v == "theme";
                if (v.is_array() && v.size() == 3)
                    valid = std::all_of(v.begin(), v.end(), [](const Json &c) { return c.is_number() && std::isfinite(c.get<double>()) && c >= 0 && c <= 1; });
                if (!valid)
                    throw std::runtime_error("reticle_colour must be \"theme\" or RGB 0..1");
                result[section][key] = v;
                continue;
            }
            // Removed: Super+MMB never reached it (MMB is the tools' Alternate; rotation is
            // Super+Ctrl+Wheel). Older configs that still set it keep loading; it has no effect.
            if (section == "input" && key == "roll_button") {
                if (!v.is_number_integer() || v.get<int>() < 0 || v.get<int>() > 767)
                    throw std::runtime_error("roll_button must be an evdev integer");
                continue;
            }
            if (!result[section].contains(key))
                throw std::runtime_error("unknown config option: " + section + "." + key);
            if (section == "input" && key == "bindings") {
                if (!v.is_object())
                    throw std::runtime_error("bindings must be a table");
                for (auto &[action, sym] : v.items()) {
                    if (!result[section][key].contains(action) || !sym.is_string() || sym.get<std::string>().empty() ||
                        sym.get<std::string>().size() > 32)
                        throw std::runtime_error("invalid binding: " + action);
                    result[section][key][action] = sym;
                }
                std::set<std::string> names;
                for (auto &[action, sym] : result[section][key].items())
                    if (!names.insert(sym).second)
                        throw std::runtime_error("duplicate binding");
                continue;
            }
            if (section == "graphics" && render::graphicsOptions().contains(key)) {
                auto options = render::graphicsOptions().at(key);
                bool inherited = render::graphicsDefaults().at(key).is_null() && v.is_null();
                bool legacy = key == "quality" && v == "intel";
                if (!inherited && !legacy && std::find(options.begin(), options.end(), v) == options.end())
                    throw std::runtime_error("unsupported graphics." + key);
                result[section][key] = v;
                continue;
            }
            if (section == "graphics" && (key == "offload_power_cap_w" || key == "offload_device")) {
                // null cap = automatic: laptop profile ceilings, or a share of the GPU's enforced limit.
                bool valid = key == "offload_power_cap_w"
                                 ? v.is_null() || (v.is_number() && std::isfinite(v.get<double>()) && v >= 20 && v <= 1000)
                                 : v.is_string() && std::regex_match(v.get<std::string>(),
                                                                     std::regex("auto|[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\\.[0-7]"));
                if (!valid) throw std::runtime_error("invalid graphics." + key);
                result[section][key] = v;
                continue;
            }
            if (section == "graphics" && (key == "outline_width_px" || key == "outline_colour")) {
                bool valid = v.is_null();
                if (key == "outline_width_px") valid |= v.is_number() && std::isfinite(v.get<double>()) && v >= 1 && v <= 4;
                else if (v.is_array() && v.size() == 3)
                    valid = std::all_of(v.begin(), v.end(), [](const Json &c) { return c.is_number() && std::isfinite(c.get<double>()) && c >= 0 && c <= 1; });
                if (!valid) throw std::runtime_error("invalid graphics." + key);
                result[section][key] = v;
                continue;
            }
            if (section == "graphics" && key == "render_scale") {
                if (v != "auto" && (!v.is_number() || !std::isfinite(v.get<double>()) || v < .375 || v > 1))
                    throw std::runtime_error("render_scale must be auto or 0.375..1");
                result[section][key] = v;
                continue;
            }
            if (section == "npc" && key == "motion" && v != "auto" && v != "always" && v != "still" && v != "off")
                throw std::runtime_error("npc.motion must be auto, always, still or off");
            if (section == "movement" && key == "climb_mode") {
                if (!v.is_string())
                    throw std::runtime_error("climb_mode must be a string");
                sim::climbMode(v.get<std::string>());
            }
            auto old = result[section][key];
            if (old.is_boolean()) {
                if (!v.is_boolean())
                    throw std::runtime_error(key + " must be boolean");
            } else if (old.is_number()) {
                if (!v.is_number() || !std::isfinite(v.get<double>()))
                    throw std::runtime_error(key + " must be finite number");
                double min = 0, max = 100;
                if (key.ends_with("_button")) {
                    min = 0;
                    max = 767;
                    if (!v.is_number_integer())
                        throw std::runtime_error("typing_button must be an evdev integer");
                }
                if (key == "world_workspace") {
                    min = 1;
                    max = 999999;
                    if (!v.is_number_integer())
                        throw std::runtime_error("world_workspace must be a positive integer");
                }
                if (key == "workspace_base") {
                    min = 0;
                    max = 990000;
                    if (!v.is_number_integer())
                        throw std::runtime_error("workspace_base must be a non-negative integer");
                }
                if (key == "fly_bounds_allowance") { min = 0; max = 20; }
                if (key == "scale_min" || key == "scale_max") { min = .375; max = 1; }
                if (key == "target_fps") {
                    min = 30;
                    max = 240;
                    if (!v.is_number_integer())
                        throw std::runtime_error("target_fps must be integer");
                }
                if (key == "fly_speed") {
                    min = .1;
                    max = 20;
                }
                if (playerRanges().contains(key)) {
                    min = playerRanges().at(key)[0];
                    max = playerRanges().at(key)[1];
                }
                if (key == "jump_speed") {
                    min = 0;
                    max = 15;
                }
                if (key == "look_sensitivity") {
                    min = .00001;
                    max = .05;
                }
                if (key == "look_inertia" || key == "move_inertia") {
                    max = 1;
                }
                if (key == "step_height") {
                    max = .5;
                }
                if (key == "spawn_distance") {
                    min = .2;
                    max = 50;
                }
                if (key == "scale") {
                    min = .0001;
                    max = .02;
                }
                if (key == "wheel_factor") {
                    min = 1.001;
                    max = 1.5;
                }
                if (key == "transition_seconds") {
                    max = 2;
                }
                if (key == "fov") {
                    min = 30;
                    max = 120;
                }
                if (key == "exposure") {
                    min = .01;
                    max = 16;
                }
                if (key == "sharpen")
                    max = 1;
                if (key == "bloom_strength") {
                    max = 4;
                }
                if (v.get<double>() < min || v.get<double>() > max)
                    throw std::runtime_error(key + " outside supported range");
            } else if (key == "quality") {
                if (v != "auto" && v != "high" && v != "intel")
                    throw std::runtime_error("quality must be auto, high or intel");
            } else if (key == "slots") {
                if (!v.is_array() || v.size() != 5)
                    throw std::runtime_error("tools.slots needs five entries");
                for (auto &s : v)
                    if (s != "pointer" && s != "mount" && s != "path" && s != "position" && s != "placement")
                        throw std::runtime_error("unknown tool slot");
            }
            result[section][key] = v;
        }
    }
    std::set<int> buttons;
    for (auto key : {"typing_button", "carry_button", "resize_button"}) {
        int value = result["input"][key];
        if (value && !buttons.insert(value).second)
            throw std::runtime_error("duplicate mouse button");
    }
    if (result["graphics"]["scale_min"] > result["graphics"]["scale_max"])
        throw std::runtime_error("scale_min must not exceed scale_max");
    return result;
}
} // namespace hyprune::interaction
