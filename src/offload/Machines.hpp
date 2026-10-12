#pragma once
// Machine draw state for the offload renderer (RFC-0009; RFC-0013 draft machines): the
// plugin's machines runtime (world/Machines) sets per-instance draw state, extra draws
// (stack copies, levels, items, effect pieces) and world labels for the renderer; the
// worker's renderer draws the same from a delta in each pose:
// - states: per bound instance, its animation turns, stale look and the non-default
//   mesh paints (hidden, offset, palette colour and emission);
// - extras: per owner instance, its extra draws; a relative one carries its local matrix
//   (a translation when it is one) and the worker multiplies its own copy of the owner's
//   model, so both sides compute the same model;
// - labels: the whole list when it changes (a few per machine).
// Owners change independently, so a sloshing level resends its two draws, not a full
// stack. A delta is committed to the sender's mirror only once its pose was sent; past
// the byte budget the rest follows in the next pose. Internal to core: not part of the
// IPC contract.
#include "render/Renderer.hpp"
#include "world/ObjectState.hpp"
#include "world/World.hpp"
#include <json.hpp>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace hyprune::offload {
namespace machines {
using Json = nlohmann::json;
inline bool defaultPaint(const world::MeshPaint &p) {
    return !p.hidden && p.offset.x == 0 && p.offset.y == 0 && p.offset.z == 0 && !p.painted && p.colour.x == 1 &&
           p.colour.y == 1 && p.colour.z == 1 && p.emission == 0 && p.emissiveScale == 1;
}
inline Json paintJson(const world::MeshPaint &p) {
    return Json::array({p.hidden, p.painted, p.colour.x, p.colour.y, p.colour.z, p.emission, p.emissiveScale, p.offset.x,
                        p.offset.y, p.offset.z});
}
inline world::MeshPaint paintOf(const Json &j) {
    if (!j.is_array() || j.size() != 10)
        throw std::runtime_error("machine paint");
    world::MeshPaint p;
    p.hidden = j[0], p.painted = j[1];
    p.colour = {j[2], j[3], j[4]};
    p.emission = j[5], p.emissiveScale = j[6];
    p.offset = {j[7], j[8], j[9]};
    return p;
}
inline Json stateJson(const world::ObjectDrawState &s) {
    Json meshes = Json::array();
    for (size_t m = 0; m < s.meshes.size(); ++m)
        if (!defaultPaint(s.meshes[m]))
            meshes.push_back({m, paintJson(s.meshes[m])});
    return Json{{"count", s.meshes.size()}, {"meshes", meshes}, {"turns", s.turns}, {"look", {s.saturation, s.brightness}}};
}
inline world::ObjectDrawState stateOf(const Json &j) {
    world::ObjectDrawState s;
    s.bound = true;
    size_t count = j.at("count");
    if (count > 4096)
        throw std::runtime_error("machine state meshes");
    s.meshes.assign(count, {});
    for (auto &m : j.at("meshes")) {
        size_t k = m.at(0);
        if (k >= count)
            throw std::runtime_error("machine state mesh");
        s.meshes[k] = paintOf(m.at(1));
    }
    s.turns = j.at("turns").get<std::vector<double>>();
    s.saturation = j.at("look").at(0), s.brightness = j.at("look").at(1);
    return s;
}
inline bool translationOnly(const Mat4 &m) {
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 4; ++r)
            if (m.v[c * 4 + r] != (c == r ? 1.f : 0.f))
                return false;
    return m.v[15] == 1.f;
}
// One owner's extras: {"p": paints, "m": painted-mesh lists, "x": draws}; a draw is
// [asset, node, relative, matrix (3 or 16 floats), paint, meshes (-1: every mesh), scale,
// saturation, brightness].
inline Json extrasJson(const std::vector<const world::ObjectExtra *> &extras) {
    Json paints = Json::array(), lists = Json::array(), draws = Json::array();
    std::map<std::string, size_t> paintIndex;
    std::map<const std::vector<unsigned> *, size_t> listIndex;
    for (auto *e : extras) {
        auto paint = paintJson(e->paint);
        auto key = paint.dump();
        auto [it, added] = paintIndex.emplace(key, paints.size());
        if (added)
            paints.push_back(paint);
        long list = -1;
        if (e->paintMeshes) {
            auto [li, fresh] = listIndex.emplace(e->paintMeshes, lists.size());
            if (fresh)
                lists.push_back(*e->paintMeshes);
            list = long(li->second);
        }
        const Mat4 &m = e->relative ? e->local : e->model;
        Json matrix = e->relative && translationOnly(m) ? Json::array({m.v[12], m.v[13], m.v[14]}) : Json(m.v);
        draws.push_back({e->asset, e->node, e->relative, matrix, it->second, list, e->scale, e->saturation, e->brightness});
    }
    return Json{{"p", paints}, {"m", lists}, {"x", draws}};
}
inline Json labelJson(const world::WorldLabel &l) {
    return Json::array({l.owner, l.centre.x, l.centre.y, l.centre.z, l.right.x, l.right.y, l.right.z, l.up.x, l.up.y, l.up.z,
                        l.text, l.colour.x, l.colour.y, l.colour.z, l.intensity, l.saturation, l.brightness});
}
inline world::WorldLabel labelOf(const Json &j) {
    if (!j.is_array() || j.size() != 17 || !j[10].is_string() || j[10].get_ref<const std::string &>().size() > 256)
        throw std::runtime_error("machine label");
    world::WorldLabel l;
    l.owner = j[0];
    l.centre = {j[1], j[2], j[3]}, l.right = {j[4], j[5], j[6]}, l.up = {j[7], j[8], j[9]};
    l.text = j[10];
    l.colour = {j[11], j[12], j[13]};
    l.intensity = j[14], l.saturation = j[15], l.brightness = j[16];
    return l;
}
} // namespace machines

