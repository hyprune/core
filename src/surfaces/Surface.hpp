#pragma once
#include "sim/Math.hpp"
#include <optional>
#include <string>
namespace hyprune::surfaces {
struct Surface {
    std::string id, title;
    Vec3 center;
    float width = 3.2f, height = 1.8f, yaw = 0;
    std::array<float, 4> rotation{0, 0, 0, 1};
    Vec3 right() const { return rotate(rotation, {std::cos(yaw), 0, -std::sin(yaw)}); }
    Vec3 up() const { return rotate(rotation, {0, 1, 0}); }
    Vec3 normal() const { return cross(right(), up()); }
    int pixelWidth = 1, pixelHeight = 1;
    bool mipmapped = false; // texture carries a full mip chain (composed workspace walls)
    // Logical client units, never buffer pixels or output scale.
    void logicalSize(int w, int h, float metresPerUnit) {
        pixelWidth = std::max(1, w);
        pixelHeight = std::max(1, h);
        width = pixelWidth * metresPerUnit;
        height = pixelHeight * metresPerUnit;
    }
    float opacity = 1; // placement previews only; never part of client buffer alpha
    unsigned texture = 0;
    bool focused = false;
};
struct Hit {
    float distance, u, v;
    bool back = false; // seen from behind (only with backFace); u/v stay in surface coordinates
};
// Front faces only, unless backFace: clicks never reach a window from behind, but a
// carry gesture may grab it there (and turns it to face the player).
std::optional<Hit> intersect(const Surface &, Vec3 origin, Vec3 direction, bool clip = true, bool backFace = false);
} // namespace hyprune::surfaces
