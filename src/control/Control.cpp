#include "Control.hpp"
#include <regex>
namespace hyprune::control {
namespace {
using Json = ipc::Json;
using Clock = std::chrono::steady_clock;
template <class Predicate> void eraseJson(Json &j, Predicate p) {
    j.erase(std::remove_if(j.begin(), j.end(), p), j.end());
}
void require(bool b, int code, const char *text) {
    if (!b)
        throw ipc::Error(code, text);
}
void fields(const Json &p, std::initializer_list<const char *> required,
            std::initializer_list<const char *> optional = {}) {
    require(p.is_object(), -32602, "params must be object");
    std::set<std::string> keys;
    for (auto k : required) {
        require(p.contains(k), -32602, "missing parameter");
        keys.insert(k);
    }
    for (auto k : optional)
        keys.insert(k);
    for (auto i = p.begin(); i != p.end(); ++i)
        require(keys.contains(i.key()), -32602, "unknown parameter");
}
Vec3 vector(const Json &j) {
    require(j.is_array() && j.size() == 3, -32602, "expected vec3");
    float v[3];
    for (int i = 0; i < 3; i++) {
        require(j[i].is_number(), -32602, "expected number");
        v[i] = j[i];
        require(std::isfinite(v[i]) && std::abs(v[i]) <= 100000, -32602, "position out of bounds");
    }
    return {v[0], v[1], v[2]};
}
Json xyz(Vec3 v) {
    return Json::array({v.x, v.y, v.z});
}
void text(const Json &v, size_t max = 128) {
    require(v.is_string() && !v.get_ref<const std::string &>().empty() &&
                v.get_ref<const std::string &>().size() <= max,
            -32602, "invalid string");
}
Json orientation(const sim::Camera &c) {
    float cy = std::cos(-c.yaw / 2), sy = std::sin(-c.yaw / 2), cp = std::cos(c.pitch / 2), sp = std::sin(c.pitch / 2);
    return Json::array({cy * sp, sy * cp, -sy * sp, cy * cp});
}
} // namespace
std::map<std::string, std::string> Control::methods() {
    return {{"camera.get", "state.read"},
            {"camera.set", "movement.control"},
            {"waypoint.set", "world.control"},
            {"waypoint.clear", "world.control"},
            {"route.set", "world.control"},
            {"route.clear", "world.control"},
            {"travel.start", "movement.control"},
            {"travel.cancel", "movement.control"},
            {"autodrive.start", "movement.control"},
            {"autodrive.stop", "movement.control"},
            {"shell.claim", "shell.control"},
            {"shell.release", "shell.control"},
            {"shell.overlay", "shell.control"},
            {"ui.open", "shell.control"},
            {"ui.close", "shell.control"},
            {"data.publish", "data.publish"},
            {"data.remove", "data.publish"}};
}
Vec3 Control::target(const Json &t, const world::World *w) const {
    require(w, -32005, "no world loaded");
    if (t.contains("position")) {
        fields(t, {"position"});
        return vector(t["position"]);
    }
    fields(t, {"zoneId"});
    text(t["zoneId"]);
    for (const auto &z : w->manifest.value("zones", Json::array()))
        if (z.at("id") == t["zoneId"]) {
            auto a = vector(z.at("bounds").at("min")), b = vector(z.at("bounds").at("max"));
            return {(a.x + b.x) / 2, a.y + 1.7f, (a.z + b.z) / 2};
        }
    throw ipc::Error(-32003, "unknown zone");
}
void Control::transition(const std::string &phase, float progress) {
    if (location["transition"].is_null())
        return;
    location["transition"]["monotonicMs"] =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
    location["transition"]["phase"] = phase;
    location["transition"]["progress"] = progress;
    events.emplace_back("location.transition", location["transition"]);
}
void Control::beginLoad(const Json &to, const Json &output) {
    stop();
    location["transition"] = {
        {"id", "transition-" + std::to_string(++transitions)},
        {"outputId", output},
        {"phase", "loading"},
        {"progress", nullptr},
        {"to", to},
        {"monotonicMs",
         std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count()}};
    events.emplace_back("location.transition", location["transition"]);
}
void Control::endLoad(bool success, bool cancelled) {
    transition(success ? "arrived" : cancelled ? "cancelled" : "failed", success ? 1.f : 0.f);
}
void Control::stop() {
    holding = false;
    held = 0;
    opened = false;
    location["autodrive"] = false;
    if (duration > 0)
        transition("cancelled", std::clamp(elapsed / duration, 0.f, 1.f));
    duration = 0;
    for (auto &l : leases)
        for (auto &o : l["overlays"]) {
            if (o["visible"] == true)
                events.emplace_back("ui.closed", Json{{"leaseId", l["id"]}, {"outputId", o["outputId"]}});
            o["visible"] = false;
            o["exclusiveInput"] = false;
        }
}
void Control::reset(const world::World *w) {
    stop();
    location = {{"zones", w ? w->manifest.value("zones", Json::array()) : Json::array()},
                {"waypoint", nullptr},
                {"route", Json::array()},
                {"autodrive", false},
                {"transition", nullptr}};
}
void Control::associate(const std::string &actor, bool verified) {
    if (verified)
        associations.insert(actor);
    else {
        associations.erase(actor);
        for (auto &l : leases)
            if (owners.at(l.at("id").get<std::string>()) == actor)
                for (auto &o : l["overlays"]) {
                    if (o["visible"] == true)
                        events.emplace_back("ui.closed", Json{{"leaseId", l["id"]}, {"outputId", o["outputId"]}});
                    o["visible"] = false;
                    o["exclusiveInput"] = false;
                }
    }
}
bool Control::ownsLease(const std::string &actor, const std::string &lease) const {
    return owners.contains(lease) && owners.at(lease) == actor;
}
bool Control::exclusive() const {
    for (const auto &l : leases)
        for (const auto &o : l["overlays"])
            if (o["exclusiveInput"] == true)
                return true;
    return false;
}
void Control::disconnect(const std::string &actor) {
    associations.erase(actor);
    eraseJson(leases, [&](const Json &l) { return owners[l.at("id").get<std::string>()] == actor; });
    eraseJson(sources, [&](const Json &s) { return owners[s.at("sourceId").get<std::string>()] == actor; });
    std::erase_if(owners, [&](const auto &o) {
        if (o.second != actor)
            return false;
        expiry.erase(o.first);
        publications.erase(o.first);
        return true;
    });
}
void Control::retainOutputs(const Json &outputs) {
    auto absent = [&](const Json &id) { return std::find(outputs.begin(), outputs.end(), id) == outputs.end(); };
    for (auto &lease : leases) {
        eraseJson(lease["outputs"], absent);
        eraseJson(lease["overlays"], [&](const Json &o) { return absent(o["outputId"]); });
    }
    eraseJson(leases, [&](const Json &lease) {
        if (!lease["outputs"].empty())
            return false;
        owners.erase(lease["id"].get<std::string>());
        return true;
    });
}
bool Control::driving() const {
    return duration > 0 || location["autodrive"] == true;
}
bool Control::uiVisible() const {
    for (const auto &l : leases)
        for (const auto &o : l["overlays"])
            if (o["visible"] == true)
                return true;
    return false;
}
void Control::hold(bool pressed) {
    if (!pressed) {
        holding = false;
        held = 0;
        opened = false;
    } else if (!holding) {
        holding = true;
        held = 0;
    }
}
Control::Json Control::execute(const std::string &m, const Json &p, const std::string &actor, const std::string &client,
                               const world::World *w, sim::Camera &camera, const Json &outputs) {
    if (m == "camera.get") {
        fields(p, {});
        return {{"position", xyz(camera.position)}, {"orientation", orientation(camera)}};
    }
    if (m == "camera.set") {
        fields(p, {"position", "orientation"});
        auto position = vector(p["position"]);
        auto q = p["orientation"];
        require(q.is_array() && q.size() == 4, -32602, "expected quaternion");
        float x = q[0], y = q[1], z = q[2], a = q[3];
        require(std::isfinite(x + y + z + a) && std::abs(x * x + y * y + z * z + a * a - 1) < .001f, -32602,
                "quaternion must be normalized");
        Vec3 f{-2 * (x * z + a * y), -2 * (y * z - a * x), -(1 - 2 * (x * x + y * y))};
        float yaw = std::atan2(f.x, -f.z), pitch = std::asin(std::clamp(f.y, -1.f, 1.f));
        sim::Camera proposed = camera;
        proposed.yaw = yaw;
        proposed.pitch = pitch;
        auto canonical = orientation(proposed);
        float agreement = std::abs(x * canonical[0].get<float>() + y * canonical[1].get<float>() +
                                   z * canonical[2].get<float>() + a * canonical[3].get<float>());
        require(agreement > .9999f && std::abs(pitch) < 1.49f, -32007, "camera roll or vertical pitch unsupported");
        require(w, -32005, "no world loaded");
        location["autodrive"] = false;
        if (duration > 0)
            transition("cancelled", elapsed / duration);
        duration = 0;
        camera = proposed;
        camera.position = position;
        camera.verticalSpeed = 0;
    } else if (m == "waypoint.set") {
        fields(p, {"id", "label"}, {"target", "position"});
        text(p["id"]);
        text(p["label"], 256);
        require(p.contains("target") != p.contains("position"), -32602, "one target required");
        auto t = p.contains("target") ? p["target"] : Json{{"position", p["position"]}};
        auto point = target(t, w);
        location["waypoint"] = {{"id", p["id"]}, {"label", p["label"]}, {"target", t}, {"position", xyz(point)}};
    } else if (m == "waypoint.clear") {
        fields(p, {});
        location["waypoint"] = nullptr;
    } else if (m == "route.set") {
        fields(p, {"points"});
        require(p["points"].is_array() && !p["points"].empty() && p["points"].size() <= 64, -32602,
                "route requires 1..64 points");
        Json points = Json::array();
        for (auto &t : p["points"])
            points.push_back({{"target", t}, {"position", xyz(target(t, w))}});
        location["route"] = points;
        location["autodrive"] = false;
        routeIndex = 0;
    } else if (m == "route.clear") {
        fields(p, {});
        location["route"] = Json::array();
        location["autodrive"] = false;
    } else if (m == "travel.start") {
        fields(p, {"to"}, {"durationMs"});
        auto to = target(p["to"], w);
        sim::Camera probe;
        probe.position = to;
        sim::resolveCapsule(probe, w->collision);
        require(length(probe.position - to) < .03f, -32004, "destination obstructed");
        if (p.contains("durationMs"))
            require(p["durationMs"].is_number_integer(), -32602, "duration must be integer");
        int ms = p.value("durationMs", 600);
        require(ms >= 0 && ms <= 10000, -32602, "duration out of bounds");
        require(!driving(), -32004, "movement already in progress");
        stop(); // Departing closes Director overlays before passive warp presentation.
        travelFrom = camera.position;
        travelTo = to;
        elapsed = 0;
        duration = ms / 1000.f;
        location["transition"] = {{"id", "transition-" + std::to_string(++transitions)},
                                  {"outputId", outputId},
                                  {"phase", "departing"},
                                  {"progress", 0},
                                  {"to", p["to"]}};
        transition("departing", 0);
        if (!ms) {
            camera.position = to;
            camera.verticalSpeed = 0;
            transition("arrived", 1);
        }
    } else if (m == "travel.cancel") {
        fields(p, {});
        if (duration > 0)
            transition("cancelled", elapsed / duration);
        duration = 0;
    } else if (m == "autodrive.start") {
        fields(p, {}, {"speed"});
        require(w && !location["route"].empty(), -32005, "route required");
        require(!driving(), -32004, "movement already in progress");
        speed = p.value("speed", 3.5f);
        require(std::isfinite(speed) && speed >= .1f && speed <= 10, -32602, "speed out of bounds");
        stop();
        routeIndex = 0;
        location["autodrive"] = true;
    } else if (m == "autodrive.stop") {
        fields(p, {});
        location["autodrive"] = false;
    } else if (m == "shell.claim") {
        fields(p, {"outputs"});
        require(p["outputs"].is_array() && !p["outputs"].empty() && p["outputs"].size() <= 16, -32602,
                "outputs required");
        std::set<std::string> unique;
        for (const auto &o : p["outputs"]) {
            text(o);
            require(std::find(outputs.begin(), outputs.end(), o) != outputs.end(), -32003, "unknown output");
            require(unique.insert(o.get<std::string>()).second, -32602, "duplicate output");
            for (const auto &l : leases)
                require(std::find(l["outputs"].begin(), l["outputs"].end(), o) == l["outputs"].end(), -32004,
                        "output already leased");
        }
        std::string id = "lease-" + ipc::randomId().substr(0, 16);
        owners[id] = actor;
        Json overlays = Json::array();
        for (auto &o : p["outputs"])
            overlays.push_back({{"outputId", o}, {"visible", false}, {"exclusiveInput", false}});
        leases.push_back({{"id", id}, {"outputs", p["outputs"]}, {"overlays", overlays}});
        return {{"leaseId", id}};
    } else if (m == "shell.release" || m == "shell.overlay" || m == "ui.open" || m == "ui.close") {
        if (m == "shell.release")
            fields(p, {"leaseId"});
        else if (m == "shell.overlay")
            fields(p, {"leaseId", "outputId", "visible", "exclusiveInput"});
        else if (m == "ui.open")
            fields(p, {"leaseId", "outputId", "panel"});
        else
            fields(p, {"leaseId", "outputId"});
        text(p["leaseId"]);
        std::string id = p["leaseId"];
        require(owners.contains(id) && owners.at(id) == actor, -32004, "lease not owned by connection");
        if (m == "shell.release") {
            eraseJson(leases, [&](const Json &l) { return l["id"] == id; });
            owners.erase(id);
            return Json::object();
        }
        if (m == "ui.open")
            text(p["panel"], 128);
        if (m == "shell.overlay") {
            require(p["visible"].is_boolean() && p["exclusiveInput"].is_boolean(), -32602, "invalid overlay flags");
            require(p["visible"] == true || p["exclusiveInput"] == false, -32602, "hidden overlay cannot grab input");
            require(p["exclusiveInput"] == false || associations.contains(actor), -32007,
                    "exclusive input requires verified Wayland association");
        }
        bool found = false;
        for (auto &l : leases)
            if (l["id"] == id)
                for (auto &o : l["overlays"])
                    if (o["outputId"] == p["outputId"]) {
                        found = true;
                        o["visible"] = m == "ui.open" || (m == "shell.overlay" && p["visible"] == true);
                        o["exclusiveInput"] = m == "shell.overlay" && p["exclusiveInput"] == true;
                    }
        require(found, -32003, "output not leased");
        if (m == "ui.open" || m == "ui.close")
            events.emplace_back(m == "ui.open" ? "ui.opened" : "ui.closed", p);
    } else if (m == "data.publish" || m == "data.remove") {
        if (m == "data.remove")
            fields(p, {"sourceId"});
        else
            fields(p, {"sourceId", "values", "sequence"}, {"ttlMs"});
        text(p["sourceId"]);
        std::string id = p["sourceId"];
        require(std::regex_match(id, std::regex("[a-z][a-z0-9-]*(\\.[a-z][a-z0-9-]*)+")), -32602, "invalid source ID");
        require(id == client || id.starts_with(client + "."), -32002, "source outside authenticated client namespace");
        require(!owners.contains(id) || owners.at(id) == actor, -32004, "source owned by another connection");
        if (m == "data.remove") {
            eraseJson(sources, [&](const Json &s) { return s["sourceId"] == id; });
            owners.erase(id);
            expiry.erase(id);
            publications.erase(id);
            return Json::object();
        }
        auto &values = p["values"];
        require(values.is_object() && values.size() <= 32 && values.dump().size() <= 16384, -32602,
                "invalid data values");
        for (auto it = values.begin(); it != values.end(); ++it) {
            require(it.key().size() <= 128, -32602, "data key too long");
            require(it.value().is_boolean() || it.value().is_number() ||
                        (it.value().is_string() && it.value().get_ref<const std::string &>().size() <= 256),
                    -32602, "data value must be scalar");
        }
        require(p["sequence"].is_number_unsigned() && p["sequence"].get<uint64_t>() <= 9007199254740991ULL, -32602,
                "invalid sequence");
        if (p.contains("ttlMs"))
            require(p["ttlMs"].is_number_integer(), -32602, "ttl must be integer");
        int ttl = p.value("ttlMs", 5000);
        require(ttl >= 100 && ttl <= 60000, -32602, "invalid ttl");
        auto now = Clock::now();
        require(!publications.contains(id) || now - publications[id] >= std::chrono::milliseconds(100), -32008,
                "publication rate exceeds 10 Hz");
        auto it = std::find_if(sources.begin(), sources.end(), [&](const Json &s) { return s["sourceId"] == id; });
        if (it != sources.end())
            require(p["sequence"] > (*it)["sequence"], -32004, "stale source sequence");
        else
            require(sources.size() < 32, -32008, "source limit");
        size_t aggregate = values.dump().size();
        for (const auto &source : sources)
            if (source["sourceId"] != id)
                aggregate += source["values"].dump().size();
        require(aggregate <= 65536, -32008, "aggregate integration data exceeds 64 KiB");
        Json value = {
            {"sourceId", id},
            {"values", values},
            {"sequence", p["sequence"]},
            {"monotonicMs", std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()},
            {"ttlMs", ttl}};
        if (it == sources.end())
            sources.push_back(value);
        else
            *it = value;
        owners[id] = actor;
        publications[id] = now;
        expiry[id] = now + std::chrono::milliseconds(ttl);
        events.emplace_back("data.updated", value);
    } else
        throw ipc::Error(-32601, "unknown method");
    return Json::object();
}
void Control::tick(float dt, sim::Camera &camera, const world::World *w) {
    dt = std::clamp(dt, 0.f, .1f);
    auto now = Clock::now();
    eraseJson(sources, [&](const Json &s) {
        auto id = s["sourceId"].get<std::string>();
        if (expiry[id] > now)
            return false;
        owners.erase(id);
        expiry.erase(id);
        publications.erase(id);
        return true;
    });
    if (holding && !opened) {
        held += dt;
        if (held >= .35f) {
            opened = true;
            for (auto &l : leases)
                for (auto &o : l["overlays"]) {
                    o["visible"] = true;
                    events.emplace_back("ui.opened",
                                        Json{{"leaseId", l["id"]}, {"outputId", o["outputId"]}, {"panel", "overview"}});
                }
        }
    }
    if (duration > 0) {
        elapsed += dt;
        float p = std::min(1.f, elapsed / duration);
        float eased = p * p * (3 - 2 * p);
        camera.position = travelFrom + (travelTo - travelFrom) * eased;
        camera.verticalSpeed = 0;
        transition(p >= 1 ? "arrived" : "travelling", p);
        if (p >= 1)
            duration = 0;
    } else if (location["autodrive"] == true && w) {
        auto to = vector(location["route"][routeIndex]["position"]);
        auto d = to - camera.position;
        float distance = length(d);
        Vec3 next = distance <= speed * dt ? to : camera.position + d * (speed * dt / std::max(distance, .0001f));
        // Reuse the capsule solver: stop at obstruction rather than tunnel through world geometry.
        sim::Camera probe = camera;
        auto delta = next - camera.position;
        int steps = std::max(1, int(std::ceil(length(delta) / .05f)));
        for (int step = 0; step < steps; step++) {
            probe.position = probe.position + delta / float(steps);
            sim::resolveCapsule(probe, w->collision);
        }
        bool blocked = length(probe.position - next) > .03f;
        if (blocked) {
            location["autodrive"] = false;
            return;
        }
        camera.position = next;
        camera.verticalSpeed = 0;
        if (distance <= speed * dt) {
            if (++routeIndex >= location["route"].size())
                location["autodrive"] = false;
        }
    }
}
} // namespace hyprune::control
