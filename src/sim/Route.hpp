#pragma once
// Path geometry (RFC-0011 "Paths"): the curve tool 3 draws and what follows it, so a drawn
// path and a character walking it are the same line. A segment from a to b is straight for
// `line`; `curve` is a cubic Hermite with start tangent (b - previous point) / 2 and end
// tangent b - a; `arc` is the straight line plus a horizontal offset of 0.3·|b - a|·sin(πt)
// towards (b - a) × +Y. Followers move by arc length over 24 samples per segment.
#include "Math.hpp"
#include <string>
#include <vector>
namespace hyprune::sim {
enum class Segment { Line, Curve, Arc };
inline Segment segmentType(const std::string &type) {
    return type == "curve" ? Segment::Curve : type == "arc" ? Segment::Arc : Segment::Line;
}
inline constexpr int samplesPerSegment = 24;
// Point t (0..1) of the segment a -> b; before is the point preceding a (a itself at the start).
inline Vec3 segmentPoint(Vec3 before, Vec3 a, Vec3 b, Segment type, float t) {
    Vec3 next = a + (b - a) * t;
    if (type == Segment::Curve) {
        auto tangent = (b - before) * .5f;
        next = a * (2 * t * t * t - 3 * t * t + 1) + tangent * (t * t * t - 2 * t * t + t) + b * (-2 * t * t * t + 3 * t * t) +
               (b - a) * (t * t * t - t * t);
    }
    if (type == Segment::Arc)
        next += normalized(cross(b - a, {0, 1, 0})) * std::sin(t * 3.141593f) * length(b - a) * .3f;
    return next;
}
struct Route {
    std::vector<Vec3> samples;   // the polyline, first point included
    std::vector<float> distance; // cumulative arc length per sample
    std::vector<float> points;   // arc length of each authored point (closed: the closing point too)
    bool closed = false;
    float length() const { return distance.empty() ? 0 : distance.back(); }
    bool empty() const { return samples.size() < 2 || length() < 1e-4f; }
    // Arc length s: wrapped on a closed route, clamped on an open one.
    float wrap(float s) const {
        float l = length();
        if (l <= 0)
            return 0;
        if (closed)
            return std::fmod(std::fmod(s, l) + l, l);
        return std::clamp(s, 0.f, l);
    }
    size_t index(float s) const {
        auto it = std::upper_bound(distance.begin(), distance.end(), s);
        size_t i = it == distance.begin() ? 0 : size_t(it - distance.begin()) - 1;
        return std::min(i, samples.size() - 2);
    }
    Vec3 at(float s) const {
        if (samples.empty())
            return {};
        if (samples.size() == 1)
            return samples[0];
        s = wrap(s);
        size_t i = index(s);
        float span = distance[i + 1] - distance[i];
        float u = span > 1e-6f ? (s - distance[i]) / span : 0;
        return samples[i] + (samples[i + 1] - samples[i]) * std::clamp(u, 0.f, 1.f);
    }
    // Unit direction of increasing arc length (horizontal when flat is set).
    Vec3 tangent(float s, bool flat = true) const {
        if (samples.size() < 2)
            return {0, 0, 1};
        s = wrap(s);
        size_t i = index(s);
        Vec3 d = samples[i + 1] - samples[i];
        if (flat)
            d.y = 0;
        auto n = normalized(d);
        return hyprune::length(n) > .5f ? n : Vec3{0, 0, 1};
    }
    // Arc length of the sample nearest to p (horizontal distance), for joining a route.
    float nearest(Vec3 p) const {
        float best = 1e30f, at = 0;
        for (size_t i = 0; i + 1 < samples.size(); ++i) {
            Vec3 a = samples[i], d = samples[i + 1] - a;
            d.y = 0;
            float dd = dot(d, d), u = dd > 1e-9f ? std::clamp(dot(Vec3{p.x - a.x, 0, p.z - a.z}, d) / dd, 0.f, 1.f) : 0;
            Vec3 q = a + d * u;
            float e = std::hypot(p.x - q.x, p.z - q.z);
            if (e < best)
                best = e, at = distance[i] + (distance[i + 1] - distance[i]) * u;
        }
        return at;
    }
    static Route build(const std::vector<Vec3> &positions, const std::vector<Segment> &types, bool closed) {
        Route r;
        r.closed = closed && positions.size() > 2;
        if (positions.empty())
            return r;
        r.samples.push_back(positions[0]);
        r.distance.push_back(0);
        r.points.push_back(0);
        size_t n = positions.size(), count = n + (r.closed ? 1 : 0);
        for (size_t i = 1; i < count; ++i) {
            Vec3 a = positions[(i - 1) % n], b = positions[i % n], before = positions[(i > 1 ? i - 2 : 0) % n];
            Segment type = i % n < types.size() ? types[i % n] : Segment::Line;
            for (int k = 1; k <= samplesPerSegment; ++k) {
                Vec3 next = segmentPoint(before, a, b, type, k / float(samplesPerSegment));
                r.distance.push_back(r.distance.back() + hyprune::length(next - r.samples.back()));
                r.samples.push_back(next);
            }
            r.points.push_back(r.distance.back());
        }
        return r;
    }
};
} // namespace hyprune::sim
