#pragma once
#include "Math.hpp"
#include <span>
#include <string_view>
namespace hyprune::sim {
struct Camera {
    Vec3 position{0, 1.7f, 4};
    float yaw = 0, pitch = 0, verticalSpeed = 0, fov = 60;
    bool grounded = false;
    float eyeHeight = 1.7f;
    Vec3 forward() const {
        return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
    }
    Mat4 view() const { return lookAt(position, forward()); }
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
    virtual void step(Camera &, Intent, float, std::span<const Triangle>) const = 0;
};
class Walk final : public MotionStrategy {
  public:
    std::string_view name() const override { return "walk"; }
    void step(Camera &, Intent, float, std::span<const Triangle>) const override;
};
class Fly final : public MotionStrategy {
  public:
    std::string_view name() const override { return "fly"; }
    void step(Camera &, Intent, float, std::span<const Triangle>) const override;
};
// Capsule eye height 1.7 m, total height 1.8 m, radius .3 m.
bool setCrouch(Camera &, bool, std::span<const Triangle>);
void resolveCapsule(Camera &, std::span<const Triangle>);
} // namespace hyprune::sim
