#pragma once
#include "Camera.hpp"
#include <string>
#include <vector>

namespace hyprune::sim {
// World-space feet rail. The authored prism surrounds this segment; facing
// points out of the climb face. Visual rungs sit behind the capsule rail.
struct Climbable {
    std::string id, type;
    Vec3 bottom, top, facing, bottomExit, topExit;
    float width = 1, depth = .8f, speed = 2;
    Vec3 axis() const { return normalized(top - bottom); }
    Vec3 side() const { return normalized(cross(axis(), facing)); }
    float extent() const { return length(top - bottom); }
};
enum class ClimbMode { Grab, Look, Auto };
ClimbMode climbMode(std::string_view);
// One controller per player; never retains pointers into a replaceable world.
class Climbing {
  public:
    std::string ladderId;
    bool active() const { return !ladderId.empty(); }
    void reset(Camera &);
    void jump() {
        if (active())
            jumpPending = true;
    }
    const Climbable *nearby(const Camera &, std::span<const Climbable>) const;
    bool use(Camera &, std::span<const Climbable>, ClimbMode);
    void step(Camera &, Intent, float, std::span<const Climbable>, ClimbMode, bool reducedMotion,
              std::span<const Triangle>, const TriangleIndex * = nullptr);
    std::string prompt(ClimbMode) const;

  private:
    float cooldown = 0, velocity = 0, lateral = 0, phase = 0, autoDirection = 0;
    Vec3 kick{};
    int exiting = 0;
    bool entering = false, jumpPending = false;
    ClimbMode mode = ClimbMode::Grab;
    void attach(Camera &, const Climbable &);
    void detach(Camera &, float delay = .4f);
};
// Swept capsule translation, including intermediate clearance. Returns false
// at obstruction, retaining the last safe substep (never pushes through a wall).
bool moveCapsule(Camera &, Vec3, std::span<const Triangle>, const TriangleIndex * = nullptr);
} // namespace hyprune::sim
