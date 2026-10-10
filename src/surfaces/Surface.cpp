#include "Surface.hpp"
namespace hyprune::surfaces {
std::optional<Hit> intersect(const Surface &s, Vec3 origin, Vec3 direction, bool clip, bool backFace) {
    Vec3 normal = s.normal(), right = s.right(), up = s.up();
    float d = dot(direction, normal);
    bool back = d > 1e-5f;
    if (d >= -1e-5f && !(backFace && back))
        return {};
    float t = dot(s.center - origin, normal) / d;
    if (t <= 0)
        return {};
    Vec3 p = origin + direction * t - s.center;
    float u = dot(p, right) / s.width + .5f, v = .5f - dot(p, up) / s.height;
    if (clip && (u < 0 || u > 1 || v < 0 || v > 1))
        return {};
    return Hit{t, u, v, back};
}
} // namespace hyprune::surfaces
