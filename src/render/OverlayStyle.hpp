#pragma once
#include "world/World.hpp"
#include <set>
namespace hyprune::render {
inline world::Json overlayDefaults() {
    return {{"fontFamily", "monospace"},  {"waypointColor", {.25, 1., .65}}, {"routeColor", {.25, 1., .65}},
            {"labelColor", {1., 1., 1.}}, {"promptColor", {.7, .9, 1.}},     {"iconAtlas", nullptr},
            // IPC 0.19: the reticle's neutral, valid-target and invalid-target colours.
            {"reticleColor", {.95, .95, .95}}, {"validColor", {.35, .95, .6}}, {"invalidColor", {1., .62, .35}},
            // IPC 0.21: quest, focus and break markers (RFC-0012).
            {"questColor", {1., .78, .3}}};
}
inline world::Json overlayStylePatch(const world::Json &old, const world::Json &patch) {
    if (!patch.is_object())
        throw std::runtime_error("overlay style must be object");
    auto next = old;
    for (auto &[key, value] : patch.items()) {
        if (!next.contains(key))
            throw std::runtime_error("unknown overlay style field");
        if (key == "fontFamily") {
            if (!value.is_string() || value.get<std::string>().empty() || value.get<std::string>().size() > 96)
                throw std::runtime_error("font family length");
            for (unsigned char c : value.get<std::string>())
                if (c < 32 || c == 127)
                    throw std::runtime_error("font family control character");
        } else if (key == "iconAtlas") {
            if (!value.is_null()) {
                if (!value.is_object() || value.size() != 4 || !value.contains("path") || !value.contains("cellSize") ||
                    !value.contains("waypointIndex") || !value.contains("promptIndex"))
                    throw std::runtime_error("invalid icon atlas descriptor");
                if (!value["path"].is_string() || value["path"].get<std::string>().empty() ||
                    value["path"].get<std::string>()[0] != '/' || value["path"].get<std::string>().size() > 4096 ||
                    value["path"].get<std::string>().find('\0') != std::string::npos)
                    throw std::runtime_error("atlas needs absolute local path");
                for (auto field : {"cellSize", "waypointIndex", "promptIndex"})
                    if (!value[field].is_number_integer() ||
                        value[field] < (std::string(field) == "cellSize" ? 16 : 0) ||
                        value[field] > (std::string(field) == "cellSize" ? 128 : 255))
                        throw std::runtime_error("atlas cell/index range");
            }
        } else {
            if (!value.is_array() || value.size() != 3)
                throw std::runtime_error("overlay colour needs RGB");
            for (auto &v : value)
                if (!v.is_number() || !std::isfinite(v.get<double>()) || v < 0 || v > 1)
                    throw std::runtime_error("overlay colour range");
        }
        next[key] = value;
    }
    return next;
}
} // namespace hyprune::render
