#include "interaction/Config.hpp"
#include "hud/Telemetry.hpp"
#include "interaction/InputFile.hpp"
#include "interaction/Resize.hpp"
#include "interaction/Tools.hpp"
#include "render/Graphics.hpp"
#include "render/OverlayStyle.hpp"
#include "sim/Camera.hpp"
#undef NDEBUG
#include <cassert>
#include <iostream>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>
using namespace hyprune;
using namespace interaction;
int main() {
    Input hudInput;
    auto cycle = hudInput.event("world", "5", 0, true, 0);
    assert(cycle.size() == 1 && cycle[0].name == "hud.cycle" && cycle[0].edge);
    auto repeat = hudInput.event("world", "5", 0, true, 10);
    assert(repeat.empty() || !repeat[0].edge);
    hudInput.event("world", "5", 0, false, 20);
    for (auto mode : {"interactive", "focus", "menu"})
        assert(hudInput.event(mode, "5", 0, true, 30).empty());
    auto legacyCycle = legacyInputNames(inputDefaults());
    assert(legacyCycle["modes"]["world"].contains("tool5"));
    assert(validateInput(legacyCycle) == validateInput(inputDefaults()));
    world::World testWorld;
    testWorld.id = "test"; testWorld.name = "Test world";
    testWorld.manifest = {{"zones", {{{"id", "home"}, {"name", "Room"},
        {"bounds", {{"min", {-1, 0, -1}}, {"max", {1, 3, 1}}}}}}}};
    assert(hud::zoneAt(&testWorld, {0, 1, 0})["homeName"] == "Room");
    assert(hud::zoneAt(&testWorld, {2, 1, 0}).is_null());
    assert(hud::zoneAt(nullptr, {}).is_null());
    assert(resizeExtent(800, 160, 640) == 640 && resizeExtent(600, 100, 480) == 480);
    assert(resizeExtent(80, 160, 640) == 160 && resizeExtent(40, 100, 480) == 100);
    assert(resizeExtent(200, 160, 80) == 80 && resizeExtent(150, 100, 60) == 60);
    assert(resizeExtent(200, 160, 0) == 1);

    auto context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_rule_names names{};
    names.layout = "us";
    auto keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    auto keyboard = xkb_state_new(keymap);
    auto shift = 1u << xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
    xkb_state_update_mask(keyboard, shift, 0, 0, 0, 0, 0);
    assert(xkb_state_key_get_one_sym(keyboard, 25) == XKB_KEY_W);
    assert(bindingKey(keyboard, 25) == "w");
    assert(bindingKey(keyboard, 23) == "Tab");
    assert(bindingName(parseBinding("Shift+W")) == "Shift+w");
    Input tabHold;
    xkb_state_update_mask(keyboard, 0, 0, 0, 0, 0, 0);
    tabHold.event("world", bindingKey(keyboard, 23), 0, true, 0);
    xkb_state_update_mask(keyboard, shift, 0, 0, 0, 0, 0);
    tabHold.event("world", bindingKey(keyboard, 23), Shift, false, 500);
    assert(tabHold.tick(1200).empty());
    assert(tabHold.holds().empty());
    tabHold.event("world", "Tab", 0, true, 2000);
    assert(tabHold.holds() ==
           Json::array({{{"action", "menu"}, {"binding", "Tab[L]"}, {"startedAtMs", 2000}, {"durationMs", 1000}}}));
    assert(tabHold.tick(2999).empty() && tabHold.holds().size() == 1);
    assert(tabHold.tick(3000).size() == 1 && tabHold.holds().empty());
    tabHold.clear();
    tabHold.event("world", "Tab", 0, true, 4000);
    tabHold.clear();
    assert(tabHold.holds().empty());
    xkb_state_unref(keyboard);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    auto graphics = configPatch(defaultConfig(), {{"graphics", {{"render_scale", .5}, {"target_fps", 90}}}});
    assert(graphics["graphics"]["render_scale"] == .5);
    assert(configPatch(graphics, {{"graphics", {{"render_scale", "auto"}}}})["graphics"]["render_scale"] == "auto");
    for (auto bad : {world::Json(.1), world::Json(2), world::Json("fast"), world::Json(true)}) {
        bool rejected = false;
        try {
            configPatch(graphics, {{"graphics", {{"render_scale", bad}}}});
        } catch (...) {
            rejected = true;
        }
        assert(rejected);
    }
    auto presets = render::graphicsPresets();
    for (auto &[name, values] : presets.items()) {
        auto cfg = configPatch(defaultConfig(), {{"graphics", {{"quality", name}}}})["graphics"];
        auto effective = render::effectiveGraphics(cfg, "Intel PTL");
        for (auto &[key, value] : values.items())
            assert(effective[key] == value);
    }
    assert(render::effectiveGraphics(render::graphicsDefaults(), "Intel PTL")["quality"] == "low");
    assert(render::effectiveGraphics(render::graphicsDefaults(), "AMD")["quality"] == "medium");
    auto graphicsOptions = render::graphicsOptions();
    for (auto &[key, values] : graphicsOptions.items()) {
        for (auto &value : values)
            configPatch(defaultConfig(), {{"graphics", {{key, value}}}});
        bool rejected = false;
        try {
            configPatch(defaultConfig(), {{"graphics", {{key, "nonsense"}}}});
        } catch (...) {
            rejected = true;
        }
        assert(rejected);
    }
    auto custom = configPatch(defaultConfig(), {{"graphics", {{"quality", "low"}, {"normal_maps", true}}}});
    assert(render::effectiveGraphics(custom["graphics"], "Intel")["normal_maps"] == true);
    custom = configPatch(custom, {{"graphics", {{"normal_maps", nullptr}}}});
    assert(render::effectiveGraphics(custom["graphics"], "Intel")["normal_maps"] == false);
    auto defaults = validateInput(inputDefaults());
    assert(inputConflicts(defaults).empty());
    unsigned checked = 0;
    for (auto &[mode, actions] : defaults["modes"].items())
        for (auto &[action, bindings] : actions.items())
            for (auto &v : bindings) {
                Input input;
                auto t = parseBinding(v);
                auto events = input.event(mode, t.key, t.mods, true, 100);
                assert(events.size() == 1 && events[0].name == action && events[0].pressed == !t.hold);
                if (t.hold) {
                    assert(input.tick(1099).empty());
                    assert(input.tick(1100)[0].name == action);
                    assert(input.tick(1200).empty());
                }
                if (t.key != "Wheel" && t.key != "MouseMove") {
                    auto release = input.event("other", t.key, 0, false, 1300);
                    assert(release.size() == 1 && !release[0].pressed);
                }
                checked++;
            }
    assert(!defaults["invert_y"].get<bool>());
    auto legacy = defaults;
    legacy.erase("invert_y");
    for (auto mode : {"interactive", "focus", "menu"}) {
        legacy["modes"][mode].erase("debug");
        legacy["modes"][mode].erase("capture");
    }
    assert(validateInput(legacy) == defaults);
    for (auto mode : {"interactive", "focus", "menu"}) {
        Input typing;
        assert(typing.event(mode, "u", 0, true, 100).empty());
        assert(typing.event(mode, "y", 0, true, 100).empty());
        auto captured = typing.event(mode, "u", Super, true, 100);
        assert(captured.size() == 1 && captured[0].name == "capture");
    }
    assert(sim::Camera::lookPitch(-10, .0025f, false) > 0);
    assert(sim::Camera::lookPitch(-10, .0025f, true) < 0);
    for (float yaw : {-.5235988f, 0.f, .5235988f}) {
        sim::Camera c;
        c.yaw = yaw;
        c.pitch = .2f;
        assert(length(rotate(c.orientation(), Vec3{0, 0, -1}) - c.forward()) < .0001f);
        auto viewForward = c.view().point(c.position + c.forward() * 3);
        assert(length(viewForward - Vec3{0, 0, -3}) < .0001f);
        for (auto xy : {Vec3{.2f, .25f, 0}, Vec3{.8f, .75f, 0}}) {
            auto ray = c.ray(xy.x, xy.y, 16.f / 9);
            auto projected = c.project(c.position + ray * 5, 16.f / 9);
            assert(projected.visible && std::abs(projected.x - xy.x) < .0001f && std::abs(projected.y - xy.y) < .0001f);
            auto p = c.view().point(c.position + ray * 5);
            float tanFov = std::tan(c.fov * .01745329252f / 2);
            assert(std::abs((p.x / -p.z / tanFov / (16.f / 9) + 1) / 2 - xy.x) < .0001f);
            assert(std::abs((1 - p.y / -p.z / tanFov) / 2 - xy.y) < .0001f);
        }
    }
    sim::Camera carryCamera;
    carryCamera.position = {0, 1.7f, 0};
    surfaces::Surface carried;
    carried.center = {0, 1.7f, -3};
    carried.width = 2;
    carried.height = 1;
    Hands carry;
    carry.begin(Gesture::Carry, carried, {3, .7f, .3f}, carryCamera);
    auto initialOffset = carry.offset;
    carryCamera.yaw = .5235988f;
    carryCamera.pitch = .15f;
    assert(carry.update(carryCamera, carryCamera.forward(), .016f, {}));
    auto facing = carry.pose.normal();
    assert(std::abs(facing.y) < .0001f);
    auto yawOnly = carryCamera;
    yawOnly.pitch = 0;
    assert(dot(facing, yawOnly.forward() * -1) > .9999f);
    assert(length(carry.pose.center + rotate(yawOnly.orientation(), initialOffset) -
                  (carryCamera.position + carryCamera.forward() * 3)) < .0001f);
    assert(carry.pose.center.x > 0);
    // Initial pitch/roll survive arbitrary look pitch and a world-Y yaw delta.
    carried.rotation =
        quaternionProduct(orientation({0, .3f, 1}), std::array<float, 4>{0, 0, std::sin(.2f), std::cos(.2f)});
    carryCamera.pitch = -.3f;
    carry.begin(Gesture::Carry, carried, {3, .6f, .4f}, carryCamera);
    auto startRotation = carry.pose.rotation;
    carryCamera.pitch = .7f;
    assert(carry.update(carryCamera, carryCamera.forward(), .016f, {}));
    assert(carry.pose.rotation == startRotation);
    for (auto key : {"w", "a", "s", "d", "h", "j", "k", "l", "Up", "Down", "Left", "Right", "space"})
        for (unsigned mods : {unsigned(Super), unsigned(Super | Shift), unsigned(Super | Ctrl)}) {
            Input gesture;
            auto press = gesture.event("world", key, mods, true, 0, 0, true);
            assert(press.size() == 1);
            auto release = gesture.event("world", key, 0, false, 1);
            assert(release.size() == 1 && !release[0].pressed);
        }
    Input native;
    assert(native.event("world", "w", Super, true, 0).empty());
    surfaces::Surface logical;
    logical.logicalSize(576, 670, .002f);
    assert(std::abs(logical.width - 1.152f) < .00001f);
    assert(std::abs(logical.width / logical.height - 576.f / 670) < .00001f);
    for (auto normal : {Vec3{1, 0, 0}, Vec3{0, 0, -1}, Vec3{0, 1, 0}})
        assert(length(rotate(orientation(normal), Vec3{0, 0, 1}) - normal) < .0001f);
    auto positiveY = std::array<float, 4>{0, std::sin(.5235988f / 2), 0, std::cos(.5235988f / 2)};
    assert(rotate(positiveY, Vec3{1, 0, 0}).z < 0); // Surface/editor angles are right-handed.
    auto style = render::overlayDefaults();
    assert(render::overlayStylePatch(
               style, {{"waypointColor", {1, 0, 0}}, {"fontFamily", "DejaVu Sans"}})["waypointColor"][0] == 1);
    for (auto invalid : {world::Json{{"unknown", 1}}, world::Json{{"routeColor", {1, 2, 0}}},
                         world::Json{{"fontFamily", "\n"}}, world::Json{{"iconAtlas", {{"path", "relative"}}}}}) {
        bool rejected = false;
        try {
            render::overlayStylePatch(style, invalid);
        } catch (...) {
            rejected = true;
        }
        assert(rejected);
    }
    auto oldAreas = defaults;
    oldAreas.erase("show_area_markers");
    assert(validateInput(oldAreas) == defaults);
    Input multi;
    multi.event("world", "k", 0, true, 0);
    multi.event("world", "w", Shift, true, 1);
    assert(multi.event("world", "k", 0, false, 2)[0].pressed);
    assert(!multi.event("focus", "w", 0, false, 3)[0].pressed);
    multi.event("world", "Tab", 0, true, 10);
    assert(!multi.event("world", "Tab", 0, true, 20)[0].pressed);
    auto next = defaults;
    next["longHoldMs"] = 1500;
    multi.apply(next);
    assert(multi.tick(1509).empty());
    assert(multi.tick(1510).size() == 1);
    multi.event("menu", "Tab", 0, false, 1600);
    assert(multi.event("focus", "Tab", Super, true, 2000)[0].name == "exit");
    assert(multi.event("world", "Return", Super, true, 2001).empty());
    for (auto key : {"WheelUp", "WheelDown"}) {
        Input i;
        assert(i.event("world", key, Super | Shift, true, 0, 1)[0].name == "rotate");
        assert(i.event("world", key, Super, true, 0, 1)[0].name == "distance");
    }
    next = defaults;
    next["modes"]["world"]["debug"] = {"e"};
    assert(inputConflicts(validateInput(next)).size() == 1);
    for (auto bad : {"Super+Super+e", "garbage-key", "Wheel[L]", "Button9999"}) {
        bool rejected = false;
        try {
            parseBinding(bad);
        } catch (...) {
            rejected = true;
        }
        assert(rejected);
    }
    auto dir = std::filesystem::temp_directory_path() / ("hyprune-m3b-unit-" + std::to_string(getpid()));
    std::filesystem::create_directories(dir);
    setenv("HYPRUNE_INPUT_FILE", (dir / "input.json").c_str(), 1);
    {
        InputFile file;
        assert(file.current == defaults);
        next = defaults;
        next["longHoldMs"] = 1200;
        atomicJSON(file.path, next);
        assert(file.poll() == next);
        assert(file.current == next);
        auto invalid = next;
        invalid["longHoldMs"] = 0;
        atomicJSON(file.path, invalid);
        assert(!file.poll());
        assert(file.current == next && !file.error.empty());
        atomicJSON(file.path, next);
        assert(!file.poll() && file.error.empty()); // repairing with the same last-good value clears errors
        file.save(defaults);
        assert(readJSON(file.path) == defaults);
    }
    setenv("HYPRUNE_GRAPHICS_FILE", (dir / "graphics.json").c_str(), 1);
    {
        InputFile file("graphics", Json::object(), [](const Json &patch) {
            configPatch(defaultConfig(), {{"graphics", patch}});
            return patch;
        });
        file.save({{"quality", "low"}, {"bloom", "high"}});
        assert(readJSON(file.path) == file.current);
        atomicJSON(file.path, {{"msaa", 999}});
        assert(!file.poll() && !file.error.empty() && file.current["quality"] == "low");
        atomicJSON(file.path, file.current);
        assert(!file.poll() && file.error.empty());
        atomicJSON(file.path, Json::object());
        assert(file.poll() == Json::object());
    }
    std::filesystem::remove_all(dir);
    Tools tools;
    tools.slot = 2;
    assert(tools.submode() == "place");
    tools.wheel(1);
    assert(tools.submode() == "lock");
    tools.slot = 3;
    for (int i = 0; i < 6; i++) {
        tools.point("org.test.world", "home", {float(i), 0, 0});
        tools.point("org.test.world", "home", {float(i), 0, -2});
        auto lines = pathLines(tools.paths.back());
        assert(!lines.empty());
        tools.finishPath();
        tools.wheel(1);
    }
    surfaces::Instance instance;
    instance.id = "window";
    instance.appClass = "kitty";
    instance.title = "terminal";
    tools.toggleLock("org.test.world", instance);
    auto sibling = instance;
    sibling.id = "other-window";
    assert(tools.locked("org.test.world", sibling));
    instance.title = "editor";
    assert(tools.locked("org.test.world", instance));
    tools.toggleLock("org.test.world", instance);
    assert(!tools.locked("org.test.world", instance));
    assert(!tools.locked("org.test.world", sibling));
    auto saved = tools.saved();
    Tools restored;
    restored.restore(saved);
    assert(restored.saved() == saved);
    auto move = gizmo({0, 0, -3}, "move"), scale = gizmo({0, 0, -3}, "scale"), rotate = gizmo({0, 0, -3}, "rotate");
    assert(move.size() == 3 && scale.size() == 4 && scale.back().uniform && rotate.size() == 120);
    assert(pickHandle(scale, {0, 0, 0}, {0, 0, -1}) == 3);
    assert(pickHandle(move, {0, 0, 0}, normalized(Vec3{.5, 0, -3})).has_value());
    surfaces::Surface window;
    window.center = {0, 1.7, -3};
    window.width = 2;
    window.height = 1;
    window.pixelWidth = 1000;
    window.pixelHeight = 500;
    sim::Camera c;
    c.position = {0, 1.7, 0};
    Hands hands;
    hands.begin(Gesture::CornerResize, window, {3, .7, .4});
    auto ray = normalized(Vec3{.4, .1, -3});
    assert(hands.update(c, ray, .1, {}));
    assert(std::abs(hands.pose.width - 2) < .001 && std::abs(hands.pose.height - 1) < .001);
    auto opposite = window.center - window.right() - window.up() * .5f;
    assert(hands.update(c, normalized(Vec3{.7, .3, -3}), .1, {}));
    assert(length(hands.pose.center - hands.pose.right() * hands.pose.width * .5f -
                  hands.pose.up() * hands.pose.height * .5f - opposite) < .001);
    Transform transform;
    transform.position = {5, 0, 0};
    transform.scale = {2, 2, 2};
    assert(length(transform.matrix({1, 0, 0}).point({1, 0, 0}) - Vec3{5, 0, 0}) < .0001);
    sim::Camera camera;
    camera.position = {0, 1.7, 0};
    sim::setCrouch(camera, true, {});
    assert(camera.eyeHeight == 1 && std::abs(camera.position.y - 1) < .001);
    sim::setCrouch(camera, false, {});
    assert(camera.eyeHeight == 1.7f && std::abs(camera.position.y - 1.7) < .001);
    std::cout << "PASS M3B: " << checked
              << " default variants, mode ownership, hold/repeat/aggregate releases, wheel chords, config "
                 "validation/watch/save, six path modes, persistence, gizmo and crouch\n";
}
