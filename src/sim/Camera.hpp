#pragma once
#include "Math.hpp"
#include "TriangleIndex.hpp"
#include <span>
#include <string_view>
namespace hyprune::sim {
struct Camera {
    Vec3 position{0, 1.7f, 4};
    float yaw = 0, pitch = 0, verticalSpeed = 0, fov = 60;
    bool grounded = false;
    float eyeHeight = 1.7f;
    float climbBob = 0; // visual-only, metres; reduced motion sets this to zero
    Vec3 forward() const {
        return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
    }
    // World +Y is up, forward at zero is -Z; heading +yaw turns right.
    // Quaternion angles use right-handed axes, hence -yaw about world +Y.
    std::array<float, 4> orientation() const {
        return quaternionProduct({0, std::sin(-yaw / 2), 0, std::cos(yaw / 2)},
                                 {std::sin(pitch / 2), 0, 0, std::cos(pitch / 2)});
    }
    Vec3 ray(float x, float y, float aspect) const {
        auto f = forward(), r = normalized(cross(f, {0, 1, 0})), u = cross(r, f);
        float tangent = std::tan(fov * .01745329252f / 2);
        return normalized(f + r * ((x * 2 - 1) * tangent * aspect) + u * ((1 - y * 2) * tangent));
    }
    static float lookPitch(float dy, float sensitivity, bool inverted) { return (inverted ? dy : -dy) * sensitivity; }
    struct ScreenPoint {
        float x, y, depth;
        bool visible;
    };
    ScreenPoint project(Vec3 point, float aspect) const {
        auto p = view().point(point);
        float depth = -p.z;
        if (depth <= .01f)
            return {0, 0, depth, false};
        float tangent = std::tan(fov * .01745329252f / 2);
        float x = (1 + p.x / depth / tangent / aspect) / 2, y = (1 - p.y / depth / tangent) / 2;
        return {x, y, depth, x >= 0 && x <= 1 && y >= 0 && y <= 1};
    }
    Mat4 view() const { return lookAt(position + Vec3{0, climbBob, 0}, forward()); }
};
struct Intent {
    float forward = 0, right = 0, up = 0;
    bool jump = false, crouch = false;
    float speed = 1, jumpSpeed = 5, stepHeight = .35f;
};
class MotionStrategy {
  public:
    virtual ~MotionStrategy() = default;
    virtual std::string_view name() const = 0;
    virtual void step(Camera &, Intent, float, std::span<const Triangle>, const TriangleIndex * = nullptr) const = 0;
};
class Walk final : public MotionStrategy {
  public:
    std::string_view name() const override { return "walk"; }
    void step(Camera &, Intent, float, std::span<const Triangle>, const TriangleIndex * = nullptr) const override;
};
class Fly final : public MotionStrategy {
  public:
    std::string_view name() const override { return "fly"; }
    void step(Camera &, Intent, float, std::span<const Triangle>, const TriangleIndex * = nullptr) const override;
};
// Capsule eye height 1.7 m, total height 1.8 m, radius .3 m.
bool setCrouch(Camera &, bool, std::span<const Triangle>, const TriangleIndex * = nullptr);
void resolveCapsule(Camera &, std::span<const Triangle>, const TriangleIndex * = nullptr);
} // namespace hyprune::sim
