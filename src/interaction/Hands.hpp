#pragma once
#include "surfaces/Layout.hpp"
namespace hyprune::interaction {
enum class Gesture { None, Carry, Roll, Resize, CornerResize };
// Seat-independent gesture authority. The adapter owns physical button balancing.
class Hands {
  public:
    Gesture gesture = Gesture::None;
    surfaces::Surface start, pose;
    float distance = 1, targetDistance = 1;
    Vec3 offset{}, reference{};
    float grabYaw = 0;
    bool facePlayer = false;
    void facing(bool, const sim::Camera &);
    void begin(Gesture, const surfaces::Surface &, surfaces::Hit, const sim::Camera & = {});
    bool update(const sim::Camera &, Vec3 ray, float dt, std::span<const Triangle>);
    void wheel(float steps);
    void end() { gesture = Gesture::None; }
};
// Obstacle order cannot replace an already tighter visibility bound. OBB
// recovery runs once after these ray constraints, never before another ray.
inline float limitSpawnDistance(float current, float obstacle) {
    return std::min(current, std::max(.2f, obstacle - .15f));
}
struct WorldHit {
    float distance;
    Vec3 point, normal;
};
std::optional<WorldHit> raycast(Vec3 origin, Vec3 direction, std::span<const Triangle>,
                                const sim::TriangleIndex * = nullptr);
std::optional<WorldHit> raycast(Vec3 origin, Vec3 direction, const world::World &);
bool sweepSafe(const surfaces::Surface &from, const surfaces::Surface &to, std::span<const Triangle>);
std::array<float, 4> orientation(Vec3 normal);
} // namespace hyprune::interaction
