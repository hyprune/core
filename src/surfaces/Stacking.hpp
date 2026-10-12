#pragma once
#include <cstddef>
#include <optional>
#include <span>
#include <utility>

namespace hyprune::surfaces {
// A native window as the compositor stacks it: layer 0 tiled, 1 floating, 2 pinned,
// 3 fullscreen; within a layer, its index in the compositor's window list (a raise moves a
// window to the end). The same order composes a workspace wall.
struct NativeSlot {
    double x = 0, y = 0, w = 0, h = 0; // logical box, global coordinates
    int layer = 0;
    size_t order = 0;
};
// The topmost window containing (x, y): the one a click there reaches natively, whatever
// order the windows were captured in.
inline std::optional<size_t> topmostAt(std::span<const NativeSlot> windows, double x, double y) {
    std::optional<size_t> best;
    for (size_t i = 0; i < windows.size(); ++i) {
        const auto &s = windows[i];
        if (x < s.x || x >= s.x + s.w || y < s.y || y >= s.y + s.h)
            continue;
        if (!best || std::pair{s.layer, s.order} > std::pair{windows[*best].layer, windows[*best].order})
            best = i;
    }
    return best;
}
} // namespace hyprune::surfaces
