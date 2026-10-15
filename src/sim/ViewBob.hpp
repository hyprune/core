#pragma once
#include "Math.hpp"
#include <algorithm>
#include <cmath>
#include <string_view>
namespace hyprune::sim {
// Walking view bob (docs/VIEW-BOB.md): one smooth dip per footstep, at a step rate near
// human walking (1.9 Hz at walk speed, 2.5 Hz at run speed), strongly reduced while
// running. "normal" adds a small sideways sway at half the step rate (one per stride).
// The eye never rises above standing height, so it can't poke into low ceilings. The
// envelope eases in and out; with nothing to do it is exactly zero. A small head tilt
// (player.view_bob_roll degrees at a full walking stride, one left-right cycle per stride)
// rides the same envelope, so it fades with the bob.
struct ViewBob {
    enum Level { Off, Subtle, Normal };
    static constexpr float SubtleDip = .010f, NormalDip = .022f, SwayRatio = .3f, Ease = .18f;
    float phase = 0, envelope = 0, sway = 0, roll = 0;
    Vec3 offset{};
    static Level level(std::string_view name) { return name == "normal" ? Normal : name == "subtle" ? Subtle : Off; }
    // speed: measured horizontal speed (m/s); active: grounded walking under player control.
    void update(float dt, float speed, float walkSpeed, float runSpeed, Level level, bool active, float yaw,
                float rollDegrees = 0) {
        walkSpeed = std::max(walkSpeed, .1f);
        float runT = runSpeed > walkSpeed + .01f ? std::clamp((speed - walkSpeed) / (runSpeed - walkSpeed), 0.f, 1.f) : 0.f;
        bool moving = active && level != Off && speed > .3f;
        float dip = level == Normal ? NormalDip : SubtleDip;
        float target = moving ? dip * std::min(1.f, speed / walkSpeed) * (1 - .7f * runT) : 0.f;
        float targetSway = moving && level == Normal ? target * SwayRatio : 0.f;
        float blend = dt > 0 ? 1 - std::exp(-dt / Ease) : 0.f;
        envelope += (target - envelope) * blend;
        sway += (targetSway - sway) * blend;
        if (envelope < 1e-4f && !moving) {
            // Settled: restart from the top of a step next time, so starting never pops.
            envelope = sway = roll = 0;
            phase = 0;
            offset = {};
            return;
        }
        float rate = 1.9f * std::sqrt(std::clamp(speed / walkSpeed, .5f, 1.f)) + .6f * runT;
        phase = std::fmod(phase + rate * dt, 2.f); // steps; two steps per stride
        constexpr float tau = 6.2831853f;
        float down = envelope * (1 - std::cos(tau * phase)) * .5f;
        float side = sway * std::sin(tau * phase * .5f);
        offset = Vec3{std::cos(yaw), 0, std::sin(yaw)} * side + Vec3{0, -down, 0};
        roll = rollDegrees * .01745329f * std::min(1.f, envelope / dip) * std::sin(tau * phase * .5f);
    }
};
} // namespace hyprune::sim
