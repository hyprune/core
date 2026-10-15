#pragma once
#include "Math.hpp"
#include "TriangleIndex.hpp"
#include <span>
#include <string_view>
namespace hyprune::sim {
struct Camera {
    Vec3 position{0, 1.7f, 4};
    float yaw = 0, pitch = 0, roll = 0, verticalSpeed = 0, fov = 60;
    float shiftX = 0, shiftY = 0; // lens centre offset in NDC, +right/+up
    bool grounded = false;
    float eyeHeight = 1.7f;
    float climbBob = 0; // visual-only, metres; reduced motion sets this to zero
    Vec3 viewBob{};     // visual-only walking bob offset, metres; zero with reduced motion
    float viewRoll = 0; // visual-only head tilt of the walking bob, radians (the view only, not aim)
    Vec3 forward() const {
        return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
    }
    // World +Y is up, forward at zero is -Z; heading +yaw turns right.
    // Quaternion angles use right-handed axes, hence -yaw about world +Y.
    std::array<float, 4> orientation() const {
        return quaternionProduct(quaternionProduct({0, std::sin(-yaw / 2), 0, std::cos(yaw / 2)},
                                 {std::sin(pitch / 2), 0, 0, std::cos(pitch / 2)}),
                                 {0, 0, std::sin(roll / 2), std::cos(roll / 2)});
    }
    Vec3 ray(float x, float y, float aspect) const {
        auto f = forward(), r = right(), u = up();
        float tangent = std::tan(fov * .01745329252f / 2);
        return normalized(f + r * ((x * 2 - 1 - shiftX) * tangent * aspect) + u * ((1 - y * 2 - shiftY) * tangent));
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
        float x = (1 + shiftX + p.x / depth / tangent / aspect) / 2, y = (1 - shiftY - p.y / depth / tangent) / 2;
        return {x, y, depth, x >= 0 && x <= 1 && y >= 0 && y <= 1};
    }
    Vec3 right() const { return rotate(orientation(), {1, 0, 0}); }
    Vec3 up() const { return rotate(orientation(), {0, 1, 0}); }
    Mat4 projection(float aspect) const {
        auto m = perspective(aspect, fov * .01745329252f);
        m.v[8] = -shiftX; m.v[9] = -shiftY;
        return m;
    }
    Mat4 view() const {
        auto tilted = *this;
        tilted.roll += viewRoll;
        auto eye = position + Vec3{0, climbBob, 0} + viewBob, r = tilted.right(), u = tilted.up(), f = forward();
        auto m = Mat4::identity();
        m.v[0]=r.x; m.v[4]=r.y; m.v[8]=r.z; m.v[12]=-dot(r,eye);
        m.v[1]=u.x; m.v[5]=u.y; m.v[9]=u.z; m.v[13]=-dot(u,eye);
        m.v[2]=-f.x; m.v[6]=-f.y; m.v[10]=-f.z; m.v[14]=dot(f,eye);
        return m;
    }
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
