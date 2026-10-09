#pragma once
#include "Resize.hpp"
#include <array>
namespace hyprune::interaction {
// Positive wheel steps enlarge UI by requesting fewer client logical pixels.
// Never derive these dimensions from a buffer's physical pixels/output scale.
inline std::array<int, 2> contentSize(int w, int h, float steps, double maxW, double maxH) {
    double factor = std::pow(1.12, -std::clamp(double(steps), -4., 4.));
    return {int(std::lround(resizeExtent(w * factor, 320., maxW))),
            int(std::lround(resizeExtent(h * factor, 200., maxH)))};
}
} // namespace hyprune::interaction
