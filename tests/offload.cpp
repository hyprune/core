#include "interaction/Config.hpp"
#include "offload/Machines.hpp"
#include "offload/Objects.hpp"
#include "offload/Packet.hpp"
#include "offload/Pacing.hpp"
#include "offload/Policy.hpp"
#include "offload/Sandbox.hpp"
#include "offload/Supervisor.hpp"
#include "offload/Watchdog.hpp"
#include "render/Resolution.hpp"
#include <cassert>
#include <fcntl.h>
#include <sys/wait.h>
#include <thread>
using namespace hyprune;
// World objects in the worker (Objects.hpp): deltas carry only what changed, survive
// backpressure, and the worker's instances match the plugin's exactly.
static void objectSync() {
    using namespace offload;
    world::World live, worker;
    for (auto *w : {&live, &worker}) {
        w->objectAssets.resize(2);
        w->objectAssets[1].animations.push_back({});
        w->objectAssets[1].nodeAnimation = {0};
    }
    auto instance = [](unsigned asset, float x) {
        world::ObjectInstance o;
        o.asset = asset;
        o.model = world::instanceModel({x, 1.f / 3, -2}, {0, .3826834f, 0, .9238795f}, 1.25f);
        o.center = {x, 1, 2}, o.radius = .7f, o.ambientUp = {.1f, .2f, .3f}, o.sun = .55f;
        o.regions = {2, 5};
        return o;
    };
    live.objectInstances = {instance(0, 1), instance(1, 2), instance(0, 3)};
    worker.objectInstances = {instance(1, 9)}; // the package's own: replaced by the first delta
    ObjectSender sender;
    ObjectReceiver receiver;
    sender.reset(2);
    receiver.start(worker);
    assert(worker.objectInstances.empty());
    ObjectReceiver::Loader noLoad = [](const std::filesystem::path &, const Json &) -> std::shared_ptr<world::World> {
        throw std::runtime_error("unexpected load");
    };
    auto same = [&] {
        assert(worker.objectInstances.size() == live.objectInstances.size());
        for (size_t k = 0; k < live.objectInstances.size(); ++k) {
            auto &a = live.objectInstances[k], &b = worker.objectInstances[k];
            assert(a.hidden == b.hidden);
            if (!a.hidden)
                assert(instanceRecord(a) == instanceRecord(b) && a.model == b.model && a.regions == b.regions);
        }
    };
    auto send = [&](const std::map<unsigned, Mat4> &transforms = {}) {
        auto d = sender.delta(live, transforms, {}, 2);
        if (!d.is_null()) {
            // Through the transport, as the worker receives it.
            auto wire = Json::parse(Json{{"objects", d}}.dump());
            receiver.apply(wire["objects"], worker.objectAssets.size(), noLoad);
            sender.commit();
        }
        receiver.materialize(worker);
        return d;
    };
    auto first = send();
    assert(first["count"] == 3 && first["instances"].size() == 3 && !first.contains("hidden"));
    same();
    assert(send().is_null()); // nothing changed: no delta at all
    // One moved instance: one record, nothing else.
    live.objectInstances[1].model.v[12] += .5f;
    auto moved = send();
    assert(moved["instances"].size() == 1 && moved["instances"][0][0] == 1 && !moved.contains("count") &&
           !moved.contains("hidden") && !moved.contains("transforms"));
    same();
    // Hidden set: its own revision, no records.
    live.objectInstances[2].hidden = true;
    auto hidden = send();
    assert(hidden["hidden"]["revision"] == 1 && hidden["hidden"]["set"] == Json::array({2}) && !hidden.contains("instances"));
    same();
    // Backpressure: an unsent delta is not committed; the next one still holds the change.
    live.objectInstances[0].sun = .25f;
    auto lost = sender.delta(live, {}, {}, 2);
    assert(lost["instances"].size() == 1);
    auto again = send();
    assert(again["base"] == lost["base"] && again["instances"] == lost["instances"]);
    same();
    // A delta from the wrong base is a protocol error.
    bool threw = false;
    try {
        receiver.apply(again, worker.objectAssets.size(), noLoad);
    } catch (const std::runtime_error &) {
        threw = true;
    }
    assert(threw);
    // Removing a placement shrinks the list; edited node transforms: changed and removed.
    live.objectInstances.pop_back();
    auto shrink = send();
    assert(shrink["count"] == 2 && shrink["hidden"]["set"].empty());
    same();
    auto m = Mat4::identity();
    m.v[13] = 2;
    auto t1 = send({{4, m}, {5, m}});
    assert(t1["transforms"]["set"].size() == 2 && receiver.transforms.size() == 2 && receiver.transforms.at(5) == m);
    m.v[13] = 3;
    auto t2 = send({{4, m}});
    assert(t2["transforms"]["set"].size() == 1 && t2["transforms"]["erase"] == Json::array({5}));
    assert(receiver.transforms.size() == 1 && receiver.transforms.at(4) == m);
    // A catalog asset merged after load: the worker decodes it from its source; its
    // instance stays hidden there until the asset merged.
    auto scratch = std::make_shared<world::World>();
    scratch->objectAssets.resize(1);
    scratch->objectAssets[0].id = "rack";
    live.objectAssets.push_back(scratch->objectAssets[0]);
    live.objectInstances.push_back(instance(2, 7));
    std::promise<void> gate;
    auto opened = gate.get_future().share();
    ObjectReceiver::Loader load = [&, opened](const std::filesystem::path &dir, const Json &manifest) {
        assert(dir == "/objects/rack" && manifest["id"] == "rack");
        opened.wait();
        auto copy = std::make_shared<world::World>();
        copy->objectAssets = scratch->objectAssets;
        return copy;
    };
    std::vector<AssetSource> sources{{"/objects/rack", {{"id", "rack"}}, "library/org.example.kit@1.0.0/rack"}};
    auto added = sender.delta(live, {{4, m}}, sources, 2);
    assert(added["assets"].size() == 1 && added["assets"][0]["index"] == 2 && added["count"] == 3 &&
           added["assets"][0]["ref"] == "library/org.example.kit@1.0.0/rack");
    receiver.apply(Json::parse(added.dump()), worker.objectAssets.size(), load);
    sender.commit();
    receiver.materialize(worker);
    assert(receiver.loading() && worker.objectInstances.size() == 3 && worker.objectInstances[2].hidden);
    assert(!receiver.merge(worker, true)); // still decoding
    gate.set_value();
    while (!receiver.merge(worker, true))
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    receiver.materialize(worker);
    assert(!receiver.loading() && worker.objectAssets.size() == 3 && worker.objectAssets[2].id == "rack" &&
           worker.objectAssets[2].ref == "library/org.example.kit@1.0.0/rack");
    same();
    // Mid-session adds (a companion, then a library installed later): a decode the worker
    // can't do leaves a placeholder at its index, so the next asset keeps its own index,
    // its instances stay hidden, and nothing throws (the frame still draws).
    for (auto id : {"broken", "lamp"}) {
        auto a = std::make_shared<world::World>();
        a->objectAssets.resize(1);
        a->objectAssets[0].id = id;
        live.objectAssets.push_back(a->objectAssets[0]);
        sources.push_back({std::string("/objects/") + id, {{"id", id}}, std::string("library/org.example.late@2.0.0/") + id});
    }
    live.objectInstances.push_back(instance(3, 8));
    live.objectInstances.push_back(instance(4, 9));
    ObjectReceiver::Loader late = [&](const std::filesystem::path &, const Json &manifest) {
        if (manifest["id"] == "broken")
            throw std::runtime_error("Landlock: no access");
        auto copy = std::make_shared<world::World>();
        copy->objectAssets.resize(1);
        copy->objectAssets[0].id = manifest["id"];
        return copy;
    };
    auto two = sender.delta(live, {{4, m}}, sources, 2);
    assert(two["assets"].size() == 2 && two["assets"][0]["index"] == 3 && two["assets"][1]["index"] == 4);
    receiver.apply(Json::parse(two.dump()), worker.objectAssets.size(), late);
    sender.commit();
    while (receiver.loading())
        if (!receiver.merge(worker, true))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    receiver.materialize(worker);
    assert(worker.objectAssets.size() == 5 && worker.objectAssets[3].meshes.empty() && worker.objectAssets[4].id == "lamp");
    assert(worker.objectAssets[3].ref == "library/org.example.late@2.0.0/broken" &&
           worker.objectAssets[4].ref == "library/org.example.late@2.0.0/lamp");
    assert(receiver.failures().size() == 1 && receiver.failures()[0]["index"] == 3 &&
           receiver.failures()[0]["ref"] == "library/org.example.late@2.0.0/broken");
    assert(worker.objectInstances.size() == 5 && worker.objectInstances[3].hidden && !worker.objectInstances[4].hidden);
    assert(instanceRecord(worker.objectInstances[4]) == instanceRecord(live.objectInstances[4]));
    // A new worker (restart) loads the package again and gets every merged asset, in order.
    {
        world::World fresh;
        fresh.objectAssets.resize(2);
        ObjectSender again;
        ObjectReceiver receiver2;
        again.reset(2);
        receiver2.start(fresh);
        auto all = again.delta(live, {{4, m}}, sources, 2);
        assert(all["assets"].size() == 3 && all["assets"][0]["index"] == 2 && all["count"] == 5);
        ObjectReceiver::Loader any = [](const std::filesystem::path &, const Json &manifest) {
            auto copy = std::make_shared<world::World>();
            copy->objectAssets.resize(1);
            copy->objectAssets[0].id = manifest["id"];
            return copy;
        };
        receiver2.apply(Json::parse(all.dump()), fresh.objectAssets.size(), any);
        while (receiver2.loading())
            if (!receiver2.merge(fresh, true))
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        receiver2.materialize(fresh);
        assert(fresh.objectAssets.size() == 5 && fresh.objectAssets[4].ref == "library/org.example.late@2.0.0/lamp");
        for (size_t k = 0; k < 5; ++k)
            assert(fresh.objectInstances[k].hidden == live.objectInstances[k].hidden);
    }
    // An asset with no recorded source can't be sent: offload stops instead of drawing wrong.
    live.objectAssets.push_back({});
    threw = false;
    try {
        sender.delta(live, {}, sources, 2);
    } catch (const std::runtime_error &) {
        threw = true;
    }
    assert(threw);
}
// Machine draw state in the worker (Machines.hpp): states, extras and labels arrive
// change-only, relative extras rebuild bit-identical models from the worker's instances,
// and a budget defers owners to the next pose without losing them.
// Live pacing: the plugin's frame budget and the worker's average-rate cap.
static void pacing() {
    using namespace hyprune::offload;
    for (auto [fps, want] : std::initializer_list<std::pair<double, int>>{{60, 600}, {45, 450}, {30, 300}}) {
        FrameBudget b;
        int sent = 0, run = 0, longest = 0;
        for (int frame = 0; frame < 600; ++frame) { // 10 s of 60 Hz frame ticks
            b.accrue(fps / 60);
            if (b.ready()) {
                b.spend(), ++sent;
                run = 0;
            } else
                longest = std::max(longest, ++run);
        }
        assert(std::abs(sent - want) <= 1);
        assert(longest <= 1); // never two frames in a row without a pose (an even cadence)
    }
    FrameBudget idle;
    idle.accrue(5); // a long idle gap banks one spare frame, not five
    idle.spend();
    idle.spend();
    assert(!idle.ready());
    // The worker: requests every 16.7 ms at a 45 FPS cap. Three in four go at once (none is
    // pushed off the frame grid by a 22 ms spacing), and the average stays at 45.
    RateCap cap;
    int rendered = 0, immediate = 0;
    double t = 0;
    for (int k = 0; k < 600; ++k, t += 1000. / 60) {
        bool go = cap.ready(t, 45);
        if (go)
            cap.spend(), ++rendered, ++immediate;
    }
    assert(rendered >= 448 && rendered <= 452);
    // A burst after idle is bounded: at most `burst` frames back to back faster than the cap.
    RateCap burst;
    burst.ready(0, 45);
    int quick = 0;
    for (int k = 0; k < 10; ++k)
        if (burst.ready(10000 + k * .1, 45))
            burst.spend(), ++quick;
    assert(quick == 2);
}
static void machineSync() {
    using namespace offload;
    world::World w;
    w.objectAssets.resize(2);
    for (float x : {1.f, 2.f, 3.f})
        w.objectInstances.push_back({}), w.objectInstances.back().model = world::instanceModel({x, .25f, -2}, {0, .3826834f, 0, .9238795f}, 1.25f);
    render::Renderer live, worker;
    live.objectStates.assign(3, {});
    auto &s = live.objectStates[1];
    s.bound = true, s.turns = {.25, .5}, s.saturation = .3f, s.brightness = .85f;
    s.meshes.assign(5, {});
    s.meshes[2].painted = true, s.meshes[2].colour = {1, .55f, .08f}, s.meshes[2].emission = .45f;
    s.meshes[3].hidden = true;
    s.meshes[4].offset = {0, .37f, 0};
    static const std::vector<unsigned> painted{2};
    auto copy = [&](size_t owner, Vec3 t) {
        world::ObjectExtra e;
        e.asset = 1, e.node = 3, e.owner = owner, e.relative = true, e.paintMeshes = &painted, e.paint = s.meshes[2];
        e.local.v[12] = t.x, e.local.v[13] = t.y, e.local.v[14] = t.z;
        e.model = w.objectInstances[owner].model * e.local;
        return e;
    };
    for (int i = 0; i < 100; ++i)
        live.objectExtras.push_back(copy(1, {-.05f * (i % 10), .07f * (i / 10), 0}));
    world::ObjectExtra fill = copy(2, {});
    fill.local.v[5] = .61803f, fill.local.v[13] = .14f; // a scaled fill: a full matrix
    fill.model = w.objectInstances[2].model * fill.local;
    live.objectExtras.push_back(fill);
    world::ObjectExtra piece; // world space (an effect piece)
    piece.asset = 1, piece.owner = 0, piece.model = world::instanceModel({5, 6, 7}, {0, 0, 0, 1}, .5f), piece.scale = .5f;
    live.objectExtras.push_back(piece);
    world::WorldLabel label;
    label.owner = 1, label.centre = {1, 2, 3}, label.right = {.2f, 0, 0}, label.up = {0, .05f, 0}, label.text = "71 GB \xe2\x97\xb7";
    live.worldLabels.push_back(label);
    MachineSender sender;
    MachineReceiver receiver;
    sender.reset();
    auto send = [&](size_t budget = 64 * 1024) {
        auto d = sender.delta(live, budget);
        if (!d.is_null()) {
            auto wire = Json::parse(Json{{"machines", d}}.dump());
            receiver.apply(wire["machines"], w);
            sender.commit();
        }
        receiver.materialize(w, worker);
        return d;
    };
    auto same = [&] {
        assert(worker.objectStates.size() == 3 && worker.objectStates[1].bound && !worker.objectStates[0].bound);
        auto &a = live.objectStates[1], &b = worker.objectStates[1];
        assert(a.turns == b.turns && a.saturation == b.saturation && a.brightness == b.brightness && a.meshes.size() == b.meshes.size());
        for (size_t m = 0; m < a.meshes.size(); ++m)
            assert(machines::paintJson(a.meshes[m]) == machines::paintJson(b.meshes[m]));
        assert(worker.objectExtras.size() == live.objectExtras.size());
        for (auto &e : live.objectExtras) { // owners are sent in order; draws keep their order per owner
            auto it = std::find_if(worker.objectExtras.begin(), worker.objectExtras.end(), [&](auto &x) {
                return x.owner == e.owner && x.model == e.model;
            });
            assert(it != worker.objectExtras.end() && it->asset == e.asset && it->node == e.node && it->scale == e.scale);
            assert(machines::paintJson(it->paint) == machines::paintJson(e.paint));
            assert((it->paintMeshes == nullptr) == (e.paintMeshes == nullptr) && (!e.paintMeshes || *it->paintMeshes == *e.paintMeshes));
        }
        assert(worker.worldLabels.size() == 1 && worker.worldLabels[0].text == live.worldLabels[0].text && worker.worldLabels[0].centre.z == 3);
    };
    auto d = send();
    assert(!d.is_null() && d["states"]["set"].size() == 1 && d["extras"]["set"].size() == 3 && d.contains("labels"));
    same();
    assert(send().is_null()); // nothing changed: nothing sent
    // A sloshing fill: only its owner travels.
    live.objectExtras[100].local.v[5] = .6f;
    live.objectExtras[100].model = w.objectInstances[2].model * live.objectExtras[100].local;
    d = send();
    assert(d["extras"]["set"].size() == 1 && d["extras"]["set"][0][0] == 2 && !d.contains("states") && !d.contains("labels"));
    same();
    // Backpressure: a delta not committed is sent again.
    live.worldLabels[0].text = "72 GB";
    d = sender.delta(live);
    assert(!d.is_null() && d.contains("labels"));
    d = send();
    assert(d.contains("labels"));
    same();
    // A small budget defers owners; they follow in the next pose.
    live.objectStates[1].meshes[2].emission = .5f;
    for (auto &e : live.objectExtras)
        if (e.owner == 1)
            e.paint.emission = .5f;
    live.objectExtras[101].scale = .4f;
    d = send(64);
    assert(d.contains("states") || d.contains("extras"));
    for (int i = 0; i < 4 && !send(64).is_null(); ++i)
        ;
    same();
    // An extra of an asset the worker is still decoding: kept, drawn once the asset merges.
    world::ObjectExtra later = copy(2, {.1f, .2f, .3f});
    later.asset = 2;
    live.objectExtras.push_back(later);
    send();
    assert(std::none_of(worker.objectExtras.begin(), worker.objectExtras.end(), [](auto &e) { return e.asset == 2; }));
    w.objectAssets.resize(3);
    receiver.materialize(w, worker);
    assert(std::count_if(worker.objectExtras.begin(), worker.objectExtras.end(), [](auto &e) { return e.asset == 2; }) == 1);
    // Machines gone: everything erased.
    live.objectStates.assign(3, {}), live.objectExtras.clear(), live.worldLabels.clear();
    d = send();
    assert(d["states"]["erase"].size() == 1 && d["extras"]["erase"].size() == 3 && d["labels"].empty());
    assert(worker.objectExtras.empty() && worker.worldLabels.empty() && !worker.objectStates[1].bound);
}
int main(int argc, char **argv) {
    if (argc == 8 && std::string(argv[3]) == "/dev/null") {
        offload::Watchdog watchdog(std::stoi(argv[7]));
        for (;;)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    using namespace offload;
    pacing();
    Supply usb{true, true, true, false, 0}, battery{true, false, false, true, 0}, full{true, true, false, false, 240};
    assert(!policy(usb, "auto", 60, 80).admitted);
    auto p = policy(usb, "on", 60, 80);
    assert(p.admitted && p.cap == 50 && p.fps == 45 && p.profile == "usb-c");
    p = policy(battery, "on", 60, 80);
    assert(p.admitted && p.cap == 50 && p.fps == 45);
    assert(!policy(battery, "auto", 60, 80).admitted);
    p = policy(full, "on", 90, 80);
    assert(p.admitted && p.cap == 80 && p.fps == 60);
    assert(policy(full, "auto", 60, 80).admitted && !policy(full, "off", 60, 80).admitted);
    assert(!policy({}, "on", 60, 80).admitted);
    full.discharging = true;
    assert(!policy(full, "on", 60, 80).admitted);
    full.discharging = false;
    // Laptop defaults (automatic cap) are the profile ceilings; a configured cap only lowers them.
    p = policy(full, "on", 60, std::nullopt, 350);
    assert(p.cap == 80 && p.capSource == "profile");
    assert(policy(usb, "on", 60, std::nullopt).cap == 50);
    p = policy(full, "on", 60, 60.);
    assert(p.cap == 60 && p.capSource == "configured" && policy(full, "on", 60, 200.).cap == 80);
    // No battery: mains, the cap scales with the GPU's enforced limit.
    Supply mains{.known = true, .ac = true, .mains = true};
    p = policy(mains, "auto", 60, std::nullopt, 350);
    assert(p.admitted && p.profile == "full-ac" && p.fps == 60 && p.cap == 263 && p.capSource == "device");
    p = policy(mains, "on", 60, std::nullopt);
    assert(p.cap == 80 && p.capSource == "fallback");
    p = policy(mains, "on", 60, 500., 350);
    assert(p.cap == 350 && p.capSource == "configured" && policy(mains, "on", 60, 30., 350).cap == 30);
    // A GPU enforcing less than the 20 W floor: the cap is its limit (the clamp range
    // stays ordered); an unknown (0 W) limit keeps the configured cap and the automatic fallback.
    for (double want : {5., 18., 300.}) {
        p = policy(mains, "on", 60, want, 15);
        assert(p.cap == 15 && p.capSource == "configured"); // the floor is the device limit itself
    }
    assert(policy(mains, "on", 60, 30., 0).cap == 30 && policy(mains, "on", 60, std::nullopt, 0).cap == 80);
    assert(policy(mains, "on", 60, std::nullopt, 15).cap == 11 && policy(mains, "on", 60, std::nullopt, 15).capSource == "device");
    // Offload GPU: an explicit address, else not the compositor's GPU, then the least utilised.
    std::vector<Device> gpus{{"/dev/dri/renderD128", "0000:65:00.0"}, {"/dev/dri/renderD129", "0000:b3:00.0"}};
    auto idle = [](const std::string &pci) { return std::optional<unsigned>(pci == "0000:65:00.0" ? 50 : 10); };
    assert(chooseDevice(gpus, "auto", "0000:b3:00.0", idle)->pci == "0000:65:00.0");
    assert(chooseDevice(gpus, "auto", "0000:65:00.0", idle)->pci == "0000:b3:00.0");
    assert(chooseDevice(gpus, "auto", "", idle)->pci == "0000:b3:00.0");
    assert(chooseDevice(gpus, "0000:65:00.0", "0000:65:00.0", idle)->node == "/dev/dri/renderD128");
    assert(!chooseDevice(gpus, "0000:17:00.0", "", idle) && !chooseDevice({}, "auto", "", idle));
    assert(safeSample(49, 70, 500, 50));
    for (auto v : {51., -1.})
        assert(!safeSample(v, 70, 500, 50));
    assert(!safeSample(40, 76, 500, 50) && !safeSample(40, 70, 1501, 50));
    auto defaults = interaction::defaultConfig();
    assert(defaults["graphics"]["offload"] == "off");
    for (auto patch :
         {Json{{"scale_min", .8}, {"scale_max", .5}}, Json{{"offload_power_cap_w", 2000}}, Json{{"offload_power_cap_w", 10}},
          Json{{"offload", "yes"}}, Json{{"offload_device", "renderD128"}}, Json{{"offload_device", "0000:65:00.0x"}}}) {
        bool threw = false;
        try {
            interaction::configPatch(defaults, {{"graphics", patch}});
        } catch (...) {
            threw = true;
        }
        assert(threw);
    }
    assert(defaults["graphics"]["offload_power_cap_w"].is_null() && defaults["graphics"]["offload_device"] == "auto");
    for (auto patch : {Json{{"offload_power_cap_w", 200}}, Json{{"offload_power_cap_w", nullptr}},
                       Json{{"offload_device", "0000:65:00.0"}}, Json{{"offload_device", "auto"}}})
        interaction::configPatch(defaults, {{"graphics", patch}});
    // Exercise supervision and an AC -> USB policy withdrawal without waking a GPU.
    full.discharging = false;
    Json supervisorState;
    {
        Supervisor supervisor(
            std::filesystem::canonical(argv[0]),
            [&](Json j, int channel) {
                if (channel < 0)
                    supervisorState = j.at("state");
            },
            [&] { return full; }, [] { return std::vector<Device>{{"/dev/null", "fake"}}; },
            [](double &w, double &t) {
                w = 30;
                t = 40;
                return true;
            });
        auto config = defaults["graphics"];
        config["offload"] = "auto";
        supervisor.request({{"enabled", true},
                            {"graphics", config},
                            {"worldPath", "/unused"},
                            {"width", 16},
                            {"height", 16},
                            {"generation", 1}});
        supervisor.tick();
        assert(supervisorState["pid"].is_number_integer() && supervisorState["profile"] == "full-ac");
        full.usb = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(110));
        supervisor.tick();
        assert(supervisorState["reason"] == "auto requires qualified full AC");
        std::this_thread::sleep_for(std::chrono::milliseconds(110));
        supervisor.tick();
        assert(supervisorState["pid"].is_null());
    }
    render::Resolution resolution;
    resolution.minimum = .75f;
    resolution.maximum = .9f;
    resolution.reset(.5f, 60, true, 3840 * 2160);
    assert(resolution.scale == .5f);
    resolution.reset(0, 60, true, 3840 * 2160);
    assert(resolution.scale == .75f);
    auto directory = std::filesystem::temp_directory_path() / ("hyprune-sandbox-" + std::to_string(getpid()));
    std::filesystem::create_directory(directory);
    std::ofstream(directory / "world.json") << "{}";
    // A catalog object root is readable (not writable) like the package; its siblings aren't.
    std::filesystem::create_directories(directory.string() + "-objects/a");
    std::ofstream(directory.string() + "-objects/a/object.json") << "{}";
    std::filesystem::create_directories(directory.string() + "-other");
    std::ofstream(directory.string() + "-other/object.json") << "{}";
    auto sandboxPid = fork();
    assert(sandboxPid >= 0);
    if (!sandboxPid) {
        sandbox(directory, {directory.string() + "-objects", "relative/ignored"});
        int allowed = open((directory / "world.json").c_str(), O_RDONLY);
        assert(allowed >= 0);
        close(allowed);
        allowed = open((directory.string() + "-objects/a/object.json").c_str(), O_RDONLY);
        assert(allowed >= 0);
        close(allowed);
        assert(open((directory.string() + "-objects/a/new").c_str(), O_WRONLY | O_CREAT, 0600) < 0);
        assert(open((directory.string() + "-other/object.json").c_str(), O_RDONLY) < 0);
        assert(open("/etc/passwd", O_RDONLY) < 0);
        assert(open((directory / "forbidden").c_str(), O_WRONLY | O_CREAT, 0600) < 0);
        int sock = socket(AF_UNIX, SOCK_STREAM, 0);
        assert(sock >= 0);
        assert(connect(sock, nullptr, 0) < 0 && errno == EPERM);
        close(sock);
        _exit(0);
    }
    int sandboxStatus = 0;
    waitpid(sandboxPid, &sandboxStatus, 0);
    assert(WIFEXITED(sandboxStatus) && WEXITSTATUS(sandboxStatus) == 0);
    std::filesystem::remove_all(directory);
    std::filesystem::remove_all(directory.string() + "-objects");
    std::filesystem::remove_all(directory.string() + "-other");
    objectSync();
    machineSync();
    int pair[2];
    assert(!socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair));
    int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    assert(sendPacket(pair[0], {{"type", "fd"}}, {fd}));
    close(fd);
    {
        auto p = receivePacket(pair[1]);
        assert(p && p->fds.size() == 1 && (fcntl(p->fds[0], F_GETFD) & FD_CLOEXEC));
    }
    assert(!receivePacket(pair[1]));
    // The independent worker thread terminates even if the renderer thread stalls.
    auto pid = fork();
    assert(pid >= 0);
    if (!pid) {
        close(pair[0]);
        Watchdog watchdog(pair[1]);
        std::this_thread::sleep_for(std::chrono::seconds(3));
        _exit(99);
    }
    close(pair[1]);
    int status;
    waitpid(pid, &status, 0);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 72);
    close(pair[0]);
}