// Plugin side: the mirror of what the worker has.
class MachineSender {
    struct Mirror {
        uint64_t revision = 0;
        std::map<size_t, std::string> states, extras; // per instance: the encoded JSON
        std::string labels = "[]";
    } sent_, next_;
    bool pending_ = false;

  public:
    void reset() { sent_ = next_ = {}, pending_ = false; }
    // The delta from the worker's machine state to the renderer's, or null.
    nlohmann::json delta(const render::Renderer &r, size_t budget = 64 * 1024) {
        using Json = nlohmann::json;
        next_ = sent_;
        pending_ = false;
        Json d = Json::object();
        size_t bytes = 0;
        // Per-owner encodings of the current state.
        std::map<size_t, Json> states, extras;
        for (size_t k = 0; k < r.objectStates.size(); ++k)
            if (r.objectStates[k].bound)
                states[k] = machines::stateJson(r.objectStates[k]);
        std::map<size_t, std::vector<const world::ObjectExtra *>> owned;
        for (auto &e : r.objectExtras)
            owned[e.owner].push_back(&e);
        for (auto &[owner, list] : owned)
            extras[owner] = machines::extrasJson(list);
        auto part = [&](const char *name, std::map<size_t, Json> &now, std::map<size_t, std::string> &mirror) {
            Json set = Json::array(), erase = Json::array();
            for (auto &[k, j] : now) {
                auto text = j.dump();
                auto it = mirror.find(k);
                if (it != mirror.end() && it->second == text)
                    continue;
                if (bytes + text.size() > budget && bytes > 0)
                    continue; // the next pose
                bytes += text.size() + 8;
                set.push_back({k, std::move(j)});
                mirror[k] = std::move(text);
            }
            for (auto it = mirror.begin(); it != mirror.end();)
                if (!now.contains(it->first))
                    erase.push_back(it->first), it = mirror.erase(it);
                else
                    ++it;
            if (!set.empty() || !erase.empty())
                d[name] = {{"set", set}, {"erase", erase}};
        };
        part("states", states, next_.states);
        part("extras", extras, next_.extras);
        Json labels = Json::array();
        for (auto &l : r.worldLabels)
            labels.push_back(machines::labelJson(l));
        if (auto text = labels.dump(); text != sent_.labels)
            d["labels"] = std::move(labels), next_.labels = text;
        if (d.empty())
            return nullptr;
        d["base"] = sent_.revision;
        d["revision"] = next_.revision = sent_.revision + 1;
        pending_ = true;
        return d;
    }
    void commit() {
        if (pending_)
            sent_ = std::move(next_);
        pending_ = false;
    }
};

