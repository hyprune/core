#include "interaction/Content.hpp"
#include "interaction/Tools.hpp"
#include "surfaces/Layout.hpp"
#include "surfaces/Stacking.hpp"
#undef NDEBUG
#include <cassert>
#include <iostream>
using namespace hyprune;
namespace {
// Two floor triangles spanning [x0,x1] x [z0,z1] at height y, facing up.
void floorQuad(world::World &w, float x0, float x1, float z0, float z1, float y) {
    w.collision.push_back({{x0, y, z0}, {x0, y, z1}, {x1, y, z1}});
    w.collision.push_back({{x0, y, z0}, {x1, y, z1}, {x1, y, z0}});
}
// The pose the capture adapter hands to spawn: the configured distance along the view.
surfaces::Surface ahead(const sim::Camera &camera, int pw, int ph, float distance = 5) {
    surfaces::Surface s;
    s.id = "spawned";
    s.logicalSize(pw, ph, .002f);
    s.yaw = -camera.yaw;
    s.center = camera.position + camera.forward() * distance;
    return s;
}
float floorGap(const surfaces::Surface &s, float floor) {
    return s.center.y - (std::abs(s.up().y) * s.height + std::abs(s.right().y) * s.width) * .5f - floor;
}
// A new room window lands in front of the player on floor it can be used from, never in
// mid-air in a far corner of the area's free zone (live take: a kitty opened from the
// Sun Court plaza appeared 13 m away, its lower edge 3.2 m above the terrace).
void spawnReachability() {
    world::World court;
    court.id = "org.hyprune.test.court";
    floorQuad(court, -24, 26, -27, 16, 0);
    court.spawns = {{"court", {0, 0, 10}, 0, 0}};
    court.areas = world::Json::array({{{"id", "court"}, {"workspaceSlot", "01"}, {"spawnId", "court"},
                                       {"freePlacementZones", {"court-open-work"}}}});
    court.placementZones = world::Json::array({{{"id", "court-open-work"},
                                                {"bounds", {{"min", {-2, .15, 0}}, {"max", {7, 4.8, 8}}}},
                                                {"clearance", .15},
                                                {"windowSizes", {{1.6, .9}, {3.2, 1.8}}}}});
    sim::Camera camera;
    camera.position = {-13.23f, 1.7f, 11.65f}; // the capture's pose
    camera.yaw = .927f;
    camera.pitch = .0407f;
    surfaces::Instance kitty{"kitty", "kitty", "~", 20};
    surfaces::Layout layout;
    assert(layout.spawn(kitty, &court, camera, ahead(camera, 1200, 675), 16.f / 9));
    auto placed = layout.freePoses.at(kitty.id);
    assert(length(placed.center - camera.position) < 5.5f); // in front of the player, not the zone
    assert(dot(placed.center - camera.position, camera.forward()) > 1);
    assert(floorGap(placed, 0) >= 0 && floorGap(placed, 0) <= surfaces::MaxSpawnLift);
    assert(surfaces::reachable(placed, court) && layout.spawnAreas.at(kitty.id) == "court");

    // At a terrace edge with only a drop ahead and no free zone: queue, never float.
    world::World ledge;
    ledge.id = "org.hyprune.test.ledge";
    floorQuad(ledge, -24, 26, 0, 16, 0);
    floorQuad(ledge, -60, 60, -80, 0, -9); // the valley far below
    camera = {};
    camera.position = {0, 1.7f, .6f};
    surfaces::Layout edge;
    assert(!edge.spawn(kitty, &ledge, camera, ahead(camera, 1200, 675), 16.f / 9));
    assert(edge.unplaced.contains(kitty.id) && !edge.freePoses.contains(kitty.id));

    // A playable volume bounds the floor too: a ledge outside it is no place to stand.
    world::World fenced = court;
    fenced.playable = world::loadPlayable({{"playable", {{"volumes", {{{"type", "box"}, {"min", {-24, -20, -27}},
                                                                       {"max", {-14, 60, 16}}}}}}}});
    camera.position = {-15, 1.7f, 10};
    camera.yaw = 1.5708f; // facing +X, out of the volume
    surfaces::Layout outside;
    outside.spawn(kitty, &fenced, camera, ahead(camera, 1200, 675), 16.f / 9);
    assert(!outside.freePoses.contains(kitty.id) || fenced.playable.contains(outside.freePoses.at(kitty.id).center));

    // The zone is the fallback when the view ahead is blocked; its window rests at the
    // player's viewing height above the zone floor, not stacked up its full height.
    world::World blocked = court;
    blocked.collision.push_back({{-50, 0, -5.2f}, {50, 0, -5.2f}, {50, 10, -5.2f}});
    blocked.collision.push_back({{-50, 0, -5.2f}, {50, 10, -5.2f}, {-50, 10, -5.2f}});
    blocked.placementZones[0]["bounds"] = {{"min", {-3, .15, -4.6}}, {"max", {3, 4.8, -1}}};
    camera = {};
    camera.position = {0, 1.7f, 4};
    auto against = ahead(camera, 1200, 675, 9.2f); // in the wall: every column ahead overlaps it
    surfaces::Layout zoned;
    assert(zoned.spawn(kitty, &blocked, camera, against, 16.f / 9));
    placed = zoned.freePoses.at(kitty.id);
    assert(placed.center.z > -4.6f && placed.center.z < -1 && std::abs(placed.center.y - 1.7f) < .3f);
    assert(surfaces::reachable(placed, blocked) && !surfaces::overlaps(placed, blocked.collision));
    // A second arrival takes the next free column, never a row stacked above the first.
    surfaces::Instance second{"second", "kitty", "~", 20};
    if (zoned.spawn(second, &blocked, camera, against, 16.f / 9))
        assert(std::abs(zoned.freePoses.at(second.id).center.y - placed.center.y) < .3f);
}
// Home workspace windows are tiled windows of that workspace, on its wall, however they
// got there (opened there, opened in focus, moved there). Only the room floats: a home
// window the user moves into the world goes to the room and returns to its own wall.
void homeWindows() {
    world::World w;
    w.id = "org.hyprune.test.homes";
    auto tool = [](const char *id, float x) {
        return world::Json{{"id", id}, {"areaId", "court"}, {"size", {2.4, 1.35}},
                           {"pose", {{"position", {x, 1.5, -4}}, {"rotation", {0, 0, 0, 1}}}},
                           {"mount", {{"type", "operator-desk"}, {"role", "tool-mount"}, {"interaction", "in-place"}}}};
    };
    w.anchors = world::Json::array(
        {{{"id", "wall"}, {"areaId", "court"}, {"size", {16, 9}},
          {"pose", {{"position", {0, 6, -20}}, {"rotation", {0, 0, 0, 1}}}},
          {"mount", {{"type", "facade-wall"}, {"role", "mounted-workspace"}, {"interaction", "focus-in"}}}},
         tool("desk-b", 6), tool("desk-a", -6), tool("rig", 0)});
    // Authored order differs from the manifest's anchor order on purpose.
    w.areas = world::Json::array({{{"id", "court"}, {"workspaceSlot", "01"}, {"primaryAnchor", "wall"},
                                   {"toolAnchors", {"desk-a", "desk-b", "rig"}}}});
    sim::Camera camera;
    camera.position = {0, 1.7f, 4};
    surfaces::Layout layout;
    layout.workspaceBase = 20;
    layout.deriveDefaults(&w);
    surfaces::Instance desk{"desk", "kitty", "Desk", 21}, kiosk{"kiosk", "kitty", "Kiosk", 21},
        signal{"signal", "kitty", "Signal", 21};
    auto refresh = [&](std::vector<surfaces::Instance *> order) {
        layout.instances = world::Json::array();
        std::map<std::string, std::string> at;
        for (auto *i : order)
            if (layout.locate(*i, &w, camera, 1200, 675))
                at[i->id] = layout.instances.back()["anchorId"];
        return at;
    };
    surfaces::Instance room{"room-kiosk", "kitty", "Kiosk", 20};
    layout.placeOnMount(room, w.anchors[2], &w); // a durable rule for a room window titled Kiosk
    layout.instances = world::Json::array();
    for (int k = 0; k < 3; ++k) // any compositor order: every home window stays on the wall
        assert(refresh({&desk, &kiosk, &signal}).empty() && refresh({&signal, &kiosk, &desk}).empty());
    for (auto *i : {&desk, &kiosk, &signal})
        assert(!layout.pulled(*i, &w) && !layout.leaving(*i));
    assert(layout.events.empty());
    // Pulled off the wall (carry / Director): it leaves for the room, then places there.
    layout.pull(kiosk, &w);
    assert(layout.leaving(kiosk) && layout.pulled(kiosk, &w) && layout.homes.at(kiosk.id) == 21);
    kiosk.workspaceId = 20; // moved natively to the room
    auto anchor = layout.place(kiosk, "desk-b", &w);
    assert(anchor == "desk-b" && layout.homeOf(kiosk, &w) && layout.homeOf(kiosk, &w)->at("id") == 21);
    auto at = refresh({&desk, &kiosk, &signal});
    assert(at.size() == 1 && at.at("kiosk") == "desk-b" && layout.pulled(kiosk, &w));
    // Returned: back to its own workspace, on the wall again.
    assert(layout.release(kiosk, &w) == 21 && !layout.homes.contains(kiosk.id));
    kiosk.workspaceId = 21;
    assert(refresh({&desk, &kiosk, &signal}).empty() && !layout.pulled(kiosk, &w));
    // A relaunched window restored by a durable rule in the room belongs to that rule's area:
    // it can be placed again and returns to that area's home.
    surfaces::Instance before{"before", "kitty", "Relaunched", 20}, relaunched{"relaunched", "kitty", "Relaunched", 20};
    layout.placeOnMount(before, w.anchors[3], &w); // its durable rule, saved before it closed
    layout.forget(before.id);
    layout.instances = world::Json::array();
    assert(layout.homeOf(relaunched, &w) && layout.homeOf(relaunched, &w)->at("id") == 21);
    assert(layout.place(relaunched, "rig", &w) == "rig" && layout.release(relaunched, &w) == 21);
    // A free room mount (tool 5 in free space) records an area for the wire, but no home:
    // relaunched from its durable rule it is an unzoned room window again (no areaId).
    surfaces::Instance loose{"loose", "foot", "Loose", 20}, again{"again", "foot", "Loose", 20};
    surfaces::Surface spot;
    spot.logicalSize(1200, 675, .002f);
    spot.center = {3, 1.7f, 2};
    layout.mount(loose, &w, spot);
    layout.forget(loose.id);
    layout.instances = world::Json::array();
    assert(!layout.homeOf(again, &w));
    assert(layout.locate(again, &w, camera, 1200, 675) && !layout.instances.back().contains("areaId"));
    // Pocketing a home window takes it into the world too.
    layout.stash(signal, {});
    assert(layout.leaving(signal) && layout.pulled(signal, &w));
    // A free room window moved onto a home workspace is a tiled window there.
    surfaces::Instance free{"free", "foot", "Free", 20};
    surfaces::Surface pose;
    pose.logicalSize(1200, 675, .002f);
    pose.center = {0, 1.7f, 0};
    assert(layout.spawn(free, &w, camera, pose, 16.f / 9) && layout.locate(free, &w, camera, 1200, 675));
    free.workspaceId = 21;
    layout.instances = world::Json::array();
    assert(!layout.locate(free, &w, camera, 1200, 675) && !layout.freePoses.contains(free.id));

    // Two live clients matching one rule share no target and raise no relocation notice.
    surfaces::Layout twins;
    twins.workspaceBase = 20;
    twins.deriveDefaults(&w);
    surfaces::Instance one{"one", "kitty", "~", 20}, two{"two", "kitty", "~", 20};
    twins.placeOnMount(one, w.anchors[2], &w);
    std::map<std::string, std::string> seen;
    for (auto order : {std::vector{&one, &two}, std::vector{&two, &one}, std::vector{&one, &two}}) {
        twins.instances = world::Json::array();
        for (auto *i : order)
            if (twins.locate(*i, &w, camera, 1200, 675)) {
                std::string anchor = twins.instances.back()["anchorId"];
                assert(!seen.contains(i->id) || seen[i->id] == anchor);
                seen[i->id] = anchor;
            }
    }
    assert(seen.at("one") != seen.at("two") && twins.events.empty());
}
void wallPick() {
    std::vector<surfaces::NativeSlot> m3a{{1612, 12, 941, 1056, 0, 0},  // subsurface lab, tiled
                                          {2567, 12, 941, 1056, 0, 3},  // X11 lab, tiled, captured last
                                          {2108, 258, 900, 560, 1, 1},  // popup lab, floating
                                          {2296, 291, 528, 498, 1, 2}}; // terminal, floating, raised last
    assert(surfaces::topmostAt(m3a, 2617, 540) == 3u);
    assert(surfaces::topmostAt(m3a, 3100, 540) == 1u); // only the X11 lab there
    assert(surfaces::topmostAt(m3a, 1700, 40) == 0u && !surfaces::topmostAt(m3a, 100, 40));
    m3a[1].layer = 3; // fullscreen covers everything
    assert(surfaces::topmostAt(m3a, 2617, 540) == 1u);
    m3a[1].layer = 0;
    m3a[2].order = 9; // a focus raise reorders the floating layer
    assert(surfaces::topmostAt(m3a, 2617, 540) == 2u);
    assert(!surfaces::topmostAt(m3a, 2567 + 941, 540)); // boxes are half-open: past the right edge
}
} // namespace
int main() {
    world::World world;
    world.id = "org.hyprune.test";
    sim::Camera camera;
    camera.position = {0, 2, 0};
    surfaces::Layout layout;
    surfaces::Instance a{"a", "kitty", "First", 20}, b{"b", "kitty", "Second", 20};
    surfaces::Surface s;
    s.id = a.id;
    s.center = {0, 2, -2};
    s.logicalSize(1800, 1000, .002f);
    assert(layout.spawn(a, &world, camera, s, 1));
    assert(layout.locate(a, &world, camera, 1800, 1000));
    assert(!layout.instances.back().contains("areaId")); // Unzoned after warp: omit, never empty.
    s.id = b.id;
    assert(!layout.spawn(b, &world, camera, s, 1));
    assert(layout.unplaced.contains(b.id) && !layout.locate(b, &world, camera, 1800, 1000));
    auto at = layout.freePoses[a.id];
    layout.stash(a, at);
    assert(layout.unplaced.contains(a.id) && layout.pocketOrder.front() == a.id);
    assert(!layout.locate(a, &world, camera, 1800, 1000));
    assert(layout.vacant(at, a.id));
    layout.mount(a, &world, at);
    assert(!layout.unplaced.contains(a.id) && layout.hasPlacement(a, &world));
    auto sizing = world::Json{{"baseSize", {1800, 1000}}, {"logicalSize", {900, 500}}, {"worldSize", {3.6, 2.0}}};
    layout.setContent(a, &world, sizing);
    layout.mount(a, &world, at);
    surfaces::Layout restored;
    restored.restore({{"bindings", layout.bindings}, {"placements", layout.placements}});
    assert(restored.contentFor(a, &world) && *restored.contentFor(a, &world) == sizing);
    auto scaled = restored.locate(a, &world, camera, 900, 500);
    assert(scaled && std::abs(scaled->width - 3.6f) < .001f && std::abs(scaled->height - 2) < .001f);
    a.workspaceId = 1;
    assert(!restored.hasPlacement(a, &world)); // no implicit adoption on desktop workspaces
    a.workspaceId = 20;
    layout.stash(a, at);
    layout.stash(b, at);
    assert(layout.pocketOrder[0] == b.id && layout.pocketOrder[1] == a.id);
    layout.stash(a, at);
    assert(layout.pocketOrder.size() == 2 && layout.pocketOrder[0] == a.id);
    interaction::Hands hands;
    at.yaw = .6f;
    at.rotation = interaction::orientation({.3f, .2f, 1.f});
    hands.begin(interaction::Gesture::Carry, at, {2, .7f, .3f}, camera);
    auto grab = hands.pose.center + hands.offset;
    hands.facing(true, camera);
    assert(length(hands.pose.normal() - Vec3{0, 0, 1}) < .001f);
    assert(length(hands.pose.center + hands.offset - grab) < .001f);
    camera.yaw = .4f;
    camera.pitch = .5f;
    assert(hands.update(camera, camera.forward(), .1, {}));
    assert(std::abs(hands.pose.normal().y) < .001f);
    hands.facing(false, camera);
    auto q = hands.pose.rotation;
    camera.yaw = .9f;
    hands.update(camera, camera.forward(), .1, {});
    assert(hands.pose.rotation == q);
    hands.facing(true, camera);
    assert(dot(hands.pose.normal(), {std::sin(-camera.yaw), 0, std::cos(camera.yaw)}) > .999f);
    interaction::Tools tools;
    for (int slot = 1; slot <= 5; ++slot) {
        tools.slot = slot;
        auto declared = tools.slots();
        assert(declared.size() == 4);
        for (auto key : {"primary", "secondary", "alternate", "cycle"})
            assert(declared.contains(key));
    }
    tools.slot = 4;
    auto worldHandles = tools.handles({}, {0, .70710678f, 0, .70710678f});
    tools.localAxes = true;
    auto localHandles = tools.handles({}, {0, .70710678f, 0, .70710678f});
    assert(length(worldHandles[0].end - Vec3{1, 0, 0}) < .001f);
    assert(length(localHandles[0].end - Vec3{0, 0, -1}) < .001f);
    {
        // Tool-mount snap (live finding): a screen face at z=0 facing +z, a bezel around
        // it and a hood lip 0.5 m ahead above it. Collision push-out shoves a window
        // off such a face (live: 0.59 m at Lumen Reach's lectern); the mount fit keeps it
        // flush, aspect-fit and centred. The nested check covers the real lectern.
        world::World stand;
        stand.id = "org.hyprune.mounts";
        auto quad = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
            stand.collision.push_back({a, b, c});
            stand.collision.push_back({a, c, d});
        };
        quad({-1, 1, 0}, {1, 1, 0}, {1, 2.2f, 0}, {-1, 2.2f, 0});                // screen glass
        quad({-1.05f, 2.2f, 0}, {1.05f, 2.2f, 0}, {1.05f, 2.2f, .5f}, {-1.05f, 2.2f, .5f}); // hood lip
        quad({-1.05f, 1, 0}, {-1.05f, 2.2f, 0}, {-1.05f, 2.2f, .08f}, {-1.05f, 1, .08f}); // bezel sides
        quad({1.05f, 1, 0}, {1.05f, 1, .08f}, {1.05f, 2.2f, .08f}, {1.05f, 2.2f, 0});
        world::Json mount = {{"id", "stand-screen"},
                             {"areaId", "hall"},
                             {"pose", {{"position", {0, 1.6, 0}}, {"rotation", {0, 0, 0, 1}}}},
                             {"size", {1.8, 1.1}},
                             {"mount", {{"type", "lectern"}, {"role", "tool-mount"}, {"interaction", "in-place"}}}};
        stand.anchors = world::Json::array({mount});
        stand.areas = world::Json::array({{{"id", "hall"}, {"workspaceSlot", "01"}, {"primaryAnchor", "stand-screen"}}});
        auto fitted = surfaces::fitToMount(mount, 1600, 900, stand.collision);
        assert(fitted && std::abs(fitted->center.z - .05f) < .02f && std::abs(fitted->center.x) < .001f);
        assert(std::abs(fitted->width / fitted->height - 16.f / 9) < .01f && fitted->width <= 1.8f + 1e-4f);
        assert(!surfaces::overlaps(*fitted, stand.collision));
        auto portrait = surfaces::fitToMount(mount, 900, 1600, {});
        assert(portrait && std::abs(portrait->height - 1.1f) < .001f && portrait->width < 1.1f); // fits by height
        // A room client (unbound workspace 20) snaps into the frame and stays there.
        surfaces::Layout snap;
        surfaces::Instance room{"room", "kitty", "Room", 20};
        snap.placeOnMount(room, mount, &stand);
        assert(snap.placements.size() == 1 && snap.placements[0]["anchorId"] == "stand-screen" &&
               snap.placements[0]["areaId"] == "hall");
        auto placed = snap.locate(room, &stand, camera, 1600, 900);
        assert(placed && length(placed->center - fitted->center) < .001f);
        assert(snap.instances.back()["anchorId"] == "stand-screen" && snap.instances.back()["areaId"] == "hall");
        surfaces::Instance other{"other", "foot", "Other", 20};
        bool occupied = false;
        try { snap.placeOnMount(other, mount, &stand); } catch (const std::exception &) { occupied = true; }
        assert(occupied);
    }
    {
        // Area-less worlds author room-window docks as anchors (Switchyard). A client already
        // in the room at activation takes its dock, visible; with no dock left it waits in the
        // pocket. It is never hidden at a dock pose. Clients opened later spawn.
        world::World docks;
        auto window = [](float x) {
            return world::Json{{"id", "dock"}, {"screenType", "com.hyprune.window"}, {"size", {4.2, 2.36}},
                               {"pose", {{"position", {x, 2.25, -6.79}}, {"rotation", {0, 0, 0, 1}}}}};
        };
        docks.manifest["anchors"] = {window(-10.7f), window(1.3f),
                                     {{"id", "sign"}, {"screenType", "com.hyprune.panel"}, {"size", {1, 1}},
                                      {"pose", {{"position", {0, 0, 0}}, {"rotation", {0, 0, 0, 1}}}}}};
        using A = surfaces::RoomAdoption;
        assert(surfaces::adoptRoomClient(docks, 0, true, false) == A::Anchor);
        assert(surfaces::adoptRoomClient(docks, 1, true, false) == A::Anchor);
        assert(surfaces::adoptRoomClient(docks, 2, true, false) == A::Pocket); // not a window anchor
        assert(surfaces::adoptRoomClient(docks, 3, true, false) == A::Pocket); // no anchor left
        assert(surfaces::adoptRoomClient(docks, 0, true, true) == A::Pocket);  // explicitly pocketed
        assert(surfaces::adoptRoomClient(docks, 0, false, false) == A::Spawn); // opened after entering
        assert(surfaces::roomAnchor(docks, 1) && !surfaces::roomAnchor(docks, 2) && !surfaces::roomAnchor(docks, 9));
        surfaces::Surface dock;
        dock.pixelWidth = 1200, dock.pixelHeight = 675;
        surfaces::roomAnchorPose(dock, *surfaces::roomAnchor(docks, 1));
        assert(length(dock.center - Vec3{1.3f, 2.25f, -6.79f}) < 1e-4f && std::abs(dock.yaw) < 1e-6f);
        assert(std::abs(dock.width - 4.1955f) < 1e-3f && std::abs(dock.height - 2.36f) < 1e-3f); // aspect-fit
        world::World areas = docks;
        areas.areas = {{{"id", "hall"}}};
        assert(!surfaces::roomAnchor(areas, 0) && surfaces::adoptRoomClient(areas, 0, true, false) == A::Pocket);
        world::World bare;
        assert(!surfaces::roomAnchor(bare, 0) && surfaces::adoptRoomClient(bare, 0, true, false) == A::Pocket);
        // A user workspace screen's home (IPC 0.27) keeps an area-less world's room anchors.
        world::World screened = docks;
        screened.areas = {{{"id", "screen-u-000001"}}};
        screened.screenHomes = 1;
        assert(!screened.authoredHomes() && surfaces::roomAnchor(screened, 1));
        assert(surfaces::adoptRoomClient(screened, 0, true, false) == A::Anchor);
    }
    {
        // Workspace screens bind after the authored homes, on a workspace nobody else holds.
        world::World w;
        w.id = "w";
        w.areas = world::Json::array({{{"id", "court"}, {"workspaceSlot", "01"}}, {{"id", "screen-u-a"}, {"workspaceSlot", "21"}},
                                      {{"id", "screen-u-b"}, {"workspaceSlot", "23"}}, {{"id", "screen-u-c"}, {"workspaceSlot", "24"}}});
        w.screenHomes = 3;
        surfaces::Layout l;
        l.workspaceBase = 20;
        l.reservedWorkspaces = {20};
        l.bindings = world::Json::array({{{"id", 24}, {"worldId", "other"}, {"areaId", "x"}}});
        l.screenHomes = {{21, "screen-u-a"}, {23, "screen-u-b"}, {24, "screen-u-c"}};
        l.deriveDefaults(&w);
        assert(l.binding(21, &w) && l.binding(21, &w)->at("areaId") == "court"); // the authored home keeps 21
        assert(l.binding(23, &w) && l.binding(23, &w)->at("areaId") == "screen-u-b");
        assert((l.screenBound == std::set<std::string>{"screen-u-b"}));        // 24 is an explicit home elsewhere
        assert(l.defaults.size() == 2);                                         // the screen areas derive no slot homes
        l.workspaceBase = 0;
        l.deriveDefaults(&w);
        // Without slot homes (workspace_base 0) screens still bind, and 21 is free for one.
        assert(l.defaults.size() == 2 && l.binding(21, &w)->at("areaId") == "screen-u-a" && l.binding(23, &w));
    }
    {
        // World-owned workspaces: room, homes (explicit and derived), world locations.
        auto owned = surfaces::worldWorkspaces(20, {{{"id", 21}, {"worldId", "w"}, {"areaId", "a"}}, {{"id", 24}}},
                                               {{{"id", 30}, {"label", "Switchyard"}}, {{"id", "x"}}, {{"label", "none"}}});
        assert((owned == std::set<int>{20, 21, 24, 30}));
        assert((surfaces::worldWorkspaces(0, world::Json::array(), world::Json::array()).empty()));
    }
    spawnReachability();
    std::cout << "PASS spawn in front of the player on reachable floor, zone fallback at viewing height\n";
    homeWindows();
    wallPick();
    std::cout << "PASS home windows stay on their wall; pulled ones go to the room and return home\n";
    std::cout << "PASS placement queue, pocket ordering, durable client size, workspace scope and facing toggle\n";
}
