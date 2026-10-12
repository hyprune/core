#pragma once
// World objects for the offload renderer (RFC-0009, world 0.8 objects, RFC-0011
// placements). The worker loads the same package, so it starts with the authored
// assets and instances. The plugin then sends the object state that draws
// differently from that package as deltas against what the worker already has:
// - assets merged into the live world after load (catalog library and user objects),
//   by directory and manifest; the worker decodes them itself;
// - the instance list: count, and the records of changed instances only (model,
//   speed/phase, bounds, regions, probe lighting);
// - the hidden set, with its own revision, only when it changes;
// - edited scene-node transforms: changed entries and removed nodes.
// The animation clock travels in every pose. A delta is committed to the sender's
// mirror only once its packet was sent, so backpressure never loses a change.
// Internal to core: not part of the IPC contract.
#include "world/Objects.hpp"
#include "world/World.hpp"
#include <filesystem>
#include <functional>
#include <future>
#include <json.hpp>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>
namespace hyprune::offload {
using Json = nlohmann::json;

// Renderer-visible fields of an instance, as a flat array (floats round-trip exactly):
// asset, speed, phase, scale, model[16], center[3], radius, ambientUp[3], sun,
// ambientDown[3], sunVisibility, regions[].
inline Json instanceRecord(const world::ObjectInstance &o) {
    Json r = Json::array({o.asset, o.speed, o.phase, o.scale});
    for (float v : o.model.v)
        r.push_back(v);
    for (float v : {o.center.x, o.center.y, o.center.z, o.radius, o.ambientUp.x, o.ambientUp.y, o.ambientUp.z, o.sun,
                    o.ambientDown.x, o.ambientDown.y, o.ambientDown.z, o.sunVisibility})
        r.push_back(v);
    r.push_back(o.regions);
    return r;
}
inline constexpr size_t recordSize = 4 + 16 + 12 + 1;
inline void applyRecord(world::ObjectInstance &o, const Json &r) {
    if (!r.is_array() || r.size() != recordSize || !r.back().is_array())
        throw std::runtime_error("object record");
    o.asset = r[0].get<unsigned>();
    o.speed = r[1], o.phase = r[2], o.scale = r[3];
    for (size_t i = 0; i < 16; ++i)
        o.model.v[i] = r[4 + i].get<float>();
    auto f = [&](size_t i) { return r[20 + i].get<float>(); };
    o.center = {f(0), f(1), f(2)};
    o.radius = f(3);
    o.ambientUp = {f(4), f(5), f(6)};
    o.sun = f(7);
    o.ambientDown = {f(8), f(9), f(10)};
    o.sunVisibility = f(11);
    o.regions = r[32].get<std::vector<int>>();
}
inline Json matrixJson(const Mat4 &m) { return Json(m.v); }

// An asset merged into the live world after load: where the worker reads it from.
struct AssetSource {
    std::filesystem::path directory;
    Json manifest;
};

// Plugin side: the mirror of what the worker has.
class ObjectSender {
    struct Mirror {
        uint64_t revision = 0, hiddenRevision = 0;
        size_t assets = 0;
        std::vector<Json> records;
        std::set<size_t> hidden;
        std::map<unsigned, Mat4> transforms;
    } sent_, next_;
    bool pending_ = false;

  public:
    // A new worker: it has loaded the package (packageAssets assets) and nothing else.
    // Everything else, including every instance record, goes in the first delta.
    void reset(size_t packageAssets) {
        sent_ = {};
        sent_.assets = packageAssets;
        next_ = sent_;
        pending_ = false;
    }
    uint64_t revision() const { return sent_.revision; }
    // The delta from the worker's state to the current one, or null when nothing changed.
    // Records stop at about `budget` bytes; the rest follows in the next pose.
    Json delta(const world::World &w, const std::map<unsigned, Mat4> &transforms,
               const std::vector<AssetSource> &sources, size_t packageAssets, size_t budget = 96 * 1024) {
        next_ = sent_;
        pending_ = false;
        Json d = Json::object();
        size_t bytes = 0;
        if (w.objectAssets.size() > sent_.assets) {
            Json assets = Json::array();
            for (size_t k = sent_.assets; k < w.objectAssets.size(); ++k) {
                if (k < packageAssets || k - packageAssets >= sources.size())
                    throw std::runtime_error("object asset without a source");
                auto &s = sources[k - packageAssets];
                Json a = {{"index", k}, {"directory", s.directory.string()}, {"manifest", s.manifest}};
                bytes += a.dump().size();
                assets.push_back(std::move(a));
                next_.assets = k + 1;
                if (bytes > budget)
                    break;
            }
            d["assets"] = std::move(assets);
        }
        const auto &instances = w.objectInstances;
        // Instances of assets the worker doesn't have yet wait for them.
        size_t count = instances.size();
        for (size_t k = 0; k < instances.size(); ++k)
            if (instances[k].asset >= next_.assets) {
                count = k;
                break;
            }
        if (count != sent_.records.size())
            d["count"] = count;
        next_.records.resize(count);
        Json records = Json::array();
        for (size_t k = 0; k < count && bytes <= budget; ++k) {
            auto r = instanceRecord(instances[k]);
            if (k < sent_.records.size() && sent_.records[k] == r)
                continue;
            bytes += r.dump().size() + 8;
            records.push_back({k, r});
            next_.records[k] = std::move(r);
        }
        // New records the budget cut off stay null here and hidden in the worker until sent.
        if (!records.empty())
            d["instances"] = std::move(records);
        std::set<size_t> hidden;
        for (size_t k = 0; k < count; ++k)
            if (instances[k].hidden)
                hidden.insert(k);
        if (hidden != sent_.hidden) {
            next_.hidden = hidden;
            next_.hiddenRevision = sent_.hiddenRevision + 1;
            d["hidden"] = {{"revision", next_.hiddenRevision}, {"set", hidden}};
        }
        if (transforms != sent_.transforms) {
            Json set = Json::array(), erase = Json::array();
            for (auto &[node, m] : transforms)
                if (auto it = sent_.transforms.find(node); it == sent_.transforms.end() || !(it->second == m))
                    set.push_back({node, matrixJson(m)});
            for (auto &[node, m] : sent_.transforms)
                if (!transforms.contains(node))
                    erase.push_back(node);
            d["transforms"] = {{"set", set}, {"erase", erase}};
            next_.transforms = transforms;
        }
        if (d.empty())
            return nullptr;
        d["base"] = sent_.revision;
        d["revision"] = next_.revision = sent_.revision + 1;
        pending_ = true;
        return d;
    }
    // The packet holding the last delta was sent.
    void commit() {
        if (pending_)
            sent_ = std::move(next_);
        pending_ = false;
    }
};

// Worker side: applies deltas to its own world. Catalog assets decode off the frame
// loop (load) and merge in index order (merge returns the new index); instances of
// an asset not merged yet stay hidden until it is.
class ObjectReceiver {
    struct Load {
        size_t index;
        std::future<std::shared_ptr<world::World>> scratch;
    };
    std::vector<Load> loads_;
    std::vector<world::ObjectInstance> wanted_;
    std::vector<bool> received_;
    std::set<size_t> hidden_;
    uint64_t revision_ = 0, hiddenRevision_ = 0;
    bool dirty_ = false;

