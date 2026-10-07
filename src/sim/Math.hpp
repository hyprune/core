#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace hyprune {
struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3 operator+(Vec3 b) const { return {x + b.x, y + b.y, z + b.z}; }
    Vec3 operator-(Vec3 b) const { return {x - b.x, y - b.y, z - b.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return *this * (1 / s); }
    Vec3 &operator+=(Vec3 b) { return *this = *this + b; }
};
inline float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 a) {
    return std::sqrt(dot(a, a));
}
inline Vec3 normalized(Vec3 a) {
    float l = length(a);
    return l > 1e-8f ? a / l : Vec3{};
}
inline Vec3 rotate(const std::array<float, 4> &q, Vec3 v) {
    Vec3 u{q[0], q[1], q[2]};
    return v + cross(u, v) * (2 * q[3]) + cross(u, cross(u, v)) * 2;
}
struct Mat4 {
    std::array<float, 16> v{};
    static Mat4 identity() {
        Mat4 m;
        m.v[0] = m.v[5] = m.v[10] = m.v[15] = 1;
        return m;
    }
    Mat4 operator*(const Mat4 &b) const {
        Mat4 c;
        for (int j = 0; j < 4; j++)
            for (int i = 0; i < 4; i++)
                for (int k = 0; k < 4; k++)
                    c.v[j * 4 + i] += v[k * 4 + i] * b.v[j * 4 + k];
        return c;
    }
    Vec3 point(Vec3 p) const {
        return {v[0] * p.x + v[4] * p.y + v[8] * p.z + v[12], v[1] * p.x + v[5] * p.y + v[9] * p.z + v[13],
                v[2] * p.x + v[6] * p.y + v[10] * p.z + v[14]};
    }
};
inline Mat4 perspective(float aspect, float fov = 1.04719755f) {
    Mat4 m;
    float f = 1 / std::tan(fov / 2);
    m.v[0] = f / aspect;
    m.v[5] = f;
    m.v[10] = -1.0002f;
    m.v[11] = -1;
    m.v[14] = -.10001f;
    return m;
}
inline Mat4 lookAt(Vec3 eye, Vec3 forward) {
    Vec3 r = normalized(cross(forward, {0, 1, 0})), u = cross(r, forward);
    auto m = Mat4::identity();
    m.v[0] = r.x;
    m.v[4] = r.y;
    m.v[8] = r.z;
    m.v[1] = u.x;
    m.v[5] = u.y;
    m.v[9] = u.z;
    m.v[2] = -forward.x;
    m.v[6] = -forward.y;
    m.v[10] = -forward.z;
    m.v[12] = -dot(r, eye);
    m.v[13] = -dot(u, eye);
    m.v[14] = dot(forward, eye);
    return m;
}
struct Triangle {
    Vec3 a, b, c;
};
} // namespace hyprune
