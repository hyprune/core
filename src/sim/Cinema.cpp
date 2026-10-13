#include "Cinema.hpp"
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace hyprune::sim::cinema {
float wrap(float a) {
    constexpr float pi = std::numbers::pi_v<float>;
    a = std::fmod(a + pi, 2 * pi);
    if (a <= 0)
        a += 2 * pi;
    return a - pi;
}
float smoothstep(float u) {
    u = std::clamp(u, 0.f, 1.f);
    return u * u * (3 - 2 * u);
}
Pose lookAt(Vec3 eye, Vec3 target, float fov) {
    auto d = target - eye;
    float flat = std::hypot(d.x, d.z);
    if (flat < 1e-6f && std::abs(d.y) < 1e-6f)
        return {eye, 0, 0, fov};
    return {eye, std::atan2(d.x, -d.z), std::atan2(d.y, flat), fov};
}
Pose blend(const Pose &a, const Pose &b, float u) {
    return {a.eye + (b.eye - a.eye) * u, a.yaw + wrap(b.yaw - a.yaw) * u, a.pitch + (b.pitch - a.pitch) * u,
            a.fov + (b.fov - a.fov) * u};
}

Monotone::Monotone(std::vector<double> xs, std::vector<double> ys, std::vector<bool> rest)
    : x(std::move(xs)), y(std::move(ys)) {
    size_t n = x.size();
    if (n != y.size() || n == 0)
        throw std::invalid_argument("monotone cubic needs matching knots");
    m.assign(n, 0);
    if (n == 1)
        return;
    std::vector<double> h(n - 1), d(n - 1);
    for (size_t k = 0; k + 1 < n; ++k) {
        h[k] = x[k + 1] - x[k];
        if (!(h[k] > 0))
            throw std::invalid_argument("monotone cubic needs increasing knots");
        d[k] = (y[k + 1] - y[k]) / h[k];
    }
    auto sign = [](double v) { return (v > 0) - (v < 0); };
    // Interior: weighted harmonic mean of the secants, zero at extrema (Fritsch-Butland).
    for (size_t k = 1; k + 1 < n; ++k)
        if (d[k - 1] * d[k] > 0) {
            double w1 = 2 * h[k] + h[k - 1], w2 = h[k] + 2 * h[k - 1];
            m[k] = (w1 + w2) / (w1 / d[k - 1] + w2 / d[k]);
        }
    // Ends: three-point estimate kept shape-preserving.
    auto end = [&](double h0, double h1, double d0, double d1) {
        if (n == 2)
            return d0;
        double e = ((2 * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
        if (sign(e) != sign(d0))
            return 0.;
        if (sign(d0) != sign(d1) && std::abs(e) > 3 * std::abs(d0))
            return 3 * d0;
        return e;
    };
    m[0] = end(h[0], n > 2 ? h[1] : 0, d[0], n > 2 ? d[1] : 0);
    m[n - 1] = end(h[n - 2], n > 2 ? h[n - 3] : 0, d[n - 2], n > 2 ? d[n - 3] : 0);
    for (size_t k = 0; k < n && k < rest.size(); ++k)
        if (rest[k])
            m[k] = 0;
}
double Monotone::at(double t) const {
    if (x.empty())
        return 0;
    if (t <= x.front())
        return y.front();
    if (t >= x.back())
        return y.back();
    size_t k = std::upper_bound(x.begin(), x.end(), t) - x.begin() - 1;
    double h = x[k + 1] - x[k], s = (t - x[k]) / h, s2 = s * s, s3 = s2 * s;
    return (2 * s3 - 3 * s2 + 1) * y[k] + (s3 - 2 * s2 + s) * h * m[k] + (-2 * s3 + 3 * s2) * y[k + 1] +
           (s3 - s2) * h * m[k + 1];
}

Curve::Curve(std::vector<Vec3> p) : points(std::move(p)) {
    if (points.empty())
        throw std::invalid_argument("curve needs a point");
    knots.assign(points.size(), 0);
    tables.resize(points.size() - 1);
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        auto &table = tables[i];
        table.resize(Samples + 1);
        Vec3 prev = segment(i, 0);
        for (int k = 1; k <= Samples; ++k) {
            Vec3 q = segment(i, float(k) / Samples);
            table[k] = table[k - 1] + hyprune::length(q - prev);
            prev = q;
        }
        knots[i + 1] = knots[i] + table.back();
    }
}
Vec3 Curve::segment(size_t i, float u) const {
    constexpr float eps = 1e-4f;
    Vec3 p1 = points[i], p2 = points[i + 1];
    if (hyprune::length(p2 - p1) < eps)
        return p1;
    // Neighbours: real points unless missing or coincident, then mirrored.
    Vec3 p0 = i > 0 ? points[i - 1] : p1 * 2 - p2, p3 = i + 2 < points.size() ? points[i + 2] : p2 * 2 - p1;
    if (hyprune::length(p1 - p0) < eps)
        p0 = p1 * 2 - p2;
    if (hyprune::length(p3 - p2) < eps)
        p3 = p2 * 2 - p1;
    // Barry-Goldman pyramid with centripetal (alpha 0.5) knot spacing.
    float t0 = 0, t1 = t0 + std::sqrt(hyprune::length(p1 - p0)), t2 = t1 + std::sqrt(hyprune::length(p2 - p1)),
          t3 = t2 + std::sqrt(hyprune::length(p3 - p2));
    float t = t1 + (t2 - t1) * u;
    auto lerp = [](Vec3 a, Vec3 b, float ta, float tb, float t) { return a * ((tb - t) / (tb - ta)) + b * ((t - ta) / (tb - ta)); };
    Vec3 a1 = lerp(p0, p1, t0, t1, t), a2 = lerp(p1, p2, t1, t2, t), a3 = lerp(p2, p3, t2, t3, t);
    Vec3 b1 = lerp(a1, a2, t0, t2, t), b2 = lerp(a2, a3, t1, t3, t);
    return lerp(b1, b2, t1, t2, t);
}
Vec3 Curve::at(float s) const {
    if (points.size() == 1 || s <= 0)
        return points.front();
    if (s >= knots.back())
        return points.back();
    size_t i = std::upper_bound(knots.begin(), knots.end(), s) - knots.begin() - 1;
    auto &table = tables[i];
    float local = s - knots[i];
    size_t k = std::upper_bound(table.begin(), table.end(), local) - table.begin();
    k = std::clamp<size_t>(k, 1, Samples);
    float span = table[k] - table[k - 1];
    float frac = span > 1e-9f ? (local - table[k - 1]) / span : 0;
    return segment(i, (float(k - 1) + frac) / Samples);
}

Track::Track(std::vector<Key> k, float defaultFov) : keys(std::move(k)) {
    if (keys.empty())
        throw std::invalid_argument("a track needs a key");
    size_t n = keys.size();
    std::vector<double> t(n), fovs(n);
    std::vector<bool> rest(n, false);
    std::vector<Vec3> eyes(n);
    bool targets = true;
    float f = defaultFov;
    for (size_t i = 0; i < n; ++i) {
        if (i && !(keys[i].t > keys[i - 1].t))
            throw std::invalid_argument("key times must increase");
        t[i] = keys[i].t;
        eyes[i] = keys[i].eye;
        targets = targets && keys[i].target;
        fovs[i] = f = keys[i].fov.value_or(f);
        auto e = keys[i].ease;
        if (e == Ease::In || e == Ease::InOut)
            rest[i] = true;
        if ((e == Ease::Out || e == Ease::InOut) && i + 1 < n)
            rest[i + 1] = true;
    }
    auto timing = [&](const Curve &c) {
        std::vector<double> s(n);
        for (size_t i = 0; i < n; ++i)
            s[i] = c.knot(i);
        return Monotone(t, s, rest);
    };
    eye.emplace(eyes);
    eyeTiming = timing(*eye);
    if (targets) {
        std::vector<Vec3> points(n);
        for (size_t i = 0; i < n; ++i)
            points[i] = *keys[i].target;
        target.emplace(points);
        targetTiming = timing(*target);
    } else {
        std::vector<double> yaws(n), pitches(n);
        for (size_t i = 0; i < n; ++i) {
            auto p = keys[i].target ? lookAt(keys[i].eye, *keys[i].target, 0) : Pose{keys[i].eye, keys[i].yaw, keys[i].pitch};
            yaws[i] = i ? yaws[i - 1] + wrap(p.yaw - float(yaws[i - 1])) : p.yaw;
            pitches[i] = p.pitch;
        }
        yaw = Monotone(t, yaws, rest);
        pitch = Monotone(t, pitches, rest);
    }
    fov = Monotone(t, fovs, rest);
}
Pose Track::at(double t) const {
    Vec3 e = eye->at(float(eyeTiming.at(t)));
    float f = float(fov.at(t));
    if (target)
        return lookAt(e, target->at(float(targetTiming.at(t))), f);
    return {e, wrap(float(yaw.at(t))), float(pitch.at(t)), f};
}

void Spring::step(float goal, float halfLife, float dt) {
    if (halfLife <= 1e-4f) {
        x = goal;
        v = 0;
        return;
    }
    float y = 2 * std::numbers::ln2_v<float> / halfLife, j0 = x - goal, j1 = v + j0 * y, e = std::exp(-y * dt);
    x = e * (j0 + j1 * dt) + goal;
    v = e * (v - j1 * y * dt);
}
void Spring3::step(Vec3 goal, float halfLife, float dt) {
    Spring sx{x.x, v.x}, sy{x.y, v.y}, sz{x.z, v.z};
    sx.step(goal.x, halfLife, dt);
    sy.step(goal.y, halfLife, dt);
    sz.step(goal.z, halfLife, dt);
    x = {sx.x, sy.x, sz.x};
    v = {sx.v, sy.v, sz.v};
}

std::pair<Vec3, Vec3> Follow::goal(Vec3 q, float h) const {
    Vec3 f{std::sin(h), 0, std::cos(h)}, left{f.z, 0, -f.x};
    return {q - f * p.back + left * p.side + Vec3{0, p.height, 0}, q + f * p.ahead + Vec3{0, p.lookHeight, 0}};
}
Pose Follow::step(const Subject &s, float dt, float fov, const std::function<Vec3(Vec3, Vec3)> &clip) {
    if (!started || hyprune::length(s.position - last) > p.snap) {
        heading = {s.heading, 0};
        auto [e, t] = goal(s.position, s.heading);
        eye = {clip ? clip(t, e) : e, {}};
        target = {t, {}};
        started = true;
    } else {
        heading.step(heading.x + wrap(s.heading - heading.x), p.headingHalfLife, dt);
        auto [e, t] = goal(s.position, heading.x);
        eye.step(clip ? clip(t, e) : e, p.halfLife, dt);
        target.step(t, p.lookHalfLife, dt);
    }
    last = s.position;
    return lookAt(eye.x, target.x, p.fov.value_or(fov));
}

Pose Director::Driver::evaluate(double dt, const Lookup &lookup, float fov, const std::function<Vec3(Vec3, Vec3)> &clip) {
    switch (mode) {
    case Mode::Path:
        time = std::min(time + dt, track->duration());
        return hold = track->at(track->start() + time);
    case Mode::Follow:
        if (auto s = lookup ? lookup(subject) : std::nullopt) {
            lost = false;
            return hold = follow->step(*s, float(dt), fov, clip);
        }
        lost = true;
        return hold;
    default:
        return hold;
    }
}
Pose Director::handOver(double blendSeconds, const Pose &current) {
    Pose from = last && active() ? *last : current;
    // A blend fades out of the driver in charge (still moving) or out of the camera as it
    // is; a new blend during a blend starts from where the camera is.
    if (now.mode == Mode::Idle || blending()) {
        before = {};
        before.mode = Mode::Hold;
        before.hold = from;
    } else
        before = std::move(now);
    now = {};
    blendTime = 0;
    blendLength = std::max(0., blendSeconds);
    return from;
}
void Director::path(Track track, double blendSeconds, const Pose &current) {
    now.hold = handOver(blendSeconds, current);
    now.mode = Mode::Path;
    now.track.emplace(std::move(track));
}
void Director::follow(std::string subject, FollowParams p, double blendSeconds, const Pose &current) {
    now.hold = handOver(blendSeconds, current);
    now.mode = Mode::Follow;
    now.subject = std::move(subject);
    now.follow.emplace(p);
}
std::optional<Pose> Director::tick(double dt, const Lookup &lookup, float fov, const std::function<Vec3(Vec3, Vec3)> &clip) {
    if (!active())
        return std::nullopt;
    Pose p = now.evaluate(dt, lookup, fov, clip);
    if (blending()) {
        blendTime += dt;
        Pose from = before.evaluate(dt, lookup, fov, clip);
        p = blend(from, p, smoothstep(float(blendTime / blendLength)));
        if (!blending())
            before = {};
    }
    last = p;
    return p;
}
} // namespace hyprune::sim::cinema