  public:
    using Loader = std::function<std::shared_ptr<world::World>(const std::filesystem::path &, const Json &)>;
    std::map<unsigned, Mat4> transforms;
    uint64_t revision() const { return revision_; }
    bool loading() const { return !loads_.empty(); }
    // Starts from the sender's reset mirror: the package's assets and no instances; the
    // plugin's first delta carries every instance.
    void start(world::World &w) {
        w.objectInstances.clear();
        *this = {};
    }
    void apply(const Json &d, size_t worldAssets, const Loader &load) {
        if (d.at("base").get<uint64_t>() != revision_ || d.at("revision").get<uint64_t>() != revision_ + 1)
            throw std::runtime_error("object state revision");
        if (d.contains("assets"))
            for (auto &a : d["assets"]) {
                size_t index = a.at("index");
                if (index != worldAssets + loads_.size())
                    throw std::runtime_error("object asset order");
                std::filesystem::path dir = a.at("directory").get<std::string>();
                Json manifest = a.at("manifest");
                loads_.push_back({index, std::async(std::launch::async, [load, dir, manifest] { return load(dir, manifest); })});
            }
        if (d.contains("count")) {
            size_t count = d["count"];
            if (count > 65536)
                throw std::runtime_error("object count");
            wanted_.resize(count);
            received_.resize(count, false);
        }
        if (d.contains("instances"))
            for (auto &entry : d["instances"]) {
                size_t k = entry.at(0);
                if (k >= wanted_.size())
                    throw std::runtime_error("object index");
                applyRecord(wanted_[k], entry.at(1));
                received_[k] = true;
            }
        if (d.contains("hidden")) {
            hiddenRevision_ = d["hidden"].at("revision");
            hidden_ = d["hidden"].at("set").get<std::set<size_t>>();
        }
        if (d.contains("transforms")) {
            for (auto &n : d["transforms"].at("erase"))
                transforms.erase(n.get<unsigned>());
            for (auto &e : d["transforms"].at("set")) {
                Mat4 m;
                m.v = e.at(1).get<std::array<float, 16>>();
                transforms[e.at(0).get<unsigned>()] = m;
            }
        }
        revision_ = d["revision"];
        dirty_ = true;
    }
    // Merges finished decodes in order while `canMerge` (no other upload pending);
    // true when an asset was merged (stage its upload).
    bool merge(world::World &w, bool canMerge) {
        bool merged = false;
        while (canMerge && !loads_.empty() &&
               loads_.front().scratch.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto scratch = loads_.front().scratch.get(); // rethrows a failed decode
            if (world::mergeObjectAsset(w, *scratch) != loads_.front().index)
                throw std::runtime_error("object asset index");
            loads_.erase(loads_.begin());
            merged = dirty_ = true;
            canMerge = false; // one upload at a time
        }
        return merged;
    }
    // Writes the received instances into the world the renderer draws.
    void materialize(world::World &w) {
        if (!dirty_)
            return;
        dirty_ = false;
        w.objectInstances.resize(wanted_.size());
        for (size_t k = 0; k < wanted_.size(); ++k) {
            auto &o = w.objectInstances[k];
            o = wanted_[k];
            o.hidden = hidden_.contains(k) || !received_[k] || o.asset >= w.objectAssets.size();
            if (o.asset >= w.objectAssets.size())
                o.asset = 0, o.speed = 0;
        }
    }
};
} // namespace hyprune::offload
