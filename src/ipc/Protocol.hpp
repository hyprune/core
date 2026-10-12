#pragma once
#include <array>
#include <charconv>
#include <compare>
#include <optional>
#include <string_view>

namespace hyprune::ipc::protocol {
struct Version {
    unsigned major, minor;
    auto operator<=>(const Version &) const = default;
};
// Canonical wire spelling only. Numeric comparison handles 0.10 correctly;
// malformed/overflowing versions never pass a capability gate.
inline std::optional<Version> parse(std::string_view text) {
    auto dot = text.find('.');
    if (dot == text.npos)
        return std::nullopt;
    Version result{};
    auto number = [](std::string_view part, unsigned &value) {
        if (part.empty() || (part.size() > 1 && part.front() == '0'))
            return false;
        auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), value);
        return error == std::errc{} && end == part.data() + part.size();
    };
    if (!number(text.substr(0, dot), result.major) || !number(text.substr(dot + 1), result.minor))
        return std::nullopt;
    return result;
}
inline bool atLeast(std::string_view value, std::string_view minimum) {
    auto v = parse(value), m = parse(minimum);
    return v && m && *v >= *m;
}
inline bool exactly(std::string_view value, std::string_view expected) {
    auto v = parse(value), e = parse(expected);
    return v && e && *v == *e;
}
// This is the only supported-version inventory. Capability gates use floors,
// not this list: adding a negotiated version inherits existing functionality.
inline constexpr std::array Supported{"0.1", "0.2",  "0.3",  "0.4",  "0.5",  "0.6",  "0.7", "0.8",
                                      "0.9", "0.10", "0.11", "0.12", "0.13", "0.14", "0.15", "0.16", "0.17", "0.18", "0.19", "0.20", "0.21", "0.22", "0.23"};
inline constexpr std::string_view Latest = Supported.back();
template <class Offers> std::optional<std::string_view> negotiate(const Offers &offers) {
    std::optional<std::string_view> selected;
    for (auto supported : Supported)
        for (const auto &offer : offers)
            if (exactly(offer, supported) && (!selected || atLeast(supported, *selected)))
                selected = supported;
    return selected;
}
inline bool methodAvailable(std::string_view version, std::string_view method) {
    if (!atLeast(version, "0.1"))
        return false;
    if (method.starts_with("npcs.") || method.starts_with("paths.") || method == "interaction.choose")
        return atLeast(version, "0.23");
    if (method.starts_with("objects.") || method.starts_with("libraries."))
        return atLeast(version, "0.22");
    if (method.starts_with("markers.") || method.starts_with("quests.") || method.starts_with("focus.") ||
        method.starts_with("guidance."))
        return atLeast(version, "0.21");
    if (method == "surface.pocket" || method == "surface.unpocket")
        return atLeast(version, "0.20");
    if (method.starts_with("player."))
        return atLeast(version, "0.19");
    if (method.starts_with("capture."))
        return atLeast(version, "0.17");
    if (method == "camera.photo" || method == "camera.release")
        return atLeast(version, "0.13");
    if (method.starts_with("recording.") || method.starts_with("replay."))
        return atLeast(version, "0.12");
    if (method.starts_with("hud."))
        return atLeast(version, "0.9");
    if (method.starts_with("graphics."))
        return atLeast(version, "0.7");
    if (method.starts_with("input.") || method == "overlay.style")
        return atLeast(version, "0.6");
    for (auto name : {"workspace.bind", "workspace.focus", "workspace.unfocus", "surface.pull", "surface.place",
                      "surface.return", "surfaces.recall", "placements.list", "placements.remove", "data.object.bind",
                      "camera.fov", "atmosphere.get", "atmosphere.set"})
        if (method == name)
            return atLeast(version, "0.4");
    if (method == "shell.bind" || method == "workspace.activate")
        return atLeast(version, "0.3");
    for (auto name : {"world.load", "surface.focus", "movement.set", "runtime.exit", "shell.claim", "shell.release",
                      "shell.overlay"})
        if (method == name)
            return true;
    return atLeast(version, "0.2");
}
} // namespace hyprune::ipc::protocol
