#include "Config.hpp"
#include "render/Graphics.hpp"
#include <cmath>
#include <set>
namespace hyprune::interaction {
using Json = world::Json;
Json defaultConfig() {
    return {
        {"input",
         {{"typing_button", 275},
          {"carry_button", 272},
          {"resize_button", 273},
          {"roll_button", 274},
          {"typing_cursor", true},
          {"bindings",
           {{"forward", "w"},
            {"back", "s"},
            {"left", "a"},
            {"right", "d"},
            {"jump", "space"},
            {"down", "Control_L"},
            {"focus", "f"},
            {"noclip", "v"},
            {"fly", "F4"},
            {"zoom", "c"}}}}},
        {"movement", {{"climb_mode", "grab"}}},
        {"player",
         {{"walk_speed", 3.5},
          {"fly_speed", 5.0},
          {"fly_bounds_allowance", 0.5},
          {"run_multiplier", 1.8},
          {"jump_speed", 5.0},
          {"look_sensitivity", .0025},
          {"look_inertia", .03},
          {"move_inertia", .05},
          {"step_height", .35},
          {"respawn_below", 30.0}}},
        {"windows",
         {{"world_workspace", 8},
          {"workspace_base", 10},
          {"carry_face_player", true},
          {"spawn_distance", 5.0},
          {"scale", .002},
          {"wheel_factor", 1.06},
          {"transition_seconds", .35}}},
        {"tools", {{"enabled", true}, {"slots", Json::array({"pointer", "mount", "path", "position", "placement"})}}},
        {"graphics", render::graphicsDefaults()}};
}
Json configPatch(const Json &current, const Json &patch) {
    if (!patch.is_object())
        throw std::runtime_error("config must be a table");
    auto result = current;
    for (auto &[section, value] : patch.items()) {
        if (!result.contains(section) || !value.is_object())
            throw std::runtime_error("unknown config section: " + section);
        for (auto &[key, v] : value.items()) {
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
                if (key == "offload_power_cap_w") { min = 20; max = 100; }
                if (key == "scale_min" || key == "scale_max") { min = .375; max = 1; }
                if (key == "target_fps") {
                    min = 30;
                    max = 240;
                    if (!v.is_number_integer())
                        throw std::runtime_error("target_fps must be integer");
                }
                if (key == "walk_speed" || key == "fly_speed") {
                    min = .1;
                    max = 20;
                }
                if (key == "run_multiplier") {
                    min = 1;
                    max = 4;
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
                    max = 110;
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
    for (auto key : {"typing_button", "carry_button", "resize_button", "roll_button"}) {
        int value = result["input"][key];
        if (value && !buttons.insert(value).second)
            throw std::runtime_error("duplicate mouse button");
    }
    if (result["graphics"]["scale_min"] > result["graphics"]["scale_max"])
        throw std::runtime_error("scale_min must not exceed scale_max");
    return result;
}
} // namespace hyprune::interaction