// Worker side: applies deltas and writes the renderer's machine draw state.
class MachineReceiver {
    struct Owner {
        std::vector<world::ObjectExtra> extras;
        std::vector<std::unique_ptr<std::vector<unsigned>>> lists; // paintMeshes storage
    };
    std::map<size_t, world::ObjectDrawState> states_;
    std::map<size_t, Owner> owners_;
    std::vector<world::WorldLabel> labels_;
    uint64_t revision_ = 0;

  public:
    uint64_t revision() const { return revision_; }
    void start() { *this = {}; }
    void apply(const nlohmann::json &d, const world::World &) {
        if (d.at("base").get<uint64_t>() != revision_ || d.at("revision").get<uint64_t>() != revision_ + 1)
            throw std::runtime_error("machine state revision");
        if (d.contains("states")) {
            for (auto &k : d["states"].at("erase"))
                states_.erase(k.get<size_t>());
            for (auto &e : d["states"].at("set"))
                states_[e.at(0).get<size_t>()] = machines::stateOf(e.at(1));
        }
        if (d.contains("extras")) {
            for (auto &k : d["extras"].at("erase"))
                owners_.erase(k.get<size_t>());
            for (auto &e : d["extras"].at("set")) {
                Owner o;
                auto &j = e.at(1);
                for (auto &l : j.at("m")) {
                    auto list = std::make_unique<std::vector<unsigned>>(l.get<std::vector<unsigned>>());
                    o.lists.push_back(std::move(list));
                }
                std::vector<world::MeshPaint> paints;
                for (auto &p : j.at("p"))
                    paints.push_back(machines::paintOf(p));
                if (j.at("x").size() > 4096)
                    throw std::runtime_error("machine extras");
                for (auto &x : j.at("x")) {
                    world::ObjectExtra extra;
                    extra.owner = e.at(0);
                    extra.asset = x.at(0);
                    extra.node = x.at(1);
                    extra.relative = x.at(2);
                    auto &m = x.at(3);
                    Mat4 matrix = Mat4::identity();
                    if (m.size() == 3)
                        matrix.v[12] = m[0], matrix.v[13] = m[1], matrix.v[14] = m[2];
                    else
                        matrix.v = m.get<std::array<float, 16>>();
                    (extra.relative ? extra.local : extra.model) = matrix;
                    extra.paint = paints.at(x.at(4).get<size_t>());
                    long list = x.at(5);
                    extra.paintMeshes = list < 0 ? nullptr : o.lists.at(size_t(list)).get();
                    extra.scale = x.at(6), extra.saturation = x.at(7), extra.brightness = x.at(8);
                    o.extras.push_back(extra); // kept while its asset still decodes here (drawn once merged)
                }
                owners_[e.at(0).get<size_t>()] = std::move(o);
            }
        }
        if (d.contains("labels")) {
            labels_.clear();
            for (auto &l : d["labels"])
                labels_.push_back(machines::labelOf(l));
        }
        revision_ = d["revision"];
    }
    // The renderer's machine draw state for the world's current instances (models of
    // relative extras from the worker's own instance models).
    void materialize(const world::World &w, render::Renderer &r) const {
        r.objectStates.assign(w.objectInstances.size(), {});
        for (auto &[k, s] : states_)
            if (k < r.objectStates.size())
                r.objectStates[k] = s;
        r.objectExtras.clear();
        for (auto &[owner, o] : owners_) {
            if (owner >= w.objectInstances.size())
                continue;
            for (auto e : o.extras) {
                if (e.asset >= w.objectAssets.size())
                    continue;
                if (e.relative)
                    e.model = w.objectInstances[owner].model * e.local;
                r.objectExtras.push_back(e);
            }
        }
        r.worldLabels.clear();
        for (auto &l : labels_)
            if (l.owner < w.objectInstances.size())
                r.worldLabels.push_back(l);
    }
    nlohmann::json diagnostics() const {
        size_t extras = 0;
        for (auto &[k, o] : owners_)
            extras += o.extras.size();
        return {{"revision", revision_}, {"states", states_.size()}, {"extras", extras}, {"labels", labels_.size()}};
    }
};
} // namespace hyprune::offload
