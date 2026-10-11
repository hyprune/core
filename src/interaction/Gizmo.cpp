#include "Gizmo.hpp"
#include <cstdio>
#include <cstdlib>
namespace hyprune::interaction {
namespace {
constexpr float pi = 3.14159265358979f;
// Handle proportions, in axis lengths.
constexpr float shaftStart = .14f, coneBase = .80f, coneRadius = .07f;
constexpr float planeInner = .24f, planeOuter = .44f;
constexpr float scaleCube = .92f, scaleHalf = .075f, universalCube = .58f, universalHalf = .06f, uniformHalf = .085f;
constexpr float rotateRing = .92f, universalRing = 1.28f, viewRing = 1.18f;
constexpr float centreDiscPx = 9, pickPx = 8;
constexpr float near = .08f;
float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }
float component(Vec3 v, int i) { return i == 0 ? v.x : i == 1 ? v.y : v.z; }
Vec3 axisVector(int i) { return i == 0 ? Vec3{1, 0, 0} : i == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1}; }
std::optional<float> closestParam(Vec3 o, Vec3 ray, Vec3 c, Vec3 axis) {
    auto w = c - o;
    float b = dot(axis, ray), denom = 1 - b * b;
    if (denom < 1e-4f)
        return {};
    return (b * dot(ray, w) - dot(axis, w)) / denom;
}
std::optional<Vec3> rayPlane(Vec3 o, Vec3 ray, Vec3 c, Vec3 n) {
    float d = dot(ray, n);
    if (std::abs(d) < 1e-4f)
        return {};
    float s = dot(c - o, n) / d;
    if (s <= 0 || s > 2000)
        return {};
    return o + ray * s;
}
float signedAngle(Vec3 a, Vec3 b, Vec3 n) { return std::atan2(dot(cross(a, b), n), dot(a, b)); }
std::array<float, 4> normalize(std::array<float, 4> q) {
    float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (l < 1e-8f)
        return {0, 0, 0, 1};
    for (auto &v : q)
        v /= l;
    return q;
}
Rgba scaled(Rgba c, float k) { return {c.r * k, c.g * k, c.b * k, c.a}; }
Rgba mixWhite(Rgba c, float t) { return {c.r + (1 - c.r) * t, c.g + (1 - c.g) * t, c.b + (1 - c.b) * t, c.a}; }
Rgba withAlpha(Rgba c, float a) { return {c.r, c.g, c.b, c.a * a}; }
float cross2(GizmoPoint o, GizmoPoint a, GizmoPoint b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }
std::vector<GizmoPoint> hull(std::vector<GizmoPoint> p) {
    if (p.size() < 3)
        return p;
    std::sort(p.begin(), p.end(), [](auto &a, auto &b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    std::vector<GizmoPoint> h(2 * p.size());
    size_t k = 0;
    for (size_t i = 0; i < p.size(); ++i) {
        while (k >= 2 && cross2(h[k - 2], h[k - 1], p[i]) <= 0)
            --k;
        h[k++] = p[i];
    }
    for (size_t i = p.size() - 1, t = k + 1; i > 0; --i) {
        while (k >= t && cross2(h[k - 2], h[k - 1], p[i - 1]) <= 0)
            --k;
        h[k++] = p[i - 1];
    }
    h.resize(k - 1);
    return h;
}
float segmentDistance(float x, float y, GizmoPoint a, GizmoPoint b) {
    float dx = b.x - a.x, dy = b.y - a.y, l = dx * dx + dy * dy;
    float t = l > 1e-6f ? std::clamp(((x - a.x) * dx + (y - a.y) * dy) / l, 0.f, 1.f) : 0;
    return std::hypot(x - a.x - dx * t, y - a.y - dy * t);
}
bool insideConvex(float x, float y, const std::vector<GizmoPoint> &p) {
    if (p.size() < 3)
        return false;
    int sign = 0;
    for (size_t i = 0; i < p.size(); ++i) {
        auto &a = p[i], &b = p[(i + 1) % p.size()];
        float c = (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
        if (std::abs(c) < 1e-6f)
            continue;
        int s = c > 0 ? 1 : -1;
        if (sign && s != sign)
            return false;
        sign = s;
    }
    return true;
}
// Two perpendicular unit vectors spanning the plane normal to n.
std::pair<Vec3, Vec3> basis(Vec3 n) {
    Vec3 u = std::abs(n.y) < .9f ? normalized(cross(n, {0, 1, 0})) : normalized(cross(n, {1, 0, 0}));
    return {u, cross(n, u)};
}
} // namespace

Rgba palette::axis(int i) { return i == 0 ? coral : i == 1 ? mint : i == 2 ? sky : neutral; }
float snapValue(float value, float step) { return std::round(value / step) * step; }
std::array<float, 4> axisAngle(Vec3 axis, float radians) {
    auto u = normalized(axis) * std::sin(radians / 2);
    return {u.x, u.y, u.z, std::cos(radians / 2)};
}
const char *partName(Part p) {
    static const char *names[] = {"move-x",  "move-y",   "move-z",   "plane-yz", "plane-zx", "plane-xy",
                                  "move-view", "rotate-x", "rotate-y", "rotate-z", "rotate-view", "rotate-free",
                                  "scale-x", "scale-y",  "scale-z",  "scale-yz", "scale-zx", "scale-xy",
                                  "scale-uniform"};
    return p == Part::None || p == Part::Count ? "none" : names[int(p)];
}
int partAxis(Part p) {
    switch (p) {
    case Part::MoveX: case Part::PlaneX: case Part::RotateX: case Part::ScaleX: case Part::ScalePlaneX: return 0;
    case Part::MoveY: case Part::PlaneY: case Part::RotateY: case Part::ScaleY: case Part::ScalePlaneY: return 1;
    case Part::MoveZ: case Part::PlaneZ: case Part::RotateZ: case Part::ScaleZ: case Part::ScalePlaneZ: return 2;
    default: return -1;
    }
}
bool isMove(Part p) { return p >= Part::MoveX && p <= Part::MoveView; }
bool isRotate(Part p) { return p >= Part::RotateX && p <= Part::RotateFree; }
bool isScale(Part p) { return p >= Part::ScaleX && p <= Part::ScaleUniform; }
static bool isPlane(Part p) { return (p >= Part::PlaneX && p <= Part::PlaneZ) || (p >= Part::ScalePlaneX && p <= Part::ScalePlaneZ); }
static bool isAxis(Part p) { return (p >= Part::MoveX && p <= Part::MoveZ) || (p >= Part::ScaleX && p <= Part::ScaleZ); }
static bool isRing(Part p) { return p >= Part::RotateX && p <= Part::RotateZ; }

std::optional<GizmoPoint> GizmoView::project(Vec3 p) const {
    auto s = camera.project(p, aspect());
    if (s.depth <= near)
        return {};
    return GizmoPoint{s.x * width, s.y * height, s.depth};
}
Vec3 GizmoView::ray(float x, float y) const { return camera.ray(x / width, y / height, aspect()); }

float Gizmo::ringFactor() const { return options.mode == GizmoMode::Universal ? universalRing : rotateRing; }
float Gizmo::ringRadius() const { return layout_.size * ringFactor(); }
bool Gizmo::offered(Part p) const {
    auto m = options.mode;
    bool move = m == GizmoMode::Move || m == GizmoMode::Universal, rotate = m == GizmoMode::Rotate || m == GizmoMode::Universal,
         scale = m == GizmoMode::Scale || m == GizmoMode::Universal;
    int i = partAxis(p);
    if (isMove(p))
        return move && (m == GizmoMode::Move || p != Part::MoveView);
    if (isRotate(p)) {
        if (!rotate)
            return false;
        if (options.yawOnly)
            return p == Part::RotateY;
        return m == GizmoMode::Rotate || isRing(p);
    }
    if (!scale)
        return false;
    if (p == Part::ScaleUniform)
        return true;
    if (options.uniformScaleOnly)
        return false;
    if (p >= Part::ScaleX && p <= Part::ScaleZ)
        return options.scaleAxes[i];
    // Plane scale: both in-plane axes scale; scale mode only (universal stays uncluttered).
    return m == GizmoMode::Scale && options.scaleAxes[(i + 1) % 3] && options.scaleAxes[(i + 2) % 3];
}
float Gizmo::alpha(Part p) const { return p == Part::None || p == Part::Count ? 0 : alpha_[int(p)]; }

void Gizmo::layout(const GizmoView &view, Vec3 centre, const std::array<float, 4> &rotation) {
    view_ = view;
    auto previous = layout_.sign;
    Layout l;
    l.centre = centre;
    alpha_.fill(0);
    shapes_.clear();
    auto p = view.project(centre);
    visible_ = p && p->depth > .15f;
    if (!visible_) {
        layout_ = l;
        return;
    }
    l.centrePx = *p;
    l.unitPx = view.height / (2 * p->depth * std::tan(view.camera.fov * .01745329252f / 2));
    l.size = options.pixels * view.dpi / l.unitPx;
    bool local = options.local && !options.yawOnly;
    for (int i = 0; i < 3; ++i)
        l.axes[i] = local ? normalized(rotate(rotation, axisVector(i))) : axisVector(i);
    l.toCamera = normalized(view.camera.position - centre);
    for (int i = 0; i < 3; ++i) {
        float c = std::abs(dot(l.axes[i], l.toCamera)), r = std::sqrt(std::max(0.f, 1 - c * c));
        // An axis seen within ~9 degrees of end-on is disabled, never a nub; it fades in by ~20.
        l.axisFade[i] = clamp01((r - .16f) / .18f);
        l.planeFade[i] = clamp01((c - .14f) / .18f);
        float d = dot(l.axes[i], l.toCamera);
        if (dragging())
            l.sign[i] = previous[i];
        else if (!flipsValid_)
            l.sign[i] = d >= 0 ? 1 : -1;
        else
            l.sign[i] = d * previous[i] < -.2f ? -previous[i] : previous[i];
    }
    flipsValid_ = true;
    layout_ = l;
    for (int k = 0; k < int(Part::Count); ++k) {
        auto part = Part(k);
        if (!offered(part))
            continue;
        int i = partAxis(part);
        alpha_[k] = isAxis(part) ? l.axisFade[i] : isPlane(part) ? l.planeFade[i] : 1;
    }
    buildShapes();
}

void Gizmo::buildShapes() {
    auto &l = layout_;
    auto px = [&](Vec3 p) { return view_.project(p); };
    auto c = l.centre;
    bool universal = options.mode == GizmoMode::Universal;
    for (int k = 0; k < int(Part::Count); ++k) {
        auto part = Part(k);
        if (alpha_[k] <= 0)
            continue;
        Shape s;
        s.part = part;
        int i = partAxis(part);
        if (part == Part::MoveView || part == Part::ScaleUniform) {
            s.priority = 0;
            s.circle = l.centrePx;
            s.radius = part == Part::MoveView ? centreDiscPx * view_.dpi : std::max(centreDiscPx * view_.dpi, uniformHalf * 1.5f * l.size * l.unitPx);
        } else if (isAxis(part)) {
            auto dir = l.axes[i] * l.sign[i];
            bool scale = isScale(part);
            float end = scale ? (universal ? universalCube : scaleCube) : 1.f;
            auto a = px(c + dir * (l.size * shaftStart)), b = px(c + dir * (l.size * end));
            if (!a || !b)
                continue;
            s.priority = 2;
            if (!(scale && universal))
                s.segments.push_back({*a, *b});
            // The tip (cone or cube) hit area.
            std::vector<GizmoPoint> tip;
            if (scale) {
                float h = (universal ? universalHalf : scaleHalf) * l.size;
                auto q = c + dir * (l.size * end);
                for (int n = 0; n < 8; ++n)
                    if (auto p = px(q + l.axes[0] * (n & 1 ? h : -h) + l.axes[1] * (n & 2 ? h : -h) + l.axes[2] * (n & 4 ? h : -h)))
                        tip.push_back(*p);
                if (universal)
                    s.priority = 1; // sits on the move arrow's shaft
            } else {
                auto [u, w] = basis(dir);
                auto base = c + dir * (l.size * coneBase);
                if (auto p = px(c + dir * l.size))
                    tip.push_back(*p);
                for (int n = 0; n < 12; ++n) {
                    float t = n * 2 * pi / 12;
                    if (auto p = px(base + (u * std::cos(t) + w * std::sin(t)) * (coneRadius * l.size)))
                        tip.push_back(*p);
                }
            }
            s.polygon = hull(tip);
        } else if (isPlane(part)) {
            auto u = l.axes[(i + 1) % 3] * l.sign[(i + 1) % 3], w = l.axes[(i + 2) % 3] * l.sign[(i + 2) % 3];
            float a = planeInner * l.size, b = planeOuter * l.size;
            for (auto [x, y] : {std::pair{a, a}, {b, a}, {b, b}, {a, b}})
                if (auto p = px(c + u * x + w * y))
                    s.polygon.push_back(*p);
            if (s.polygon.size() != 4)
                continue;
            s.priority = 1;
        } else if (isRing(part) || part == Part::RotateView) {
            Vec3 u, w;
            float r = ringRadius();
            if (part == Part::RotateView) {
                u = view_.camera.right(), w = view_.camera.up();
                r = l.size * viewRing * (universal ? universalRing / rotateRing : 1);
            } else {
                u = l.axes[(i + 1) % 3], w = l.axes[(i + 2) % 3];
            }
            bool faceOn = part == Part::RotateView || std::abs(dot(l.axes[i], l.toCamera)) > .97f;
            constexpr int count = 72;
            std::optional<GizmoPoint> prev;
            float prevAngle = 0;
            bool prevFront = false;
            for (int n = 0; n <= count; ++n) {
                float t = n * 2 * pi / count;
                auto dir = u * std::cos(t) + w * std::sin(t);
                auto p = px(c + dir * r);
                bool front = faceOn || dot(dir, l.toCamera) >= -.02f;
                if (p && prev && front && prevFront) {
                    s.segments.push_back({*prev, *p});
                    s.segmentAngles.push_back(prevAngle);
                }
                prev = p;
                prevAngle = t;
                prevFront = front;
            }
            s.priority = 2;
        } else if (part == Part::RotateFree) {
            s.priority = 4;
            s.circle = l.centrePx;
            s.radius = ringRadius() * l.unitPx * .96f;
        }
        shapes_.push_back(std::move(s));
    }
}

Part Gizmo::pick(float x, float y) const {
    if (!visible_)
        return Part::None;
    float tolerance = pickPx * view_.dpi;
    Part best = Part::None;
    float bestScore = 1e30f;
    for (auto &s : shapes_) {
        if (alpha(s.part) < .05f)
            continue;
        float d = 1e30f;
        if (s.part == Part::RotateFree) {
            if (std::hypot(x - s.circle.x, y - s.circle.y) <= s.radius)
                d = 0;
        } else if (s.radius >= 0)
            d = std::max(0.f, std::hypot(x - s.circle.x, y - s.circle.y) - s.radius);
        if (insideConvex(x, y, s.polygon))
            d = 0;
        else
            for (size_t n = 0; n < s.polygon.size(); ++n)
                d = std::min(d, segmentDistance(x, y, s.polygon[n], s.polygon[(n + 1) % s.polygon.size()]));
        for (auto &[a, b] : s.segments)
            d = std::min(d, segmentDistance(x, y, a, b));
        if (d > tolerance)
            continue;
        // Inside hits rank by priority (centre, planes, tips); near misses by distance.
        // The trackball is only a fallback inside the sphere.
        float score = s.part == Part::RotateFree ? 1e20f : d <= 0 ? -10.f + s.priority : d;
        if (score < bestScore) {
            bestScore = score;
            best = s.part;
        }
    }
    return best;
}

bool Gizmo::begin(Part part, float x, float y, const GizmoPose &start, const GizmoView &view) {
    if (!enabled(part))
        return false;
    view_ = view;
    drag_ = {};
    typed_.clear();
    drag_.part = part;
    drag_.x0 = x, drag_.y0 = y;
    drag_.start = start;
    drag_.layout = layout_;
    auto &l = drag_.layout;
    auto o = view.camera.position;
    auto ray = view.ray(x, y);
    auto c = l.centre;
    int i = partAxis(part);
    drag_.ringPx = std::max(1.f, ringRadius() * l.unitPx);
    if (isAxis(part)) {
        drag_.axis = l.axes[i];
        drag_.t0 = closestParam(o, ray, c, drag_.axis).value_or(l.sign[i] * l.size);
        if (isScale(part) && std::abs(drag_.t0) < .05f * l.size)
            drag_.t0 = l.sign[i] * l.size * scaleCube;
    } else if (isPlane(part)) {
        drag_.axis = l.axes[i];
        drag_.grab = rayPlane(o, ray, c, drag_.axis).value_or(c);
    } else if (part == Part::MoveView) {
        drag_.axis = view.camera.forward();
        drag_.grab = rayPlane(o, ray, c, drag_.axis).value_or(c);
    } else if (isRing(part) || part == Part::RotateView) {
        drag_.axis = part == Part::RotateView ? view.camera.forward() : l.axes[i];
        // Rings near edge-on drag along their screen tangent; otherwise by the angle
        // swept in the ring's plane.
        drag_.tangent = part != Part::RotateView && std::abs(dot(drag_.axis, l.toCamera)) < .3f;
        Vec3 g = c + basis(drag_.axis).first * ringRadius();
        float bestDistance = 1e30f;
        for (auto &s : shapes_)
            if (s.part == part)
                for (size_t n = 0; n < s.segments.size(); ++n) {
                    float d = segmentDistance(x, y, s.segments[n].first, s.segments[n].second);
                    if (d < bestDistance && part != Part::RotateView) {
                        bestDistance = d;
                        float t = s.segmentAngles[n] + pi / 72;
                        auto u = l.axes[(i + 1) % 3], w = l.axes[(i + 2) % 3];
                        g = c + (u * std::cos(t) + w * std::sin(t)) * ringRadius();
                    }
                }
        if (drag_.tangent) {
            auto tangent = normalized(cross(drag_.axis, g - c));
            auto a = view.project(g), b = view.project(g + tangent * (l.size * .05f));
            if (a && b) {
                float dx = b->x - a->x, dy = b->y - a->y, n = std::hypot(dx, dy);
                if (n > 1e-4f)
                    drag_.tx = dx / n, drag_.ty = dy / n;
                else
                    drag_.tangent = false;
            } else
                drag_.tangent = false;
        }
        drag_.grab = rayPlane(o, ray, c, drag_.axis).value_or(g);
    }
    last_ = {};
    last_.pose = start;
    return true;
}

Gizmo::Result Gizmo::drag(float x, float y, bool snap, const GizmoView &view) {
    if (!dragging())
        return last_;
    if (numeric())
        return last_;
    view_ = view;
    auto &d = drag_;
    auto &l = d.layout;
    Result r;
    r.pose = d.start;
    auto o = view.camera.position;
    auto ray = view.ray(x, y);
    auto c = l.centre;
    int i = partAxis(d.part);
    auto p = d.part;
    if (p >= Part::MoveX && p <= Part::MoveZ) {
        auto t = closestParam(o, ray, c, d.axis);
        float delta = t ? std::clamp(*t - d.t0, -500.f, 500.f) : last_.delta.x + last_.delta.y + last_.delta.z;
        if (snap)
            delta = snapValue(delta, snapMetres);
        r.delta = axisVector(i) * delta;
        r.pose.position = d.start.position + d.axis * delta;
    } else if (isPlane(p) && isMove(p)) {
        auto hit = rayPlane(o, ray, c, d.axis);
        if (!hit)
            return last_;
        auto delta = *hit - d.grab;
        if (length(delta) > 500)
            return last_;
        for (int k : {(i + 1) % 3, (i + 2) % 3}) {
            float v = dot(delta, l.axes[k]);
            if (snap)
                v = snapValue(v, snapMetres);
            r.delta = r.delta + axisVector(k) * v;
            r.pose.position += l.axes[k] * v;
        }
    } else if (p == Part::MoveView) {
        auto hit = rayPlane(o, ray, c, d.axis);
        if (!hit)
            return last_;
        auto delta = *hit - d.grab;
        if (length(delta) > 500)
            return last_;
        for (int k = 0; k < 3; ++k) {
            float v = dot(delta, l.axes[k]);
            if (snap)
                v = snapValue(v, snapMetres);
            r.delta = r.delta + axisVector(k) * v;
            r.pose.position += l.axes[k] * v;
        }
    } else if (isRing(p) || p == Part::RotateView) {
        float angle = d.accumulated;
        if (d.tangent)
            angle = ((x - d.x0) * d.tx + (y - d.y0) * d.ty) / d.ringPx;
        else if (auto hit = rayPlane(o, ray, c, d.axis)) {
            float a = signedAngle(d.grab - c, *hit - c, d.axis), step = a - d.previous;
            while (step > pi)
                step -= 2 * pi;
            while (step < -pi)
                step += 2 * pi;
            d.accumulated += step;
            d.previous = a;
            angle = d.accumulated;
        }
        if (snap)
            angle = snapValue(angle, snapRadians);
        r.angle = angle;
        r.pose.rotation = normalize(quaternionProduct(axisAngle(d.axis, angle), d.start.rotation));
    } else if (p == Part::RotateFree) {
        float dx = x - d.x0, dy = y - d.y0, n = std::hypot(dx, dy);
        if (n > .5f) {
            auto axis = normalized(view.camera.up() * dx + view.camera.right() * dy);
            r.angle = n / d.ringPx;
            r.pose.rotation = normalize(quaternionProduct(axisAngle(axis, r.angle), d.start.rotation));
        }
    } else if (p >= Part::ScaleX && p <= Part::ScaleZ) {
        auto t = closestParam(o, ray, c, d.axis);
        float f = t ? *t / d.t0 : component(last_.factor, i);
        f = std::clamp(f, .05f, 20.f);
        if (snap)
            f = std::max(snapFactor, snapValue(f, snapFactor));
        r.factor = {1, 1, 1};
        (i == 0 ? r.factor.x : i == 1 ? r.factor.y : r.factor.z) = f;
    } else if (isPlane(p)) {
        auto hit = rayPlane(o, ray, c, d.axis);
        int j = (i + 1) % 3, k = (i + 2) % 3;
        auto diagonal = normalized(l.axes[j] * l.sign[j] + l.axes[k] * l.sign[k]);
        float from = dot(d.grab - c, diagonal);
        float f = hit && std::abs(from) > 1e-4f ? dot(*hit - c, diagonal) / from : component(last_.factor, j);
        f = std::clamp(f, .05f, 20.f);
        if (snap)
            f = std::max(snapFactor, snapValue(f, snapFactor));
        r.factor = {1, 1, 1};
        (j == 0 ? r.factor.x : j == 1 ? r.factor.y : r.factor.z) = f;
        (k == 0 ? r.factor.x : k == 1 ? r.factor.y : r.factor.z) = f;
    } else if (p == Part::ScaleUniform) {
        float f = 1 + ((x - d.x0) - (y - d.y0)) / (2 * options.pixels * view.dpi);
        f = std::clamp(f, .05f, 20.f);
        if (snap)
            f = std::max(snapFactor, snapValue(f, snapFactor));
        r.factor = {f, f, f};
    }
    if (isScale(p))
        r.pose.scale = {d.start.scale.x * r.factor.x, d.start.scale.y * r.factor.y, d.start.scale.z * r.factor.z};
    r.snapped = snap;
    last_ = r;
    return r;
}
void Gizmo::end() {
    drag_ = {};
    typed_.clear();
}

bool Gizmo::numericAllowed() const {
    auto p = drag_.part;
    return (p >= Part::MoveX && p <= Part::MoveZ) || isRing(p) || p == Part::RotateView || (isScale(p) && p != Part::None);
}
bool Gizmo::type(char ch) {
    if (!dragging() || !numericAllowed() || typed_.size() >= 12)
        return false;
    if (ch == '-' ? !typed_.empty() : ch == '.' ? typed_.find('.') != std::string::npos : (ch < '0' || ch > '9'))
        return false;
    typed_ += ch;
    return true;
}
bool Gizmo::backspace() {
    if (typed_.empty())
        return false;
    typed_.pop_back();
    return true;
}
Gizmo::Result Gizmo::applyTyped() {
    if (!dragging() || typed_.empty())
        return last_;
    char *end = nullptr;
    float v = std::strtof(typed_.c_str(), &end);
    if (end == typed_.c_str() || !std::isfinite(v))
        return last_;
    auto &d = drag_;
    Result r;
    r.pose = d.start;
    r.typed = true;
    int i = partAxis(d.part);
    if (d.part >= Part::MoveX && d.part <= Part::MoveZ) {
        v = std::clamp(v, -500.f, 500.f);
        r.delta = axisVector(i) * v;
        r.pose.position = d.start.position + d.axis * v;
    } else if (isRotate(d.part)) {
        r.angle = v * pi / 180;
        r.pose.rotation = normalize(quaternionProduct(axisAngle(d.axis, r.angle), d.start.rotation));
    } else if (isScale(d.part)) {
        float f = std::clamp(v, .05f, 20.f);
        r.factor = {1, 1, 1};
        if (d.part == Part::ScaleUniform)
            r.factor = {f, f, f};
        else if (isPlane(d.part)) {
            int j = (i + 1) % 3, k = (i + 2) % 3;
            (j == 0 ? r.factor.x : j == 1 ? r.factor.y : r.factor.z) = f;
            (k == 0 ? r.factor.x : k == 1 ? r.factor.y : r.factor.z) = f;
        } else
            (i == 0 ? r.factor.x : i == 1 ? r.factor.y : r.factor.z) = f;
        r.pose.scale = {d.start.scale.x * r.factor.x, d.start.scale.y * r.factor.y, d.start.scale.z * r.factor.z};
    }
    last_ = r;
    return r;
}

std::string Gizmo::readout() const {
    if (!dragging())
        return "";
    char text[160];
    auto p = drag_.part;
    auto &r = last_;
    static const char *axes = "XYZ";
    int i = partAxis(p);
    if (numeric()) {
        const char *unit = isMove(p) ? " m" : isRotate(p) ? " deg" : "x";
        std::snprintf(text, sizeof text, "%s %c = %s_%s  [Enter / Esc]", isMove(p) ? "MOVE" : isRotate(p) ? "ROTATE" : "SCALE",
                      i >= 0 ? axes[i] : p == Part::RotateView ? 'V' : 'U', typed_.c_str(), unit);
        return text;
    }
    if (isMove(p))
        std::snprintf(text, sizeof text, "dX %+.2f  dY %+.2f  dZ %+.2f m", r.delta.x, r.delta.y, r.delta.z);
    else if (isRotate(p))
        std::snprintf(text, sizeof text, "%s %+.1f deg", p == Part::RotateFree ? "FREE" : p == Part::RotateView ? "VIEW" : i == 0 ? "X" : i == 1 ? "Y" : "Z",
                      r.angle * 180 / pi);
    else if (p == Part::ScaleUniform)
        std::snprintf(text, sizeof text, "SCALE x%.2f", r.factor.x);
    else
        std::snprintf(text, sizeof text, "SCALE X x%.2f  Y x%.2f  Z x%.2f", r.factor.x, r.factor.y, r.factor.z);
    return text;
}

GizmoFrame Gizmo::draw(Part hover, bool snap, const GizmoView &view) const {
    GizmoFrame f;
    if (!visible_)
        return f;
    const auto &l = layout_;
    auto c = l.centre;
    float dpi = view.dpi, depthC = l.centrePx.depth;
    bool universal = options.mode == GizmoMode::Universal;
    auto active = drag_.part;
    f.occlusionSlack = l.size * 1.2f;
    // Light from the upper left of the camera: faces read as solids.
    auto light = normalized(view.camera.up() * .55f - view.camera.right() * .35f + l.toCamera * .75f);
    auto clipSegment = [&](Vec3 a, Vec3 b) -> std::optional<std::pair<GizmoPoint, GizmoPoint>> {
        auto viewM = view.camera.view();
        float da = -viewM.point(a).z, db = -viewM.point(b).z;
        if (da <= near && db <= near)
            return {};
        if (da <= near)
            a = a + (b - a) * ((near * 1.01f - da) / (db - da));
        else if (db <= near)
            b = b + (a - b) * ((near * 1.01f - db) / (da - db));
        auto pa = view.project(a), pb = view.project(b);
        if (!pa || !pb)
            return {};
        return std::pair{*pa, *pb};
    };
    auto stroke = [&](Vec3 a, Vec3 b, Rgba color, float width, bool dashed = false, bool occlude = true) {
        if (color.a <= .01f)
            return;
        if (auto s = clipSegment(a, b)) {
            float depth = (s->first.depth + s->second.depth) * .5f;
            float cue = std::clamp(depthC / std::max(depth, .01f), .8f, 1.25f);
            f.strokes.push_back({s->first, s->second, color, width * dpi * cue, dashed, occlude});
        }
    };
    auto fill = [&](const std::vector<Vec3> &points, Rgba color, bool occlude = true) {
        if (color.a <= .01f)
            return;
        GizmoFill out{{}, color, occlude};
        for (auto &p : points) {
            auto s = view.project(p);
            if (!s)
                return;
            out.points.push_back(*s);
        }
        f.fills.push_back(std::move(out));
    };
    auto outline = [&](std::vector<GizmoPoint> pts, Rgba color, float width) {
        auto h = hull(std::move(pts));
        for (size_t n = 0; n < h.size(); ++n)
            f.strokes.push_back({h[n], h[(n + 1) % h.size()], color, width * dpi, false, true});
    };
    // Colour and alpha for a part in the current hover/drag state.
    auto look = [&](Part p, Rgba base) {
        float a = alpha(p);
        if (active != Part::None) {
            if (p == active)
                return Rgba{mixWhite(base, .35f).r, mixWhite(base, .35f).g, mixWhite(base, .35f).b, 1};
            return withAlpha(base, a * .22f);
        }
        if (p == hover)
            return withAlpha(mixWhite(base, .45f), a);
        return withAlpha(base, a);
    };
    auto widthOf = [&](Part p, float w) { return p == hover || p == active ? w * 1.4f : w; };
    auto box = [&](Vec3 q, float h, Rgba color) {
        std::vector<GizmoPoint> silhouette;
        for (int axis = 0; axis < 3; ++axis)
            for (float s : {-1.f, 1.f}) {
                auto n = l.axes[axis] * s;
                if (dot(n, normalized(view.camera.position - q)) <= 0)
                    continue;
                auto u = l.axes[(axis + 1) % 3] * h, w = l.axes[(axis + 2) % 3] * h, o = q + n * h;
                float shade = .58f + .42f * std::max(0.f, dot(n, light));
                fill({o - u - w, o + u - w, o + u + w, o - u + w}, {color.r * shade, color.g * shade, color.b * shade, color.a});
            }
        for (int n = 0; n < 8; ++n)
            if (auto p = view.project(q + l.axes[0] * (n & 1 ? h : -h) + l.axes[1] * (n & 2 ? h : -h) + l.axes[2] * (n & 4 ? h : -h)))
                silhouette.push_back(*p);
        outline(silhouette, scaled(color, .8f), 1.1f);
    };
    auto cone = [&](Vec3 base, Vec3 dir, float length, float radius, Rgba color) {
        auto [u, w] = basis(dir);
        auto apex = base + dir * length;
        constexpr int sides = 18;
        std::vector<GizmoPoint> silhouette;
        if (auto p = view.project(apex))
            silhouette.push_back(*p);
        auto eye = normalized(view.camera.position - base);
        if (dot(dir, eye) < 0) { // base faces the camera
            std::vector<Vec3> disc;
            for (int n = 0; n < sides; ++n) {
                float t = n * 2 * pi / sides;
                disc.push_back(base + (u * std::cos(t) + w * std::sin(t)) * radius);
            }
            fill(disc, scaled(color, .55f));
        }
        for (int n = 0; n < sides; ++n) {
            float t0 = n * 2 * pi / sides, t1 = (n + 1) * 2 * pi / sides, tm = (t0 + t1) / 2;
            auto r0 = u * std::cos(t0) + w * std::sin(t0), r1 = u * std::cos(t1) + w * std::sin(t1);
            auto normal = normalized(u * std::cos(tm) + w * std::sin(tm) + dir * (radius / length));
            if (auto p = view.project(base + r0 * radius))
                silhouette.push_back(*p);
            if (dot(normal, eye) <= 0)
                continue;
            float shade = .55f + .45f * std::max(0.f, dot(normal, light));
            fill({apex, base + r0 * radius, base + r1 * radius}, {color.r * shade, color.g * shade, color.b * shade, color.a});
        }
        outline(silhouette, scaled(color, .85f), 1.1f);
    };
    auto circlePx = [&](GizmoPoint centre, float radius, Rgba color, float width, bool filled, Rgba fillColor) {
        constexpr int n = 40;
        GizmoFill disc{{}, fillColor, true};
        for (int k = 0; k < n; ++k) {
            float t0 = k * 2 * pi / n, t1 = (k + 1) * 2 * pi / n;
            GizmoPoint a{centre.x + std::cos(t0) * radius, centre.y + std::sin(t0) * radius, centre.depth};
            GizmoPoint b{centre.x + std::cos(t1) * radius, centre.y + std::sin(t1) * radius, centre.depth};
            if (width > 0 && color.a > .01f)
                f.strokes.push_back({a, b, color, width * dpi, false, true});
            disc.points.push_back(a);
        }
        if (filled && fillColor.a > .01f)
            f.fills.push_back(std::move(disc));
    };
    // Guides for the active handle: infinite axis lines and snap ticks.
    auto guide = [&](int axis, Rgba color) {
        auto start = drag_.layout.centre;
        auto dir = drag_.layout.axes[axis];
        stroke(start - dir * 1000.f, start + dir * 1000.f, withAlpha(color, .55f), 1.3f, false, false);
    };
    auto ticks = [&](int axis, float step, Rgba color) {
        auto start = drag_.layout.centre;
        auto dir = drag_.layout.axes[axis];
        auto [u, w] = basis(dir);
        auto side = normalized(cross(dir, l.toCamera));
        if (length(side) < .1f)
            side = u;
        for (int k = -40; k <= 40; ++k) {
            auto at = start + dir * (k * step);
            float len = (k % 5 == 0 ? .05f : .025f) * l.size * (k == 0 ? 1.6f : 1);
            stroke(at - side * len, at + side * len, withAlpha(color, .8f), k % 5 == 0 ? 1.6f : 1.1f, false, false);
        }
        (void)w;
    };
    // Sphere for free rotation (underneath everything else).
    if (alpha(Part::RotateFree) > 0) {
        auto a = look(Part::RotateFree, palette::neutral);
        float fillAlpha = active == Part::RotateFree ? .14f : hover == Part::RotateFree ? .10f : .045f;
        circlePx(l.centrePx, ringRadius() * l.unitPx * .96f, a, 0, true, withAlpha(palette::neutral, fillAlpha));
    }
    // Rings: solid camera-facing halves, faint back halves; the active ring in full.
    for (int i = 0; i < 3; ++i) {
        auto part = Part(int(Part::RotateX) + i);
        if (alpha(part) <= 0)
            continue;
        auto color = look(part, palette::axis(i));
        auto u = l.axes[(i + 1) % 3], w = l.axes[(i + 2) % 3];
        float r = ringRadius();
        bool faceOn = std::abs(dot(l.axes[i], l.toCamera)) > .97f;
        constexpr int count = 72;
        for (int n = 0; n < count; ++n) {
            float t0 = n * 2 * pi / count, t1 = (n + 1) * 2 * pi / count;
            auto d0 = u * std::cos(t0) + w * std::sin(t0), d1 = u * std::cos(t1) + w * std::sin(t1);
            bool front = faceOn || dot(d0 + d1, l.toCamera) >= -.04f || part == active;
            stroke(c + d0 * r, c + d1 * r, front ? color : withAlpha(color, .3f), front ? widthOf(part, 2.6f) : 1.3f, !front);
        }
    }
    if (alpha(Part::RotateView) > 0) {
        auto color = look(Part::RotateView, palette::neutral);
        float r = l.size * viewRing * (universal ? universalRing / rotateRing : 1);
        auto u = view.camera.right(), w = view.camera.up();
        constexpr int count = 72;
        for (int n = 0; n < count; ++n) {
            float t0 = n * 2 * pi / count, t1 = (n + 1) * 2 * pi / count;
            stroke(c + (u * std::cos(t0) + w * std::sin(t0)) * r, c + (u * std::cos(t1) + w * std::sin(t1)) * r,
                   withAlpha(color, color.a * .85f), widthOf(Part::RotateView, 2.f), false, false);
        }
    }
    // Active rotation: swept wedge, start/current spokes and 15-degree ticks.
    if (isRotate(active) && active != Part::RotateFree) {
        auto axis = drag_.axis;
        auto g = drag_.grab - drag_.layout.centre;
        g = normalized(g - axis * dot(g, axis));
        if (length(g) < .5f)
            g = basis(axis).first;
        float r = active == Part::RotateView ? l.size * viewRing : ringRadius();
        int i = partAxis(active);
        auto color = i >= 0 ? palette::axis(i) : palette::neutral;
        float angle = last_.angle;
        int steps = std::clamp(int(std::abs(angle) / (pi / 36)) + 1, 1, 144);
        auto cc = drag_.layout.centre;
        for (int n = 0; n < steps; ++n) {
            float a0 = angle * n / steps, a1 = angle * (n + 1) / steps;
            fill({cc, cc + rotate(axisAngle(axis, a0), g) * r, cc + rotate(axisAngle(axis, a1), g) * r}, withAlpha(color, .22f), false);
        }
        stroke(cc, cc + g * r, withAlpha(palette::neutral, .7f), 1.4f, false, false);
        stroke(cc, cc + rotate(axisAngle(axis, angle), g) * r, withAlpha(palette::neutral, .95f), 1.8f, false, false);
        if (snap)
            for (int n = 0; n < 24; ++n) {
                auto dir = rotate(axisAngle(axis, n * snapRadians), g);
                float in = n % 6 == 0 ? .86f : .92f;
                stroke(cc + dir * (r * in), cc + dir * (r * 1.06f), withAlpha(palette::neutral, .85f), n % 6 == 0 ? 1.8f : 1.2f, false, false);
            }
    }
    // Plane handles.
    for (int i = 0; i < 3; ++i)
        for (auto part : {Part(int(Part::PlaneX) + i), Part(int(Part::ScalePlaneX) + i)}) {
            if (alpha(part) <= 0)
                continue;
            auto color = look(part, palette::axis(i));
            auto u = l.axes[(i + 1) % 3] * l.sign[(i + 1) % 3], w = l.axes[(i + 2) % 3] * l.sign[(i + 2) % 3];
            float a = planeInner * l.size, b = planeOuter * l.size;
            std::vector<Vec3> q{c + u * a + w * a, c + u * b + w * a, c + u * b + w * b, c + u * a + w * b};
            bool lit = part == hover || part == active;
            fill(q, withAlpha(color, color.a * (lit ? .55f : .3f)));
            for (int k = 0; k < 4; ++k)
                stroke(q[k], q[(k + 1) % 4], color, widthOf(part, 1.6f));
        }
    // Axes: arrows (move) and line-and-box (scale), farthest first.
    std::array<int, 3> order{0, 1, 2};
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return dot(l.axes[a] * l.sign[a], l.toCamera) < dot(l.axes[b] * l.sign[b], l.toCamera);
    });
    for (int i : order) {
        auto dir = l.axes[i] * l.sign[i];
        auto move = Part(int(Part::MoveX) + i), scale = Part(int(Part::ScaleX) + i);
        if (alpha(move) > 0) {
            auto color = look(move, palette::axis(i));
            stroke(c + dir * (l.size * shaftStart), c + dir * (l.size * coneBase), color, widthOf(move, 3.f));
            cone(c + dir * (l.size * coneBase), dir, l.size * (1 - coneBase), l.size * coneRadius * (move == hover ? 1.15f : 1), color);
        }
        if (alpha(scale) > 0) {
            auto color = look(scale, palette::axis(i));
            float end = universal ? universalCube : scaleCube, half = (universal ? universalHalf : scaleHalf) * l.size;
            if (!universal)
                stroke(c + dir * (l.size * shaftStart), c + dir * (l.size * end - half), color, widthOf(scale, 3.f));
            box(c + dir * (l.size * end), half * (scale == hover ? 1.15f : 1), color);
        }
    }
    // Centre: screen-plane disc (move) or uniform cube (scale).
    if (alpha(Part::ScaleUniform) > 0)
        box(c, uniformHalf * l.size * (hover == Part::ScaleUniform ? 1.15f : 1), look(Part::ScaleUniform, palette::neutral));
    if (alpha(Part::MoveView) > 0) {
        auto color = look(Part::MoveView, palette::neutral);
        bool lit = hover == Part::MoveView || active == Part::MoveView;
        circlePx(l.centrePx, centreDiscPx * dpi, color, lit ? 2.6f : 2.f, true, withAlpha(color, color.a * (lit ? .45f : .2f)));
    }
    // Guides and snap ticks for the active handle.
    if (active != Part::None && !isRotate(active)) {
        int i = partAxis(active);
        if (isAxis(active)) {
            guide(i, palette::axis(i));
            if (snap) {
                float step = isScale(active) ? std::abs(drag_.t0) * snapFactor : snapMetres;
                ticks(i, step, palette::axis(i));
            }
        } else if (isPlane(active))
            for (int k : {(i + 1) % 3, (i + 2) % 3}) {
                guide(k, palette::axis(k));
                if (snap && isMove(active))
                    ticks(k, snapMetres, palette::axis(k));
            }
    }
    return f;
}
} // namespace hyprune::interaction
