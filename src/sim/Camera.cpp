#include "Camera.hpp"
#include "interaction/Hands.hpp"
namespace hyprune::sim {
namespace {
Vec3 closestTriangle(Vec3 p, const Triangle &t) {
    const Vec3 ab = t.b - t.a, ac = t.c - t.a, ap = p - t.a;
    float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0)
        return t.a;
    Vec3 bp = p - t.b;
    float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3)
        return t.b;
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0)
        return t.a + ab * (d1 / (d1 - d3));
    Vec3 cp = p - t.c;
    float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6)
        return t.c;
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0)
        return t.a + ac * (d2 / (d2 - d6));
    float va = d3 * d6 - d5 * d4;
    if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0)
        return t.b + (t.c - t.b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    float inv = 1 / (va + vb + vc);
    return t.a + ab * (vb * inv) + ac * (vc * inv);
}
// Squared distance to a convex triangle is convex along the capsule axis.
// Ternary minimization also handles the triangle face / segment interior case.
std::pair<Vec3, Vec3> nearest(Vec3 a, Vec3 b, const Triangle &t) {
    float lo = 0, hi = 1;
    for (int i = 0; i < 24; i++) {
        float u = (2 * lo + hi) / 3, v = (lo + 2 * hi) / 3;
        Vec3 p = a + (b - a) * u, q = a + (b - a) * v;
        if (dot(p - closestTriangle(p, t), p - closestTriangle(p, t)) <
            dot(q - closestTriangle(q, t), q - closestTriangle(q, t)))
            hi = v;
        else
            lo = u;
    }
    Vec3 p = a + (b - a) * ((lo + hi) * .5f);
    Vec3 q = closestTriangle(p, t);
    for (auto endpoint : {a, b}) {
        auto near = closestTriangle(endpoint, t);
        if (length(endpoint - near) < length(p - q)) {
            p = endpoint;
            q = near;
        }
    }
    return {p, q};
}
} // namespace
void resolveCapsule(Camera &c, std::span<const Triangle> mesh) {
    constexpr float r = .3f;
    c.grounded = false;
    for (int iteration = 0; iteration < 4; iteration++)
        for (const auto &t : mesh) {
            Vec3 a = c.position + Vec3{0, -1.7f + r, 0}, b = c.position + Vec3{0, .1f - r, 0};
            Vec3 low{std::min({t.a.x, t.b.x, t.c.x}) - r, std::min({t.a.y, t.b.y, t.c.y}) - r,
                     std::min({t.a.z, t.b.z, t.c.z}) - r};
            Vec3 high{std::max({t.a.x, t.b.x, t.c.x}) + r, std::max({t.a.y, t.b.y, t.c.y}) + r,
                      std::max({t.a.z, t.b.z, t.c.z}) + r};
            if (a.x < low.x || a.x > high.x || a.z < low.z || a.z > high.z || b.y < low.y || a.y > high.y)
                continue;
            auto [p, q] = nearest(a, b, t);
            Vec3 d = p - q;
            float l = length(d);
            if (l > r + .001f)
                continue;
            Vec3 n = l > 1e-6f ? d / l : normalized(cross(t.b - t.a, t.c - t.a));
            c.position += n * std::max(0.f, r - l);
            if (n.y > .65f) {
                c.grounded = true;
                c.verticalSpeed = std::max(0.f, c.verticalSpeed);
            }
            if (n.y < -.65f)
                c.verticalSpeed = std::min(0.f, c.verticalSpeed);
        }
}
void Walk::step(Camera &c, Intent i, float dt, std::span<const Triangle> mesh) const {
    dt = std::clamp(dt, 0.f, .05f);
    Vec3 d{std::sin(c.yaw) * i.forward + std::cos(c.yaw) * i.right, 0,
           -std::cos(c.yaw) * i.forward + std::sin(c.yaw) * i.right};
    if (length(d) > 1)
        d = normalized(d);
    if (i.jump && c.grounded)
        c.verticalSpeed = i.jumpSpeed;
    // At most 8 mm horizontal movement per substep: no thin-wall tunneling at M0 speeds.
    int steps = std::max(1, int(std::ceil(dt / .002f)));
    float h = dt / steps;
    for (int k = 0; k < steps; k++) {
        bool supported = c.grounded && !i.jump;
        c.verticalSpeed -= 9.81f * h;
        auto before = c;
        auto desired = c.position + d * (3.5f * i.speed * h) + Vec3{0, c.verticalSpeed * h, 0};
        c.position = desired;
        resolveCapsule(c, mesh);
        Vec3 lost = desired - c.position;
        lost.y = 0;
        if (supported && i.stepHeight > 0 && length(d) > .001f && length(lost) > .0005f) {
            // Probe the forward footprint for a discrete top, then sweep the entire capsule
            // up and forward. A tall wall or a low ceiling must never become a step.
            auto probe = desired + d * .3f;
            probe.y = before.position.y - 1.7f + i.stepHeight + .01f;
            auto hit = interaction::raycast(probe, {0, -1, 0}, mesh);
            float rise = hit ? hit->point.y - (before.position.y - 1.7f) : 0;
            if (hit && hit->normal.y > .65f && rise > .002f && rise <= i.stepHeight + .001f) {
                bool clear = true;
                auto candidate = before;
                for (float y = .02f; y < rise + .02f; y += .02f) {
                    auto check = before;
                    check.position.y += std::min(y, rise) + .002f;
                    auto expected = check.position;
                    resolveCapsule(check, mesh);
                    if (length(check.position - expected) > .004f) {
                        clear = false;
                        break;
                    }
                }
                candidate.position = desired;
                candidate.position.y = hit->point.y + 1.7f + .002f;
                auto expected = candidate.position;
                resolveCapsule(candidate, mesh);
                if (clear && length(candidate.position - expected) < .004f) {
                    candidate.grounded = true;
                    candidate.verticalSpeed = 0;
                    c = candidate;
                }
            }
        }
        // Follow continuous descending ramps without alternating airborne frames.
        // This bounded support snap does not climb stair risers or cross walls.
        if (supported && !c.grounded && c.verticalSpeed <= 0) {
            auto candidate = c;
            candidate.position.y -= .025f;
            resolveCapsule(candidate, mesh);
            if (candidate.grounded && candidate.position.y <= c.position.y &&
                candidate.position.y > c.position.y - .025f)
                c = candidate;
        }
    }
}
void Fly::step(Camera &c, Intent i, float dt, std::span<const Triangle>) const {
    Vec3 r{std::cos(c.yaw), 0, std::sin(c.yaw)};
    Vec3 d = c.forward() * i.forward + r * i.right + Vec3{0, i.up, 0};
    c.position += normalized(d) * (5 * i.speed * std::clamp(dt, 0.f, .05f));
    c.verticalSpeed = 0;
}
} // namespace hyprune::sim
