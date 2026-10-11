#pragma once
// World 0.8 declarative node animation of object assets (RFC-0004 "world 0.8
// placed world objects"). A target node and its whole subtree move rigidly in
// asset space; the instance model is applied after the animation.
#include "sim/Math.hpp"
#include <cmath>
#include <string>
namespace hyprune::world {
struct Animation {
    enum Type { Spin, Orbit, Bob } type = Spin;
    unsigned node = 0;  // asset node index (resolved from its unique name)
    std::string name;   // the asset node name, for diagnostics
    Vec3 axis{0, 1, 0}, pivot{};
    float period = 1, phase = 0, amplitude = 0;
    Vec3 origin{};      // the target node's origin in asset space (the point an orbit carries)
};
namespace animation {
// Fraction of a cycle at t seconds of the world-animation clock with the
// instance's speed multiplier and phase: t·speed/period + phase + instancePhase,
// reduced to one cycle in double precision so long sessions stay periodic.
inline double cycle(const Animation &a, double t, double speed = 1, double instancePhase = 0) {
    double turns = t * speed / a.period + a.phase + instancePhase;
    return turns - std::floor(turns);
}
// Angle in radians: 2π · cycle.
inline double angle(const Animation &a, double t, double speed = 1, double instancePhase = 0) {
    return cycle(a, t, speed, instancePhase) * 6.283185307179586;
}
// Right-hand rotation about a unit axis (counter-clockwise looking down the
// axis toward the pivot), column-major like Mat4.
inline Mat4 rotation(Vec3 k, double angle) {
    float c = float(std::cos(angle)), s = float(std::sin(angle)), t = 1 - c;
    auto m = Mat4::identity();
    m.v[0] = c + k.x * k.x * t, m.v[4] = k.x * k.y * t - k.z * s, m.v[8] = k.x * k.z * t + k.y * s;
    m.v[1] = k.y * k.x * t + k.z * s, m.v[5] = c + k.y * k.y * t, m.v[9] = k.y * k.z * t - k.x * s;
    m.v[2] = k.z * k.x * t - k.y * s, m.v[6] = k.z * k.y * t + k.x * s, m.v[10] = c + k.z * k.z * t;
    return m;
}
// Asset-space model of the subtree's rest-pose vertices.
//  spin:  rotate about the line (pivot, axis);
//  orbit: carry the target origin around that line, orientation kept;
//  bob:   translate along the axis by amplitude · sin(angle).
inline Mat4 matrixAt(const Animation &a, double cycle);
inline Mat4 matrix(const Animation &a, double t, double speed = 1, double instancePhase = 0) {
    return matrixAt(a, cycle(a, t, speed, instancePhase));
}
// The same pose at a cycle position (fraction of a turn), for clocks kept elsewhere.
inline Mat4 matrixAt(const Animation &a, double cycle) {
    double theta = (cycle - std::floor(cycle)) * 6.283185307179586;
    auto m = Mat4::identity();
    Vec3 offset{};
    if (a.type == Animation::Bob)
        offset = a.axis * float(a.amplitude * std::sin(theta));
    else {
        auto r = rotation(a.axis, theta);
        if (a.type == Animation::Spin) {
            m = r;
            offset = a.pivot - r.point(a.pivot);
        } else
            offset = r.point(a.origin - a.pivot) + a.pivot - a.origin;
    }
    m.v[12] = offset.x, m.v[13] = offset.y, m.v[14] = offset.z;
    return m;
}
// Upper bound of |v' - center| over every pose, for a rest-pose vertex v of the
// animated subtree (swept bounds).
inline float sweptDistance(const Animation &a, Vec3 v, Vec3 center) {
    switch (a.type) {
    case Animation::Spin:
        return length(v - a.pivot) + length(a.pivot - center);
    case Animation::Orbit:
        return length(a.origin - a.pivot) + length(v - a.origin) + length(a.pivot - center);
    case Animation::Bob:
        return length(v - center) + a.amplitude;
    }
    return length(v - center);
}
} // namespace animation
} // namespace hyprune::world
