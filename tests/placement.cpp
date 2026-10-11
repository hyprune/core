#include "interaction/Content.hpp"
#include "interaction/Tools.hpp"
#include "surfaces/Layout.hpp"
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
// Windows arriving on a home workspace without a durable rule (opened or moved there):
// the first keeps the primary wall, later ones take the area's tool anchors in authored
// order and keep them, whatever order the compositor lists windows in (live take: moved
// windows got no pose, and a second matching client "relocated" off a free anchor).
void homeArrivals() {
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
    auto first = refresh({&desk, &kiosk, &signal});
    assert(!first.contains("desk") && first.at("kiosk") == "desk-a" && first.at("signal") == "desk-b");
    assert(!layout.pulled(desk, &w) && layout.pulled(kiosk, &w) && layout.walled.at("desk") == 21);
    for (int k = 0; k < 3; ++k) // a focus raise reorders the compositor's list: nobody moves
        assert(refresh({&signal, &kiosk, &desk}) == first && refresh({&kiosk, &desk, &signal}) == first);
    assert(layout.placements.empty() && layout.events.empty()); // runtime only, no notices
    // The wall window closes: the next arrival takes the wall, tools stay where they are.
    layout.forget(desk.id);
    surfaces::Instance late{"late", "foot", "Late", 21};
    auto after = refresh({&late, &kiosk, &signal});
    assert(!after.contains("late") && after.at("kiosk") == "desk-a" && after.at("signal") == "desk-b");
    // A window floated natively on the workspace belongs to its wall's own layout.
    surfaces::Instance floated{"floated", "kitty", "Floated", 21, true};
    after = refresh({&late, &kiosk, &signal, &floated});
    assert(!after.contains("floated") && !layout.pulled(floated, &w) && after.at("signal") == "desk-b");
    // Returned (or dropped) onto the wall: it stays there.
    layout.release(signal, &w);
    after = refresh({&late, &kiosk, &signal});
    assert(!after.contains("signal") && !layout.pulled(signal, &w) && layout.walled.at("signal") == 21);

    // Anchors that durable rules restore to are left for them while others are free.
    surfaces::Layout reserved;
    reserved.workspaceBase = 20;
    reserved.deriveDefaults(&w);
    surfaces::Instance owner{"owner", "btop", "", 20};
    reserved.placeOnMount(owner, w.anchors[2], &w); // desk-a, for the room's btop
    reserved.instances = world::Json::array();
    surfaces::Instance a{"a", "kitty", "A", 21}, b{"b", "kitty", "B", 21};
    reserved.locate(a, &w, camera, 1200, 675);
    assert(reserved.locate(b, &w, camera, 1200, 675) && reserved.instances.back()["anchorId"] == "desk-b");
    assert(reserved.locate(owner, &w, camera, 1200, 675) && reserved.instances.back()["anchorId"] == "desk-a");

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

    // A free room window moved onto a home workspace arrives there: no stale room pose.
    surfaces::Layout moved;
    moved.workspaceBase = 20;
    moved.deriveDefaults(&w);
    surfaces::Instance wall{"walled", "kitty", "Wall", 21}, room{"room", "kitty", "Room", 20};
    moved.locate(wall, &w, camera, 1200, 675);
    surfaces::Surface pose;
    pose.logicalSize(1200, 675, .002f);
    pose.center = {0, 1.7f, 0};
    assert(moved.spawn(room, &w, camera, pose, 16.f / 9) && moved.freePoses.contains(room.id));
    moved.locate(room, &w, camera, 1200, 675);
    room.workspaceId = 21;
    moved.instances = world::Json::array();
    moved.locate(wall, &w, camera, 1200, 675);
    assert(moved.locate(room, &w, camera, 1200, 675) && moved.instances.back()["anchorId"] == "desk-a");
    assert(!moved.freePoses.contains(room.id));
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
    }
    spawnReachability();
    std::cout << "PASS spawn in front of the player on reachable floor, zone fallback at viewing height\n";
    homeArrivals();
    std::cout << "PASS home arrivals: wall first, tool anchors in authored order, stable, no stale poses\n";
    std::cout << "PASS placement queue, pocket ordering, durable client size, workspace scope and facing toggle\n";
}
