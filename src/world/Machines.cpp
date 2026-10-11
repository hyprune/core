#include "Machines.hpp"
#include "interaction/InputFile.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <stdexcept>
namespace hyprune::world {
namespace {
void require(bool good, const std::string &why) {
    if (!good)
        throw std::runtime_error(why);
}
Vec3 vec3(const Json &j, const std::string &what) {
    require(j.is_array() && j.size() == 3 && j[0].is_number() && j[1].is_number() && j[2].is_number(), what + ": expected [x, y, z]");
    Vec3 v{j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
    require(std::isfinite(v.x + v.y + v.z), what + ": nonfinite");
    return v;
}
std::array<float, 4> quaternion(const Json &j) {
    if (!j.is_array() || j.size() != 4)
        return {0, 0, 0, 1};
    std::array<float, 4> q{j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
    float n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    require(n > .5f, "rotation must be a unit quaternion");
    for (auto &c : q)
        c /= n;
    return q;
}
Mat4 translation(Vec3 p) {
    auto m = Mat4::identity();
    m.v[12] = p.x, m.v[13] = p.y, m.v[14] = p.z;
    return m;
}
// Rotation about +Y turning +Z towards the horizontal direction d, then uniform scale.
Mat4 heading(Vec3 d, float scale = 1) {
    float yaw = std::atan2(d.x, d.z), c = std::cos(yaw) * scale, s = std::sin(yaw) * scale;
    auto m = Mat4::identity();
    m.v[0] = c, m.v[2] = -s, m.v[8] = s, m.v[10] = c, m.v[5] = scale;
    return m;
}
bool same(const Json &a, const Json &b) {
    if (a.is_number() && b.is_number())
        return a.get<double>() == b.get<double>();
    return a == b;
}
uint64_t mix(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ull; }
uint64_t mix(uint64_t h, const std::string &s) {
    for (unsigned char c : s)
        h = mix(h, c);
    return mix(h, 0xff);
}
float hashUnit(uint32_t &state) { // xorshift32, deterministic effect pieces
    state ^= state << 13, state ^= state >> 17, state ^= state << 5;
    return float(state & 0xffffff) / float(0x1000000);
}
} // namespace
namespace machines {
double number(const Json &value, double fallback) {
    if (value.is_boolean())
        return value.get<bool>() ? 1 : 0;
    if (value.is_number())
        return value.get<double>();
    if (value.is_string()) {
        const auto &s = value.get_ref<const std::string &>();
        char *end = nullptr;
        double d = std::strtod(s.c_str(), &end);
        if (!s.empty() && end && *end == 0 && std::isfinite(d))
            return d;
    }
    return fallback;
}
std::string text(const Json &value, int decimals, bool compact) {
    if (value.is_string())
        return value.get<std::string>();
    if (value.is_boolean())
        return value.get<bool>() ? "true" : "false";
    if (!value.is_number())
        return "";
    double d = value.get<double>();
    const char *suffix = "";
    if (compact && std::abs(d) >= 1e6)
        d /= 1e6, suffix = "M";
    else if (compact && std::abs(d) >= 1e3)
        d /= 1e3, suffix = "k";
    int places = decimals >= 0 ? decimals : *suffix ? 1 : std::abs(d - std::round(d)) < 1e-9 ? 0 : 2;
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f%s", places, d, suffix);
    return buffer;
}
Mapping Mapping::parse(const Json &j) {
    Mapping m;
    if (j.is_null() || (j.is_object() && j.empty()))
        return m;
    require(j.is_object(), "mapping must be an object");
    if (j.contains("else"))
        m.otherwise = j["else"];
    if (j.contains("cases")) {
        m.kind = Cases;
        require(j["cases"].is_array() && j["cases"].size() <= 16, "at most 16 cases");
        for (auto &c : j["cases"]) {
            Case k;
            if (c.contains("in")) {
                require(c["in"].is_array(), "cases: `in` takes an array");
                for (auto &v : c["in"])
                    k.match.push_back(v);
            } else
                k.match.push_back(c.at("equals"));
            k.to = c.at("to");
            m.cases.push_back(std::move(k));
        }
    } else if (j.contains("thresholds")) {
        m.kind = Thresholds;
        require(j["thresholds"].is_array() && j["thresholds"].size() <= 16, "at most 16 thresholds");
        for (auto &c : j["thresholds"]) {
            Case k;
            k.below = c.contains("below");
            k.bound = (k.below ? c.at("below") : c.at("atLeast")).get<double>();
            k.to = c.at("to");
            m.cases.push_back(std::move(k));
        }
    } else if (j.contains("scale")) {
        m.kind = Scale;
        auto &s = j["scale"];
        m.from0 = s.at("from").at(0), m.from1 = s.at("from").at(1), m.to0 = s.at("to").at(0), m.to1 = s.at("to").at(1);
        require(m.from0 != m.from1, "scale: empty `from` range");
        m.clamp = j.value("clamp", true);
        m.round = j.value("round", "");
        require(m.round.empty() || m.round == "floor" || m.round == "ceil" || m.round == "nearest", "scale: unknown round");
    } else if (j.contains("format")) {
        m.kind = Format;
        auto &f = j["format"];
        m.decimals = f.value("decimals", -1);
        m.compact = f.value("compact", false);
        m.unit = f.value("unit", "");
    } else if (j.contains("becomes")) {
        m.kind = Becomes;
        m.becomes = j["becomes"];
        m.from = j.value("from", Json(nullptr));
    } else if (j.contains("counter")) {
        m.kind = Counter;
    } else
        throw std::runtime_error("unknown mapping " + j.dump());
    return m;
}
std::optional<Json> Mapping::apply(const Json &value) const {
    switch (kind) {
    case Direct:
    case Becomes:
    case Counter:
        return value;
    case Cases:
        for (auto &c : cases)
            for (auto &m : c.match)
                if (same(m, value))
                    return c.to;
        break;
    case Thresholds: {
        if (!value.is_number() && !value.is_boolean())
            break;
        double v = number(value);
        for (auto &c : cases)
            if (c.below ? v < c.bound : v >= c.bound)
                return c.to;
        break;
    }
    case Scale: {
        double v = number(value, NAN);
        if (!std::isfinite(v))
            break;
        double out = to0 + (v - from0) / (from1 - from0) * (to1 - to0);
        if (clamp)
            out = std::clamp(out, std::min(to0, to1), std::max(to0, to1));
        if (round == "floor")
            out = std::floor(out);
        else if (round == "ceil")
            out = std::ceil(out);
        else if (round == "nearest")
            out = std::round(out);
        return out;
    }
    case Format: {
        auto t = text(value, decimals, compact);
        return unit.empty() ? t : t + " " + unit;
    }
    }
    if (otherwise)
        return *otherwise;
    return std::nullopt;
}
int EventGate::feed(const Mapping &m, const Json &value) {
    if (m.kind == Mapping::Becomes) {
        bool fires = !previous.is_null() && !same(previous, value) && same(value, m.becomes) &&
                     (m.from.is_null() || same(previous, m.from));
        previous = value;
        return fires;
    }
    double v = number(value, NAN);
    if (!std::isfinite(v) || v < 0)
        return 0;
    if (!counter || v < *counter) { // first value or a reset: fire nothing
        counter = v;
        return 0;
    }
    double n = std::floor(v - *counter);
    if (n >= 1)
        counter = *counter + n;
    return int(std::min(n, 1e6));
}
Track Track::build(const std::vector<std::pair<Vec3, std::string>> &points, bool closed) {
    require(points.size() >= 2, "a track needs at least two points");
    Track t;
    auto p = [&](long i) { long n = long(points.size()); return points[size_t(((i % n) + n) % n)].first; };
    t.samples.push_back(points[0].first);
    t.pointAt.push_back(0);
    size_t segments = closed ? points.size() : points.size() - 1;
    for (size_t s = 0; s < segments; ++s) {
        long i = long(s) + 1;
        Vec3 a = p(i - 1), b = p(i);
        const auto &type = points[size_t(i) % points.size()].second;
        Vec3 previous = (closed || i >= 2) ? p(i - 2) : a;
        Vec3 m0 = (b - previous) * .5f, m1 = b - a;
        Vec3 chord = b - a, flat{chord.x, 0, chord.z};
        require(type == "line" || type == "curve" || type == "arc", "unknown segment type " + type);
        require(type != "arc" || hyprune::length(flat) >= .1f, "an arc needs a horizontal chord of at least 0.1 m");
        Vec3 side = normalized(cross(chord, Vec3{0, 1, 0}));
        for (int k = 1; k <= 24; ++k) {
            float u = k / 24.f, u2 = u * u, u3 = u2 * u;
            Vec3 q = a + chord * u;
            if (type == "curve")
                q = a * (2 * u3 - 3 * u2 + 1) + m0 * (u3 - 2 * u2 + u) + b * (-2 * u3 + 3 * u2) + m1 * (u3 - u2);
            else if (type == "arc")
                q = q + side * (.3f * hyprune::length(chord) * std::sin(3.14159265f * u));
            t.samples.push_back(q);
        }
        t.pointAt.push_back(0); // filled below
    }
    t.distance.assign(t.samples.size(), 0);
    for (size_t i = 1; i < t.samples.size(); ++i)
        t.distance[i] = t.distance[i - 1] + hyprune::length(t.samples[i] - t.samples[i - 1]);
    for (size_t s = 1; s < t.pointAt.size(); ++s)
        t.pointAt[s] = t.distance[s * 24];
    if (closed)
        t.pointAt.pop_back(); // the closing point is point 0
    return t;
}
Vec3 Track::at(float s, Vec3 *tangent) const {
    s = std::clamp(s, 0.f, length());
    size_t k = std::upper_bound(distance.begin(), distance.end(), s) - distance.begin();
    k = std::clamp<size_t>(k, 1, samples.size() - 1);
    float span = distance[k] - distance[k - 1], u = span > 1e-6f ? (s - distance[k - 1]) / span : 0;
    if (tangent)
        *tangent = normalized(samples[k] - samples[k - 1]);
    return samples[k - 1] + (samples[k] - samples[k - 1]) * u;
}
float Track::nearest(Vec3 p) const {
    size_t best = 0;
    for (size_t i = 1; i < samples.size(); ++i)
        if (hyprune::length(samples[i] - p) < hyprune::length(samples[best] - p))
            best = i;
    return distance[best];
}
} // namespace machines
using machines::Mapping;
int Machines::Palette::find(const std::string &id) const {
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].first == id)
            return int(i);
    return -1;
}
std::vector<unsigned> Machines::subtreeMeshes(unsigned asset, unsigned node) const {
    const auto &a = world_->objectAssets[asset];
    std::vector<unsigned> out;
    for (unsigned m = 0; m < a.meshes.size(); ++m)
        for (int n = int(a.meshes[m].node); n >= 0; n = a.nodeParents[n])
            if (unsigned(n) == node) {
                out.push_back(m);
                break;
            }
    return out;
}
Machines::Definition Machines::define(unsigned asset, const Json &manifest) const {
    const auto &a = world_->objectAssets[asset];
    Definition d;
    d.asset = asset;
    d.id = a.id;
    const std::string where = "machine " + a.id;
    auto node = [&](const std::string &name) {
        int found = -1;
        for (size_t i = 0; i < a.nodeNames.size(); ++i)
            if (a.nodeNames[i] == name) {
                require(found < 0, where + ": node \"" + name + "\" is not unique");
                found = int(i);
            }
        require(found >= 0, where + ": no node \"" + name + "\"");
        return unsigned(found);
    };
    auto animationOf = [&](const std::string &name) {
        for (size_t i = 0; i < a.animations.size(); ++i)
            if (a.animations[i].name == name)
                return int(i);
        throw std::runtime_error(where + ": no decorative animation on node \"" + name + "\"");
    };
    auto pose = [&](const Json &p, const std::string &what) {
        return std::make_pair(vec3(p.at("position"), where + " " + what), quaternion(p.value("rotation", Json(nullptr))));
    };
    const auto &machine = manifest.at("traits").at("machine");
    const auto &slots = manifest.value("slots", Json::object());
    auto kind = [&](const char *name) {
        auto list = slots.value(name, Json::array());
        require(list.is_array() && list.size() <= 8, where + ": at most 8 " + name + " slots");
        return list;
    };
    // Inputs; `repeat` expands id to id.1 .. id.N.
    std::map<std::string, int> repeats;
    for (auto &j : machine.at("inputs")) {
        Input in;
        auto id = j.at("id").get<std::string>();
        auto type = j.at("type").get<std::string>();
        static const std::map<std::string, Input::Type> types{{"enum", Input::Enum}, {"int", Input::Int}, {"number", Input::Number},
                                                              {"bool", Input::Bool}, {"text", Input::Text}, {"event", Input::Event}};
        require(types.contains(type), where + ": input " + id + " has unknown type " + type);
        in.type = types.at(type);
        for (auto &v : j.value("values", Json::array()))
            in.values.push_back(v.get<std::string>());
        require(in.type != Input::Enum || !in.values.empty(), where + ": enum input " + id + " needs values");
        in.min = j.value("min", in.type == Input::Number ? 0. : -1e9);
        in.max = j.value("max", in.type == Input::Number ? 1. : 1e9);
        in.maxLength = j.value("maxLength", 64);
        in.fallback = j.contains("default") ? j["default"]
                      : in.type == Input::Enum ? Json(in.values[0])
                      : in.type == Input::Text ? Json("")
                      : in.type == Input::Bool ? Json(false)
                      : in.type == Input::Event ? Json(nullptr)
                                                : Json(std::max(0., in.min));
        int repeat = j.value("repeat", 1);
        require(repeat >= 1 && repeat <= 16, where + ": repeat must be 2-16");
        if (j.contains("repeat"))
            repeats[id] = repeat;
        for (int k = 1; k <= repeat; ++k) {
            in.id = j.contains("repeat") ? id + "." + std::to_string(k) : id;
            d.inputs.push_back(in);
        }
    }
    require(d.inputs.size() <= 16, where + ": at most 16 inputs, counting repeats");
    d.primary = machine.value("primary", "");
    // Slots.
    for (auto &j : kind("states")) {
        Palette p;
        p.id = j.at("id");
        std::set<unsigned> meshes;
        for (auto &name : j.value("materials", Json::array())) {
            bool found = false;
            for (size_t i = 0; i < a.materialNames.size(); ++i)
                if (a.materialNames[i] == name.get<std::string>())
                    for (unsigned m = 0; m < a.meshes.size(); ++m)
                        if (a.meshes[m].material == a.firstMaterial + i)
                            meshes.insert(m), found = true;
            require(found, where + ": states " + p.id + ": no drawn mesh uses material " + name.get<std::string>());
        }
        for (auto &name : j.value("nodes", Json::array()))
            for (auto m : subtreeMeshes(asset, node(name)))
                meshes.insert(m);
        require(!meshes.empty(), where + ": states " + p.id + " paints nothing");
        p.meshes.assign(meshes.begin(), meshes.end());
        require(j.at("palette").size() >= 1 && j["palette"].size() <= 16, where + ": states " + p.id + ": 1-16 palette entries");
        for (auto &e : j["palette"]) {
            MeshPaint paint;
            paint.painted = true;
            paint.colour = vec3(e.at("colour"), where + " palette colour");
            paint.emission = e.value("emission", 0.f);
            p.entries.push_back({e.at("id").get<std::string>(), paint});
        }
        p.stale = p.find("stale");
        d.palettes.push_back(std::move(p));
    }
    for (auto &j : kind("labels")) {
        LabelSlot l;
        l.id = j.at("id");
        auto [position, q] = pose(j.at("pose"), "label " + l.id);
        float w = j.at("size").at(0), h = j.at("size").at(1);
        l.right = rotate(q, {1, 0, 0}) * (w / 2), l.up = rotate(q, {0, 1, 0}) * (h / 2);
        l.centre = position + rotate(q, {0, 0, 1}) * .004f; // just in front of the panel
        l.maxChars = std::clamp<size_t>(j.value("maxChars", 24), 1, 64);
        if (j.contains("colour"))
            l.colour = vec3(j["colour"], where + " label colour");
        d.labels.push_back(std::move(l));
    }
    auto itemAsset = [&](const std::string &id) {
        for (unsigned i = 0; i < world_->objectAssets.size(); ++i)
            if (world_->objectAssets[i].id == id)
                return i;
        throw std::runtime_error(where + ": item asset " + id + " is not in objectAssets");
    };
    for (auto &j : kind("tracks")) {
        TrackSlot t;
        t.id = j.at("id");
        std::vector<std::pair<Vec3, std::string>> points;
        require(j.at("points").size() <= 32, where + ": at most 32 track points");
        for (auto &p : j["points"])
            points.push_back({vec3(p.at("position"), where + " track point"), p.value("type", "line")});
        t.track = machines::Track::build(points, false);
        t.speed = j.value("speed", .6f);
        require(t.speed >= .1f && t.speed <= 30, where + ": track speed must be 0.1-30 m/s");
        auto items = j.value("items", Json::object());
        t.max = std::clamp<size_t>(items.value("max", 24), 1, 256);
        t.spacing = items.value("spacing", .45f);
        t.dropOldest = items.value("overflow", "refuse") == "drop-oldest";
        for (auto &id : items.value("assets", Json::array()))
            t.assets.push_back(itemAsset(id));
        for (auto &s : j.value("stops", Json::array())) {
            size_t point = s.at("point");
            require(point < t.track.pointAt.size(), where + ": track stop point out of range");
            if (s.value("seconds", 0.f) > 0)
                t.stops.push_back({t.track.pointAt[point], s["seconds"].get<float>()});
        }
        std::sort(t.stops.begin(), t.stops.end());
        d.tracks.push_back(std::move(t));
    }
    for (auto &j : kind("spawns")) {
        SpawnSlot s;
        s.id = j.at("id");
        auto [position, q] = pose(j.at("pose"), "spawn " + s.id);
        (void)q;
        if (j.contains("track")) {
            for (size_t i = 0; i < d.tracks.size(); ++i)
                if (d.tracks[i].id == j["track"].get<std::string>())
                    s.track = int(i);
            require(s.track >= 0, where + ": spawn " + s.id + " names no track slot");
            s.at = d.tracks[s.track].track.nearest(position);
            auto items = j.value("items", Json::array());
            require(!items.empty() || !d.tracks[s.track].assets.empty(), where + ": spawn " + s.id + " has no item asset");
            s.asset = items.empty() ? d.tracks[s.track].assets[0] : itemAsset(items[0]);
        }
        s.maxRate = std::clamp(j.value("maxRate", 2.f), .1f, 10.f);
        d.spawns.push_back(std::move(s));
    }
    for (auto &j : kind("stacks")) {
        StackSlot s;
        s.id = j.at("id");
        s.node = node(j.at("node"));
        s.meshes = subtreeMeshes(asset, s.node);
        require(!s.meshes.empty(), where + ": stack " + s.id + " node draws nothing");
        auto layout = j.at("layout");
        for (int k = 0; k < 3; ++k)
            s.grid[k] = std::max(1, layout.at("grid").at(k).get<int>());
        s.spacing = vec3(layout.at("spacing"), where + " stack spacing");
        std::string fill = layout.value("fill", "xzy");
        require(fill.size() == 3 && std::is_permutation(fill.begin(), fill.end(), std::string("xyz").begin()),
                where + ": stack fill must order x, y and z");
        for (int k = 0; k < 3; ++k)
            s.order[k] = fill[k] - 'x';
        s.cap = std::clamp<size_t>(j.value("cap", 16), 1, 64);
        s.cap = std::min<size_t>(s.cap, size_t(s.grid[0]) * s.grid[1] * s.grid[2]);
        d.stacks.push_back(std::move(s));
    }
    for (auto &j : kind("toggles")) {
        std::set<unsigned> meshes;
        for (auto &name : j.at("nodes"))
            for (auto m : subtreeMeshes(asset, node(name)))
                meshes.insert(m);
        d.toggles.push_back({j.at("id").get<std::string>(), std::vector<unsigned>(meshes.begin(), meshes.end())});
    }
    for (auto &j : kind("effects")) {
        EffectSlot e;
        e.id = j.at("id");
        e.position = pose(j.at("pose"), "effect " + e.id).first;
        auto k = j.value("kind", "burst");
        e.kind = k == "pulse" ? EffectSlot::Pulse : k == "flash" ? EffectSlot::Flash : k == "ring" ? EffectSlot::Ring : EffectSlot::Burst;
        if (j.contains("colour"))
            e.colour = vec3(j["colour"], where + " effect colour");
        e.seconds = std::clamp(j.value("durationMs", 1500), 200, 3000) / 1000.f;
        d.effects.push_back(std::move(e));
    }
    d.positions.assign(a.animations.size(), {-1.f, -1.f});
    d.driven.assign(a.animations.size(), false);
    for (auto &j : machine.value("positions", Json::array())) {
        int k = animationOf(j.at("node"));
        d.positions[k] = {j.at("range").at(0).get<float>(), j.at("range").at(1).get<float>()};
    }
    // Drives; `{n}` in a drive expands over a repeated input (prototype extension).
    auto slotIndex = [&](const auto &list, const std::string &id, const char *what) {
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].id == id)
                return int(i);
        throw std::runtime_error(where + ": no " + what + " slot " + id);
    };
    auto drives = machine.value("drives", Json::array());
    for (auto &j : drives) {
        std::string input = j.at("input"), target = j.at("target");
        std::vector<std::pair<std::string, std::string>> expanded;
        if (auto at = input.find("{n}"); at != std::string::npos) {
            std::string base = input.substr(0, at);
            if (!base.empty() && base.back() == '.')
                base.pop_back();
            require(repeats.contains(base), where + ": drive input " + input + " is not a repeated input");
            for (int k = 1; k <= repeats[base]; ++k) {
                auto sub = [&](std::string s) {
                    for (size_t p; (p = s.find("{n}")) != std::string::npos;)
                        s.replace(p, 3, std::to_string(k));
                    return s;
                };
                expanded.push_back({sub(input), sub(target)});
            }
        } else
            expanded.push_back({input, target});
        for (auto &[in, tg] : expanded) {
            Drive v;
            for (size_t i = 0; i < d.inputs.size(); ++i)
                if (d.inputs[i].id == in)
                    v.input = int(i);
            require(v.input >= 0, where + ": drive names unknown input " + in);
            auto colon = tg.find(':');
            require(colon != std::string::npos, where + ": drive target " + tg + " must be kind:name");
            auto kindName = tg.substr(0, colon), name = tg.substr(colon + 1);
            v.map = Mapping::parse(j.value("map", Json::object()));
            if (kindName == "animation") {
                v.target = Drive::Animation, v.index = animationOf(name);
                require(d.positions[v.index].first < 0, where + ": " + name + " is value-driven; use position:");
                d.driven[v.index] = true;
            } else if (kindName == "position") {
                v.target = Drive::Position, v.index = animationOf(name);
                require(d.positions[v.index].first >= 0, where + ": " + name + " is not in machine.positions");
                d.driven[v.index] = true;
            } else if (kindName == "state") {
                v.target = Drive::State, v.index = slotIndex(d.palettes, name, "states");
                // Validation: every state the mapping can produce exists in the palette.
                const auto &p = d.palettes[v.index];
                std::vector<Json> outs;
                if (v.map.kind == Mapping::Direct && d.inputs[v.input].type == Input::Enum)
                    for (auto &s : d.inputs[v.input].values)
                        outs.push_back(s);
                for (auto &c : v.map.cases)
                    outs.push_back(c.to);
                if (v.map.otherwise)
                    outs.push_back(*v.map.otherwise);
                for (auto &o : outs)
                    require(o.is_string() && p.find(o.get<std::string>()) >= 0,
                            where + ": drive to state:" + name + " can produce " + o.dump() + ", not in its palette");
            } else if (kindName == "emission")
                v.target = Drive::Emission, v.index = slotIndex(d.palettes, name, "states");
            else if (kindName == "label")
                v.target = Drive::Label, v.index = slotIndex(d.labels, name, "labels");
            else if (kindName == "visible") {
                v.target = Drive::Visible;
                for (size_t i = 0; i < d.toggles.size(); ++i)
                    if (d.toggles[i].first == name)
                        v.index = int(i);
                require(v.index >= 0, where + ": no toggles slot " + name);
            } else if (kindName == "count")
                v.target = Drive::Count, v.index = slotIndex(d.stacks, name, "stacks");
            else if (kindName == "spawn") {
                v.target = Drive::Spawn, v.index = slotIndex(d.spawns, name, "spawns");
                require(d.spawns[v.index].track >= 0, where + ": spawn " + name + " has no track");
            } else if (kindName == "effect")
                v.target = Drive::Effect, v.index = slotIndex(d.effects, name, "effects");
            else
                throw std::runtime_error(where + ": unknown target kind " + kindName);
            bool eventTarget = v.target == Drive::Spawn || v.target == Drive::Effect;
            bool eventSource = d.inputs[v.input].type == Input::Event || v.map.event();
            require(eventTarget == eventSource, where + ": " + tg + (eventTarget ? " needs an event input or a becomes/counter map"
                                                                                : " needs a value, not an event"));
            d.drives.push_back(std::move(v));
        }
    }
    require(d.drives.size() <= 32 * 8, where + ": too many drives");
    return d;
}
MeshPaint Machines::itemPaint(unsigned asset, const std::string &state) const {
    for (auto &[id, entry] : items_)
        if (entry.first == asset) {
            auto states = entry.second.value("slots", Json::object()).value("states", Json::array());
            if (states.empty())
                break;
            const auto &palette = states[0].at("palette");
            const Json *pick = &palette[0];
            for (auto &e : palette)
                if (e.at("id") == state)
                    pick = &e;
            MeshPaint p;
            p.painted = true;
            p.colour = vec3(pick->at("colour"), "item palette");
            p.emission = pick->value("emission", 0.f);
            return p;
        }
    return {};
}
void Machines::load(const Json &spec, const World &w, const std::filesystem::path &root) {
    *this = Machines{};
    world_ = &w;
    staleMs_ = spec.value("staleMs", int64_t(8000));
    require(staleMs_ >= 100, "staleMs must be at least 100");
    std::map<std::string, unsigned> assets;
    for (unsigned i = 0; i < w.objectAssets.size(); ++i)
        assets[w.objectAssets[i].id] = i;
    for (auto &[id, file] : spec.value("manifests", Json::object()).items()) {
        require(assets.contains(id), "manifests: no object asset " + id);
        auto path = root / file.get<std::string>();
        require(path.lexically_normal().string().starts_with(root.lexically_normal().string()), "manifests: path outside the package");
        auto manifest = interaction::readJSON(path, 1 << 20);
        items_[id] = {assets[id], manifest};
        if (manifest.value("traits", Json::object()).contains("machine"))
            definitions_[id] = define(assets[id], manifest);
    }
    if (spec.contains("effectAsset")) {
        require(assets.contains(spec["effectAsset"]), "effectAsset is not in objectAssets");
        effectAsset_ = int(assets[spec["effectAsset"]]);
    }
    states.assign(w.objectInstances.size(), {});
    auto instancesSpec = spec.value("instances", Json::object());
    require(instancesSpec.size() <= 64, "at most 64 bound instances");
    for (auto &[objectId, data] : instancesSpec.items()) {
        size_t object = w.objectInstances.size();
        for (size_t i = 0; i < w.objectInstances.size(); ++i)
            if (w.objectInstances[i].id == objectId)
                object = i;
        require(object < w.objectInstances.size(), "instances: no object " + objectId);
        const auto &asset = w.objectAssets[w.objectInstances[object].asset];
        require(definitions_.contains(asset.id), "instances: " + objectId + "'s asset " + asset.id + " has no machine manifest");
        Instance in;
        in.object = object;
        in.def = &definitions_.at(asset.id);
        const auto &d = *in.def;
        size_t n = d.inputs.size();
        in.bindings.resize(n), in.gates.resize(n), in.driveGates.resize(in.def->drives.size()), in.raw.assign(n, nullptr), in.condition.assign(n, Condition::Missing);
        in.age.assign(n, -1), in.fired.assign(n, 0), in.seenSequence.assign(n, 0);
        for (size_t i = 0; i < n; ++i)
            in.value.push_back(d.inputs[i].fallback);
        for (auto &[inputId, b] : data.value("inputs", Json::object()).items()) {
            bool matched = false;
            for (size_t i = 0; i < n; ++i) {
                const auto &id = d.inputs[i].id;
                std::string suffix;
                if (id == inputId)
                    ;
                else if (id.starts_with(inputId + ".") && b.at("key").get<std::string>().find("{n}") != std::string::npos)
                    suffix = id.substr(inputId.size() + 1);
                else
                    continue;
                auto &bind = in.bindings[i];
                bind.source = b.at("source");
                bind.key = b.at("key");
                if (!suffix.empty())
                    for (size_t p; (p = bind.key.find("{n}")) != std::string::npos;)
                        bind.key.replace(p, 3, suffix);
                bind.map = Mapping::parse(b.value("map", Json::object()));
                require(d.inputs[i].type != Input::Event || bind.map.event() || bind.map.kind == Mapping::Direct,
                        "instances " + objectId + ": event input " + id + " needs a counter or becomes map");
                if (d.inputs[i].type == Input::Event && bind.map.kind == Mapping::Direct)
                    bind.map.kind = Mapping::Counter;
                bind.bound = matched = true;
            }
            require(matched, "instances " + objectId + ": no input " + inputId);
        }
        in.paletteEntry.assign(d.palettes.size(), 0), in.emission.assign(d.palettes.size(), 1.f);
        in.paletteStale.assign(d.palettes.size(), false);
        in.labelText.assign(d.labels.size(), "");
        in.labelStale.assign(d.labels.size(), false);
        in.toggleHidden.assign(d.toggles.size(), false);
        in.stackCount.assign(d.stacks.size(), 0);
        size_t animations = asset.animations.size();
        in.speedTarget.assign(animations, 0), in.speed.assign(animations, 0), in.turns.assign(animations, 0);
        in.positionTarget.assign(animations, 0), in.position.assign(animations, 0);
        in.easeFrom.assign(animations, 0), in.easeTo.assign(animations, 0), in.easeTime.assign(animations, 0);
        const auto &o = w.objectInstances[object];
        for (size_t k = 0; k < animations; ++k) {
            in.turns[k] = asset.animations[k].phase + o.phase;
            if (!d.driven[k])
                in.speed[k] = in.speedTarget[k] = in.easeTo[k] = o.speed; // undriven: as authored
        }
        in.items.resize(d.tracks.size());
        in.backlog.assign(d.spawns.size(), 0), in.rateClock.assign(d.spawns.size(), 0);
        in.spawned.assign(d.spawns.size(), 0), in.dropped.assign(d.spawns.size(), 0);
        states[object].bound = true;
        instances_.push_back(std::move(in));
    }
}
std::vector<size_t> Machines::instances() const {
    std::vector<size_t> out;
    for (auto &in : instances_)
        out.push_back(in.object);
    return out;
}
void Machines::evaluate(Instance &in, const Json &sources, int64_t nowMs, bool still) {
    const auto &d = *in.def;
    in.anyStale = false;
    for (size_t i = 0; i < d.inputs.size(); ++i) {
        const auto &input = d.inputs[i];
        auto &bind = in.bindings[i];
        in.fired[i] = 0;
        if (!bind.bound) {
            in.condition[i] = Condition::Missing, in.value[i] = input.fallback;
            continue;
        }
        const Json *entry = nullptr;
        for (auto &s : sources)
            if (s.value("sourceId", "") == bind.source)
                entry = &s;
        const Json *raw = entry && (*entry)["values"].contains(bind.key) ? &(*entry)["values"][bind.key] : nullptr;
        if (!raw) {
            // Missing: the source expired (ttlMs) or never published this key.
            in.condition[i] = Condition::Missing, in.raw[i] = nullptr, in.value[i] = input.fallback, in.age[i] = -1;
            in.gates[i].rebaseline();
            continue;
        }
        in.raw[i] = *raw;
        in.age[i] = nowMs - (*entry)["monotonicMs"].get<int64_t>();
        auto status = (*entry)["values"].value("_status", "ok");
        in.condition[i] = status == "error" ? Condition::Error : in.age[i] > staleMs_ ? Condition::Stale : Condition::Fresh;
        if (input.type == Input::Event) {
            // No spawns or effects fire on stale data; coming back re-baselines (no burst).
            if (in.condition[i] == Condition::Fresh)
                in.fired[i] = in.gates[i].feed(bind.map, *raw);
            else
                in.gates[i].rebaseline();
            continue;
        }
        auto mapped = bind.map.apply(*raw);
        Json v = mapped ? *mapped : Json(nullptr);
        bool fits = false;
        switch (input.type) {
        case Input::Enum:
            fits = v.is_string() && std::find(input.values.begin(), input.values.end(), v.get<std::string>()) != input.values.end();
            break;
        case Input::Int:
        case Input::Number: {
            double x = v.is_number() || v.is_boolean() ? machines::number(v) : NAN;
            if ((fits = std::isfinite(x)))
                v = std::clamp(input.type == Input::Int ? std::round(x) : x, input.min, input.max);
            break;
        }
        case Input::Bool:
            fits = v.is_boolean() || v.is_number();
            if (fits)
                v = machines::number(v) != 0;
            break;
        case Input::Text:
            fits = !v.is_null();
            if (fits)
                v = machines::text(v).substr(0, input.maxLength);
            break;
        case Input::Event:
            break;
        }
        if (!fits) // a type mismatch is the error condition (RFC-0013), shown as stale
            in.condition[i] = Condition::Error, v = in.value[i].is_null() ? input.fallback : in.value[i];
        in.value[i] = v;
    }
    for (size_t i = 0; i < d.inputs.size(); ++i)
        in.anyStale |= in.bindings[i].bound && (in.condition[i] == Condition::Stale || in.condition[i] == Condition::Error);
    for (size_t k = 0; k < d.drives.size(); ++k)
        apply(in, k, still);
}
void Machines::apply(Instance &in, size_t index, bool still) {
    const auto &d = *in.def;
    const auto &drive = d.drives[index];
    auto condition = in.condition[drive.input];
    bool stale = condition == Condition::Stale || condition == Condition::Error, missing = condition == Condition::Missing,
         fresh = condition == Condition::Fresh;
    const auto &value = in.value[drive.input];
    int fired = in.fired[drive.input];
    if (drive.map.kind == Mapping::Becomes && d.inputs[drive.input].type != Input::Event) {
        auto &gate = in.driveGates[index];
        if (fresh)
            fired = gate.feed(drive.map, value);
        else
            gate.rebaseline(), fired = 0;
    }
    switch (drive.target) {
    case Drive::Spawn:
        if (fired > 0) {
            in.backlog[drive.index] += fired;
            if (in.backlog[drive.index] > maxBacklog) // a number, not a queue (RFC-0013 batching)
                in.dropped[drive.index] += in.backlog[drive.index] - maxBacklog, in.backlog[drive.index] = maxBacklog;
        }
        return;
    case Drive::Effect:
        if (fired > 0)
            fire(in, d.effects[drive.index], still);
        return;
    default:
        break;
    }
    auto out = drive.map.apply(value);
    if (!out)
        return;
    switch (drive.target) {
    case Drive::State: {
        int entry = out->is_string() ? d.palettes[drive.index].find(out->get<std::string>()) : -1;
        in.paletteEntry[drive.index] = std::max(entry, 0);
        in.paletteStale[drive.index] = stale;
        break;
    }
    case Drive::Animation:
        in.speedTarget[drive.index] = stale ? 0 : std::clamp(machines::number(*out), 0., 10.);
        break;
    case Drive::Position:
        in.positionTarget[drive.index] = std::clamp(machines::number(*out), 0., 1.);
        break;
    case Drive::Emission:
        in.emission[drive.index] = float(std::clamp(machines::number(*out, 1), 0., 16.));
        break;
    case Drive::Label: {
        auto t = missing ? std::string("\xe2\x80\x94") : machines::text(*out); // em dash
        // Cut to maxChars code points (UTF-8 aware).
        size_t chars = 0, cut = 0;
        while (cut < t.size() && chars < d.labels[drive.index].maxChars) {
            cut += (t[cut] & 0x80) == 0 ? 1 : (t[cut] & 0xe0) == 0xc0 ? 2 : (t[cut] & 0xf0) == 0xe0 ? 3 : 4;
            ++chars;
        }
        in.labelText[drive.index] = t.substr(0, std::min(cut, t.size()));
        in.labelStale[drive.index] = stale;
        break;
    }
    case Drive::Visible:
        in.toggleHidden[drive.index] = machines::number(*out) == 0;
        break;
    case Drive::Count:
        in.stackCount[drive.index] = size_t(std::clamp(std::floor(machines::number(*out)), 0., double(d.stacks[drive.index].cap)));
        break;
    default:
        break;
    }
}
void Machines::fire(Instance &in, const EffectSlot &e, bool still) {
    if (effectAsset_ < 0)
        return;
    std::set<uint32_t> playing;
    for (auto &p : pieces_)
        playing.insert(p.effect);
    if (playing.size() >= size_t(maxEffects)) // skipped, never queued (RFC-0013 caps)
        return;
    MeshPaint paint;
    paint.painted = true, paint.colour = e.colour, paint.emission = 4;
    const auto &o = world_->objectInstances[in.object];
    auto kind = still && e.kind != EffectSlot::Flash ? EffectSlot::Flash : e.kind; // still: effects become flashes
    int count = kind == EffectSlot::Burst ? 24 : kind == EffectSlot::Ring ? 16 : 1;
    count = std::min(count, maxPieces);
    uint32_t effect = ++nextEffect_;
    for (int i = 0; i < count; ++i) {
        Piece p;
        p.instance = in.object;
        p.origin = o.model.point(e.position);
        p.kind = int(kind), p.effect = effect;
        p.life = e.seconds;
        p.paint = paint;
        float a = 6.2831853f * (kind == EffectSlot::Ring ? i / float(count) : hashUnit(seed_)), up = hashUnit(seed_);
        if (kind == EffectSlot::Burst) {
            float speed = 1.6f + 1.4f * hashUnit(seed_);
            p.velocity = Vec3{std::cos(a) * (.35f + .65f * (1 - up)), 1.2f + 1.6f * up, std::sin(a) * (.35f + .65f * (1 - up))} * speed;
            p.size = .7f + .6f * hashUnit(seed_);
            p.paint.colour = i % 3 == 0 ? Vec3{1, .78f, .3f} : i % 3 == 1 ? e.colour : Vec3{.45f, .85f, 1};
        } else if (kind == EffectSlot::Ring)
            p.velocity = Vec3{std::cos(a), 0, std::sin(a)} * 1.4f;
        p.axis = normalized(Vec3{hashUnit(seed_) - .5f, 1, hashUnit(seed_) - .5f});
        p.spin = 4 + 8 * hashUnit(seed_);
        pieces_.push_back(p);
    }
}
void Machines::tick(double dt, const Json &sources, int64_t nowMs, bool still) {
    if (!world_)
        return;
    dt = std::clamp(dt, 0., .25);
    extras.clear();
    labels.clear();
    for (auto &in : instances_) {
        const auto &d = *in.def;
        const auto &o = world_->objectInstances[in.object];
        const auto &asset = world_->objectAssets[d.asset];
        auto &state = states[in.object];
        bool moving = false;
        // Coalescing: at most 4 evaluations per second per instance; counters accumulate.
        in.sinceUpdate += dt;
        if (in.sinceUpdate >= coalesce_) {
            in.sinceUpdate = 0;
            evaluate(in, sources, nowMs, still);
        }
        // Stale look eases over 0.5 s (snaps when still).
        float look = in.anyStale ? 1.f : 0.f;
        if (in.look != look) {
            in.look = still ? look : in.look + std::clamp(look - in.look, float(-dt * 2), float(dt * 2));
            moving |= in.look != look;
        }
        // Animations: speed and value-driven poses ease to a new target over 300 ms
        // (smoothstep; restarted from the current value when the target moves), and a
        // bound animation integrates its own phase, so speed changes never jump.
        for (size_t k = 0; k < asset.animations.size(); ++k) {
            const auto &an = asset.animations[k];
            auto [r0, r1] = d.positions[k];
            bool valueDriven = r0 >= 0;
            double target = valueDriven ? in.positionTarget[k] : still ? 0 : in.speedTarget[k];
            double &current = valueDriven ? in.position[k] : in.speed[k];
            if (target != in.easeTo[k])
                in.easeFrom[k] = current, in.easeTo[k] = target, in.easeTime[k] = 0;
            if (current != target) {
                in.easeTime[k] += dt;
                double u = still ? 1 : std::min(1., in.easeTime[k] / .3);
                current = in.easeFrom[k] + (target - in.easeFrom[k]) * u * u * (3 - 2 * u);
                if (u >= 1)
                    current = target;
                moving |= valueDriven;
            }
            if (valueDriven) {
                in.turns[k] = r0 + (r1 - r0) * current;
                continue;
            }
            if (current > 0 && !still) {
                in.turns[k] += dt * current / an.period;
                in.turns[k] -= std::floor(in.turns[k]);
                moving = true;
            }
        }
        // Items: spawns at maxRate from the backlog, travel, spacing, dwell at stops, sink.
        for (size_t s = 0; s < d.spawns.size(); ++s) {
            const auto &spawn = d.spawns[s];
            if (spawn.track < 0)
                continue;
            auto &items = in.items[spawn.track];
            const auto &track = d.tracks[spawn.track];
            if (still) { // no travel: the backlog shows nothing moving
                in.dropped[s] += in.backlog[s], in.backlog[s] = 0;
                continue;
            }
            in.rateClock[s] = std::min(in.rateClock[s] + dt, 1.0 / spawn.maxRate);
            if (in.backlog[s] <= 0 || in.rateClock[s] < 1.0 / spawn.maxRate)
                continue;
            bool clear = items.empty() || items.back().s - spawn.at >= track.spacing;
            if (!clear)
                continue;
            if (items.size() >= track.max) {
                if (!track.dropOldest) {
                    --in.backlog[s], ++in.dropped[s];
                    continue;
                }
                items.erase(items.begin()), ++in.dropped[s];
            }
            Item item;
            item.s = spawn.at;
            while (item.nextStop < track.stops.size() && track.stops[item.nextStop].first <= spawn.at + 1e-4f)
                ++item.nextStop;
            item.state = 0;
            items.push_back(item);
            --in.backlog[s], ++in.spawned[s], in.rateClock[s] = 0;
        }
        for (size_t t = 0; t < d.tracks.size(); ++t) {
            auto &items = in.items[t];
            const auto &track = d.tracks[t];
            if (still) {
                items.clear();
                continue;
            }
            float end = track.track.length();
            for (size_t i = 0; i < items.size(); ++i) {
                auto &item = items[i];
                if (item.dwell > 0) {
                    item.dwell = float(std::max(0., item.dwell - dt));
                    continue;
                }
                float limit = i == 0 ? end + 1 : items[i - 1].s - track.spacing;
                float next = item.s + float(track.speed * dt);
                if (item.nextStop < track.stops.size() && next >= track.stops[item.nextStop].first) {
                    next = track.stops[item.nextStop].first;
                    item.dwell = track.stops[item.nextStop].second;
                    ++item.nextStop;
                }
                next = std::min(next, limit);
                if (next > item.s + 1e-6f)
                    item.s = next, moving = true;
            }
            while (!items.empty() && items.front().s >= end - 1e-4f)
                items.erase(items.begin()); // the sink at the end of the track
        }
        // Draw state.
        state.turns = in.turns;
        state.meshes.assign(asset.meshes.size(), {});
        state.saturation = 1 - .7f * in.look;
        state.brightness = 1 - .15f * in.look;
        for (size_t p = 0; p < d.palettes.size(); ++p) {
            const auto &palette = d.palettes[p];
            bool grey = in.paletteStale[p] && palette.stale >= 0;
            auto paint = palette.entries[grey ? palette.stale : in.paletteEntry[p]].second;
            paint.emissiveScale = in.emission[p] * (in.paletteStale[p] && !grey ? .25f : 1.f);
            for (auto m : palette.meshes)
                state.meshes[m].painted = true, state.meshes[m].colour = paint.colour, state.meshes[m].emission = paint.emission,
                state.meshes[m].emissiveScale = paint.emissiveScale;
        }
        for (size_t t = 0; t < d.toggles.size(); ++t)
            if (in.toggleHidden[t])
                for (auto m : d.toggles[t].second)
                    state.meshes[m].hidden = true;
        for (size_t s = 0; s < d.stacks.size(); ++s) {
            const auto &stack = d.stacks[s];
            for (auto m : stack.meshes)
                state.meshes[m].hidden = true; // drawn as copies only
            for (size_t i = 0; i < in.stackCount[s]; ++i) {
                int cell[3]{}, rest = int(i);
                for (int k = 0; k < 3; ++k) {
                    int axis = stack.order[k];
                    cell[axis] = rest % stack.grid[axis];
                    rest /= stack.grid[axis];
                }
                ObjectExtra e;
                e.asset = d.asset, e.node = int(stack.node), e.owner = in.object;
                e.model = o.model * translation({cell[0] * stack.spacing.x, cell[1] * stack.spacing.y, cell[2] * stack.spacing.z});
                e.scale = o.scale;
                e.saturation = state.saturation, e.brightness = state.brightness;
                extras.push_back(e);
            }
        }
        for (size_t s = 0; s < d.spawns.size(); ++s) {
            if (d.spawns[s].track < 0)
                continue;
            const auto &track = d.tracks[d.spawns[s].track];
            auto paint = itemPaint(d.spawns[s].asset, "running");
            for (auto &item : in.items[d.spawns[s].track]) {
                Vec3 tangent;
                Vec3 p = track.track.at(item.s, &tangent);
                ObjectExtra e;
                e.asset = d.spawns[s].asset, e.owner = in.object, e.paint = paint;
                e.model = o.model * translation(p) * heading(tangent);
                e.scale = o.scale;
                e.saturation = state.saturation, e.brightness = state.brightness;
                extras.push_back(e);
            }
        }
        for (size_t l = 0; l < d.labels.size(); ++l) {
            const auto &slot = d.labels[l];
            if (in.labelText[l].empty())
                continue;
            WorldLabel label;
            label.owner = in.object;
            label.centre = o.model.point(slot.centre);
            label.right = o.model.point(slot.centre + slot.right) - label.centre;
            label.up = o.model.point(slot.centre + slot.up) - label.centre;
            label.text = in.labelText[l] + (in.labelStale[l] ? " \xe2\x97\xb7" : ""); // a clock mark when stale
            label.colour = slot.colour;
            label.saturation = state.saturation, label.brightness = state.brightness;
            labels.push_back(label);
        }
        // Revision: any change that is not continuous motion.
        uint64_t h = 1469598103934665603ull;
        for (auto e : in.paletteEntry)
            h = mix(h, uint64_t(e));
        for (auto e : in.emission)
            h = mix(h, uint64_t(e * 1000));
        for (bool b : in.paletteStale)
            h = mix(h, b);
        for (bool b : in.toggleHidden)
            h = mix(h, b);
        for (auto c : in.stackCount)
            h = mix(h, c);
        for (size_t l = 0; l < d.labels.size(); ++l)
            h = mix(mix(h, in.labelText[l]), in.labelStale[l]);
        h = mix(h, uint64_t(in.look * 1000));
        for (auto p : in.position)
            h = mix(h, uint64_t(p * 1e4));
        if (h != in.signature)
            in.signature = h, ++state.revision;
        state.moving = moving;
    }
    // Effect pieces (world-wide pool, capped).
    for (auto &p : pieces_)
        p.age += float(dt);
    std::erase_if(pieces_, [](const Piece &p) { return p.age >= p.life; });
    for (auto &p : pieces_) {
        auto &owner = states[p.instance];
        float u = p.age / p.life, scale = 1;
        Vec3 position = p.origin;
        auto kind = EffectSlot::Kind(p.kind);
        MeshPaint paint = p.paint;
        if (kind == EffectSlot::Burst) {
            position = p.origin + p.velocity * p.age + Vec3{0, -4.9f * p.age * p.age, 0};
            scale = p.size * (u > .7f ? (1 - u) / .3f : 1);
        } else if (kind == EffectSlot::Ring) {
            position = p.origin + p.velocity * p.age;
            scale = 1 - u;
        } else if (kind == EffectSlot::Pulse)
            scale = .4f + 2.4f * u, paint.emissiveScale = 1 - u;
        else // flash: no motion, a brightness envelope
            scale = 2.2f, paint.emissiveScale = std::sin(3.14159265f * u);
        if (kind != EffectSlot::Flash)
            owner.moving = true;
        else
            ++owner.revision; // a flash changes the image without motion
        ObjectExtra e;
        e.asset = unsigned(effectAsset_), e.owner = p.instance, e.paint = paint, e.scale = std::max(scale, .01f);
        e.model = translation(position) * animation::rotation(p.axis, kind == EffectSlot::Burst ? p.spin * p.age : 0) *
                  heading({0, 0, 1}, e.scale);
        extras.push_back(e);
    }
}
Json Machines::diagnostics() const {
    static const char *conditions[]{"fresh", "stale", "missing", "error"};
    Json out = Json::array();
    for (auto &in : instances_) {
        const auto &d = *in.def;
        Json inputs = Json::object();
        for (size_t i = 0; i < d.inputs.size(); ++i) {
            Json entry{{"value", in.value[i]}, {"condition", conditions[int(in.condition[i])]}, {"ageMs", in.age[i]}};
            if (in.bindings[i].bound)
                entry["source"] = in.bindings[i].source, entry["key"] = in.bindings[i].key, entry["raw"] = in.raw[i];
            inputs[d.inputs[i].id] = entry;
        }
        Json tracks = Json::array();
        for (size_t s = 0; s < d.spawns.size(); ++s)
            if (d.spawns[s].track >= 0)
                tracks.push_back({{"spawn", d.spawns[s].id}, {"track", d.tracks[d.spawns[s].track].id},
                                  {"live", in.items[d.spawns[s].track].size()}, {"spawned", in.spawned[s]},
                                  {"dropped", in.dropped[s]}, {"backlog", in.backlog[s]}});
        Json palettes = Json::object();
        for (size_t p = 0; p < d.palettes.size(); ++p)
            palettes[d.palettes[p].id] = d.palettes[p].entries[in.paletteEntry[p]].first + (in.paletteStale[p] ? " (stale)" : "");
        Json speeds = Json::object();
        for (size_t k = 0; k < in.speed.size(); ++k)
            speeds[world_->objectAssets[d.asset].animations[k].name] = d.positions[k].first >= 0 ? in.position[k] : in.speed[k];
        Json labelTexts = Json::object();
        for (size_t l = 0; l < d.labels.size(); ++l)
            labelTexts[d.labels[l].id] = in.labelText[l];
        Json stacks = Json::object();
        for (size_t s = 0; s < d.stacks.size(); ++s)
            stacks[d.stacks[s].id] = in.stackCount[s];
        Json toggles = Json::object();
        for (size_t t = 0; t < d.toggles.size(); ++t)
            toggles[d.toggles[t].first] = !in.toggleHidden[t];
        const auto &state = states[in.object];
        out.push_back({{"id", world_->objectInstances[in.object].id}, {"asset", d.id}, {"inputs", inputs},
                       {"states", palettes}, {"animations", speeds}, {"labels", labelTexts}, {"stacks", stacks},
                       {"toggles", toggles}, {"tracks", tracks}, {"stale", in.anyStale}, {"look", in.look},
                       {"moving", state.moving}, {"revision", state.revision}});
    }
    return {{"instances", out}, {"pieces", pieces_.size()}, {"extras", extras.size()}, {"labels", labels.size()}};
}
} // namespace hyprune::world
