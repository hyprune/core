#pragma once
// Tool 9, the debug tool: one debug layer at a time over the world (Cycle steps through
// them), drawn as thick anti-aliased lines and light fills in the gizmo pass, with a legend
// and an info card for the item Primary picks. Pure geometry and text: the runtime gathers
// what it knows (windows, characters, seats) into DebugInputs; no GL, no compositor calls.
#include "Gizmo.hpp"
#include "Placement.hpp"
#include "world/World.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>
namespace hyprune::interaction {
using Json = world::Json;
enum class DebugLayer {
    Windows,     // window volumes and mount frames
    WindowZones, // authored placementZones (free window placement; objects refused)
    Collision,   // collision by kind: floors, walls, invisible walls (backstops), object proxies
    Playable,    // playable volumes and the fly ceiling
    Paths,       // authored and user paths, their points and stops, the walking graph
    KeepClear,   // keepClear volumes, screen approach zones, spawn and climb clearances
    Seats,
    Visibility,  // visibility regions: where the eye is, what is drawn from there
    Steering,    // NPC avoidance: offsets, detours, predicted positions, who holds whom up
    Placement,   // every volume the placement rules refuse, by rule
    Count
};
inline constexpr int debugLayerCount = int(DebugLayer::Count);
struct DebugLegend {
    std::string label;
    Rgba color;
};
struct DebugLayerInfo {
    std::string id, label; // id: the tool's submode ("collision"); label: the HUD name
    std::vector<DebugLegend> legend;
};
const DebugLayerInfo &debugLayerInfo(int layer); // clamped
int debugLayerStep(int layer, int steps);        // wraps both ways

// One coloured part of an item: lines, convex polygons (filled at `fill` alpha; also picked
// when pickFaces) and its own dash style.
struct DebugShape {
    Rgba color;
    std::vector<std::pair<Vec3, Vec3>> edges;
    std::vector<std::vector<Vec3>> faces;
    float fill = 0;
    bool dashed = false, pickFaces = false, halo = true;
};
// One thing in a layer and what its info card says.
struct DebugItem {
    std::string id, type;
    std::string owner, area;   // the object/area/character it belongs to; the area it is in
    std::string rule, blocks;  // the placement rule it enforces and its refusal ("" : none)
    std::vector<std::string> details;
    Rgba color;                // its legend colour
    std::vector<DebugShape> shapes;
    std::optional<Box> volume; // picked by entering it (or, holding the eye, as a last resort)
};
struct DebugWindow {
    std::string id, title, appClass;
    Box box;           // the window's plane with its placement margin
    bool free = true;  // in the world on its own (not on a wall or a mount): rule 4 applies
    std::string mount; // the anchor it sits on, if any
};
struct DebugCharacter {
    std::string id, name, mode;
    bool companion = false;
    Vec3 position{}, velocity{};
    float heading = 0, radius = .35f, height = 1.75f;
    float lateral = 0, steerLateral = 0, steerFactor = 1, speed = 0, blockedFor = 0;
    std::vector<Vec3> route; // the route around it, sampled (empty: standing)
    std::optional<Vec3> routePoint; // where on the route it is (its offset is measured from here)
    std::vector<std::vector<Vec3>> detours; // legs of each planned detour
    int activeDetour = -1;
    std::vector<Vec3> legs; // straight legs (seat approach, guide join)
    std::string blockedBy;  // the character holding it up
    std::optional<Vec3> blocker;
};
struct DebugSeat {
    std::string id, owner; // world seats[] id or "<object>/<anchor>"; owner "" for world seats
    Vec3 position{};
    float yaw = 0, height = .45f, approach = .6f;
    std::string takenBy;
};
struct DebugInputs {
    const world::World *world = nullptr;
    Vec3 eye{};
    std::vector<DebugWindow> windows;
    Json paths = Json::array(); // the editor overlay's paths (every world; this world's are drawn)
    std::vector<DebugCharacter> characters;
    std::vector<DebugSeat> seats;
    std::span<const uint8_t> collisionKinds; // classifyCollision(world), else every wall is "wall"
    std::vector<std::pair<std::string, Box>> keepClear; // development keepClear volumes
    float collisionRange = 30;                          // collision drawn this close to the eye
    size_t collisionEdges = 6000;                       // and at most this many edges
};
// Collision triangle kinds (DebugLayer::Collision).
enum CollisionKind : uint8_t { Floor, Wall, Backstop, ObjectProxy };
// Per collision triangle: a floor (walkable slope), an object's proxy, a wall with visible
// geometry at it, else an invisible wall (a backstop). One short ray per wall triangle.
std::vector<uint8_t> classifyCollision(const world::World &);
std::vector<DebugItem> debugItems(DebugLayer, const DebugInputs &);
// The area (manifest zone) a point is in: its id, else "".
std::string debugArea(const world::World &, Vec3);

// The item under output pixel (x, y): the nearest one along the ray that it enters (a volume
// or a picked face) or passes within a few pixels of (lines); a volume holding the eye only
// when nothing else is under the pointer, the smallest first.
struct DebugPick {
    int item = -1;
    float distance = 0;
    Vec3 point{};
};
std::optional<DebugPick> pickDebugItem(std::span<const DebugItem>, const GizmoView &, float x, float y);
// The info card: id, type, owner and area, whether (and why) it blocks placement, details.
std::vector<std::string> debugCard(const DebugItem &);
// Line width in output pixels: 3 px at 1080p, scaled with the output.
float debugLineWidth(const GizmoView &);
// The layer as gizmo-pass primitives (thick anti-aliased strokes, light fills, dimmed and
// dashed where world geometry is in front); `selected` is drawn wider and brighter.
GizmoFrame debugFrame(std::span<const DebugItem>, const GizmoView &, int selected = -1);
} // namespace hyprune::interaction
