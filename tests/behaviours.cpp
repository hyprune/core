// Data-driven behaviours (RFC-0014 amendment, IPC 0.31 draft): parsing and checks, the
// mapping from a published value to 0..1 (range, easing, clamping), smoothing and reduced
// motion, stale and missing data (hold), the display transform (scale about the bottom face,
// move in metres, rotate), colour and glow paint, visible thresholds, texture offset and
// scroll, state kept across a sync, and the summary IPC reports. Drawing is covered by the
// nested --builder-shell check (both renderers).
#include "world/Behaviours.hpp"
#include <cmath>
#include <iostream>
using namespace hyprune;
using world::Json;
namespace {
int failures = 0;
void check(bool ok, const std::string &what) {
    if (!ok) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}
bool near(float a, float b, float e = 1e-3f) { return std::abs(a - b) < e; }
bool refused(const Json &list, const std::string &needle) {
    try {
        world::parseBehaviours(list);
    } catch (const std::invalid_argument &e) {
        return std::string(e.what()).find(needle) != std::string::npos;
    }
    return false;
}
Json publish(const std::string &source, Json values, int64_t at = 1000) {
    return Json::array({{{"sourceId", source}, {"values", values}, {"monotonicMs", at}, {"sequence", 1}, {"ttlMs", 60000}}});
}
} // namespace
int main() {
    // ---- parsing ----
    auto height = Json::parse(R"([{"key":"height","property":"scale","from":[1,0.1,1],"to":[1,3,1],"smoothing":0}])");
    auto parsed = world::parseBehaviours(height);
    check(parsed.size() == 1 && parsed[0].property == world::Behaviour::Scale && parsed[0].anchor == world::Behaviour::Bottom &&
              parsed[0].input == "value" && parsed[0].min == 0 && parsed[0].max == 100,
          "defaults: key value, 0..100, bottom anchor");
    check(refused(Json::parse(R"([{"key":"a","property":"scale","from":[1,1,1]}])"), "to is needed"), "scale needs from and to");
    check(refused(Json::parse(R"([{"key":"a","property":"spin","from":1,"to":2}])"), "property is"), "unknown property");
    check(refused(Json::parse(R"([{"key":"a","property":"scale","from":[1,1,1],"to":[1,0,1]}])"), "scale per axis"), "scale 0 refused");
    check(refused(Json::parse(R"([{"key":"a","property":"colour","from":"#FFF","to":"#000000"}])"), "#rrggbb"), "colour format");
    check(refused(Json::parse(R"([{"key":"a","property":"visible"}])"), "above, below"), "visible needs a threshold");
    check(refused(Json::parse(R"([{"key":"a","property":"move","from":[0,0,0],"to":[0,1,0]},{"key":"a","property":"move","from":[0,0,0],"to":[0,1,0]}])"), "twice"), "unique keys");
    check(refused(Json::parse(R"([{"key":"a","property":"move","from":[0,0,0],"to":[0,1,0],"anchor":"top"}])"), "not known"), "anchor only on scale/rotate");
    check(refused(Json::parse(R"([{"key":"a","property":"glow","from":0,"to":4,"input":{"min":5,"max":5}}])"), "differ"), "min != max");
    check(refused(Json::array({Json::object(), Json::object(), Json::object(), Json::object(), Json::object(), Json::object(), Json::object(), Json::object(), Json::object()}), "at most 8"), "eight at most");

    // ---- a world: one placement of a 1 m box standing on y = 0 ----
    world::World w;
    world::ObjectAsset asset;
    asset.low = {-.5f, 0, -.5f}, asset.high = {.5f, 1, .5f};
    asset.meshes.resize(2);
    w.objectAssets.push_back(asset);
    world::ObjectInstance inst;
    inst.id = "u-00c001";
    inst.scale = 1;
    w.objectInstances.push_back(inst);
    world::Behaviours b;
    b.sync(w, {{0, "u-00c001", parsed}});
    // Default source: com.hyprune.sources.<id>, key value, 0..100.
    b.tick(.016, publish("com.hyprune.sources.u-00c001", {{"value", 50}}), 1000, false);
    std::vector<world::ObjectDrawState> states;
    b.apply(w, states);
    auto d = states[0].display;
    // Scale y = 0.1 + (3 - 0.1) * 0.5 = 1.55 about the bottom face: the top at 1.55, the base still at 0.
    check(states[0].bound && states[0].displayed && near(d.v[5], 1.55f) && near(d.point({0, 0, 0}).y, 0) && near(d.point({0, 1, 0}).y, 1.55f),
          "scale about the bottom face follows the value");
    check(b.summary("u-00c001")[0]["t"] == .5 && b.summary("u-00c001")[0]["condition"] == "fresh", "summary: t and condition");
    // Stale and missing data hold the last value.
    b.tick(.016, publish("com.hyprune.sources.u-00c001", {{"value", 50}}, 1000), 20000, false);
    states.clear(), b.apply(w, states);
    check(near(states[0].display.v[5], 1.55f) && b.summary("u-00c001")[0]["condition"] == "stale", "stale holds");
    b.tick(.016, Json::array(), 21000, false);
    states.clear(), b.apply(w, states);
    check(near(states[0].display.v[5], 1.55f) && b.summary("u-00c001")[0]["condition"] == "missing", "missing holds");

    // ---- smoothing, easing, other properties ----
    auto many = Json::parse(R"([
      {"key":"slide","property":"move","from":[0,0,0],"to":[0,2,0],"smoothing":1,"input":{"source":"com.hyprune.sources.cpu","key":"cpu"}},
      {"key":"tint","property":"colour","from":"#000000","to":"#ffffff","easing":"step","smoothing":0,"input":{"source":"com.hyprune.sources.cpu","key":"cpu"}},
      {"key":"warn","property":"visible","above":80,"input":{"source":"com.hyprune.sources.cpu","key":"cpu"}},
      {"key":"belt","property":"texture","mode":"scroll","from":[0,0],"to":[1,0],"smoothing":0,"input":{"source":"com.hyprune.sources.cpu","key":"cpu"}}])");
    world::Behaviours m;
    m.sync(w, {{0, "u-00c001", world::parseBehaviours(many)}});
    m.tick(.016, publish("com.hyprune.sources.cpu", {{"cpu", "100"}}), 1000, false); // first value: no smoothing from nowhere
    m.tick(.5, publish("com.hyprune.sources.cpu", {{"cpu", 0}}), 1000, false);
    states.clear(), m.apply(w, states);
    float slid = states[0].display.v[13];
    check(slid > .1f && slid < 2 && near(slid, 2 * std::exp(-.5f), .02f), "move eases towards the new value (1 s smoothing)");
    check(states[0].meshes.size() == 2 && states[0].meshes[0].painted && near(states[0].meshes[0].colour.x, 0) && states[0].meshes[0].hidden,
          "colour (step easing) and visible below the threshold paint every mesh");
    world::Behaviours still;
    still.sync(w, {{0, "u-00c001", world::parseBehaviours(many)}});
    still.tick(.016, publish("com.hyprune.sources.cpu", {{"cpu", 90}}), 1000, true);
    still.tick(.5, publish("com.hyprune.sources.cpu", {{"cpu", 90}}), 1000, true);
    states.clear(), still.apply(w, states);
    check(near(states[0].display.v[13], 1.8f) && !states[0].meshes[0].hidden && states[0].uvU == 0, "reduced motion snaps, never scrolls");
    m.tick(1.25, publish("com.hyprune.sources.cpu", {{"cpu", 100}}), 1000, false);
    states.clear(), m.apply(w, states);
    check(states[0].uvU > 0 && states[0].moving, "texture scroll advances while the value is high");
    // State carries across a sync by placement id and key.
    m.sync(w, {{0, "u-00c001", world::parseBehaviours(many)}});
    m.tick(0, publish("com.hyprune.sources.cpu", {{"cpu", 100}}), 1000, false);
    states.clear(), m.apply(w, states);
    check(states[0].uvU > 0, "scroll position kept across a sync");

    // ---- rotate and glow ----
    auto spin = Json::parse(R"([{"key":"turn","property":"rotate","from":[0,0,0],"to":[0,90,0],"anchor":"centre","smoothing":0},
                               {"key":"lamp","property":"glow","from":0,"to":8,"colour":"#ff8000","smoothing":0}])");
    world::Behaviours r;
    r.sync(w, {{0, "u-00c001", world::parseBehaviours(spin)}});
    r.tick(.016, publish("com.hyprune.sources.u-00c001", {{"value", 100}}), 1000, false);
    states.clear(), r.apply(w, states);
    auto front = states[0].display.point({0, .5f, .5f});
    check(near(front.y, .5f) && near(std::abs(front.x), .5f) && near(front.z, 0), "rotate 90 degrees about +Y at the centre");
    check(states[0].meshes[0].painted && near(states[0].meshes[0].emission, 8) && near(states[0].meshes[0].colour.x, 1),
          "glow paints its colour at its strength");
    check(world::displayUneven(d) && !world::displayUneven(states[0].display), "uneven scale is flagged for normals");
    if (failures) {
        std::cerr << failures << " failures\n";
        return 1;
    }
    std::cout << "PASS behaviours\n";
}
