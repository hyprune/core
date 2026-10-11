#pragma once
#include "interaction/InputFile.hpp"
#include <map>
#include <optional>
#include <unistd.h>
namespace hyprune::live {
// Only the explicit live build uses this non-secret bootstrap. No persistent
// compositor environment change, and no alteration of installed configuration.
class Environment {
    std::map<std::string, std::optional<std::string>> before;
  public:
    Environment() {
        auto runtime = getenv("XDG_RUNTIME_DIR"), signature = getenv("HYPRLAND_INSTANCE_SIGNATURE");
        if (!runtime || !signature)
            throw std::runtime_error("live bootstrap: missing instance");
        auto root = std::filesystem::path(runtime) / "hyprune-live" / signature;
        auto settings = interaction::readJSON(root / "bootstrap.json", 16384);
        if (settings.at("pid") != getpid())
            throw std::runtime_error("live bootstrap: compositor PID mismatch");
        for (auto &[key, value] : settings.at("environment").items()) {
            if (key != "HYPRUNE_CAPTURE_DIR" && key != "HYPRUNE_WORLD_DIR" && key != "HYPRUNE_INPUT_FILE" && key != "HYPRUNE_EDITOR_FILE" &&
                key != "HYPRUNE_LAYOUT_FILE" && key != "HYPRUNE_LIVE_ROOT" && key != "HYPRUNE_WORKSPACE_LOCATIONS" &&
                key != "HYPRUNE_DAEMON" && key != "HYPRUNE_RENDERER")
                throw std::runtime_error("live bootstrap: unknown setting");
            if (!value.is_null() && !value.is_string())
                throw std::runtime_error("live bootstrap: invalid setting");
            const char *old = getenv(key.c_str());
            before[key] = old ? std::optional<std::string>(old) : std::nullopt;
        }
        for (auto &[key, value] : settings.at("environment").items()) {
            if (value.is_null())
                unsetenv(key.c_str());
            else
                setenv(key.c_str(), value.get<std::string>().c_str(), 1);
        }
    }
    ~Environment() {
        for (auto &[key, value] : before)
            if (value) setenv(key.c_str(), value->c_str(), 1);
            else unsetenv(key.c_str());
    }
};
}
