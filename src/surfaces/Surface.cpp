#include "Surface.hpp"
namespace hyprune::surfaces {
std::optional<Hit> intersect(const Surface &s, Vec3 origin, Vec3 direction) {
    Vec3 normal{std::sin(s.yaw), 0, std::cos(s.yaw)}, right{std::cos(s.yaw), 0, -std::sin(s.yaw)};
    float d = dot(direction, normal);
    if (d >= -1e-5f)
        return {};
    float t = dot(s.center - origin, normal) / d;
    if (t <= 0)
        return {};
    Vec3 p = origin + direction * t - s.center;
    float u = dot(p, right) / s.width + .5f, v = .5f - p.y / s.height;
    if (u < 0 || u > 1 || v < 0 || v > 1)
        return {};
    return Hit{t, u, v};
}
} // namespace hyprune::surfaces
