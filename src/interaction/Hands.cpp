#include "Hands.hpp"
namespace hyprune::interaction {
std::optional<WorldHit> raycast(Vec3 o, Vec3 d, std::span<const Triangle> mesh) {
    std::optional<WorldHit> best;
    for (auto &t : mesh) {
        auto e = t.b - t.a, f = t.c - t.a, p = cross(d, f);
        float det = dot(e, p);
        if (std::abs(det) < 1e-7f)
            continue;
        auto v = o - t.a;
        float u = dot(v, p) / det;
        auto q = cross(v, e);
        float w = dot(d, q) / det, distance = dot(f, q) / det;
        if (u < 0 || w < 0 || u + w > 1 || distance <= 0 || (best && distance >= best->distance))
            continue;
        auto n = normalized(cross(e, f));
        if (dot(n, d) > 0)
            n = n * -1;
        best = WorldHit{distance, o + d * distance, n};
    }
    return best;
}
std::array<float, 4> orientation(Vec3 n) {
    n = normalized(n);
    auto axis = cross(Vec3{0, 0, 1}, n);
    float w = 1 + n.z;
    if (w < 1e-6f)
        return {0, 1, 0, 0};
    float l = std::sqrt(dot(axis, axis) + w * w);
    return {axis.x / l, axis.y / l, axis.z / l, w / l};
}
bool sweepSafe(const surfaces::Surface &a, const surfaces::Surface &b, std::span<const Triangle> mesh) {
    // Adaptive linear/rotational sweep. Every intermediate full OBB must remain clear.
    float angle = 2 * std::acos(std::clamp(std::abs(a.rotation[0] * b.rotation[0] + a.rotation[1] * b.rotation[1] +
                                                    a.rotation[2] * b.rotation[2] + a.rotation[3] * b.rotation[3]),
                                           0.f, 1.f));
    int steps = std::max(1, int(std::ceil((length(b.center - a.center) + angle * std::max(a.width, a.height) +
                                           std::abs(b.width - a.width) + std::abs(b.height - a.height)) /
                                          .015f)));
    if (steps > 4096)
        return false;
    float sign = 0;
    for (int j = 0; j < 4; j++)
        sign += a.rotation[j] * b.rotation[j];
    for (int i = 1; i <= steps; i++) {
        float t = float(i) / steps;
        auto s = b;
        s.center = a.center + (b.center - a.center) * t;
        s.width = a.width + (b.width - a.width) * t;
        s.height = a.height + (b.height - a.height) * t;
        float norm = 0;
        for (int j = 0; j < 4; j++) {
            s.rotation[j] = a.rotation[j] * (1 - t) + b.rotation[j] * (sign < 0 ? -t : t);
            norm += s.rotation[j] * s.rotation[j];
        }
        for (auto &q : s.rotation)
            q /= std::sqrt(norm);
        s.yaw = a.yaw + (b.yaw - a.yaw) * t;
        if (surfaces::overlaps(s, mesh))
            return false;
    }
    return true;
}
void Hands::begin(Gesture g, const surfaces::Surface &s, surfaces::Hit h) {
    gesture = g;
    start = pose = s;
    distance = targetDistance = h.distance;
    auto q = s.rotation;
    float sy = std::sin(s.yaw / 2), cy = std::cos(s.yaw / 2);
    start.rotation = {q[0] * cy - q[2] * sy, q[1] * cy + q[3] * sy, q[2] * cy + q[0] * sy, q[3] * cy - q[1] * sy};
    start.yaw = 0;
    pose = start;
    offset = s.right() * ((h.u - .5f) * s.width) + s.up() * ((.5f - h.v) * s.height);
    reference = length(offset) > .01f ? offset : s.up() * .01f;
}
void Hands::wheel(float steps) {
    targetDistance = std::clamp(targetDistance * std::pow(1.06f, std::clamp(steps, -2.f, 2.f)), .08f, 100.f);
}
bool Hands::update(const sim::Camera &camera, Vec3 ray, float dt, std::span<const Triangle> mesh) {
    if (gesture == Gesture::None)
        return false;
    auto next = pose;
    if (gesture == Gesture::Carry) {
        distance += (targetDistance - distance) * (1 - std::exp(-dt / .06f));
        next.center = camera.position + ray * distance - offset;
    } else {
        auto h = surfaces::intersect(start, camera.position, ray, false);
        if (!h)
            return false;
        auto v = camera.position + ray * h->distance - start.center;
        if (gesture == Gesture::Roll) {
            float a = std::atan2(dot(cross(reference, v), start.normal()), dot(reference, v));
            // Local Z rotation composed with the starting quaternion/yaw.
            float sn = std::sin(a / 2), cs = std::cos(a / 2);
            auto q = start.rotation;
            next.rotation = {q[0] * cs + q[1] * sn, q[1] * cs - q[0] * sn, q[2] * cs + q[3] * sn,
                             q[3] * cs - q[2] * sn};
        } else {
            // Independent edges, as in the prototype: client aspect follows the actual configure.
            next.width = std::clamp(2 * std::abs(dot(v, start.right())), start.width * 160 / start.pixelWidth, 12.f);
            next.height = std::clamp(2 * std::abs(dot(v, start.up())), start.height * 100 / start.pixelHeight, 8.f);
        }
    }
    if (!sweepSafe(pose, next, mesh))
        return false;
    pose = next;
    return true;
}
} // namespace hyprune::interaction
