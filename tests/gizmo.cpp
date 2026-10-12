// Position tool gizmo: handle layout and picking (edge-on and degenerate views),
// drag maths, snapping, numeric entry, undo and the placement rules.
#include "interaction/Gizmo.hpp"
#include "interaction/Placement.hpp"
#include "interaction/Tools.hpp"
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <iostream>
using namespace hyprune;
using namespace hyprune::interaction;
namespace {
constexpr float pi = 3.14159265f;
GizmoView view(Vec3 eye, float yaw, float pitch = 0) {
    GizmoView v;
    v.camera.position = eye;
    v.camera.yaw = yaw;
    v.camera.pitch = pitch;
    v.camera.fov = 60;
    v.width = 1920;
    v.height = 1080;
    return v;
}
GizmoPoint at(const GizmoView &v, Vec3 p) {
    auto s = v.project(p);
    assert(s);
    return *s;
}
bool near(float a, float b, float e = 1e-3f) { return std::abs(a - b) <= e; }
float angleOf(const std::array<float, 4> &q, Vec3 axis) {
    // Signed rotation of q about a known axis.
    float s = q[0] * axis.x + q[1] * axis.y + q[2] * axis.z;
    return 2 * std::atan2(s, q[3]);
}
void layoutAndPicking() {
    // Looking down -Z at the centre: Z points at the camera and is disabled, never a nub.
    auto v = view({0, 1.7f, 4}, 0);
    Gizmo g;
    g.layout(v, {0, 1.7f, 0}, {0, 0, 0, 1});
    assert(g.visible());
    assert(g.alpha(Part::MoveX) == 1 && g.alpha(Part::MoveY) == 1);
    assert(g.alpha(Part::MoveZ) == 0 && !g.enabled(Part::MoveZ));
    assert(g.alpha(Part::PlaneZ) == 1);                          // the XY plane faces the camera
    assert(g.alpha(Part::PlaneX) == 0 && g.alpha(Part::PlaneY) == 0); // edge-on planes disabled
    auto &l = g.geometry();
    // Constant screen size: the axis is options.pixels long on screen.
    auto tip = at(v, l.centre + l.axes[0] * l.size * l.sign[0]);
    assert(near(std::abs(tip.x - l.centrePx.x), 184, 1));
    // Twice as far: twice the world size, same pixels.
    Gizmo far;
    far.layout(view({0, 1.7f, 8}, 0), {0, 1.7f, 0}, {0, 0, 0, 1});
    assert(near(far.geometry().size, l.size * 2, 1e-3f));
    // Picks: arrow tip, shaft, plane square, centre disc, empty space.
    assert(g.pick(tip.x, tip.y) == Part::MoveX);
    auto shaft = at(v, l.centre + l.axes[1] * (l.size * .5f) * l.sign[1]);
    assert(g.pick(shaft.x + 3, shaft.y) == Part::MoveY);
    auto square = at(v, l.centre + (l.axes[0] * l.sign[0] + l.axes[1] * l.sign[1]) * (l.size * .34f));
    assert(g.pick(square.x, square.y) == Part::PlaneZ);
    assert(g.pick(l.centrePx.x + 2, l.centrePx.y - 2) == Part::MoveView);
    assert(g.pick(l.centrePx.x + 400, l.centrePx.y + 300) == Part::None);
    // DPI doubles pixel size and tolerance.
    auto hi = v;
    hi.dpi = 2;
    Gizmo h;
    h.layout(hi, {0, 1.7f, 0}, {0, 0, 0, 1});
    auto hiTip = at(hi, h.geometry().centre + h.geometry().axes[0] * h.geometry().size * h.geometry().sign[0]);
    assert(near(std::abs(hiTip.x - h.geometry().centrePx.x), 368, 1));
    // Behind the camera: nothing drawn or picked.
    Gizmo behind;
    behind.layout(v, {0, 1.7f, 6}, {0, 0, 0, 1});
    assert(!behind.visible() && behind.pick(960, 540) == Part::None && behind.draw(Part::None, false, v).empty());
}
void flips() {
    Gizmo g;
    // From +X+Y+Z every axis points towards the camera already.
    auto a = view({3, 4, 3}, -pi / 4, -.6f);
    g.layout(a, {0, 1, 0}, {0, 0, 0, 1});
    assert(g.geometry().sign[0] == 1 && g.geometry().sign[2] == 1);
    // From -X-Z, X and Z flip to the visible side.
    auto b = view({-3, 4, -3}, pi * .75f, -.6f);
    g.layout(b, {0, 1, 0}, {0, 0, 0, 1});
    assert(g.geometry().sign[0] == -1 && g.geometry().sign[2] == -1 && g.geometry().sign[1] == 1);
    // Hysteresis: just past perpendicular, the flipped direction stays.
    auto c = view({.3f, 1, 5}, -std::atan2(.3f, 5.f));
    g.layout(c, {0, 1, 0}, {0, 0, 0, 1});
    assert(g.geometry().sign[0] == -1);
    // Frozen while dragging.
    auto p = g.geometry();
    auto tipY = at(c, p.centre + p.axes[1] * p.size * p.sign[1]);
    assert(g.begin(Part::MoveY, tipY.x, tipY.y, {}, c));
    g.layout(a, {0, 1, 0}, {0, 0, 0, 1});
    assert(g.geometry().sign[0] == -1);
    g.end();
}
void edgeOnRings() {
    // Looking along -X at a horizontal ring (rotate about Y): the ring is a line.
    auto v = view({4, 1.7f, 0}, -pi / 2);
    Gizmo g;
    g.options.mode = GizmoMode::Rotate;
    g.layout(v, {0, 1.7f, 0}, {0, 0, 0, 1});
    assert(g.enabled(Part::RotateY) && g.enabled(Part::RotateX) && g.enabled(Part::RotateView) && g.enabled(Part::RotateFree));
    auto &l = g.geometry();
    float rPx = g.ringRadius() * l.unitPx;
    // A point on the edge-on line, away from the X ring (a face-on circle here) and the Z line.
    auto onLine = GizmoPoint{l.centrePx.x + rPx * .6f, l.centrePx.y + 2, 0};
    assert(g.pick(onLine.x, onLine.y) == Part::RotateY);
    // Near the centre the edge-on lines don't pick: the trackball does.
    assert(g.pick(l.centrePx.x + rPx * .2f, l.centrePx.y + 1) == Part::RotateFree);
    // The face-on X ring is picked on its circle; inside it the trackball.
    assert(g.pick(l.centrePx.x + rPx * .7071f, l.centrePx.y - rPx * .7071f) == Part::RotateX);
    assert(g.pick(l.centrePx.x + rPx * .3f, l.centrePx.y - rPx * .5f) == Part::RotateFree);
    // The view ring outside.
    float viewPx = l.size * 1.18f * l.unitPx;
    assert(g.pick(l.centrePx.x - viewPx * .7071f, l.centrePx.y - viewPx * .7071f) == Part::RotateView);
    // Dragging an edge-on ring follows its screen tangent: a nonzero, continuous angle.
    assert(g.begin(Part::RotateY, onLine.x, onLine.y, {}, v));
    // The line runs across the screen: dragging along it turns, across it does nothing.
    auto r1 = g.drag(onLine.x + 40, onLine.y, false, v);
    auto r2 = g.drag(onLine.x + 80, onLine.y, false, v);
    assert(std::abs(g.drag(onLine.x, onLine.y + 60, false, v).angle) < .02f);
    assert(std::abs(r1.angle) > .05f && near(r2.angle, 2 * r1.angle, 1e-3f));
    assert(near(std::abs(angleOf(r2.pose.rotation, {0, 1, 0})), std::abs(r2.angle), 1e-3f));
    // Snapped: 15 degree steps.
    auto r3 = g.drag(onLine.x + 61, onLine.y, true, v);
    assert(near(std::fmod(std::abs(r3.angle) + 1e-4f, snapRadians), 0, 2e-3f));
    g.end();
    // Exactly end-on axis: disabled at every mode, and its arrow can't be picked.
    Gizmo m;
    m.layout(v, {0, 1.7f, 0}, {0, 0, 0, 1});
    assert(!m.enabled(Part::MoveX) && !m.enabled(Part::PlaneY) && !m.enabled(Part::PlaneZ));
    assert(m.pick(m.geometry().centrePx.x, m.geometry().centrePx.y) == Part::MoveView);
    // Nearly end-on (5 degrees): still disabled; at 30 degrees, fully enabled.
    Gizmo n;
    n.layout(view({4 * std::cos(.087f), 1.7f, 4 * std::sin(.087f)}, -pi / 2 + .087f), {0, 1.7f, 0}, {0, 0, 0, 1});
    assert(!n.enabled(Part::MoveX));
    n.layout(view({4 * std::cos(.52f), 1.7f, 4 * std::sin(.52f)}, -pi / 2 + .52f), {0, 1.7f, 0}, {0, 0, 0, 1});
    assert(n.alpha(Part::MoveX) == 1);
}
void faceOnRotate() {
    // From above, the Y ring faces the camera: angle swept in its plane.
    auto v = view({0, 6, 0.001f}, 0, -pi / 2 + .001f);
    Gizmo g;
    g.options.mode = GizmoMode::Rotate;
    g.layout(v, {0, 1, 0}, {0, 0, 0, 1});
    auto &l = g.geometry();
    float rPx = g.ringRadius() * l.unitPx;
    assert(g.pick(l.centrePx.x + rPx, l.centrePx.y) == Part::RotateY);
    assert(g.begin(Part::RotateY, l.centrePx.x + rPx, l.centrePx.y, {}, v));
    auto quarter = g.drag(l.centrePx.x, l.centrePx.y + rPx, false, v);
    assert(near(std::abs(quarter.angle), pi / 2, .02f));
    // Keep going past 180 degrees: accumulated, no wrap.
    g.drag(l.centrePx.x - rPx, l.centrePx.y, false, v);
    auto most = g.drag(l.centrePx.x, l.centrePx.y - rPx, false, v);
    assert(near(std::abs(most.angle), 3 * pi / 2, .03f));
    g.end();
}
void moveAndScale() {
    auto v = view({0, 1.7f, 4}, 0);
    Gizmo g;
    g.layout(v, {0, 1.7f, 0}, {0, 0, 0, 1});
    auto &l = g.geometry();
    auto tip = at(v, l.centre + l.axes[0] * l.size * l.sign[0] * .9f);
    GizmoPose start{{0, 1.7f, 0}, {0, 0, 0, 1}, {1, 1, 1}};
    assert(g.begin(Part::MoveX, tip.x, tip.y, start, v));
    auto r = g.drag(tip.x + 100, tip.y + 30, false, v);
    assert(r.pose.position.x > .1f && near(r.pose.position.y, 1.7f) && near(r.pose.position.z, 0));
    assert(near(r.delta.x, r.pose.position.x));
    auto s = g.drag(tip.x + 100, tip.y, true, v);
    assert(near(std::fmod(s.pose.position.x + 1e-4f, snapMetres), 0, 2e-3f));
    assert(g.readout().find("dX +") == 0);
    // Numeric entry: typed value replaces the pointer; Enter applies it.
    assert(g.numericAllowed() && g.type('1') && g.type('.') && !g.type('.') && g.type('5') && !g.type('-') && !g.type('x'));
    assert(g.readout().find("MOVE X = 1.5_") == 0);
    auto typed = g.applyTyped();
    assert(typed.typed && near(typed.pose.position.x, 1.5f));
    assert(g.drag(tip.x + 300, tip.y, false, v).pose.position.x == typed.pose.position.x); // pointer ignored now
    assert(g.backspace() && g.typed() == "1.");
    g.end();
    // Screen-plane move from the centre disc.
    assert(g.begin(Part::MoveView, l.centrePx.x, l.centrePx.y, start, v));
    auto mv = g.drag(l.centrePx.x + 50, l.centrePx.y - 50, false, v);
    assert(mv.pose.position.x > 0 && mv.pose.position.y > 1.7f && near(mv.pose.position.z, 0, 1e-3f));
    assert(!g.numericAllowed() && !g.type('1'));
    g.end();
    // Plane move: XY square, Z unchanged.
    auto square = at(v, l.centre + (l.axes[0] * l.sign[0] + l.axes[1] * l.sign[1]) * (l.size * .34f));
    assert(g.begin(Part::PlaneZ, square.x, square.y, start, v));
    auto pm = g.drag(square.x + 40, square.y + 40, true, v);
    assert(near(pm.pose.position.z, 0) && pm.pose.position.y < 1.7f);
    g.end();
    // Scale: axis box outwards grows; snap to 0.1x; uniform cube from the centre.
    Gizmo sc;
    sc.options.mode = GizmoMode::Scale;
    sc.options.local = true;
    sc.layout(v, {0, 1.7f, 0}, {0, 0, 0, 1});
    assert(!sc.enabled(Part::ScaleZ) && sc.enabled(Part::ScaleX) && sc.enabled(Part::ScalePlaneZ) && sc.enabled(Part::ScaleUniform));
    auto &sl = sc.geometry();
    auto box = at(v, sl.centre + sl.axes[0] * sl.sign[0] * (sl.size * .92f));
    assert(sc.pick(box.x, box.y) == Part::ScaleX);
    assert(sc.begin(Part::ScaleX, box.x, box.y, start, v));
    auto grown = sc.drag(box.x + (box.x - sl.centrePx.x), box.y, false, v);
    assert(near(grown.factor.x, 2, .05f) && grown.factor.y == 1 && near(grown.pose.scale.x, grown.factor.x));
    auto snapped = sc.drag(box.x + (box.x - sl.centrePx.x) * .43f, box.y, true, v);
    assert(near(snapped.factor.x, 1.4f, 1e-4f));
    sc.end();
    assert(sc.pick(sl.centrePx.x, sl.centrePx.y) == Part::ScaleUniform);
    assert(sc.begin(Part::ScaleUniform, sl.centrePx.x, sl.centrePx.y, start, v));
    auto u = sc.drag(sl.centrePx.x + 184, sl.centrePx.y, false, v);
    assert(near(u.factor.x, 1.5f) && u.factor.x == u.factor.y && u.factor.y == u.factor.z);
    sc.end();
    // Objects scale uniformly only; windows have no depth axis.
    Gizmo obj;
    obj.options.mode = GizmoMode::Scale;
    obj.options.uniformScaleOnly = true;
    obj.layout(view({3, 3, 3}, -pi / 4, -.4f), {0, 1, 0}, {0, 0, 0, 1});
    for (auto p : {Part::ScaleX, Part::ScaleY, Part::ScaleZ, Part::ScalePlaneX, Part::ScalePlaneY, Part::ScalePlaneZ})
        assert(!obj.enabled(p));
    assert(obj.enabled(Part::ScaleUniform));
    // Uniform-only selections scale from a ring too: twice the distance, twice the size.
    auto &ol = obj.geometry();
    float ringPx = ol.size * .62f * ol.unitPx;
    assert(obj.pick(ol.centrePx.x + ringPx, ol.centrePx.y) == Part::ScaleUniform);
    assert(obj.begin(Part::ScaleUniform, ol.centrePx.x + ringPx, ol.centrePx.y, start, view({3, 3, 3}, -pi / 4, -.4f)));
    auto ratio = obj.drag(ol.centrePx.x + ringPx * 2, ol.centrePx.y, false, view({3, 3, 3}, -pi / 4, -.4f));
    assert(near(ratio.factor.x, 2, 1e-3f));
    obj.end();
    Gizmo win;
    win.options.mode = GizmoMode::Scale;
    win.options.scaleAxes = {true, true, false};
    win.layout(view({3, 3, 3}, -pi / 4, -.4f), {0, 1, 0}, {0, 0, 0, 1});
    assert(win.enabled(Part::ScaleX) && !win.enabled(Part::ScaleZ) && win.enabled(Part::ScalePlaneZ) && !win.enabled(Part::ScalePlaneX));
}
void modes() {
    auto v = view({3, 3, 3}, -pi / 4, -.4f);
    Gizmo u;
    u.options.mode = GizmoMode::Universal;
    u.layout(v, {0, 1, 0}, {0, 0, 0, 1});
    for (auto p : {Part::MoveX, Part::PlaneY, Part::RotateZ, Part::ScaleX, Part::ScaleUniform})
        assert(u.enabled(p));
    for (auto p : {Part::MoveView, Part::RotateFree, Part::RotateView, Part::ScalePlaneX})
        assert(!u.enabled(p));
    // Universal: the scale box on the arrow shaft wins over the shaft.
    auto &l = u.geometry();
    auto cube = at(v, l.centre + l.axes[0] * l.sign[0] * (l.size * .58f));
    assert(u.pick(cube.x, cube.y) == Part::ScaleX);
    auto shaft = at(v, l.centre + l.axes[0] * l.sign[0] * (l.size * .75f));
    assert(u.pick(shaft.x, shaft.y) == Part::MoveX);
    // Upright objects: the yaw ring only.
    Gizmo y;
    y.options.mode = GizmoMode::Rotate;
    y.options.yawOnly = true;
    y.options.local = true;
    y.layout(v, {0, 1, 0}, axisAngle({1, 0, 0}, .5f));
    assert(y.enabled(Part::RotateY) && !y.enabled(Part::RotateX) && !y.enabled(Part::RotateFree) && !y.enabled(Part::RotateView));
    assert(near(y.geometry().axes[1].y, 1)); // world Y despite local
    // Local axes follow the rotation.
    Gizmo lo;
    lo.options.local = true;
    lo.layout(v, {0, 1, 0}, axisAngle({0, 1, 0}, pi / 2));
    assert(near(lo.geometry().axes[0].z, -1, 1e-4f));
    // Anchors pick their own handle (the nested tests aim with them).
    for (int k = 0; k < int(Part::Count); ++k)
        if (u.enabled(Part(k))) {
            auto a = u.anchor(Part(k));
            assert(a);
            auto s = at(v, *a);
            assert(u.pick(s.x, s.y) == Part(k));
        }
    // Drawing: depth slack and primitives; hover thickens; active dims the others.
    auto frame = u.draw(Part::None, false, v);
    assert(!frame.empty() && frame.occlusionSlack > 0);
    assert(std::all_of(frame.strokes.begin(), frame.strokes.end(), [](auto &s) { return s.halo && s.width >= 1.5f; }));
    float plain = 0, hovered = 0;
    for (auto &s : frame.strokes)
        plain = std::max(plain, s.width);
    for (auto &s : u.draw(Part::MoveX, false, v).strokes)
        hovered = std::max(hovered, s.width);
    assert(hovered > plain);
    auto tip = at(v, l.centre + l.axes[1] * l.sign[1] * l.size * .9f);
    assert(u.begin(Part::MoveY, tip.x, tip.y, {}, v));
    u.drag(tip.x, tip.y - 30, true, v);
    auto dragging = u.draw(Part::None, true, v);
    bool guide = false;
    for (auto &s : dragging.strokes)
        guide |= !s.occlude && std::hypot(s.b.x - s.a.x, s.b.y - s.a.y) > 1000;
    assert(guide);
    u.end();
}
void placement() {
    // A 10 m floor at y = 0 and a wall at x = 2 (normals facing the room).
    std::vector<Triangle> world{{{-5, 0, -5}, {-5, 0, 5}, {5, 0, 5}}, {{-5, 0, -5}, {5, 0, 5}, {5, 0, -5}},
                                {{2, 0, -5}, {2, 3, 5}, {2, 3, -5}}, {{2, 0, -5}, {2, 0, 5}, {2, 3, 5}}};
    sim::TriangleIndex index(world);
    PlacementScene s;
    s.collision = world;
    s.index = &index;
    Box crate = aabbBox({-.25f, 0, -.25f}, {.25f, .5f, .25f});
    auto v = checkPlacement(s, crate, {});
    assert(v.ok && v.supported); // resting on the floor is contact, not intersection
    crate.centre.y += 1;
    v = checkPlacement(s, crate, {});
    assert(v.ok && !v.supported); // floating: advisory only
    crate.centre = {1.9f, .25f, 0};
    v = checkPlacement(s, crate, {});
    assert(!v.ok && v.rule == "walls");
    // Ignoring the selection's own triangles.
    s.ignore = [](unsigned id) { return id >= 2; };
    assert(checkPlacement(s, crate, {}).ok);
    s.ignore = {};
    // With its own collision, a concave shape straddling the wall is refined by triangles.
    std::vector<Triangle> own{{{1.5f, .1f, -.1f}, {1.5f, .1f, .1f}, {1.5f, .4f, 0}}};
    Box wide = aabbBox({1.4f, 0, -.25f}, {2.4f, .5f, .25f});
    assert(checkPlacement(s, wide, own).ok);
    own.push_back({{1.8f, .25f, 0}, {2.3f, .25f, .1f}, {2.3f, .25f, -.1f}});
    assert(checkPlacement(s, wide, own).rule == "walls");
    // Wholly inside a closed solid (a 1 m cube whose faces point outwards).
    std::vector<Triangle> solid;
    auto quad = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d) { solid.push_back({a, b, c}), solid.push_back({a, c, d}); };
    Vec3 o{-3, 1, -3};
    quad(o + Vec3{0, 0, 0}, o + Vec3{0, 1, 0}, o + Vec3{1, 1, 0}, o + Vec3{1, 0, 0});
    quad(o + Vec3{0, 0, 1}, o + Vec3{1, 0, 1}, o + Vec3{1, 1, 1}, o + Vec3{0, 1, 1});
    quad(o + Vec3{0, 0, 0}, o + Vec3{0, 0, 1}, o + Vec3{0, 1, 1}, o + Vec3{0, 1, 0});
    quad(o + Vec3{1, 0, 0}, o + Vec3{1, 1, 0}, o + Vec3{1, 1, 1}, o + Vec3{1, 0, 1});
    quad(o + Vec3{0, 0, 0}, o + Vec3{1, 0, 0}, o + Vec3{1, 0, 1}, o + Vec3{0, 0, 1});
    quad(o + Vec3{0, 1, 0}, o + Vec3{0, 1, 1}, o + Vec3{1, 1, 1}, o + Vec3{1, 1, 0});
    PlacementScene inside;
    inside.collision = solid;
    assert(!checkPlacement(inside, aabbBox(o + Vec3{.4f, .4f, .4f}, o + Vec3{.6f, .6f, .6f}), {}).ok);
    assert(checkPlacement(inside, aabbBox(o + Vec3{2.4f, .4f, .4f}, o + Vec3{2.6f, .6f, .6f}), {}).ok);
    // Playable volume and the respawn height.
    auto playable = world::loadPlayable({{"playable", {{"volumes", {{{"type", "box"}, {"min", {-4, -.5, -4}}, {"max", {1.5, 4, 4}}}}}}}});
    s.playable = &playable;
    crate.centre = {0, .25f, 0};
    assert(checkPlacement(s, crate, {}).ok);
    crate.centre = {0, .25f, 4.5f};
    assert(checkPlacement(s, crate, {}).rule == "playable");
    s.playable = nullptr;
    s.respawnBelow = -2;
    crate.centre = {0, -3, 0};
    assert(checkPlacement(s, crate, {}).rule == "playable");
    s.respawnBelow = -1e9f;
    // Windows, approaches, zones, keep-clear volumes and the player.
    crate.centre = {0, .25f, 0};
    s.windows.push_back(aabbBox({-.5f, .3f, -.05f}, {.5f, 1, .05f}));
    assert(checkPlacement(s, crate, {}).rule == "window");
    s.windows.clear();
    s.approaches.push_back(aabbBox({-1, .1f, -.5f}, {1, 2, .5f}));
    assert(checkPlacement(s, crate, {}).rule == "approach");
    crate.half.y = .04f; // a part under 0.1 m above the floor may lie in an approach
    crate.centre.y = .04f;
    assert(checkPlacement(s, crate, {}).ok);
    // Already in an approach (authored there): edits may stay, walls still refuse.
    crate.half.y = .25f, crate.centre.y = .25f;
    auto waived = brokenSoftRules(s, crate, {});
    assert(waived == std::set<std::string>{"approach"});
    crate.centre.x += .2f;
    assert(checkPlacement(s, crate, {}, waived).ok);
    crate.centre.x = 1.9f;
    assert(checkPlacement(s, crate, {}, waived).rule == "walls");
    s.approaches.clear();
    crate = aabbBox({-.25f, 0, -.25f}, {.25f, .5f, .25f});
    s.zones.push_back(aabbBox({-.3f, 0, -.3f}, {.3f, 2, .3f}));
    assert(checkPlacement(s, crate, {}).rule == "zone");
    s.zones.clear();
    s.keepClear.push_back(aabbBox({.1f, 0, -1}, {1, 2, 1}));
    assert(checkPlacement(s, crate, {}).rule == "keep-clear");
    Box low = aabbBox({-.25f, 0, -.25f}, {.25f, .2f, .25f}); // 0.2 m, no collision: may sit there
    assert(checkPlacement(s, low, {}).ok);
    s.keepClear.clear();
    s.player = true;
    s.playerLow = {.4f, .3f, 0}, s.playerHigh = {.4f, 1.5f, 0};
    std::vector<Triangle> crateTriangles{{{-.25f, .1f, 0}, {.25f, .1f, 0}, {0, .4f, 0}}};
    assert(checkPlacement(s, crate, crateTriangles).rule == "player");
    assert(checkPlacement(s, crate, {}).ok); // non-collidable shapes may overlap the player
    // Box helpers.
    Box rotated;
    rotated.axes = {normalized(Vec3{1, 0, 1}), Vec3{0, 1, 0}, normalized(Vec3{-1, 0, 1})};
    rotated.half = {.5f, .5f, .5f};
    rotated.centre = {1.15f, 0, 0};
    assert(boxBox(aabbBox({-.5f, -.5f, -.5f}, {.5f, .5f, .5f}), rotated)); // its corner reaches x = 0.44
    rotated.centre = {1.3f, 0, 0};
    assert(!boxBox(aabbBox({-.5f, -.5f, -.5f}, {.5f, .5f, .5f}), rotated));
    assert(near(boxDistance(aabbBox({0, 0, 0}, {1, 1, 1}), {2, .5f, .5f}), 1));
    assert(floorBelow(s, {0, 1, 0}, 2).value_or(-1) == 0.f && !floorBelow(s, {0, 1, 0}, .5f));
}
void undo() {
    Tools t;
    Json a = {{"worldId", "w"}, {"areaId", "a"}, {"objectId", "x"}, {"position", {0, 0, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}};
    Json b = a;
    b["position"] = {1, 0, 0};
    t.objects.push_back(b);
    t.recordEdit("object", "w", "x", nullptr, b);
    assert(t.canUndo() && !t.canRedo());
    auto e = t.undo();
    assert(e && e->before.is_null() && e->after == b && t.canRedo());
    auto r = t.redo();
    assert(r && r->after == b && !t.canRedo());
    // A new edit clears the redo branch; the stack is bounded.
    t.undo();
    t.recordEdit("object", "w", "x", nullptr, a);
    assert(!t.canRedo());
    for (int i = 0; i < 200; ++i)
        t.recordEdit("object", "w", "x", a, b);
    int n = 0;
    while (t.undo())
        ++n;
    assert(n == int(Tools::undoLimit));
    // Unchanged edits are not recorded.
    Tools u;
    u.recordEdit("object", "w", "x", a, a);
    assert(!u.canUndo());
    // Wheel cycling: three modes, four when universal is enabled.
    Tools w;
    w.slot = 4;
    w.wheel(1), w.wheel(1), w.wheel(1);
    assert(w.submode() == "move");
    w.universal = true;
    w.wheel(1), w.wheel(1), w.wheel(1);
    assert(w.positionMode == 3 && w.gizmoMode() == "universal" && w.submode() == "move");
    w.wheel(1);
    assert(w.positionMode == 0);
}
} // namespace
int main() {
    layoutAndPicking();
    flips();
    edgeOnRings();
    faceOnRotate();
    moveAndScale();
    modes();
    placement();
    undo();
    std::cout << "gizmo: layout, picking (edge-on, degenerate, DPI), flips, drags, snapping, numeric entry, "
                 "modes, drawing, placement rules and undo passed\n";
}
