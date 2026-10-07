#pragma once
#include "sim/Math.hpp"
#include <optional>
#include <string>
namespace hyprune::surfaces {
struct Surface {
    std::string id, title;
    Vec3 center;
    float width = 3.2f, height = 1.8f, yaw = 0;
    int pixelWidth = 1, pixelHeight = 1;
    unsigned texture = 0;
    bool focused = false;
};
struct Hit {
    float distance, u, v;
};
std::optional<Hit> intersect(const Surface &, Vec3 origin, Vec3 direction);
} // namespace hyprune::surfaces
