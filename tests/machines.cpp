// Machines prototype (RFC-0013 draft): the mapping language, counters and
// transitions, track geometry, and the runtime against tests/fixtures/machines:
// palettes from a repeated input, phase-continuous speed easing, value-driven
// poses, toggles, stacks, items (rate, spacing, stops, sink, overflow), effects,
// labels, staleness, missing sources, still motion, coalescing, revisions and
// load-time validation. GL drawing is covered by the nested --machines check.
#include "interaction/InputFile.hpp"
#include "world/Machines.hpp"
#include "world/World.hpp"
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
using namespace hyprune;
using world::Json;
using world::machines::Mapping;
namespace {
int failures = 0;
std::function<std::string()> context;
void check(bool ok, const std::string &what) {
    if (!ok) {
        std::cerr << "FAIL: " << what << '\n';
        if (context && getenv("MACHINES_TEST_VERBOSE"))
            std::cerr << "  " << context() << '\n';
        ++failures;
    }
}
bool near(double a, double b, double eps = 1e-4) { return std::abs(a - b) <= eps; }
Json mapped(const char *map, const Json &value) {
    auto out = Mapping::parse(Json::parse(map)).apply(value);
    return out ? *out : Json("<none>");
}
bool throws(const std::function<void()> &f) {
    try {
        f();
    } catch (const std::exception &) {
        return true;
    }
    return false;
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    const std::filesystem::path fixture = argv[1];
    // Mapping language.
    const char *cases = R"({"cases": [{"equals": "success", "to": "ok"}, {"in": ["queued", "running"], "to": "running"}], "else": "idle"})";
    check(mapped(cases, "success") == "ok" && mapped(cases, "queued") == "running" && mapped(cases, "weird") == "idle", "cases, in, else");
    check(mapped(R"({"cases": [{"equals": 3, "to": "three"}]})", 3.0) == "three", "numbers compare numerically");
    check(mapped(R"({"cases": [{"equals": 3, "to": "three"}]})", 4) == "<none>", "no case and no else: no output");
    check(mapped(R"({"cases": [{"equals": true, "to": 1}], "else": 0})", true) == 1, "booleans compare with true/false");
    const char *thresholds = R"({"thresholds": [{"below": 10, "to": "ok"}, {"below": 100, "to": "warn"}], "else": "fail"})";
    check(mapped(thresholds, 9.9) == "ok" && mapped(thresholds, 10) == "warn" && mapped(thresholds, 100) == "fail", "thresholds are strict");
    check(mapped(R"({"thresholds": [{"atLeast": 5, "to": "high"}], "else": "low"})", 5) == "high", "atLeast mirror form");
    check(mapped(R"({"scale": {"from": [0, 500], "to": [0, 24]}})", 250).get<double>() == 12, "scale");
    check(mapped(R"({"scale": {"from": [0, 500], "to": [0, 24]}})", 900).get<double>() == 24, "scale clamps by default");
    check(mapped(R"({"scale": {"from": [0, 500], "to": [0, 24]}, "clamp": false})", 1000).get<double>() == 48, "clamp off");
    check(mapped(R"({"scale": {"from": [0, 10], "to": [0, 1]}, "round": "ceil"})", 1).get<double>() == 1, "round ceil");
    check(mapped(R"({"format": {"decimals": 0, "compact": true, "unit": "jobs"}})", 1234) == "1k jobs", "format compact");
    check(mapped(R"({"format": {"compact": true}})", 1234) == "1.2k", "compact keeps one decimal by default");
    check(mapped("{}", 7) == 7 && mapped(R"({"format": {}})", "unit-tests") == "unit-tests", "direct; strings pass formats");
    check(throws([] { Mapping::parse(Json::parse(R"({"wobble": 1})")); }), "unknown mappings are rejected");
    {
        auto counter = Mapping::parse(Json::parse(R"({"counter": true})"));
        world::machines::EventGate g;
        check(g.feed(counter, 5110) == 0, "a counter's first value fires nothing");
        check(g.feed(counter, 5112) == 2, "each increase by n fires n");
        check(g.feed(counter, 5112) == 0, "no change fires nothing");
        check(g.feed(counter, 3) == 0 && g.feed(counter, 4) == 1, "a decrease (a reset) re-baselines without firing");
        auto becomes = Mapping::parse(Json::parse(R"({"becomes": "ok", "from": "running"})"));
        world::machines::EventGate b;
        check(b.feed(becomes, "ok") == 0, "becomes: the first value fires nothing");
        check(b.feed(becomes, "idle") == 0 && b.feed(becomes, "ok") == 0, "becomes from running only");
        check(b.feed(becomes, "running") == 0 && b.feed(becomes, "ok") == 1 && b.feed(becomes, "ok") == 0, "fires once on the transition");
    }
    // Track geometry (RFC-0011 path rules).
    {
        using world::machines::Track;
        auto line = Track::build({{{0, 0, 0}, "line"}, {{3, 0, 0}, "line"}, {{3, 0, 4}, "line"}}, false);
        check(near(line.length(), 7) && near(line.pointAt[1], 3) && near(line.pointAt[2], 7), "line lengths and point arc lengths");
        Vec3 tangent;
        auto p = line.at(5, &tangent);
        check(length(p - Vec3{3, 0, 2}) < 1e-4f && length(tangent - Vec3{0, 0, 1}) < 1e-4f, "position and tangent by arc length");
        auto curve = Track::build({{{0, 0, 0}, "line"}, {{2, 0, 0}, "curve"}, {{2, 0, 2}, "curve"}}, false);
        check(length(curve.at(curve.pointAt[1]) - Vec3{2, 0, 0}) < 1e-3f && curve.length() > 4.f, "a curve passes through its points");
        auto arc = Track::build({{{0, 0, 0}, "line"}, {{4, 0, 0}, "arc"}}, false);
        check(std::abs(arc.at(arc.length() / 2).z) > 1.1f, "an arc bows 0.3 x chord sideways");
        check(throws([] { Track::build({{{0, 0, 0}, "line"}, {{0, 2, 0}, "arc"}}, false); }), "a vertical arc is invalid");
        auto loop = Track::build({{{0, 0, 0}, "line"}, {{2, 0, 0}, "line"}, {{2, 0, 2}, "line"}}, true);
        check(near(loop.length(), 4 + std::sqrt(8.f), 1e-3) && loop.pointAt.size() == 3, "closed tracks return to point 0");
    }
    // The runtime.
    auto w = world::load(fixture);
    auto spec = interaction::readJSON(fixture / "machines.bindings.json");
    world::Machines m;
    m.load(spec, *w, fixture);
    check(m.active() && m.instances().size() == 2, "two bound instances (the third stays plain)");
    size_t a = 0, b = 1, plain = 2;
    check(m.states[a].bound && m.states[b].bound && !m.states[plain].bound, "draw state marks bound instances only");
    const auto &asset = w->objectAssets[0];
    auto meshOf = [&](const std::string &node) {
        for (size_t i = 0; i < asset.meshes.size(); ++i)
            if (asset.nodeNames[asset.meshes[i].node] == node)
                return i;
        return size_t(-1);
    };
    size_t lamp1 = meshOf("lamp_1"), lamp2 = meshOf("lamp_2"), alarm = meshOf("alarm"), crate = meshOf("crate");
    int64_t now = 1000000;
    auto source = [&](Json values, int64_t publishedMs, const char *id = "org.hyprune.test.line") {
        return Json{{"sourceId", id}, {"values", values}, {"sequence", 1}, {"monotonicMs", publishedMs}, {"ttlMs", 60000}};
    };
    // Sources are rebuilt every tick: fresh() republishes at the current time, pinned() keeps a publication time.
    using Sources = std::function<Json()>;
    auto fresh = [&](Json values) -> Sources { return [&, values] { return Json::array({source(values, now)}); }; };
    auto pinned = [&](Json values, int64_t at) -> Sources { return [&, values, at] { return Json::array({source(values, at)}); }; };
    auto run = [&](const Sources &sources, double seconds, bool still = false) {
        for (double t = 0; t < seconds - 1e-9; t += 1 / 60.)
            now += 16, m.tick(1 / 60., sources(), now, still);
    };
    auto diag = [&](size_t i) { return m.diagnostics()["instances"][i]; };
    context = [&] { auto d = m.diagnostics(); d["instances"].erase(1); return d.dump(); };
    // Missing: inputs at their defaults, rest palette, labels empty until bound text arrives.
    run([] { return Json::array(); }, .3);
    check(diag(a)["inputs"]["status"]["condition"] == "missing" && diag(a)["states"]["lamp_1"] == "idle", "missing source: defaults");
    check(m.states[a].meshes[alarm].hidden, "missing source: the alarm toggle follows the default (hidden)");
    check(diag(a)["labels"]["sign"] == "\xe2\x80\x94", "missing source: labels show an em dash");
    // Fresh data.
    Json values{{"status", "running"}, {"stage.1", "running"}, {"stage.2", "failed"}, {"jobsStarted", 100}, {"depth", 55},
                {"progress", .5}, {"lastJob", "main #1432 build"}};
    run(fresh(values), .5);
    auto d = diag(a);
    check(d["inputs"]["status"]["value"] == "running" && d["inputs"]["status"]["raw"] == "running" &&
              d["inputs"]["status"]["condition"] == "fresh", "binding maps raw to the input");
    check(d["states"]["lamp_1"] == "running" && d["states"]["lamp_2"] == "fail", "repeated input drives each lamp ({n})");
    auto &s = m.states[a];
    check(s.meshes[lamp1].painted && near(s.meshes[lamp1].colour.x, 1) && near(s.meshes[lamp1].emission, 3), "palette colour and emission (materials)");
    check(s.meshes[lamp2].painted && near(s.meshes[lamp2].colour.y, .2f), "palette by nodes");
    check(!s.meshes[0].painted, "unslotted meshes keep their material");
    check(d["labels"]["sign"] == "main #1432 b", "labels cut to maxChars");
    check(d["stacks"]["waiting"] == 3, "count through scale + floor (55 -> 3 crates)");
    check(s.meshes[crate].hidden, "a stack node is hidden at rest");
    size_t crates = 0;
    for (auto &e : m.extras)
        crates += e.owner == a && e.node == int(asset.meshes[crate].node);
    check(crates == 3, "three crate copies drawn as extras");
    check(d["inputs"]["started"]["condition"] == "fresh" && d["tracks"][0]["spawned"] == 0, "the first counter value spawns nothing");
    // Phase-continuous speed: eased in, then the turns advance at speed/period.
    run(fresh(values), .3);
    d = diag(a);
    check(near(d["animations"]["spinner"].get<double>(), 1, 1e-6), "spinner eased to speed 1 within 300 ms");
    double t0 = s.turns[0];
    run(fresh(values), .5);
    double advanced = s.turns[0] - t0;
    if (advanced < 0)
        advanced += 1;
    check(near(advanced, .25, .02), "turns advance by dt x speed / period (no jump)");
    check(near(s.turns[1], .75 + .5 * .5, 1e-3), "position:lift maps 0..1 onto the range [0.75, 1.25]");
    check(s.moving, "spinning: moving");
    // Spawns: +3 starts -> 3 items at maxRate 2/s, spaced, dwelling at the middle stop, sunk at the end.
    values["jobsStarted"] = 103;
    run(fresh(values), .3);
    d = diag(a);
    check(d["tracks"][0]["live"] == 1 && d["tracks"][0]["backlog"] == 2, "items enter at maxRate (one at once, the rest wait)");
    run(fresh(values), 1.2);
    d = diag(a);
    check(d["tracks"][0]["spawned"] == 3 && d["tracks"][0]["live"] == 3, "all three entered within ~1.5 s");
    size_t tokens = 0;
    for (auto &e : m.extras)
        tokens += e.asset == 1 && e.owner == a;
    check(tokens == 3, "items are drawn as instances of the item asset");
    run(fresh(values), 4);
    check(diag(a)["tracks"][0]["live"] == 2, "items queue spacing behind the one dwelling at the stop");
    run(fresh(values), 4);
    d = diag(a);
    check(d["tracks"][0]["live"] == 0 && d["tracks"][0]["dropped"] == 0, "items dwell, travel and leave at the sink");
    // Overflow: a burst over the backlog cap counts drops and the track never exceeds its max.
    values["jobsStarted"] = 143;
    run(fresh(values), 4);
    d = diag(a);
    check(d["tracks"][0]["live"].get<int>() <= 4 && d["tracks"][0]["dropped"].get<int>() >= 24, "backlog capped at 16, pool capped at max");
    run(fresh(values), 20);
    // Transitions: running -> success fires the burst once; fail shows the alarm and doubles lamp_1 emission.
    values["status"] = "success";
    run(fresh(values), .3);
    size_t pieces = m.diagnostics()["pieces"];
    check(pieces > 0, "becomes ok from running fires the burst");
    run(fresh(values), .3);
    check(near(diag(a)["animations"]["spinner"].get<double>(), 0, 1e-6), "the spinner winds down within 300 ms of the update");
    run(fresh(values), .9);
    check(m.diagnostics()["pieces"] == 0, "effects end after durationMs");
    values["status"] = "failed";
    run(fresh(values), .3);
    check(!m.states[a].meshes[alarm].hidden && near(m.states[a].meshes[lamp1].emissiveScale, 2), "toggle and emission drives");
    check(m.diagnostics()["pieces"] == 0, "fail does not fire the success burst");
    // Revision: stable while nothing changes and nothing moves.
    run(fresh(values), .5);
    auto r0 = m.states[a].revision;
    run(fresh(values), 1);
    check(m.states[a].revision == r0 && !m.states[a].moving, "an idle machine changes nothing and costs no frames");
    values["lastJob"] = "deploy";
    run(fresh(values), .3);
    check(m.states[a].revision != r0, "a label change bumps the revision");
    // Coalescing: at most 4 evaluations per second.
    values["stage.1"] = "success";
    now += 16, m.tick(1 / 60., fresh(values)(), now, false);
    bool immediate = diag(a)["states"]["lamp_1"] == "ok";
    run(fresh(values), .26);
    check(!immediate && diag(a)["states"]["lamp_1"] == "ok", "updates are coalesced to 4 per second");
    // Stale: past staleMs the last state dims (stale palette entry), animations stop, labels get a clock mark.
    values["status"] = "running";
    auto published = now;
    run(pinned(values, published), 1);
    check(diag(a)["animations"]["spinner"].get<double>() > .9, "running again");
    run(pinned(values, published), 3);
    d = diag(a);
    check(d["stale"] == true && d["inputs"]["status"]["condition"] == "stale", "stale after staleMs");
    check(d["states"]["lamp_1"] == "ok (stale)" && near(m.states[a].meshes[lamp1].colour.x, .25f), "the stale palette entry");
    check(near(d["animations"]["spinner"].get<double>(), 0, 1e-2) && near(m.states[a].saturation, .3f, .01), "stale: stops and greys");
    bool marked = false;
    for (auto &l : m.labels)
        marked |= l.owner == a && l.text.ends_with("\xe2\x97\xb7");
    check(marked, "stale labels keep their text with a clock mark");
    values["jobsStarted"] = 150;
    run(pinned(values, published), .5);
    check(diag(a)["tracks"][0]["backlog"] == 0, "no spawns on stale data");
    run(fresh(values), 1);
    check(diag(a)["stale"] == false && diag(a)["tracks"][0]["backlog"] == 0, "fresh again: the jump does not burst (re-baselined)");
    // Error: `_status: error` or a type mismatch shows as stale.
    values["_status"] = "error";
    run(fresh(values), .3);
    check(diag(a)["inputs"]["status"]["condition"] == "error" && diag(a)["stale"] == true, "_status error");
    values.erase("_status");
    values["progress"] = "lots";
    run(fresh(values), .3);
    check(diag(a)["inputs"]["progress"]["condition"] == "error", "a type mismatch is an error");
    values["progress"] = .5;
    // Independent instances: machine-b reads another source.
    run([&, values] { return Json::array({source(values, now), source(Json{{"status", "fail"}}, now, "org.hyprune.test.other")}); }, .3);
    check(diag(b)["inputs"]["status"]["value"] == "fail" && diag(a)["inputs"]["status"]["value"] == "running", "instances bind independently");
    check(!m.states[b].meshes[alarm].hidden && m.states[a].meshes[alarm].hidden, "per-instance draw state");
    // Still (reduced motion): states and labels change; no spins, no travel; effects become flashes.
    values["jobsStarted"] = 160;
    run(fresh(values), .3, true);
    double frozen = m.states[a].turns[0];
    run(fresh(values), .5, true);
    check(m.states[a].turns[0] == frozen && diag(a)["tracks"][0]["live"] == 0, "still: no spin, no items");
    values["status"] = "success";
    run(fresh(values), .3, true);
    check(diag(a)["inputs"]["status"]["value"] == "ok" && !m.states[a].meshes[alarm].hidden == false, "still: states change");
    check(m.diagnostics()["pieces"] == 1, "still: a burst becomes one flash");
    // Validation at load.
    auto broken = [&](const std::function<void(Json &, Json &)> &edit) {
        auto manifest = interaction::readJSON(fixture / "objects/mini-conveyor.json");
        auto copy = spec;
        edit(copy, manifest);
        auto dir = std::filesystem::temp_directory_path() / "hyprune-machines-test";
        std::filesystem::remove_all(dir);
        std::filesystem::copy(fixture, dir, std::filesystem::copy_options::recursive);
        std::ofstream(dir / "objects/mini-conveyor.json") << manifest.dump();
        world::Machines probe;
        bool failed = throws([&] { probe.load(copy, *w, dir); });
        std::filesystem::remove_all(dir);
        return failed;
    };
    check(broken([](Json &, Json &mf) {
              mf["traits"]["machine"]["drives"].push_back(Json::parse(R"({"input": "status", "target": "state:lamp_1", "map": {"cases": [{"equals": "running", "to": "purple"}]}})"));
          }), "a drive that can produce a state outside the palette is rejected");
    check(broken([](Json &, Json &mf) {
              mf["traits"]["machine"]["drives"].push_back(Json::parse(R"({"input": "queued", "target": "spawn:in"})"));
          }), "spawn needs an event");
    check(broken([](Json &, Json &mf) {
              mf["traits"]["machine"]["drives"].push_back(Json::parse(R"({"input": "status", "target": "animation:lift"})"));
          }), "a value-driven animation takes position:, not animation:");
    check(broken([](Json &, Json &mf) { mf["slots"]["states"][0]["materials"] = Json::array({"nope"}); }), "unknown material");
    check(broken([](Json &c, Json &) { c["instances"]["machine-a"]["inputs"]["wobble"] = {{"source", "a.b"}, {"key", "x"}}; }), "unknown input");
    check(broken([](Json &c, Json &) { c["instances"]["ghost"] = Json::object(); }), "unknown object");
    if (failures)
        std::cerr << failures << " failure(s)\n";
    else
        std::cout << "machines: all checks passed\n";
    return failures != 0;
}
