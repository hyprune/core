#pragma once
#include <algorithm>
#include <cmath>
namespace hyprune::interaction {
// Monitor dimensions are logical, and may be smaller than the usual client
// minimum after scaling. std::clamp requires an ordered interval even then.
inline double resizeExtent(double requested, double minimum, double maximum) {
    maximum = std::max(1., maximum);
    return std::clamp(requested, std::min(minimum, maximum), maximum);
}
} // namespace hyprune::interaction
