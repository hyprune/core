// Tool 9, the debug tool: layer cycling, the slot definitions, items and their info cards,
// picking, the drawn frame (line widths, near clipping), collision kinds and the keymap as
// peers before IPC 0.26 see it.
#include "interaction/DebugTool.hpp"
#include "interaction/Input.hpp"
#include "interaction/Tools.hpp"
#undef NDEBUG
#include <cassert>
#include <iostream>
#include <set>
using namespace hyprune;
using namespace hyprune::interaction;
namespace {
GizmoView view(Vec3 eye, float yaw, float pitch = 0, float width = 1920, float height = 1080) {
    GizmoView v;
    v.camera.position = eye;
    v.camera.yaw = yaw;
    v.camera.pitch = pitch;
    v.camera.fov = 60;
    v.width = width;
    v.height = height;
    return v;
}
bool has(const std::vector<std::string> &lines, const std::string &text) {
    for (auto &l : lines)
        if (l.find(text) != std::string::npos)
            return true;
    return false;
}
// A small world: one area with a window zone, a keepClear volume, a wall anchor, a seat,
// an authored path with a stop, two visibility regions and the area's playable box.
std::shared_ptr<world::World> sample() {
    auto w = std::make_shared<world::World>();
    w->id = "org.hyprune.debug-test";
    w->manifest = Json::parse(R"({
      "zones": [{"id": "sun-court", "name": "Sun Court", "bounds": {"min": [-36, -1, -26], "max": [26, 18, 16]}}],
      "playable": {"maxFlyHeight": 20},
      "visibility": {"areas": [
        {"id": "court", "bounds": {"min": [-10, 0, -10], "max": [10, 5, 20]}, "visibleFrom": ["court"], "nodes": []},
        {"id": "hall", "bounds": {"min": [-10, 0, -40], "max": [10, 5, -10]}, "visibleFrom": ["hall"], "nodes": []}]}
    })");
    w->areas = Json::parse(R"([{"id": "sun-court", "primaryAnchor": "sun-court-wall", "freePlacementZones": ["sun-court-open-work"],
                                "playable": {"volumes": [{"type": "box", "min": [-24, -2, -27], "max": [26, 60, 16]}]}}])");
    w->placementZones = Json::parse(R"([{"id": "sun-court-open-work", "bounds": {"min": [-2, 0.15, 0], "max": [7, 4.8, 8]},
                                         "windowSizes": [[1.6, 0.9], [3.2, 1.8]], "clearance": 0.15}])");
    w->keepClear = Json::parse(R"([{"id": "sun-court-hall-door", "min": [0.5, 0, -26.5], "max": [15.5, 4.4, -22.5]}])");
    w->anchors = Json::parse(R"([{"id": "sun-court-wall", "pose": {"position": [8, 3, -23], "rotation": [0, 0, 0, 1]},
                                  "size": [8, 4.5], "mount": {"type": "facade-wall", "role": "mounted-workspace", "interaction": "focus-in"}}])");
    w->seats = Json::parse(R"([{"id": "bench-west", "height": 0.4, "approach": 0.6, "pose": {"position": [19, 0, -12], "rotation": [0, 0, 0, 1]}}])");
    w->paths = Json::parse(R"([{"id": "stroll", "name": "Stroll", "use": "route", "points": [
        {"position": [-6, 0, 0], "type": "line"}, {"position": [6, 0, -1], "type": "curve"}, {"position": [14, 0, -2], "type": "line"}],
        "stops": [{"point": 1, "seconds": 5, "clip": "look_around"}]}])");
    w->visibility = {{{-10, 0, -10}, {10, 5, 20}, {0}}, {{-10, 0, -40}, {10, 5, -10}, {1}}};
    w->nodeParents = {-1, -1, -1};
    w->nodeVisibility = {-1, 1, -1}; // the hall owns a node; the court is a viewpoint only
    // A floor, a wall with a visible face just in front of it and an invisible wall.
    w->collision = {{{-20, 0, -20}, {20, 0, -20}, {20, 0, 20}},          // floor (up)
                    {{-20, 0, -20}, {20, 0, 20}, {-20, 0, 20}},          // floor
                    {{-5, 0, -15}, {5, 0, -15}, {5, 3, -15}},            // wall, seen
                    {{15, 0, -5}, {15, 0, 5}, {15, 3, 5}}};              // backstop: nothing drawn there
    w->collisionNodes = {0, 0, 1, 2};
    w->nodeNames = {"Ground", "HallWall", "EdgeStop"};
    w->collisionIndex.rebuild(w->collision);
    w->visualTriangles = {{{-5, 0, -14.95f}, {5, 0, -14.95f}, {5, 3, -14.95f}}, {{-20, 0, -20}, {20, 0, -20}, {20, 0, 20}}};
    w->visualIndex.rebuild(w->visualTriangles);
    return w;
}
void slotsAndCycling() {
    Tools t;
    assert(t.definitions().size() == 5); // the five configurable slots stay five on the wire
    t.slot = Tools::debugSlot;
    assert(t.slotAction("primary") == "debug.inspect");
    assert(t.slotAction("secondary") == "debug.clear");
    assert(t.slotAction("alternate").empty());
    assert(t.slotAction("cycle") == "tool.cycle");
    assert(t.debugDefinition()["name"] == "Debug" && t.debugDefinition()["slot"] == 9);
    // Cycle steps through every layer once and wraps both ways; the submode names the layer.
    std::set<std::string> seen;
    for (int i = 0; i < debugLayerCount; ++i) {
        assert(t.debugLayer == i);
        seen.insert(t.submode());
        assert(t.submode() == debugLayerInfo(i).id);
        assert(!debugLayerInfo(i).label.empty() && !debugLayerInfo(i).legend.empty());
        t.wheel(1);
    }
    assert(t.debugLayer == 0 && seen.size() == size_t(debugLayerCount));
    t.wheel(-1);
    assert(t.debugLayer == debugLayerCount - 1 && t.submode() == "placement");
    t.wheel(3);
    assert(t.debugLayer == 2 && t.submode() == "collision");
    // Other tools are untouched by the layer.
    t.slot = 4;
    t.wheel(1);
    assert(t.debugLayer == 2 && t.submode() == "rotate");
    t.slot = 1;
    assert(t.slotAction("primary") == "client.primary");
    assert(debugLayerStep(0, -1) == debugLayerCount - 1 && debugLayerStep(debugLayerCount - 1, 1) == 0);
    for (auto id : {"windows", "window-zones", "collision", "playable", "paths", "keep-clear", "seats", "visibility", "steering", "placement"})
        assert(seen.contains(id));
}
void windowZoneCard() {
    auto w = sample();
    DebugInputs in;
    in.world = w.get();
    in.eye = {2.5f, 1.7f, 14};
    auto items = debugItems(DebugLayer::WindowZones, in);
    assert(items.size() == 1);
    // From outside, looking at the zone (yaw 0 looks down -Z): the zone is picked and named.
    auto v = view(in.eye, 0);
    auto pick = pickDebugItem(items, v, 960, 540);
    assert(pick && pick->item == 0 && std::abs(pick->distance - 6) < .01f);
    auto card = debugCard(items[0]);
    assert(card[0] == "sun-court-open-work");
    assert(has(card, "Window zone"));
    assert(has(card, "Owner: sun-court") && has(card, "Area: sun-court"));
    assert(has(card, "Blocks placement: Inside a window zone (rule zone)"));
    assert(has(card, "Window sizes 1.6 x 0.9, 3.2 x 1.8 m"));
    // Looking away: nothing.
    assert(!pickDebugItem(items, view(in.eye, 3.14159f), 960, 540));
    // The same zone in the placement layer, beside the keepClear volume and the wall's approach.
    auto rules = debugItems(DebugLayer::Placement, in);
    bool zone = false, keep = false, approach = false;
    for (auto &i : rules) {
        zone |= i.id == "sun-court-open-work" && i.rule == "zone";
        keep |= i.id == "sun-court-hall-door" && i.blocks == "Blocks a way";
        approach |= i.id == "sun-court-wall approach" && i.blocks == "Blocks a screen approach" && i.owner == "sun-court-wall";
    }
    assert(zone && keep && approach);
}
void picking() {
    auto w = sample();
    DebugInputs in;
    in.world = w.get();
    in.eye = {0, 1.7f, 5};
    // Inside both visibility regions' neighbour: the eye's region holds it; the hall is entered
    // straight ahead and wins over the region holding the eye.
    auto items = debugItems(DebugLayer::Visibility, in);
    assert(items.size() == 2 && items[0].type.find("eye is here") != std::string::npos);
    assert(items[1].type.find("culled from here") != std::string::npos && items[1].owner == "1 scene nodes");
    auto ahead = pickDebugItem(items, view(in.eye, 0), 960, 540);
    assert(ahead && items[ahead->item].id == "hall" && std::abs(ahead->distance - 15) < .01f);
    // Looking back, nothing is entered: the region holding the eye answers.
    auto back = pickDebugItem(items, view(in.eye, 3.14159f), 960, 540);
    assert(back && items[back->item].id == "court");
    // Lines are picked within a few pixels: the path's segment under the reticle.
    auto paths = debugItems(DebugLayer::Paths, in);
    assert(paths.size() == 1 && paths[0].id == "stroll" && has(debugCard(paths[0]), "Stop at point 1: 5 s, look_around"));
    auto down = view({-6, 4, .05f}, 0, -1.55f); // above the first point
    auto onPath = pickDebugItem(paths, down, 960, 540);
    assert(onPath && paths[onPath->item].id == "stroll");
    assert(!pickDebugItem(paths, down, 960 + 300, 540 + 200));
    // Seats and the playable volumes with the fly ceiling.
    auto seats = DebugInputs{in};
    seats.seats = {{"bench-west", "", {19, 0, -12}, 0, .4f, .6f, ""}};
    auto seatItems = debugItems(DebugLayer::Seats, seats);
    assert(seatItems.size() == 1 && has(debugCard(seatItems[0]), "Height 0.40 m, approach 0.60 m"));
    auto playable = debugItems(DebugLayer::Playable, in);
    bool ceiling = false, volume = false;
    for (auto &i : playable) {
        ceiling |= i.id == "fly-ceiling" && has(i.details, "maxFlyHeight 20.00 m");
        volume |= i.owner == "sun-court" && i.volume && std::abs(i.volume->high().y - 20) < 1e-3f; // clipped at the ceiling
    }
    assert(ceiling && volume);
}
void collisionKinds() {
    auto w = sample();
    auto kinds = classifyCollision(*w);
    assert(kinds.size() == 4);
    assert(kinds[0] == Floor && kinds[1] == Floor && kinds[2] == Wall && kinds[3] == Backstop);
    DebugInputs in;
    in.world = w.get();
    in.eye = {0, 1.7f, 0};
    in.collisionKinds = kinds;
    auto items = debugItems(DebugLayer::Collision, in);
    bool floor = false, wall = false, backstop = false;
    for (auto &i : items) {
        floor |= i.id == "Ground" && i.type == "Collision: floor";
        wall |= i.id == "HallWall" && i.type == "Collision: wall";
        backstop |= i.id == "EdgeStop" && i.type.find("backstop") != std::string::npos && i.shapes[0].fill > 0;
        assert(i.rule == "walls" && i.blocks == "Inside a wall");
    }
    assert(floor && wall && backstop);
    // The floor's shared diagonal is not drawn: four outline edges.
    for (auto &i : items)
        if (i.id == "Ground")
            assert(i.shapes[0].edges.size() == 4 && i.shapes[0].faces.size() == 2);
    // Aiming at the invisible wall picks it by its face.
    auto pick = pickDebugItem(items, view({10, 1.5f, 0}, 1.5708f), 960, 540);
    assert(pick && items[pick->item].id == "EdgeStop" && std::abs(pick->distance - 5) < .01f);
    // The edge budget bounds the layer.
    in.collisionEdges = 3;
    size_t edges = 0;
    for (auto &i : debugItems(DebugLayer::Collision, in))
        edges += i.shapes[0].faces.size();
    assert(edges <= 2);
}
void frame() {
    auto w = sample();
    DebugInputs in;
    in.world = w.get();
    in.eye = {2.5f, 1.7f, 14};
    auto items = debugItems(DebugLayer::WindowZones, in);
    auto v = view(in.eye, 0);
    auto f = debugFrame(items, v);
    assert(!f.strokes.empty() && !f.fills.empty());
    for (auto &s : f.strokes)
        assert(std::abs(s.width - 3) < 1e-4f && s.occlude && s.a.depth > 0 && s.b.depth > 0);
    assert(std::abs(debugLineWidth(v) - 3) < 1e-4f);
    // Scaled with the output: 6 px at 2160p; the selection is wider and drawn last.
    auto big = view(in.eye, 0, 0, 3840, 2160);
    assert(std::abs(debugLineWidth(big) - 6) < 1e-4f);
    auto chosen = debugFrame(items, big, 0);
    assert(std::abs(chosen.strokes.back().width - 6 * 1.7f) < 1e-3f);
    for (auto &fill : chosen.fills)
        for (auto &s : chosen.strokes)
            assert(fill.sequence < s.sequence);
    // Standing inside the zone: edges crossing the eye plane are clipped, never projected behind,
    // and its fill (which would tint the whole view) is left out.
    auto inside = view({2.5f, 1.7f, 4}, 0);
    auto within = debugFrame(items, inside);
    assert(!within.strokes.empty() && within.fills.empty());
    for (auto &s : within.strokes)
        assert(s.a.depth >= .079f && s.b.depth >= .079f);
}
void keymap() {
    auto defaults = inputDefaults();
    assert(defaults["modes"]["world"]["tool9"] == Json::array({"9"}));
    // An input file from before tool 9 gains it, unless 9 already does something.
    auto old = defaults;
    old["modes"]["world"].erase("tool9");
    assert(validateInput(old)["modes"]["world"]["tool9"] == Json::array({"9"}));
    old["modes"]["world"]["tool5"] = Json::array({"9"});
    assert(validateInput(old)["modes"]["world"]["tool9"] == Json::array());
    // Peers before IPC 0.26: no tool9 action, conflict or collision naming it.
    Json keymap = {{"config", defaults},
                   {"conflicts", Json::array({{{"mode", "world"}, {"binding", "9"}, {"actions", {"tool5", "tool9"}}},
                                              {{"mode", "world"}, {"binding", "x"}, {"actions", {"autopilot", "tool5"}}}})},
                   {"steals", Json::array({{{"mode", "world"}, {"action", "tool9"}, {"binding", "9"}},
                                           {{"mode", "world"}, {"action", "tool5"}, {"binding", "5"}}})}};
    auto older = withoutDebugTool(keymap);
    assert(!older["config"]["modes"]["world"].contains("tool9") && older["config"]["modes"]["world"].contains("tool5"));
    assert(older["conflicts"].size() == 1 && older["steals"].size() == 1 && older["steals"][0]["action"] == "tool5");
    assert(older.dump().find("tool9") == std::string::npos);
}
} // namespace
int main() {
    slotsAndCycling();
    windowZoneCard();
    picking();
    collisionKinds();
    frame();
    keymap();
    std::cout << "debug tool tests passed\n";
}
