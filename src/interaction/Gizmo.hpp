#pragma once
// Position tool (tool 4) gizmo: perspective 3D handles at a constant screen size.
// Pure geometry: layout, screen-space picking, drag maths and the 2D primitives the
// renderer draws at native output resolution. No GL, no compositor calls.
#include "../sim/Camera.hpp"
#include <array>
#include <optional>
#include <string>
#include <vector>
namespace hyprune::interaction {
struct Rgba {
    float r = 1, g = 1, b = 1, a = 1;
};
// Output pixels (origin top-left, y down) and eye depth in metres.
struct GizmoPoint {
    float x = 0, y = 0, depth = 1;
};
struct GizmoStroke {
    GizmoPoint a, b;
    Rgba color;
    float width = 2;     // output pixels
    bool dashed = false; // always dashed (back halves); occluded parts dash anyway
    bool occlude = true; // dim and dash where world geometry is in front
    float along = 0;     // dash phase at a, pixels (continuous along rings)
    unsigned sequence = 0; // painter's order across strokes and fills
};
struct GizmoFill {
    std::vector<GizmoPoint> points; // convex polygon, either winding
    Rgba color;
    bool occlude = true;
    unsigned sequence = 0;
};
struct GizmoFrame {
    std::vector<GizmoFill> fills;
    std::vector<GizmoStroke> strokes;
    // Depth slack before world geometry counts as in front: the selection's own
    // extent, so the object being edited never hides its own handles.
    float occlusionSlack = 0;
    bool empty() const { return fills.empty() && strokes.empty(); }
    unsigned next() const; // the sequence after every primitive
    void append(const GizmoFrame &); // drawn after this frame's primitives
};
// HUD palette (output sRGB, drawn after tone mapping).
namespace palette {
inline constexpr Rgba coral{1.f, .42f, .37f}, mint{.43f, .95f, .69f}, sky{.36f, .72f, 1.f},
    neutral{.965f, .96f, .92f}, accent{.875f, .74f, .41f}, invalid{1.f, .27f, .24f};
Rgba axis(int);
} // namespace palette

enum class GizmoMode { Move, Rotate, Scale, Universal };
enum class Part {
    None = -1,
    MoveX, MoveY, MoveZ,
    PlaneX, PlaneY, PlaneZ, // plane handles, indexed by their normal axis (PlaneX = the YZ plane)
    MoveView,               // centre disc: move in the screen plane
    RotateX, RotateY, RotateZ,
    RotateView,             // outer ring: about the view axis
    RotateFree,             // inside the sphere: trackball
    ScaleX, ScaleY, ScaleZ,
    ScalePlaneX, ScalePlaneY, ScalePlaneZ,
    ScaleUniform,
    Count
};
const char *partName(Part);
int partAxis(Part); // 0..2, else -1
bool isMove(Part);
bool isRotate(Part);
bool isScale(Part);

struct GizmoView {
    sim::Camera camera;
    float width = 1920, height = 1080; // output pixels
    float dpi = 1;                     // output pixels per logical pixel
    float aspect() const { return width / height; }
    std::optional<GizmoPoint> project(Vec3) const;
    Vec3 ray(float x, float y) const; // through output pixel (x, y)
};
struct GizmoPose {
    Vec3 position{};
    std::array<float, 4> rotation{0, 0, 0, 1};
    Vec3 scale{1, 1, 1};
};
struct GizmoOptions {
    GizmoMode mode = GizmoMode::Move;
    bool local = false;                     // axes follow the selection's rotation
    bool yawOnly = false;                   // upright objects: the world Y ring only
    bool uniformScaleOnly = false;          // world objects: the centre cube only
    std::array<bool, 3> scaleAxes{true, true, true}; // windows have no depth to scale
    float pixels = 160;                     // axis length, logical pixels
};
// Snap steps (Shift or the snapping toggle): M3B values.
inline constexpr float snapMetres = .1f, snapRadians = .2617993878f, snapFactor = .1f;
float snapValue(float value, float step);

std::array<float, 4> axisAngle(Vec3 axis, float radians);

class Gizmo {
  public:
    GizmoOptions options;
    // Recomputes handles for this frame. Axis flips have hysteresis and freeze
    // while a drag is active, so handles never jump under the cursor.
    void layout(const GizmoView &, Vec3 centre, const std::array<float, 4> &rotation);
    bool visible() const { return visible_; }
    // Nearest enabled handle within the pick tolerance of output pixel (x, y).
    Part pick(float x, float y) const;
    float alpha(Part) const;   // 0 = disabled (degenerate or not offered)
    // A world point on the handle that picks it from the current view (tests, demos).
    std::optional<Vec3> anchor(Part) const;
    bool enabled(Part p) const { return alpha(p) > 0; }

