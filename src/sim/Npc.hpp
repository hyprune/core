#pragma once
// Ambient NPC agent (RFC-0010 prototype): follows a looping route of points at the walk
// clip's speed, turns in place, idles at each point (now and then an `idle_fidget` when the
// player is near), yields to the player. Pure logic:
// no GL, no Hyprland; the runtime poses the character from `clip`/`clipTime`.
#include "Math.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
namespace hyprune::sim {
struct NpcAgent {
    std::vector<Vec3> route;      // world points on the ground, looping
    float idleSeconds = 3, walkSpeed = 1.3f, turnRate = 2.4f, personalSpace = 1.2f;
    size_t target = 1;
    Vec3 position{};
    float heading = 0;            // radians; forward = (sin h, 0, cos h) (the character's +Z)
    float wait = 0;
    std::string clip = "idle", previous;
    float clipTime = 0, previousTime = 0, fade = 1; // fade: 0 -> 1 over 0.25 s from previous to clip
    bool yielding = false;
    // Occasional fidget while idling at a point with the player within fidgetRange: at most
    // once per fidgetEvery seconds, played through once. fidgetSeconds = 0: the character has none.
    float fidgetSeconds = 0, fidgetEvery = 45, fidgetRange = 4, sinceFidget = 0;
    Vec3 forward() const { return {std::sin(heading), 0, std::cos(heading)}; }
    void start() {
        if (route.empty())
            return;
        position = route[0];
        target = route.size() > 1 ? 1 : 0;
        if (route.size() > 1) {
            Vec3 d = route[1] - route[0];
            heading = std::atan2(d.x, d.z);
        }
    }
    void play(const std::string &name) {
        if (name == clip)
            return;
        previous = clip, previousTime = clipTime;
        clip = name, clipTime = 0, fade = 0;
    }
    // clipSpeed: metres/second the walk clip was authored for (playback = speed / clipSpeed).
    void update(float dt, Vec3 player, float clipSpeed) {
        fade = std::min(1.f, fade + dt / .25f);
        previousTime += dt;
        sinceFidget += dt;
        auto advance = [&](float rate) { clipTime += dt * rate; };
        if (route.size() < 2) {
            play("idle"), advance(1);
            return;
        }
        if (wait > 0) {
            wait -= dt;
            Vec3 near = player - position;
            if (clip == "idle_fidget" && clipTime < fidgetSeconds - .25f) // finish it, then idle
                advance(1);
            else if (fidgetSeconds > 0 && sinceFidget >= fidgetEvery && std::hypot(near.x, near.z) < fidgetRange) {
                play("idle_fidget"), advance(1);
                sinceFidget = 0, wait = std::max(wait, fidgetSeconds);
            } else
                play("idle"), advance(1);
            return;
        }
        Vec3 goal = route[target], d = goal - position;
        d.y = 0;
        float dist = std::hypot(d.x, d.z);
        if (dist < .05f) {
            position = goal;
            target = (target + 1) % route.size();
            wait = idleSeconds;
            play("idle"), advance(1);
            return;
        }
        float diff = std::remainder(std::atan2(d.x, d.z) - heading, 6.2831853f);
        if (std::abs(diff) > .6f) { // turn in place before walking off
            heading += std::copysign(std::min(std::abs(diff), turnRate * dt), diff);
            play("idle"), advance(1);
            return;
        }
        heading += diff * std::min(1.f, dt * 4);
        // Personal space: stop and wait while the player stands in the way.
        Vec3 ahead = position + forward() * .8f - player;
        yielding = std::hypot(ahead.x, ahead.z) < personalSpace && std::abs(player.y - 1.7f - position.y) < 1.5f;
        if (yielding) {
            play("idle"), advance(1);
            return;
        }
        float step = std::min(dist, walkSpeed * dt);
        position = position + forward() * step;
        // Height follows the segment between the previous point and the goal.
        size_t back = (target + route.size() - 1) % route.size();
        Vec3 a = route[back], b = route[target];
        float total = std::hypot(b.x - a.x, b.z - a.z), done = std::hypot(position.x - a.x, position.z - a.z);
        position.y = total > 1e-3f ? a.y + (b.y - a.y) * std::clamp(done / total, 0.f, 1.f) : b.y;
        play("walk"), advance(walkSpeed / std::max(.1f, clipSpeed));
    }
    Mat4 model() const {
        Mat4 m = Mat4::identity();
        float c = std::cos(heading), s = std::sin(heading);
        m.v[0] = c, m.v[2] = -s, m.v[8] = s, m.v[10] = c;
        m.v[12] = position.x, m.v[13] = position.y, m.v[14] = position.z;
        return m;
    }
};
} // namespace hyprune::sim
