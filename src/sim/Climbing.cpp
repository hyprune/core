#include "Climbing.hpp"
#include <stdexcept>

namespace hyprune::sim {
namespace {
Vec3 feet(const Camera &c) {
    return c.position - Vec3{0, c.eyeHeight, 0};
}
// Slab test against the oriented prism. A falling swept foot point catches even
// when it crosses the entire volume in one step; the back face cannot grab.
bool intersects(const Climbable &l, Vec3 a, Vec3 b, float reach = 0) {
    float lo = 0, hi = 1;
    const Vec3 axes[]{l.side(), l.axis(), l.facing};
    const float mins[]{-l.width / 2, 0, -l.depth / 2};
    const float maxs[]{l.width / 2, l.extent(), l.depth / 2 + reach};
    for (int k = 0; k < 3; ++k) {
        float p = dot(a - l.bottom, axes[k]), d = dot(b - a, axes[k]);
        if (std::abs(d) < 1e-7f) {
            if (p < mins[k] - .002f || p > maxs[k] + .002f)
                return false;
        } else {
            float u = (mins[k] - p) / d, v = (maxs[k] - p) / d;
            if (u > v)
                std::swap(u, v);
            lo = std::max(lo, u);
            hi = std::min(hi, v);
            if (lo > hi)
                return false;
        }
    }
    return true;
}
float approach(float value, float target, float amount) {
    return value + std::clamp(target - value, -amount, amount);
}
} // namespace
ClimbMode climbMode(std::string_view name) {
    if (name == "grab")
        return ClimbMode::Grab;
    if (name == "look-to-climb")
        return ClimbMode::Look;
    if (name == "auto-traverse")
        return ClimbMode::Auto;
    throw std::invalid_argument("movement.climb_mode must be grab, look-to-climb or auto-traverse");
}
bool moveCapsule(Camera &c, Vec3 target, std::span<const Triangle> mesh, const TriangleIndex *index) {
    auto delta = target - c.position;
    int steps = std::max(1, int(std::ceil(length(delta) / .008f)));
    for (int k = 0; k < steps; ++k) {
        auto candidate = c;
        auto expected = candidate.position + delta / float(steps);
        candidate.position = expected;
        resolveCapsule(candidate, mesh, index);
        if (length(candidate.position - expected) > .004f)
            return false;
        c.position = candidate.position;
        c.grounded = candidate.grounded;
    }
    return true;
}
void Climbing::reset(Camera &c) {
    *this = {};
    c.climbBob = 0;
    c.verticalSpeed = 0;
}
void Climbing::attach(Camera &c, const Climbable &l) {
    ladderId = l.id;
    kick = {};
    lateral = std::clamp(dot(feet(c) - l.bottom, l.side()), -l.width / 2 + .3f, l.width / 2 - .3f);
    velocity = 0;
    autoDirection = 0;
    exiting = 0;
    entering = false;
    c.verticalSpeed = 0;
    c.grounded = false;
}
void Climbing::detach(Camera &c, float delay) {
    ladderId.clear();
    jumpPending = false;
    cooldown = delay;
    velocity = 0;
    autoDirection = 0;
    exiting = 0;
    entering = false;
    c.verticalSpeed = 0;
}
const Climbable *Climbing::nearby(const Camera &c, std::span<const Climbable> ladders) const {
    const Climbable *best = nullptr;
    float distance = 1e9f;
    for (const auto &l : ladders) {
        bool face = intersects(l, feet(c), feet(c), .2f) && dot(feet(c) - l.bottom, l.facing) >= -.002f;
        if (!face && length(feet(c) - l.bottomExit) > .75f && length(feet(c) - l.topExit) > .75f)
            continue;
        float d = std::abs(dot(feet(c) - l.bottom, l.facing));
        if (d < distance) {
            best = &l;
            distance = d;
        }
    }
    return best;
}
bool Climbing::use(Camera &c, std::span<const Climbable> ladders, ClimbMode requested) {
    if (requested != ClimbMode::Auto)
        return false;
    const Climbable *l = nullptr;
    if (active()) {
        for (const auto &item : ladders)
            if (item.id == ladderId)
                l = &item;
    } else
        l = nearby(c, ladders);
    if (!l)
        return false;
    mode = requested;
    if (!active())
        attach(c, *l);
    entering = !intersects(*l, feet(c), feet(c));
    autoDirection = dot(feet(c) - l->bottom, l->axis()) < l->extent() / 2 ? 1 : -1;
    return true;
}
std::string Climbing::prompt(ClimbMode m) const {
    if (!active())
        return m == ClimbMode::Auto ? "E traverse ladder" : "Move into ladder to climb";
    if (m == ClimbMode::Auto)
        return autoDirection ? "Traversing / Space let go" : "E traverse / Space let go";
    return m == ClimbMode::Look ? "Look up/down + W/S climb / Ctrl slide / Space let go"
                                : "W/S climb / Ctrl slide / Space let go";
}
void Climbing::step(Camera &c, Intent input, float dt, std::span<const Climbable> ladders, ClimbMode requested,
                    bool reduced, std::span<const Triangle> mesh, const TriangleIndex *index) {
    dt = std::clamp(dt, 0.f, .05f);
    if (mode != requested) {
        if (active())
            detach(c);
        mode = requested;
    }
    // Bob is presentation only; it never changes the capsule, reported position
    // or gravity. Smooth to zero on release/detach; reduced motion removes it.
    float bob = active() ? std::sin(phase * 12) * .012f * std::min(1.f, std::abs(velocity)) : 0;
    c.climbBob = reduced ? 0 : c.climbBob + (bob - c.climbBob) * (1 - std::exp(-dt * 14));
    cooldown = std::max(0.f, cooldown - dt);
    const Climbable *l = nullptr;
    for (const auto &item : ladders)
        if (item.id == ladderId)
            l = &item;
    if (active() && !l)
        detach(c);
    if (!active()) {
        Vec3 d{std::sin(c.yaw) * input.forward + std::cos(c.yaw) * input.right, 0,
               -std::cos(c.yaw) * input.forward + std::sin(c.yaw) * input.right};
        if (cooldown == 0 && requested != ClimbMode::Auto) {
            for (const auto &item : ladders)
                if (intersects(item, feet(c), feet(c), .2f) && dot(feet(c) - item.bottom, item.facing) >= -.002f &&
                    dot(d, item.facing) < -.15f) {
                    attach(c, item);
                    l = &item;
                    break;
                }
        }
        if (!active()) {
            // Check fall catch at walking substep frequency, before walking can
            // tunnel across a short catch volume. Never catch a jump going up.
            int steps = std::max(1, int(std::ceil(dt / .002f)));
            for (int n = 0; n < steps; ++n) {
                float h = dt / steps;
                auto before = c;
                Walk{}.step(c, input, h, mesh, index);
                if (length(kick) > .001f)
                    moveCapsule(c, c.position + kick * h, mesh, index);
                kick = kick * std::exp(-h * 5);
                if (cooldown == 0 && before.verticalSpeed < -.1f) {
                    for (const auto &item : ladders)
                        if (intersects(item, feet(before), feet(c))) {
                            attach(c, item);
                            l = &item;
                            break;
                        }
                }
                if (active())
                    break;
            }
            if (!active())
                return;
        }
    }
    if (input.jump || jumpPending) {
        jumpPending = false;
        auto out = normalized(Vec3{l->facing.x, 0, l->facing.z});
        detach(c, .6f);
        kick = out * 3.5f;
        c.verticalSpeed = input.jumpSpeed;
        return;
    }
    if (entering) {
        auto target = (autoDirection > 0 ? l->bottom : l->top) + Vec3{0, c.eyeHeight, 0};
        auto delta = target - c.position;
        float distance = length(delta);
        if (distance < .008f)
            entering = false;
        else if (!moveCapsule(c, c.position + delta * (std::min(distance, l->speed * dt) / distance), mesh, index)) {
            // An obstructed connector cannot tunnel or continue on a stale rail.
            detach(c);
        }
        c.verticalSpeed = 0;
        return;
    }
    float along = std::clamp(dot(feet(c) - l->bottom, l->axis()), 0.f, l->extent());
    float direction = input.forward;
    if (requested == ClimbMode::Look)
        direction *= std::sin(c.pitch);
    if (requested == ClimbMode::Auto)
        direction = autoDirection;
    if (input.crouch && requested != ClimbMode::Auto)
        direction = -2;
    velocity = approach(velocity, direction * l->speed, dt * l->speed * 8);
    // Releasing directional input holds immediately; no inertia-induced slide.
    if (direction == 0)
        velocity = 0;
    if (!exiting) {
        lateral = std::clamp(lateral + (requested == ClimbMode::Auto ? 0 : input.right) * l->speed * dt,
                             -l->width / 2 + .3f, l->width / 2 - .3f);
        float next = std::clamp(along + velocity * dt, 0.f, l->extent());
        auto target = l->bottom + l->axis() * next + l->side() * lateral;
        // Correct the rail offset gradually: catching stops gravity immediately,
        // while alignment remains continuous and capsule checked.
        auto correction = target - feet(c);
        float limit = std::max(l->speed * 2, 3.f) * dt;
        if (length(correction) > limit)
            correction = normalized(correction) * limit;
        if (!moveCapsule(c, c.position + correction, mesh, index))
            velocity = 0;
        phase += length(correction);
        c.verticalSpeed = 0;
        if (length(feet(c) - target) < .025f) {
            if (next >= l->extent() - .002f && direction > 0)
                exiting = 1;
            if (next <= .002f && direction < 0)
                exiting = -1;
        }
    } else {
        auto target = (exiting > 0 ? l->topExit : l->bottomExit) + Vec3{0, c.eyeHeight, 0};
        auto delta = target - c.position;
        float distance = length(delta);
        // Bounded ease-out at exits, including wide-wall lateral recentering.
        float travel = std::min(distance, std::min(l->speed, distance * 6 + .08f) * dt);
        if (distance > .001f && !moveCapsule(c, c.position + delta * (travel / distance), mesh, index)) {
            exiting = 0;
            velocity = 0;
            autoDirection = 0; // blocked exit: hold safely
        } else if (distance < .015f)
            detach(c, .5f);
    }
}
} // namespace hyprune::sim
