#include "Hands.hpp"
#include "Resize.hpp"
#include <algorithm>
#include <vector>
namespace hyprune::interaction {
std::optional<WorldHit> raycast(Vec3 o, Vec3 d, std::span<const Triangle> mesh, const sim::TriangleIndex *index) {
    std::optional<WorldHit> best;
    auto test = [&](const Triangle &t) {
        auto e = t.b - t.a, f = t.c - t.a, p = cross(d, f);
        float det = dot(e, p);
        if (std::abs(det) < 1e-7f)
            return;
        auto v = o - t.a;
        float u = dot(v, p) / det;
        auto q = cross(v, e);
        float w = dot(d, q) / det, distance = dot(f, q) / det;
        if (u < 0 || w < 0 || u + w > 1 || distance <= 0 || (best && distance >= best->distance))
            return;
        auto n = normalized(cross(e, f));
        if (dot(n, d) > 0)
            n = n * -1;
        best = WorldHit{distance, o + d * distance, n};
    };
    if (index && index->size() == mesh.size()) {
        for (auto id : index->ray(o, d))
            test(mesh[id]);
    } else
        for (const auto &t : mesh)
            test(t);
    return best;
}
std::optional<WorldHit> raycast(Vec3 origin, Vec3 direction, const world::World &world) {
    auto best = raycast(origin, direction, world.collision, &world.collisionIndex);
    if (world.visualIndex.size() == world.visualTriangles.size() && !world.visualTriangles.empty()) {
        auto hit = raycast(origin, direction, world.visualTriangles, &world.visualIndex);
        if (hit && (!best || hit->distance < best->distance))
            best = hit;
        return best;
    }
    for (auto &mesh : world.meshes)
        for (size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
            Triangle t{mesh.vertices[i].position, mesh.vertices[i + 1].position, mesh.vertices[i + 2].position};
            auto hit = raycast(origin, direction, std::span<const Triangle>(&t, 1));
            if (hit && (!best || hit->distance < best->distance))
                best = hit;
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
    // Broad phase: each intermediate box (with overlaps()' 4 cm margins) stays within
    // its half-diagonal of the segment between the two centres. Testing every world
    // triangle at every step took ~0.5 s for a 180 degree flip in Lumen Reach.
    const float w = std::max(a.width, b.width) + .08f, h = std::max(a.height, b.height) + .08f;
    const float r = .5f * std::sqrt(w * w + h * h + .08f * .08f);
    const Vec3 low{std::min(a.center.x, b.center.x) - r, std::min(a.center.y, b.center.y) - r,
                   std::min(a.center.z, b.center.z) - r},
        high{std::max(a.center.x, b.center.x) + r, std::max(a.center.y, b.center.y) + r,
             std::max(a.center.z, b.center.z) + r};
    std::vector<Triangle> near;
    for (const auto &t : mesh)
        if (std::max({t.a.x, t.b.x, t.c.x}) >= low.x && std::min({t.a.x, t.b.x, t.c.x}) <= high.x &&
            std::max({t.a.y, t.b.y, t.c.y}) >= low.y && std::min({t.a.y, t.b.y, t.c.y}) <= high.y &&
            std::max({t.a.z, t.b.z, t.c.z}) >= low.z && std::min({t.a.z, t.b.z, t.c.z}) <= high.z)
            near.push_back(t);
    if (near.empty())
        return true;
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
        if (surfaces::overlaps(s, near))
            return false;
    }
    return true;
}
void Hands::begin(Gesture g, const surfaces::Surface &s, surfaces::Hit h, const sim::Camera &camera) {
    facePlayer = true;
    grabYaw = camera.yaw;
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
    if (g == Gesture::CornerResize)
        reference = s.right() * ((h.u >= .5f ? 1 : -1) * s.width * .5f) +
                    s.up() * ((h.v <= .5f ? 1 : -1) * s.height * .5f) - offset;
}
void Hands::facing(bool enabled, const sim::Camera &camera) {
    // Preserve the material point under the grab as the orientation changes.
    auto x = dot(offset, start.right()), y = dot(offset, start.up());
    auto point = pose.center + pose.right() * x + pose.up() * y;
    if (enabled) {
        pose.rotation = {0, std::sin(-camera.yaw / 2), 0, std::cos(-camera.yaw / 2)};
        pose.yaw = 0;
    }
    offset = pose.right() * x + pose.up() * y;
    pose.center = point - offset;
    start = pose;
    grabYaw = camera.yaw;
    facePlayer = enabled;
}
void Hands::flipFrom(const surfaces::Surface &from, float seconds) {
    // Signed yaw from the target orientation back to the starting one (pure yaw pair).
    auto t = start.rotation, f = from.rotation;
    std::array<float, 4> inverse{-t[0], -t[1], -t[2], t[3]};
    auto relative = quaternionProduct(f, inverse);
    float angle = 2 * std::atan2(relative[1], relative[3]);
    angle = std::remainder(angle, 6.2831853f);
    flip = seconds > 0 ? angle : 0;
    flipRate = seconds > 0 ? std::abs(angle) / seconds : 0;
    if (!flip)
        return;
    std::array<float, 4> turn{0, std::sin(flip / 2), 0, std::cos(flip / 2)};
    auto grab = pose.center + offset;
    pose.rotation = quaternionProduct(turn, start.rotation);
    pose.center = grab - rotate(turn, offset);
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
        if (flip) {
            float step = flipRate * dt;
            flip = std::abs(flip) <= step ? 0 : flip - std::copysign(step, flip);
        }
        float angle = (facePlayer ? (grabYaw - camera.yaw) * .5f : 0) + flip * .5f;
        std::array<float, 4> delta{0, std::sin(angle), 0, std::cos(angle)};
        next.rotation = quaternionProduct(delta, start.rotation);
        next.yaw = 0;
        next.center = camera.position + ray * distance - rotate(delta, offset);
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
            if (gesture == Gesture::CornerResize) {
                float sx = dot(offset, start.right()) >= 0 ? 1 : -1, sy = dot(offset, start.up()) >= 0 ? 1 : -1;
                auto opposite =
                    start.center - start.right() * (sx * start.width * .5f) - start.up() * (sy * start.height * .5f);
                auto drag = camera.position + ray * h->distance + reference;
                next.width =
                    resizeExtent(sx * dot(drag - opposite, start.right()), start.width * 160 / start.pixelWidth, 12.f);
                next.height =
                    resizeExtent(sy * dot(drag - opposite, start.up()), start.height * 100 / start.pixelHeight, 8.f);
                next.center =
                    opposite + start.right() * (sx * next.width * .5f) + start.up() * (sy * next.height * .5f);
            } else {
                // Independent edges, as in the prototype: client aspect follows the actual configure.
                next.width =
                    resizeExtent(2 * std::abs(dot(v, start.right())), start.width * 160 / start.pixelWidth, 12.f);
                next.height =
                    resizeExtent(2 * std::abs(dot(v, start.up())), start.height * 100 / start.pixelHeight, 8.f);
            }
        }
    }
    if (!sweepSafe(pose, next, mesh))
        return false;
    pose = next;
    return true;
}
} // namespace hyprune::interaction
