#include "Guidance.hpp"
#include "Files.hpp"
#include <cmath>
#include <stdexcept>
namespace hyprune::quests {
Json Guidance::defaults() {
    return {{"markers.reveal_ms", 4000},       {"markers.reveal_mode", "timer"},  {"markers.max_revealed", 12},
            {"markers.labels", 5},             {"markers.persistent", "edge"},    {"markers.core_draw", true},
            {"quests.notifications", "all"},   {"focus.work_min", 25},            {"focus.short_break_min", 5},
            {"focus.long_break_min", 15},      {"focus.long_break_every", 4},     {"focus.cycles", 4},
            {"focus.auto_start_breaks", true}, {"focus.auto_start_work", false},  {"focus.count_mode", "focus"},
            {"focus.idle_s", 180},             {"focus.grace_s", 30},             {"focus.calm", true},
            {"focus.calm_npc_distance", 6},    {"focus.desktop_chip", false},     {"focus.history_days", 90}};
}
Json Guidance::ranges() {
    return {{"markers.reveal_ms", {1000, 15000}}, {"markers.max_revealed", {4, 16}}, {"markers.labels", {2, 8}},
            {"focus.work_min", {5, 120}},         {"focus.short_break_min", {1, 30}}, {"focus.long_break_min", {5, 60}},
            {"focus.long_break_every", {2, 12}},  {"focus.cycles", {1, 12}},         {"focus.idle_s", {60, 900}},
            {"focus.grace_s", {0, 120}},          {"focus.calm_npc_distance", {2, 20}}, {"focus.history_days", {0, 365}}};
}
Json Guidance::options() {
    return {{"markers.reveal_mode", {"timer", "toggle"}},
            {"markers.persistent", {"off", "edge", "always"}},
            {"quests.notifications", {"all", "important", "off"}},
            {"focus.count_mode", {"focus", "wall"}}};
}
Json Guidance::patched(const Json &overrides, const Json &patch) {
    if (!patch.is_object())
        throw std::invalid_argument("guidance patch must be an object");
    auto next = overrides;
    auto all = defaults(), range = ranges(), option = options();
    for (auto &[key, value] : patch.items()) {
        if (!all.contains(key))
            throw std::invalid_argument("unknown guidance setting: " + key);
        if (value.is_null()) {
            next.erase(key);
            continue;
        }
        if (range.contains(key)) {
            // Every guidance number is a whole number (minutes, seconds, counts, milliseconds).
            if (!value.is_number() || !std::isfinite(value.get<double>()) ||
                value.get<double>() != std::floor(value.get<double>()) || value < range[key][0] ||
                value > range[key][1])
                throw std::invalid_argument(key + " out of range");
            next[key] = value.get<int64_t>();
        } else if (option.contains(key)) {
            if (!value.is_string() || std::find(option[key].begin(), option[key].end(), value) == option[key].end())
                throw std::invalid_argument(key + " is not an allowed value");
            next[key] = value;
        } else {
            if (!value.is_boolean())
                throw std::invalid_argument(key + " must be boolean");
            next[key] = value;
        }
    }
    return next;
}
Guidance::Guidance(std::filesystem::path file) : path(std::move(file)) {}
void Guidance::load() {
    error.clear();
    overrides = Json::object();
    try {
        auto data = readPrivate(path, 65536);
        if (!data)
            return;
        auto j = Json::parse(*data);
        if (!j.is_object())
            throw std::invalid_argument("guidance file must be an object");
        // Keep every valid key; report the first invalid one.
        for (auto &[key, value] : j.items())
            try {
                overrides = patched(overrides, Json{{key, value}});
            } catch (const std::exception &e) {
                if (error.empty())
                    error = e.what();
            }
    } catch (const std::exception &e) {
        error = e.what();
    }
}
Json Guidance::config() const {
    auto c = defaults();
    for (auto &[key, value] : overrides.items())
        c[key] = value;
    return c;
}
Json Guidance::snapshot() const {
    return {{"config", config()},   {"defaults", defaults()},           {"overrides", overrides},
            {"ranges", ranges()},   {"options", options()},             {"path", path.string().substr(0, 4096)},
            {"error", error.substr(0, 4096)}};
}
void Guidance::set(const Json &patch) {
    auto next = patched(overrides, patch);
    writeAtomic(path, next.dump(2) + "\n");
    overrides = std::move(next);
    error.clear();
}
} // namespace hyprune::quests
