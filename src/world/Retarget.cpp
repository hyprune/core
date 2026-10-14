#include "Retarget.hpp"
#include <algorithm>
#include <cmath>
#include <set>
namespace hyprune::world {
namespace {
using Quat = std::array<float, 4>;
Mat4 trsMatrix(Vec3 t, const Quat &q, Vec3 s) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    Mat4 m = Mat4::identity();
    m.v[0] = (1 - 2 * (y * y + z * z)) * s.x, m.v[1] = (2 * (x * y + z * w)) * s.x, m.v[2] = (2 * (x * z - y * w)) * s.x;
    m.v[4] = (2 * (x * y - z * w)) * s.y, m.v[5] = (1 - 2 * (x * x + z * z)) * s.y, m.v[6] = (2 * (y * z + x * w)) * s.y;
    m.v[8] = (2 * (x * z + y * w)) * s.z, m.v[9] = (2 * (y * z - x * w)) * s.z, m.v[10] = (1 - 2 * (x * x + y * y)) * s.z;
    m.v[12] = t.x, m.v[13] = t.y, m.v[14] = t.z;
    return m;
}
Mat4 inverse(const Mat4 &m) {
    const auto &v = m.v; // column-major, affine
    float a = v[0], b = v[4], c = v[8], d = v[1], e = v[5], f = v[9], g = v[2], h = v[6], i = v[10];
    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    Mat4 r = Mat4::identity();
    if (std::abs(det) < 1e-12f)
        return r;
    float k = 1 / det;
    r.v[0] = (e * i - f * h) * k, r.v[4] = (c * h - b * i) * k, r.v[8] = (b * f - c * e) * k;
    r.v[1] = (f * g - d * i) * k, r.v[5] = (a * i - c * g) * k, r.v[9] = (c * d - a * f) * k;
    r.v[2] = (d * h - e * g) * k, r.v[6] = (b * g - a * h) * k, r.v[10] = (a * e - b * d) * k;
    for (int row = 0; row < 3; ++row)
        r.v[12 + row] = -(r.v[row] * v[12] + r.v[4 + row] * v[13] + r.v[8 + row] * v[14]);
    return r;
}
Vec3 point(const Mat4 &m, Vec3 p) {
    return {m.v[0] * p.x + m.v[4] * p.y + m.v[8] * p.z + m.v[12], m.v[1] * p.x + m.v[5] * p.y + m.v[9] * p.z + m.v[13],
            m.v[2] * p.x + m.v[6] * p.y + m.v[10] * p.z + m.v[14]};
}
// The rotation of an affine matrix (columns normalised: scale removed).
Quat rotationOf(const Mat4 &m) {
    Vec3 c[3];
    for (int k = 0; k < 3; ++k)
        c[k] = normalized(Vec3{m.v[4 * k], m.v[4 * k + 1], m.v[4 * k + 2]});
    float m00 = c[0].x, m11 = c[1].y, m22 = c[2].z, tr = m00 + m11 + m22;
    Quat q;
    if (tr > 0) {
        float s = std::sqrt(tr + 1) * 2;
        q = {(c[1].z - c[2].y) / s, (c[2].x - c[0].z) / s, (c[0].y - c[1].x) / s, s / 4};
    } else if (m00 > m11 && m00 > m22) {
        float s = std::sqrt(1 + m00 - m11 - m22) * 2;
        q = {s / 4, (c[1].x + c[0].y) / s, (c[2].x + c[0].z) / s, (c[1].z - c[2].y) / s};
    } else if (m11 > m22) {
        float s = std::sqrt(1 + m11 - m00 - m22) * 2;
        q = {(c[1].x + c[0].y) / s, s / 4, (c[2].y + c[1].z) / s, (c[2].x - c[0].z) / s};
    } else {
        float s = std::sqrt(1 + m22 - m00 - m11) * 2;
        q = {(c[2].x + c[0].z) / s, (c[2].y + c[1].z) / s, s / 4, (c[0].y - c[1].x) / s};
    }
    float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (auto &v : q)
        v /= l;
    return q;
}
Quat unit(Quat q) {
    float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (l < 1e-12f)
        return {0, 0, 0, 1};
    for (auto &v : q)
        v /= l;
    return q;
}
// Rest-pose world matrices (armature included), joints in load order (parents first).
std::vector<Mat4> restWorld(const Character &c) {
    std::vector<Mat4> w(c.joints.size());
    for (size_t i = 0; i < c.joints.size(); ++i) {
        auto &j = c.joints[i];
        auto local = trsMatrix(j.translation, j.rotation, j.scale);
        w[i] = j.parent < 0 ? c.armature * local : w[j.parent] * local;
    }
    return w;
}
bool legOrRoot(const std::string &key) {
    return key == "root" || key.find("foot") != std::string::npos || key.find("toe") != std::string::npos ||
           key.find("leg") != std::string::npos;
}
} // namespace
std::string canonicalBone(std::string name) {
    if (auto colon = name.rfind(':'); colon != std::string::npos)
        name = name.substr(colon + 1);
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    return name;
}
float hipHeight(const Character &c) {
    int hips = c.role("hips");
    if (hips < 0)
        return 0;
    auto w = restWorld(c);
    float low = w[hips].v[13];
    for (size_t i = 0; i < c.joints.size(); ++i)
        if (legOrRoot(canonicalBone(c.joints[i].name)))
            low = std::min(low, w[i].v[13]);
    return w[hips].v[13] - low;
}
Character::Clip retargetClip(const Character &target, const Character &source, const Character::Clip &clip,
                             const std::map<std::string, std::string> &aliases, RetargetInfo *info) {
    // Source joint for each target joint: by canonical key, or the alias naming the target joint.
    std::map<std::string, int> sourceByKey;
    for (size_t j = 0; j < source.joints.size(); ++j)
        sourceByKey.emplace(canonicalBone(source.joints[j].name), int(j));
    std::map<std::string, std::string> keyOfTarget; // target joint name -> canonical key (aliases first)
    for (auto &[key, name] : aliases)
        keyOfTarget[name] = canonicalBone(key);
    std::vector<int> from(target.joints.size(), -1);
    std::vector<int> to(source.joints.size(), -1);
    RetargetInfo local;
    for (size_t i = 0; i < target.joints.size(); ++i) {
        auto &name = target.joints[i].name;
        auto key = keyOfTarget.contains(name) ? keyOfTarget[name] : canonicalBone(name);
        if (auto it = sourceByKey.find(key); it != sourceByKey.end() && to[it->second] < 0) {
            from[i] = it->second;
            to[it->second] = int(i);
            ++local.matched;
        } else
            ++local.unmatched;
    }
    auto ws = restWorld(source), wt = restWorld(target);
    float hs = hipHeight(source), ht = hipHeight(target);
    local.scale = hs > 1e-4f && ht > 1e-4f ? ht / hs : 1;
    auto rot = [](const std::vector<Mat4> &w, const Character &c, int i) { return i < 0 ? rotationOf(c.armature) : rotationOf(w[i]); };
    auto parentWorld = [](const std::vector<Mat4> &w, const Character &c, int i) {
        return c.joints[i].parent < 0 ? c.armature : w[c.joints[i].parent];
    };
    int sourceHips = source.role("hips");
    Character::Clip out;
    out.name = clip.name;
    out.duration = clip.duration;
    out.speed = clip.speed * local.scale;
    std::set<std::string> missing;
    for (auto &ch : clip.channels) {
        if (ch.joint < 0 || ch.joint >= int(source.joints.size()))
            continue;
        int t = to[ch.joint];
        if (t < 0) {
            if (ch.path == 1)
                missing.insert(canonicalBone(source.joints[ch.joint].name));
            continue;
        }
        Character::Channel o;
        o.joint = t;
        o.path = ch.path;
        o.step = ch.step;
        o.times = ch.times;
        if (ch.path == 1) {
            // Target local = inv(Rt parent) * Rs parent * source local * inv(Rs bone) * Rt bone,
            // all at rest (world space, armature included): the source's change from rest, in world
            // space, applied to the target's rest, with each parent assumed retargeted the same way.
            Quat pre = quaternionProduct(conjugate(rot(wt, target, target.joints[t].parent)),
                                         rot(ws, source, source.joints[ch.joint].parent));
            Quat post = quaternionProduct(conjugate(rot(ws, source, ch.joint)), rot(wt, target, t));
            o.values.resize(ch.values.size());
            for (size_t k = 0; k + 3 < ch.values.size(); k += 4) {
                Quat q{ch.values[k], ch.values[k + 1], ch.values[k + 2], ch.values[k + 3]};
                auto r = unit(quaternionProduct(quaternionProduct(pre, q), post));
                std::copy(r.begin(), r.end(), o.values.begin() + k);
            }
        } else if (ch.path == 0 && ch.joint == sourceHips) {
            // The hips path in world space, its change from rest scaled by the hip height ratio,
            // back into the target's hips parent space from the target's rest hips position.
            auto sParent = parentWorld(ws, source, ch.joint), tParent = parentWorld(wt, target, t);
            auto tInverse = inverse(tParent);
            Vec3 sRest = point(sParent, source.joints[ch.joint].translation);
            Vec3 tRest = point(tParent, target.joints[t].translation);
            o.values.resize(ch.values.size());
            for (size_t k = 0; k + 2 < ch.values.size(); k += 3) {
                Vec3 p = point(sParent, {ch.values[k], ch.values[k + 1], ch.values[k + 2]});
                Vec3 q = point(tInverse, tRest + (p - sRest) * local.scale);
                o.values[k] = q.x, o.values[k + 1] = q.y, o.values[k + 2] = q.z;
            }
        } else
            continue; // other translations and every scale keep the target's rest (its bone lengths)
        out.channels.push_back(std::move(o));
    }
    local.missing.assign(missing.begin(), missing.end());
    if (info)
        *info = local;
    return out;
}
} // namespace hyprune::world