    struct Result {
        GizmoPose pose;
        Vec3 delta{};      // metres along the gizmo axes (move)
        float angle = 0;   // radians (rotate)
        Vec3 factor{1, 1, 1};
        bool snapped = false;
        bool typed = false;
    };
    bool begin(Part, float x, float y, const GizmoPose &start, const GizmoView &);
    Result drag(float x, float y, bool snap, const GizmoView &);
    void end();
    bool dragging() const { return drag_.part != Part::None; }
    Part active() const { return drag_.part; }
    const GizmoPose &start() const { return drag_.start; }
    const Result &last() const { return last_; }

    // Numeric entry during a drag: digits, '.', '-', Backspace. Once text is typed,
    // pointer motion is ignored and releasing the button keeps the drag open until
    // Enter (commit) or Escape (cancel). Offered for single-axis, ring and uniform handles.
    bool numericAllowed() const;
    bool type(char);
    bool backspace();
    const std::string &typed() const { return typed_; }
    bool numeric() const { return !typed_.empty(); }
    Result applyTyped();

    GizmoFrame draw(Part hover, bool snap, const GizmoView &) const;
    std::string readout() const;

    // Geometry, exposed for tests and the guide/tick drawing.
    struct Layout {
        Vec3 centre{};
        std::array<Vec3, 3> axes{};     // unit, unflipped (+X/+Y/+Z of the gizmo space)
        std::array<float, 3> sign{1, 1, 1}; // drawn direction: towards the camera side
        float size = 1;                 // world length of an axis
        float unitPx = 1;               // output pixels per metre at the centre
        Vec3 toCamera{0, 0, 1};
        std::array<float, 3> axisFade{}, planeFade{};
        GizmoPoint centrePx;
    };
    const Layout &geometry() const { return layout_; }
    float ringRadius() const; // world, current mode
    void resetFlips() { flipsValid_ = false; }

  private:
    struct Shape {
        Part part = Part::None;
        int priority = 9;
        std::vector<GizmoPoint> polygon;               // inside = hit
        std::vector<std::pair<GizmoPoint, GizmoPoint>> segments;
        GizmoPoint circle{};                           // centre disc / sphere
        float radius = -1;
        std::vector<float> segmentAngles;              // ring parameter per segment start
    };
    struct Drag {
        Part part = Part::None;
        float x0 = 0, y0 = 0;
        GizmoPose start;
        Layout layout;
        Vec3 axis{}, grab{};
        float t0 = 0, previous = 0, accumulated = 0, ringPx = 1;
        float tx = 0, ty = 0; // screen tangent (rotate, edge-on)
        bool tangent = false;
    };
    Layout layout_;
    std::vector<Shape> shapes_;
    std::array<float, int(Part::Count)> alpha_{};
    bool visible_ = false, flipsValid_ = false;
    Drag drag_;
    Result last_;
    std::string typed_;
    GizmoView view_;
    float ringFactor() const;
    bool uniformRing() const;
    bool offered(Part) const;
    void buildShapes();
    Result finish(Result, bool snap) const;
};
} // namespace hyprune::interaction
