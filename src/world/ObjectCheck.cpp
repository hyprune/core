#include "ObjectCheck.hpp"
#include "Schema.hpp"
#include "SpdxData.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <regex>
#include <set>
#include <sstream>
namespace hyprune::world::objects {
namespace {
using Fail = std::function<void(const std::string &)>;
std::string fixed(double v) {
    // JS: +v.toFixed(4) prints the shortest form of the rounded value.
    std::ostringstream s;
    s.precision(10);
    s << std::round(v * 10000) / 10000;
    return s.str();
}
std::string num(const Json &v) {
    if (v.is_number_integer())
        return std::to_string(v.get<int64_t>());
    std::ostringstream s;
    s.precision(17);
    s << v.get<double>();
    auto text = s.str();
    // Shortest round-trip representation, as JS prints numbers.
    for (int p = 1; p <= 17; ++p) {
        std::ostringstream t;
        t.precision(p);
        t << v.get<double>();
        if (std::stod(t.str()) == v.get<double>())
            return t.str();
    }
    return text;
}
double at(const Json &a, size_t i) { return a.at(i).get<double>(); }
bool has(const Json &j, const char *key) { return j.is_object() && j.contains(key); }
const char *axis(int j) { return j == 0 ? "x" : j == 1 ? "y" : "z"; }
} // namespace
std::string screenAspect(double width, double height) {
    double r = width / height;
    if (std::abs(r / (16. / 9) - 1) <= .005)
        return "16:9";
    if (r >= 2.33 && r <= 2.39)
        return "21:9";
    return "";
}
namespace {
const std::vector<std::string> &versionsList() {
    static const std::vector<std::string> list{"0.9", "0.10", "0.11"};
    return list;
}
std::map<std::string, std::string> reservedTraits(const std::string &version) {
    std::map<std::string, std::string> r{{"interactive", "reserved for use actions (RFC-0011 P4)"}};
    if (!atLeast(version, "0.11"))
        r["mount"] = "needs object format 0.11 (workspace screens)";
    if (!atLeast(version, "0.10")) {
        r["mover"] = "needs object format 0.10";
        r["machine"] = "reserved for RFC-0013 live objects (object format 0.10)";
    }
    return r;
}
// Which traits may share an asset (RFC-0011 "Allowed combinations"; machine per RFC-0013).
const std::map<std::string, std::set<std::string>> &traitPairs() {
    static const std::map<std::string, std::set<std::string>> pairs{
        {"decorative", {"collidable", "movable", "seat", "mover", "machine", "mount"}},
        {"collidable", {"decorative", "movable", "seat", "machine", "mount"}},
        {"movable", {"decorative", "collidable", "seat", "machine", "mount"}},
        {"mount", {"decorative", "collidable", "movable"}},
        {"character", {}},
        {"mover", {"decorative"}},
        {"seat", {"decorative", "collidable", "movable"}},
        {"machine", {"decorative", "collidable", "movable"}}};
    return pairs;
}
std::vector<std::string> sceneExtensions(const std::string &version) {
    std::vector<std::string> base{"KHR_materials_unlit", "KHR_texture_transform", "KHR_materials_emissive_strength"};
    if (atLeast(version, "0.7"))
        for (auto e : {"KHR_texture_basisu", "EXT_meshopt_compression", "KHR_mesh_quantization"})
            base.push_back(e);
    return base;
}
using Mat = std::array<double, 16>;
Mat nodeMatrix(const Json &n) {
    if (has(n, "matrix")) {
        Mat m{};
        for (int i = 0; i < 16; ++i)
            m[i] = n["matrix"][i].get<double>();
        return m;
    }
    double x = 0, y = 0, z = 0, w = 1, sx = 1, sy = 1, sz = 1, tx = 0, ty = 0, tz = 0;
    if (has(n, "rotation"))
        x = at(n["rotation"], 0), y = at(n["rotation"], 1), z = at(n["rotation"], 2), w = at(n["rotation"], 3);
    if (has(n, "scale"))
        sx = at(n["scale"], 0), sy = at(n["scale"], 1), sz = at(n["scale"], 2);
    if (has(n, "translation"))
        tx = at(n["translation"], 0), ty = at(n["translation"], 1), tz = at(n["translation"], 2);
    return {(1 - 2 * (y * y + z * z)) * sx, 2 * (x * y + z * w) * sx, 2 * (x * z - y * w) * sx, 0,
            2 * (x * y - z * w) * sy,       (1 - 2 * (x * x + z * z)) * sy, 2 * (y * z + x * w) * sy, 0,
            2 * (x * z + y * w) * sz,       2 * (y * z - x * w) * sz,       (1 - 2 * (x * x + y * y)) * sz, 0,
            tx,                             ty,                             tz,                             1};
}
Mat mul(const Mat &a, const Mat &b) {
    Mat out{};
    for (int i = 0; i < 16; ++i) {
        int c = i >> 2, r = i & 3;
        out[i] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
    }
    return out;
}
std::array<double, 3> apply(const Mat &m, std::array<double, 3> p) {
    std::array<double, 3> out{};
    for (int r = 0; r < 3; ++r)
        out[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r];
    return out;
}
const Json &arrayOr(const Json &j, const char *key) {
    static const Json empty = Json::array();
    return has(j, key) && j[key].is_array() ? j[key] : empty;
}
// glTF extension rules shared by scene.gltf, 0.8 object assets and object manifests.
void sceneExtensionRules(const Json &gltf, const std::string &version, const Fail &fail) {
    auto allowedList = sceneExtensions(version);
    std::set<std::string> allowed(allowedList.begin(), allowedList.end()), required;
    for (auto &id : arrayOr(gltf, "extensionsRequired"))
        if (id.is_string())
            required.insert(id.get<std::string>());
    for (auto &id : required)
        if (!allowed.contains(id))
            fail("extensionsRequired: " + id + " is not allowed in world " + version);
    auto &textures = arrayOr(gltf, "textures");
    auto &images = arrayOr(gltf, "images");
    for (size_t i = 0; i < textures.size(); ++i) {
        auto &texture = textures[i];
        if (!has(texture, "extensions") || !has(texture["extensions"], "KHR_texture_basisu"))
            continue;
        if (!allowed.contains("KHR_texture_basisu"))
            fail("textures/" + std::to_string(i) + ": KHR_texture_basisu needs world 0.7");
        else if (!required.contains("KHR_texture_basisu"))
            fail("extensionsRequired: KHR_texture_basisu must be listed when textures use it");
        else {
            auto &ext = texture["extensions"]["KHR_texture_basisu"];
            const Json *image = has(ext, "source") && ext["source"].is_number_unsigned() && ext["source"].get<size_t>() < images.size()
                                    ? &images[ext["source"].get<size_t>()]
                                    : nullptr;
            auto lower = [](std::string s) {
                std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
                return s;
            };
            if (!image || (has(*image, "mimeType") && (*image)["mimeType"] != "image/ktx2") ||
                (has(*image, "uri") && !lower((*image)["uri"].get<std::string>()).ends_with(".ktx2")))
                fail("textures/" + std::to_string(i) + ": KHR_texture_basisu source must be a KTX2 image");
        }
    }
    bool meshopt = false;
    for (auto &v : arrayOr(gltf, "bufferViews"))
        meshopt = meshopt || (has(v, "extensions") && has(v["extensions"], "EXT_meshopt_compression"));
    if (meshopt && !required.contains("EXT_meshopt_compression"))
        fail(allowed.contains("EXT_meshopt_compression")
                 ? "extensionsRequired: EXT_meshopt_compression must be listed when buffer views use it"
                 : "bufferViews: EXT_meshopt_compression needs world 0.7");
}
std::map<size_t, size_t> parentsOf(const Json &nodes) {
    std::map<size_t, size_t> parents;
    for (size_t i = 0; i < nodes.size(); ++i)
        for (auto &c : arrayOr(nodes[i], "children"))
            if (c.is_number_unsigned())
                parents[c.get<size_t>()] = i;
    return parents;
}
std::vector<size_t> ancestors(const std::map<size_t, size_t> &parents, size_t node, size_t limit) {
    std::vector<size_t> out;
    size_t n = 0;
    for (auto it = parents.find(node); it != parents.end() && n <= limit; it = parents.find(it->second), ++n)
        out.push_back(it->second);
    return out;
}
// World 0.8 object asset rules (node names exist once, animated subtrees do not nest,
// the collision proxy has a mesh outside every animated subtree).
void validateObjectAsset(const Json &gltf, const Json &animations, const Json *collision, const Fail &fail,
                         const std::string &label) {
    auto &nodes = arrayOr(gltf, "nodes");
    auto parents = parentsOf(nodes);
    auto find = [&](const std::string &name) -> std::optional<size_t> {
        std::vector<size_t> hits;
        for (size_t i = 0; i < nodes.size(); ++i)
            if (has(nodes[i], "name") && nodes[i]["name"] == name)
                hits.push_back(i);
        if (hits.size() != 1)
            fail(label + ": node \"" + name + "\" " + (hits.empty() ? "not found" : "is not unique") + " in the asset");
        return hits.size() == 1 ? std::optional<size_t>(hits[0]) : std::nullopt;
    };
    std::map<size_t, std::string> targets;
    for (auto &a : animations)
        if (auto i = find(a.at("node").get<std::string>()))
            targets[*i] = a.at("node").get<std::string>();
    for (auto &[node, name] : targets)
        for (auto p : ancestors(parents, node, nodes.size()))
            if (targets.contains(p))
                fail(label + ": animation \"" + name + "\" is inside animated node \"" + targets.at(p) + "\"");
    if (collision) {
        auto proxy = collision->at("node").get<std::string>();
        if (auto i = find(proxy)) {
            if (!has(nodes[*i], "mesh"))
                fail(label + ": collision node \"" + proxy + "\" has no mesh");
            auto chain = ancestors(parents, *i, nodes.size());
            chain.insert(chain.begin(), *i);
            for (auto p : chain)
                if (targets.contains(p))
                    fail(label + ": collision node \"" + proxy + "\" is inside animated node \"" + targets.at(p) + "\"");
        }
    }
}
const std::map<int, double> componentMax{{5120, 127}, {5121, 255}, {5122, 32767}, {5123, 65535}};
struct Bounds {
    std::array<double, 3> min, max;
    bool exact;
};
std::optional<Bounds> accessorBounds(const Json &gltf, const std::map<size_t, Mat> &drawn) {
    std::array<double, 3> lo{INFINITY, INFINITY, INFINITY}, hi{-INFINITY, -INFINITY, -INFINITY};
    bool exact = true, skinned = false;
    auto &nodes = arrayOr(gltf, "nodes");
    auto &meshes = arrayOr(gltf, "meshes");
    auto &accessors = arrayOr(gltf, "accessors");
    for (auto &[i, m] : drawn) {
        auto &node = nodes[i];
        if (!has(node, "mesh") || !node["mesh"].is_number_unsigned() || node["mesh"].get<size_t>() >= meshes.size())
            continue;
        if (has(node, "skin")) {
            skinned = true;
            continue;
        }
        for (int c = 0; c < 3; ++c) {
            int nonzero = 0;
            for (int r = 0; r < 3; ++r)
                nonzero += std::abs(m[c * 4 + r]) > 1e-6;
            if (nonzero > 1)
                exact = false;
        }
        for (auto &p : arrayOr(meshes[node["mesh"].get<size_t>()], "primitives")) {
            const Json *a = nullptr;
            if (has(p, "attributes") && has(p["attributes"], "POSITION") && p["attributes"]["POSITION"].is_number_unsigned() &&
                p["attributes"]["POSITION"].get<size_t>() < accessors.size())
                a = &accessors[p["attributes"]["POSITION"].get<size_t>()];
            if (!a || !has(*a, "min") || !has(*a, "max")) {
                skinned = true;
                continue;
            }
            bool normalized = a->value("normalized", false);
            double scale = 1;
            if (normalized) {
                bool big = false;
                for (auto key : {"min", "max"})
                    for (auto &v : (*a)[key])
                        big = big || std::abs(v.get<double>()) > 1;
                if (big) {
                    auto it = componentMax.find(a->value("componentType", 0));
                    scale = it == componentMax.end() ? 1 : it->second;
                }
            }
            std::array<double, 3> mn, mx;
            for (int j = 0; j < 3; ++j) {
                mn[j] = std::max(at((*a)["min"], j) / scale, normalized ? -1. : -INFINITY);
                mx[j] = std::max(at((*a)["max"], j) / scale, normalized ? -1. : -INFINITY);
            }
            for (int k = 0; k < 8; ++k) {
                auto w = apply(m, {k & 1 ? mx[0] : mn[0], k & 2 ? mx[1] : mn[1], k & 4 ? mx[2] : mn[2]});
                for (int j = 0; j < 3; ++j)
                    lo[j] = std::min(lo[j], w[j]), hi[j] = std::max(hi[j], w[j]);
            }
        }
    }
    if (skinned || lo[0] == INFINITY)
        return std::nullopt;
    return Bounds{lo, hi, exact};
}
std::string quoted(const Json &v) { return v.dump(); }
template <class F> void unique(const Json &items, const char *field, const std::string &label, const F &fail) {
    std::set<std::string> seen;
    bool dup = false;
    for (auto &v : items)
        if (has(v, field))
            dup = !seen.insert(v[field].dump()).second || dup;
    if (dup)
        fail(label + ": duplicate " + field);
}
void validateObjectGltf(const Json &value, const std::string &version, const Json &gltf, const Measured &measured,
                        const Fail &fail, const Fail &warn, Result &result) {
    auto &traits = value["traits"];
    const Json slots = value.value("slots", Json::object());
    auto &nodes = arrayOr(gltf, "nodes");
    auto &materials = arrayOr(gltf, "materials");
    auto &meshes = arrayOr(gltf, "meshes");
    sceneExtensionRules(gltf, version, fail);
    auto &scenes = arrayOr(gltf, "scenes");
    size_t sceneIndex = gltf.value("scene", 0);
    if (sceneIndex >= scenes.size()) {
        fail("scene: the glTF has no scene");
        return;
    }
    auto drawn = sceneNodes(gltf);
    std::map<std::string, std::vector<size_t>> byName;
    for (size_t i = 0; i < nodes.size(); ++i)
        if (has(nodes[i], "name") && nodes[i]["name"].is_string())
            byName[nodes[i]["name"].get<std::string>()].push_back(i);
    auto node = [&](const std::string &name, const std::string &label) -> std::optional<size_t> {
        auto hits = byName.contains(name) ? byName[name] : std::vector<size_t>{};
        if (hits.size() != 1) {
            fail(label + ": node \"" + name + "\" " + (hits.empty() ? "not found" : "is not unique") + " in the glTF");
            return std::nullopt;
        }
        if (!drawn.contains(hits[0]))
            fail(label + ": node \"" + name + "\" is not in the scene");
        return hits[0];
    };
    const Json animations = has(traits, "decorative") ? traits["decorative"].value("animations", Json::array()) : Json::array();
    const Json *collidable = has(traits, "collidable") ? &traits["collidable"] : nullptr;
    validateObjectAsset(gltf, animations, collidable, fail, "traits");
    for (auto &a : animations) {
        auto name = a.at("node").get<std::string>();
        if (byName.contains(name) && byName[name].size() == 1 && !drawn.contains(byName[name][0]))
            fail("traits.decorative: node \"" + name + "\" is not in the scene");
    }
    // Slots: names exist once; a material or node belongs to one states slot; variants map to palette ids.
    auto materialHits = [&](const std::string &name) {
        std::vector<size_t> hits;
        for (size_t i = 0; i < materials.size(); ++i)
            if (has(materials[i], "name") && materials[i]["name"] == name)
                hits.push_back(i);
        return hits;
    };
    auto subtree = [&](size_t i) {
        std::set<size_t> seen;
        std::vector<size_t> stack{i};
        while (!stack.empty()) {
            auto n = stack.back();
            stack.pop_back();
            if (seen.contains(n) || n >= nodes.size())
                continue;
            seen.insert(n);
            for (auto &c : arrayOr(nodes[n], "children"))
                if (c.is_number_unsigned())
                    stack.push_back(c.get<size_t>());
        }
        return std::vector<size_t>(seen.begin(), seen.end());
    };
    auto meshOf = [&](size_t n) -> const Json * {
        if (n < nodes.size() && has(nodes[n], "mesh") && nodes[n]["mesh"].is_number_unsigned() &&
            nodes[n]["mesh"].get<size_t>() < meshes.size())
            return &meshes[nodes[n]["mesh"].get<size_t>()];
        return nullptr;
    };
    std::map<std::string, std::string> owner;
    std::map<std::string, std::vector<const Json *>> slotPrimitives;
    for (auto &slot : arrayOr(slots, "states")) {
        auto id = slot.at("id").get<std::string>();
        auto label = "slots.states " + id;
        std::vector<const Json *> prims;
        for (auto &name : arrayOr(slot, "materials")) {
            auto hits = materialHits(name.get<std::string>());
            if (hits.size() != 1) {
                fail(label + ": material \"" + name.get<std::string>() + "\" " + (hits.empty() ? "not found" : "is not unique") +
                     " in the glTF");
                continue;
            }
            auto key = "m" + std::to_string(hits[0]);
            if (owner.contains(key))
                fail(label + ": material \"" + name.get<std::string>() + "\" is also in states slot " + owner[key]);
            owner[key] = id;
            size_t before = prims.size();
            for (auto &[n, m] : drawn)
                if (auto mesh = meshOf(n))
                    for (auto &p : arrayOr(*mesh, "primitives"))
                        if (has(p, "material") && p["material"] == hits[0])
                            prims.push_back(&p);
            if (prims.size() == before && prims.empty())
                warn(label + ": material \"" + name.get<std::string>() + "\" is not used by the scene");
        }
        for (auto &name : arrayOr(slot, "nodes")) {
            auto i = node(name.get<std::string>(), label);
            if (!i)
                continue;
            auto key = "n" + std::to_string(*i);
            if (owner.contains(key))
                fail(label + ": node \"" + name.get<std::string>() + "\" is also in states slot " + owner[key]);
            owner[key] = id;
            std::vector<size_t> withMesh;
            for (auto n : subtree(*i))
                if (has(nodes[n], "mesh"))
                    withMesh.push_back(n);
            if (withMesh.empty())
                fail(label + ": node \"" + name.get<std::string>() + "\" has no mesh in its subtree");
            for (auto n : withMesh)
                if (auto mesh = meshOf(n))
                    for (auto &p : arrayOr(*mesh, "primitives"))
                        prims.push_back(&p);
        }
        slotPrimitives[id] = prims;
    }
    if (has(gltf, "extensions") && has(gltf["extensions"], "KHR_materials_variants") &&
        has(gltf["extensions"]["KHR_materials_variants"], "variants")) {
        auto &variants = gltf["extensions"]["KHR_materials_variants"]["variants"];
        std::set<std::string> names;
        bool duplicate = false;
        for (auto &v : variants)
            duplicate = !names.insert(v.value("name", "")).second || duplicate;
        if (duplicate)
            fail("KHR_materials_variants: duplicate variant names");
        auto mappings = [](const Json &p) -> const Json & {
            static const Json none = Json::array();
            return has(p, "extensions") && has(p["extensions"], "KHR_materials_variants")
                       ? arrayOr(p["extensions"]["KHR_materials_variants"], "mappings")
                       : none;
        };
        for (size_t m = 0; m < meshes.size(); ++m) {
            auto &prims = arrayOr(meshes[m], "primitives");
            for (size_t k = 0; k < prims.size(); ++k) {
                std::set<size_t> seen;
                auto where = "meshes/" + std::to_string(m) + "/primitives/" + std::to_string(k);
                for (auto &mapping : mappings(prims[k])) {
                    if (!mapping.value("material", Json()).is_number_unsigned() || mapping["material"].get<size_t>() >= materials.size())
                        fail(where + ": variant mapping to unknown material " + mapping.value("material", Json()).dump());
                    for (auto &v : arrayOr(mapping, "variants")) {
                        size_t index = v.get<size_t>();
                        if (index >= variants.size())
                            fail(where + ": unknown variant " + std::to_string(index));
                        if (seen.contains(index))
                            fail(where + ": variant " + std::to_string(index) + " is mapped twice");
                        seen.insert(index);
                    }
                }
            }
        }
        for (auto &slot : arrayOr(slots, "states")) {
            std::set<std::string> palette;
            for (auto &e : arrayOr(slot, "palette"))
                palette.insert(e.value("id", ""));
            for (auto *p : slotPrimitives[slot.at("id").get<std::string>()])
                for (auto &mapping : mappings(*p))
                    for (auto &v : arrayOr(mapping, "variants")) {
                        size_t index = v.get<size_t>();
                        if (index < variants.size() && !palette.contains(variants[index].value("name", "")))
                            fail("slots.states " + slot.at("id").get<std::string>() + ": variant \"" +
                                 variants[index].value("name", "") + "\" is not a palette id of the slot");
                    }
        }
    }
    for (auto &s : arrayOr(slots, "stacks"))
        node(s.at("node").get<std::string>(), "slots.stacks " + s.at("id").get<std::string>());
    if (has(traits, "mount"))
        for (auto &a : traits["mount"]["anchors"])
            if (a.contains("frameNode"))
                node(a["frameNode"].get<std::string>(), "traits.mount anchor " + a["id"].get<std::string>());
    std::map<std::string, std::string> levelNodes;
    for (auto &l : arrayOr(slots, "levels")) {
        auto where = "slots.levels " + l.at("id").get<std::string>();
        std::vector<std::string> names{l.at("node").get<std::string>()};
        if (l.contains("surface"))
            names.push_back(l["surface"].get<std::string>());
        for (auto &name : names) {
            if (collidable && (*collidable)["node"] == name) {
                fail(where + ": the collision proxy can't be moved");
                continue;
            }
            if (levelNodes.contains(name))
                fail(where + ": node \"" + name + "\" is also moved by levels slot " + levelNodes[name]);
            else
                levelNodes[name] = l["id"].get<std::string>();
        }
        auto i = node(names[0], where);
        auto j = names.size() > 1 ? node(names[1], where) : std::nullopt;
        if (i && j) {
            auto inside = subtree(*i);
            if (std::find(inside.begin(), inside.end(), *j) != inside.end())
                fail(where + ": surface \"" + names[1] + "\" must not be inside the fill node's subtree");
        }
    }
    for (auto &t : arrayOr(slots, "toggles"))
        for (auto &name : arrayOr(t, "nodes")) {
            if (collidable && name == (*collidable)["node"])
                fail("slots.toggles " + t.at("id").get<std::string>() + ": the collision proxy can't be toggled");
            node(name.get<std::string>(), "slots.toggles " + t.at("id").get<std::string>());
        }
    // Characters are skinned and carry their clips; other objects animate declaratively.
    std::map<std::string, const Json *> clips;
    for (auto &a : arrayOr(gltf, "animations"))
        if (has(a, "name") && a["name"].is_string())
            clips[a["name"].get<std::string>()] = &a;
    if (has(traits, "character")) {
        if (arrayOr(gltf, "skins").empty())
            fail("traits.character: the glTF has no skin");
        for (auto &[name, measured] : traits["character"].at("clips").items()) {
            if (!clips.contains(name)) {
                fail("traits.character: clip \"" + name + "\" not found in the glTF");
                continue;
            }
            if (has(measured, "speed")) {
                auto *clip = clips[name];
                const Json speed = has(*clip, "extras") ? clip->at("extras").value("speed", Json()) : Json();
                if (!speed.is_number())
                    fail("traits.character: clip \"" + name + "\" has no extras.speed");
                else if (std::abs(measured["speed"].get<double>() - speed.get<double>()) > .02 * speed.get<double>())
                    fail("traits.character: clip \"" + name + "\" speed " + num(measured["speed"]) +
                         " differs from extras.speed " + num(speed) + " by more than 2%");
            }
        }
    } else {
        if (!arrayOr(gltf, "skins").empty())
            fail("skins: only characters may be skinned");
        if (!arrayOr(gltf, "animations").empty())
            warn("animations: glTF animations are ignored outside characters; use traits.decorative");
    }
    // Triangle budgets (drawn scene; proxy separately) and bounds.
    uint64_t triangles = 0;
    for (auto &[i, m] : drawn)
        if (auto mesh = meshOf(i))
            for (auto &p : arrayOr(*mesh, "primitives"))
                triangles += primitiveTriangles(gltf, p);
    bool item = has(traits, "mover") && traits["mover"].value("item", false) == true;
    uint64_t cap = has(traits, "character") ? Limits::characterTriangles : item ? Limits::itemTriangles : Limits::triangles;
    if (triangles > cap)
        fail("triangles: " + std::to_string(triangles) + " exceeds " + std::to_string(cap));
    if (collidable) {
        auto name = (*collidable)["node"].get<std::string>();
        if (byName.contains(name) && !byName[name].empty()) {
            uint64_t n = 0;
            if (auto mesh = meshOf(byName[name][0]))
                for (auto &p : arrayOr(*mesh, "primitives"))
                    n += primitiveTriangles(gltf, p);
            if (n > Limits::collisionTriangles)
                fail("traits.collidable: proxy has " + std::to_string(n) + " triangles, more than " +
                     std::to_string(Limits::collisionTriangles));
        }
    }
    result.triangles = triangles;
    if (!measured.bounds) {
        auto &b = value["bounds"];
        const double t = Limits::boundsTolerance;
        auto box = accessorBounds(gltf, drawn);
        if (!box)
            warn("bounds: not checked (skinned or unbounded geometry needs measured bounds)");
        else {
            for (int j = 0; j < 3; ++j) {
                double mn = at(b["min"], j), mx = at(b["max"], j);
                if (box->exact ? std::abs(mn - box->min[j]) > t : mn < box->min[j] - t)
                    fail(std::string("bounds: min ") + axis(j) + " " + num(b["min"][j]) + " does not match the scene (" +
                         fixed(box->min[j]) + ")");
                if (box->exact ? std::abs(mx - box->max[j]) > t : mx > box->max[j] + t)
                    fail(std::string("bounds: max ") + axis(j) + " " + num(b["max"][j]) + " does not match the scene (" +
                         fixed(box->max[j]) + ")");
            }
            if (!box->exact)
                warn("bounds: checked against a containing box only (rotated meshes); pass measured bounds for the 1 cm check");
        }
    }
}
void validateMachine(const Json &value, const Fail &fail) {
    auto &machine = value["traits"]["machine"];
    const Json slots = value.value("slots", Json::object());
    const std::string label = "traits.machine";
    std::map<std::string, Json> inputs;
    int count = 0;
    for (auto &input : arrayOr(machine, "inputs")) {
        auto id = input.at("id").get<std::string>();
        if (inputs.contains(id))
            fail(label + ": duplicate input " + id);
        inputs[id] = input;
        count += input.value("repeat", 1);
        auto il = label + " input " + id;
        auto type = input.at("type").get<std::string>();
        if (type == "int" || type == "number") {
            if (input.at("min").get<double>() >= input.at("max").get<double>())
                fail(il + ": min must be below max");
            auto integral = [](const Json &v) { return v.is_number() && std::floor(v.get<double>()) == v.get<double>(); };
            if (type == "int" && !(integral(input["min"]) && integral(input["max"])))
                fail(il + ": int bounds must be integers");
        }
        if (!input.contains("default"))
            continue;
        auto &d = input["default"];
        bool ok = false;
        if (type == "enum")
            ok = std::find(input["values"].begin(), input["values"].end(), d) != input["values"].end();
        else if (type == "int")
            ok = d.is_number() && std::floor(d.get<double>()) == d.get<double>() && d.get<double>() >= input["min"].get<double>() &&
                 d.get<double>() <= input["max"].get<double>();
        else if (type == "number")
            ok = d.is_number() && d.get<double>() >= input["min"].get<double>() && d.get<double>() <= input["max"].get<double>();
        else if (type == "bool")
            ok = d.is_boolean();
        else if (type == "text") {
            size_t chars = 0;
            if (d.is_string())
                for (unsigned char c : d.get<std::string>())
                    chars += (c & 0xc0) != 0x80;
            ok = d.is_string() && chars <= size_t(input.value("maxLength", 64));
        }
        if (!ok)
            fail(il + ": default " + d.dump() + " is not a valid " + type + " value");
    }
    if (count > 16)
        fail(label + ": " + std::to_string(count) + " inputs counting repeats, more than 16");
    if (machine.contains("primary") && !inputs.contains(machine["primary"].get<std::string>()))
        fail(label + ": primary names unknown input " + machine["primary"].get<std::string>());
    auto resolve = [&](const std::string &ref, const std::string &where) -> const Json * {
        auto dot = ref.find('.');
        auto id = ref.substr(0, dot);
        if (!inputs.contains(id)) {
            fail(where + ": unknown input " + ref);
            return nullptr;
        }
        auto &input = inputs[id];
        bool indexed = dot != std::string::npos;
        if (input.contains("repeat")) {
            int repeat = input["repeat"];
            if (!indexed || std::atoi(ref.substr(dot + 1).c_str()) > repeat) {
                fail(where + ": " + ref + " needs an index 1-" + std::to_string(repeat));
                return nullptr;
            }
        } else if (indexed) {
            fail(where + ": " + ref + " takes no index");
            return nullptr;
        }
        return &input;
    };
    struct Output {
        std::optional<std::vector<Json>> values;
        std::optional<std::array<double, 2>> range;
        bool integer = false;
        std::string kind;
    };
    auto outputs = [](const Json &map, const Json &input) {
        Output o;
        auto type = input.value("type", "");
        if (!map.is_object() || map.empty()) {
            if (type == "enum")
                o.values = std::vector<Json>(input["values"].begin(), input["values"].end());
            else if (type == "bool")
                o.values = std::vector<Json>{true, false};
            else if (type == "int" || type == "number")
                o.range = std::array<double, 2>{input["min"].get<double>(), input["max"].get<double>()}, o.integer = type == "int";
            else
                o.kind = type;
            return o;
        }
        if (map.contains("cases") || map.contains("thresholds")) {
            std::vector<Json> v;
            for (auto &c : map.contains("cases") ? map["cases"] : map["thresholds"])
                v.push_back(c.value("to", Json()));
            if (map.contains("else"))
                v.push_back(map["else"]);
            o.values = v;
        } else if (map.contains("scale")) {
            if (map.value("clamp", true) == false)
                o.kind = "unbounded";
            else {
                double a = map["scale"]["to"][0], b = map["scale"]["to"][1];
                o.range = std::array<double, 2>{std::min(a, b), std::max(a, b)};
                const Json round = map.value("round", Json());
                o.integer = round.is_string() ? !round.get<std::string>().empty()
                            : round.is_boolean() ? round.get<bool>()
                            : round.is_number()  ? round.get<double>() != 0
                                                 : !round.is_null(); // JS !!map.round
            }
        } else if (map.contains("becomes") || map.contains("counter"))
            o.kind = "event";
        else if (map.contains("format"))
            o.kind = "text";
        else
            o.kind = "unknown";
        return o;
    };
    auto numeric = [](const Json *input) { return input && ((*input)["type"] == "int" || (*input)["type"] == "number"); };
    // The form applies to the running average (or the override's value): an int input's
    // average is fractional, and an override widens a direct range.
    auto averaged = [&](const Json &map, const Json &input) {
        if (!map.is_object() || !map.contains("average"))
            return outputs(map, input);
        Json form = map;
        form.erase("average");
        auto o = outputs(form, input);
        if (!form.empty())
            return o;
        const Json *over = nullptr;
        if (map["average"].contains("override")) {
            auto ref = map["average"]["override"].get<std::string>();
            auto id = ref.substr(0, ref.find('.'));
            if (inputs.contains(id))
                over = &inputs[id];
        }
        if (o.range && numeric(over))
            o.range = std::array<double, 2>{std::min((*o.range)[0], (*over)["min"].get<double>()),
                                            std::max((*o.range)[1], (*over)["max"].get<double>())};
        else if (over && !numeric(over)) {
            Output k;
            k.kind = (*over).value("type", "");
            return k;
        }
        if (o.range)
            o.integer = false;
        return o;
    };
    std::set<std::string> animated, positions;
    if (has(value["traits"], "decorative"))
        for (auto &a : arrayOr(value["traits"]["decorative"], "animations"))
            animated.insert(a["node"].get<std::string>());
    for (auto &p : arrayOr(machine, "positions"))
        positions.insert(p["node"].get<std::string>());
    auto slotIds = [&](const char *kind) {
        std::set<std::string> ids;
        for (auto &s : arrayOr(slots, kind))
            ids.insert(s["id"].get<std::string>());
        return ids;
    };
    std::map<std::string, std::set<std::string>> palettes;
    for (auto &s : arrayOr(slots, "states"))
        for (auto &e : arrayOr(s, "palette"))
            palettes[s["id"].get<std::string>()].insert(e["id"].get<std::string>());
    for (auto &s : arrayOr(slots, "states"))
        palettes[s["id"].get<std::string>()];
    auto within = [&](const Output &o, double lo, double hi, const std::string &where) {
        if (o.values) {
            for (auto &v : *o.values)
                if (!v.is_number() || v.get<double>() < lo || v.get<double>() > hi) {
                    fail(where + ": outputs must be numbers " + num(Json(lo)) + "-" + (std::isinf(hi) ? "Infinity" : num(Json(hi))));
                    return;
                }
        } else if (o.range) {
            if ((*o.range)[0] < lo || (*o.range)[1] > hi)
                fail(where + ": range " + num(Json((*o.range)[0])) + "-" + num(Json((*o.range)[1])) + " exceeds " + num(Json(lo)) +
                     "-" + (std::isinf(hi) ? "Infinity" : num(Json(hi))));
        } else
            fail(where + ": needs a numeric mapping");
    };
    for (auto &drive : arrayOr(machine, "drives")) {
        auto inputRef = drive.at("input").get<std::string>(), target = drive.at("target").get<std::string>();
        auto where = label + " drive " + inputRef + " -> " + target;
        auto *input = resolve(inputRef, where);
        auto colon = target.find(':');
        auto kind = target.substr(0, colon), name = colon == std::string::npos ? "" : target.substr(colon + 1);
        bool known = kind == "animation"  ? animated.contains(name)
                     : kind == "position" ? positions.contains(name)
                     : kind == "state" || kind == "emission" ? palettes.contains(name)
                     : kind == "label"   ? slotIds("labels").contains(name)
                     : kind == "visible" ? slotIds("toggles").contains(name)
                     : kind == "count"   ? slotIds("stacks").contains(name)
                     : kind == "spawn"   ? slotIds("spawns").contains(name)
                     : kind == "effect"  ? slotIds("effects").contains(name)
                     : kind == "level"   ? slotIds("levels").contains(name)
                                         : false;
        if (!known) {
            fail(where + ": unknown " + kind + " target \"" + name + "\"");
            continue;
        }
        if (!input)
            continue;
        const Json map = drive.value("map", Json());
        if (map.is_object() && map.contains("average")) {
            if (!numeric(input)) {
                fail(where + ": average needs an int or number input");
                continue;
            }
            if (map["average"].contains("override")) {
                auto ref = map["average"]["override"].get<std::string>();
                auto *over = resolve(ref, where + " average override");
                if (ref == inputRef)
                    fail(where + ": average override must name another input");
                else if (over && ((*over)["type"] == "event" || (*over)["type"] == "items"))
                    fail(where + ": average override " + ref + " must be a value input, not " + (*over)["type"].get<std::string>());
            }
        }
        auto o = averaged(map, *input);
        bool event = o.kind == "event" || ((*input)["type"] == "event" && map.is_null());
        if (kind == "state") {
            if (!o.values)
                fail(where + ": needs palette ids (an enum input or cases)");
            else
                for (auto &v : *o.values)
                    if (!v.is_string() || !palettes[name].contains(v.get<std::string>()))
                        fail(where + ": \"" + (v.is_string() ? v.get<std::string>() : v.dump()) + "\" is not a palette id of " + name);
        } else if (kind == "animation")
            within(o, 0, 10, where);
        else if (kind == "emission")
            within(o, 0, 16, where);
        else if (kind == "position" || kind == "level")
            within(o, 0, 1, where);
        else if (kind == "count") {
            within(o, 0, INFINITY, where);
            if (o.range && !o.integer)
                fail(where + ": counts need an int input or a rounded scale");
        } else if (kind == "visible") {
            bool booleans = o.values.has_value();
            if (o.values)
                for (auto &v : *o.values)
                    booleans = booleans && v.is_boolean();
            if (!booleans)
                fail(where + ": needs boolean outputs");
        } else if (kind == "spawn" || kind == "effect") {
            if (!event)
                fail(where + ": needs an event (an event input, counter or becomes)");
        } else if (kind == "label" && event)
            fail(where + ": events can't be shown as text");
    }
    for (auto &p : arrayOr(machine, "positions")) {
        auto node = p["node"].get<std::string>();
        if (!animated.contains(node))
            fail(label + ": position \"" + node + "\" is not a decorative animation");
        if (p["range"][0].get<double>() >= p["range"][1].get<double>())
            fail(label + ": position \"" + node + "\" range must increase");
    }
    for (auto &recipe : arrayOr(machine, "recipes")) {
        auto where = label + " recipe " + recipe.value("name", "");
        if (recipe.contains("fields"))
            for (auto &[ref, v] : recipe["fields"].items())
                resolve(ref, where);
        if (recipe.contains("maps"))
            for (auto &[ref, map] : recipe["maps"].items()) {
                auto *input = resolve(ref, where);
                if (map.is_object() && map.contains("average")) {
                    if (input && !numeric(input))
                        fail(where + ": " + ref + " average needs an int or number input");
                    if (map["average"].is_object() && map["average"].contains("override"))
                        fail(where + ": " + ref + " average override is for drives only");
                }
                if (input && (*input)["type"] == "enum") {
                    auto o = outputs(map, Json{{"type", "text"}});
                    if (o.values)
                        for (auto &v : *o.values)
                            if (std::find((*input)["values"].begin(), (*input)["values"].end(), v) == (*input)["values"].end())
                                fail(where + ": " + ref + " maps to \"" + (v.is_string() ? v.get<std::string>() : v.dump()) +
                                     "\", not a value of the input");
                }
            }
    }
}
bool covered(const Json &attribution, const std::string &path) {
    for (auto &a : attribution) {
        auto p = a.value("path", "");
        if (p == "." || p == path || path.starts_with(p + "/"))
            return true;
    }
    return false;
}
void validateObjectManifest(const Json &value, const std::string &version, const Options &options, const Fail &fail,
                            const Fail &warn, Result &result) {
    auto &traits = value["traits"];
    const Json slots = value.value("slots", Json::object());
    auto &b = value["bounds"];
    std::vector<std::string> names;
    for (auto &[k, v] : traits.items())
        names.push_back(k);
    if (options.directory && *options.directory != value["id"].get<std::string>())
        fail("id: \"" + value["id"].get<std::string>() + "\" differs from its directory \"" + *options.directory + "\"");
    for (auto &a : names)
        for (auto &c : names)
            if (a < c && !(traitPairs().contains(a) && traitPairs().at(a).contains(c)))
                fail("traits: " + a + " and " + c + " can't be combined");
    for (int j = 0; j < 3; ++j)
        if (at(b["min"], j) >= at(b["max"], j))
            fail(std::string("bounds: min ") + axis(j) + " must be below max");
    const double t = Limits::boundsTolerance;
    std::set<std::string> surfaces;
    if (has(traits, "movable")) {
        if (traits["movable"].contains("surfaces"))
            for (auto &s : traits["movable"]["surfaces"])
                surfaces.insert(s.get<std::string>());
        else
            surfaces.insert("floor");
    }
    std::string one = surfaces.size() == 1 ? *surfaces.begin() : "";
    if (one == "floor" && std::abs(at(b["min"], 1)) > t)
        fail("bounds: min y must be 0 (the base at the origin), is " + num(b["min"][1]));
    if (has(traits, "character") && std::abs(at(b["min"], 1)) > Limits::characterOriginTolerance)
        fail("bounds: min y must be within 0.05 m of 0 (the feet at the origin), is " + num(b["min"][1]));
    if (one == "wall" && std::abs(at(b["min"], 2)) > t)
        fail("bounds: min z must be 0 (the back against the wall), is " + num(b["min"][2]));
    if (one == "ceiling" && std::abs(at(b["max"], 1)) > t)
        fail("bounds: max y must be 0 (the top at the origin), is " + num(b["max"][1]));
    const double m = Limits::slotMargin;
    auto inside = [&](const Json &p, const std::string &where) {
        for (int j = 0; j < 3; ++j)
            if (at(p, j) < at(b["min"], j) - m || at(p, j) > at(b["max"], j) + m) {
                fail(where + ": lies outside the bounds grown by 0.5 m");
                return;
            }
    };
    auto quat = [&](const Json &q, const std::string &where) {
        double n = std::sqrt(at(q, 0) * at(q, 0) + at(q, 1) * at(q, 1) + at(q, 2) * at(q, 2) + at(q, 3) * at(q, 3));
        if (std::abs(n - 1) > .002)
            fail(where + ": quaternion must be normalized");
    };
    const Json animations = has(traits, "decorative") ? traits["decorative"].value("animations", Json::array()) : Json::array();
    unique(animations, "node", "traits.decorative animations", fail);
    for (auto &n : animations) {
        auto &ax = n["axis"];
        if (std::abs(std::sqrt(at(ax, 0) * at(ax, 0) + at(ax, 1) * at(ax, 1) + at(ax, 2) * at(ax, 2)) - 1) > .001)
            fail("traits.decorative: animation \"" + n["node"].get<std::string>() + "\" axis must be a unit vector");
        auto type = n["type"].get<std::string>();
        bool bob = type == "bob";
        if (bob ? !n.contains("amplitude") : !n.contains("pivot"))
            fail("traits.decorative: " + type + " animation \"" + n["node"].get<std::string>() + "\" needs " +
                 (bob ? "an amplitude" : "a pivot"));
        if (bob ? n.contains("pivot") : n.contains("amplitude"))
            fail("traits.decorative: " + type + " animation \"" + n["node"].get<std::string>() + "\" takes no " +
                 (bob ? "pivot" : "amplitude"));
    }
    if (has(traits, "collidable"))
        for (auto &n : animations)
            if (n["node"] == traits["collidable"]["node"])
                fail("traits.collidable: node \"" + traits["collidable"]["node"].get<std::string>() + "\" is animated");
    if (has(traits, "movable") && traits["movable"].contains("scale")) {
        auto &s = traits["movable"]["scale"];
        if (!(at(s, 0) <= 1 && 1 <= at(s, 1)))
            fail("traits.movable: scale must be [min, max] with min <= 1 <= max");
    }
    if (has(traits, "character"))
        unique(traits["character"].value("interactions", Json::array()), "id", "traits.character interactions", fail);
    if (has(traits, "seat")) {
        auto &anchors = traits["seat"]["anchors"];
        unique(anchors, "id", "traits.seat anchors", fail);
        if (traits["seat"].value("capacity", 0) > int(anchors.size()))
            fail("traits.seat: capacity exceeds the number of anchors");
        for (auto &a : anchors)
            inside(a["position"], "traits.seat anchor " + a["id"].get<std::string>());
    }
    if (has(traits, "mover") && traits["mover"].contains("offset"))
        inside(traits["mover"]["offset"], "traits.mover offset");
    if (has(traits, "mount")) {
        auto &anchors = traits["mount"]["anchors"];
        unique(anchors, "id", "traits.mount anchors", fail);
        if (std::count_if(anchors.begin(), anchors.end(), [](auto &a) { return a["role"] == "mounted-workspace"; }) > 1)
            fail("traits.mount: at most one mounted-workspace anchor");
        for (auto &a : anchors) {
            auto where = "traits.mount anchor " + a["id"].get<std::string>();
            double w = at(a["size"], 0), h = at(a["size"], 1);
            quat(a["pose"]["rotation"], where);
            if (a["role"] == "mounted-workspace") {
                if (screenAspect(w, h).empty())
                    fail(where + ": a workspace screen is 16:9 or 21:9, not " + fixed(std::round(w / h * 1000) / 1000) + ":1");
                if (h < ScreenLimits::minHeight)
                    fail(where + ": at least 0.9 m high, is " + num(a["size"][1]));
            }
            // The rectangle's corners lie inside the bounds grown by 0.1 m.
            auto &q = a["pose"]["rotation"];
            double x = at(q, 0), y = at(q, 1), z = at(q, 2), qw = at(q, 3);
            auto &p = a["pose"]["position"];
            bool outside = false;
            for (auto [sx, sy] : {std::pair{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}) {
                double v[3]{sx * w / 2, sy * h / 2, 0};
                double tx = 2 * (y * v[2] - z * v[1]), ty = 2 * (z * v[0] - x * v[2]), tz = 2 * (x * v[1] - y * v[0]);
                double c[3]{v[0] + qw * tx + y * tz - z * ty, v[1] + qw * ty + z * tx - x * tz, v[2] + qw * tz + x * ty - y * tx};
                for (int j = 0; j < 3; ++j)
                    if (at(p, j) + c[j] < at(b["min"], j) - ScreenLimits::boundsMargin ||
                        at(p, j) + c[j] > at(b["max"], j) + ScreenLimits::boundsMargin)
                        outside = true;
            }
            if (outside)
                fail(where + ": lies outside the bounds grown by 0.1 m");
        }
    }
    for (auto &[kind, list] : slots.items()) {
        unique(list, "id", "slots." + kind, fail);
        for (auto &s : list) {
            auto where = "slots." + kind + " " + s.value("id", "");
            if (s.contains("pose")) {
                quat(s["pose"]["rotation"], where);
                inside(s["pose"]["position"], where);
            }
            if (s.contains("palette"))
                unique(s["palette"], "id", where + " palette", fail);
        }
    }
    std::map<std::string, const Json *> tracks;
    for (auto &tr : arrayOr(slots, "tracks"))
        tracks[tr["id"].get<std::string>()] = &tr;
    for (auto &tr : arrayOr(slots, "tracks")) {
        auto &points = tr["points"];
        for (size_t i = 0; i < points.size(); ++i) {
            auto where = "slots.tracks " + tr["id"].get<std::string>() + " point " + std::to_string(i);
            inside(points[i]["position"], where);
            if (points[i].value("type", "") == "arc" && i > 0) {
                auto &a = points[i - 1]["position"], &p = points[i]["position"];
                if (std::hypot(at(p, 0) - at(a, 0), at(p, 2) - at(a, 2)) < .1)
                    fail(where + ": an arc needs a horizontal chord of at least 0.1 m");
            }
        }
    }
    for (auto &s : arrayOr(slots, "spawns"))
        if (s.contains("track") && s["track"] != "port") {
            auto id = s["track"].get<std::string>();
            if (!tracks.contains(id))
                fail("slots.spawns " + s["id"].get<std::string>() + ": unknown track " + id);
            else
                for (auto &a : arrayOr(s, "items")) {
                    auto &tr = *tracks[id];
                    if (tr.contains("items")) {
                        auto &assets = tr["items"]["assets"];
                        if (std::find(assets.begin(), assets.end(), a) == assets.end())
                            fail("slots.spawns " + s["id"].get<std::string>() + ": item " + a.get<std::string>() +
                                 " is not in track " + id + "'s items");
                    }
                }
        }
    for (auto &l : arrayOr(slots, "levels"))
        if (l.contains("axis")) {
            double x = l["axis"][0], y = l["axis"][1], z = l["axis"][2];
            if (std::abs(std::sqrt(x * x + y * y + z * z) - 1) > .001)
                fail("slots.levels " + l["id"].get<std::string>() + ": axis must be a unit vector");
        }
    for (auto &s : arrayOr(slots, "stacks")) {
        auto &grid = s["layout"]["grid"];
        double cells = 1;
        for (auto &g : grid)
            cells *= g.get<double>();
        if (s["cap"].get<double>() > cells)
            fail("slots.stacks " + s["id"].get<std::string>() + ": cap exceeds the layout grid");
        for (size_t j = 0; j < grid.size(); ++j)
            if (grid[j].get<double>() > 1 && !(std::isfinite(s["layout"]["spacing"][j].get<double>()) && s["layout"]["spacing"][j].get<double>() != 0))
                fail("slots.stacks " + s["id"].get<std::string>() + ": spacing " + axis(int(j)) + " must be non-zero for " +
                     num(grid[j]) + " cells");
    }
    if (has(traits, "machine"))
        validateMachine(value, fail);
    if (options.files) {
        uint64_t total = 0;
        std::set<std::string> paths;
        for (auto &f : *options.files)
            total += f.size, paths.insert(f.path);
        if (options.files->size() > Limits::files)
            fail("directory: " + std::to_string(options.files->size()) + " files, more than " + std::to_string(Limits::files));
        if (total > Limits::directoryBytes)
            fail("directory: " + std::to_string(total) + " bytes, more than " + std::to_string(Limits::directoryBytes));
        for (auto &f : *options.files) {
            if (f.size > Limits::fileBytes)
                fail("directory: " + f.path + " exceeds " + std::to_string(Limits::fileBytes) + " bytes");
            if (!covered(value["attribution"], f.path))
                fail("attribution: " + f.path + " is not covered");
        }
        for (auto field : {"scene", "thumbnail"})
            if (value.contains(field) && !paths.contains(value[field].get<std::string>()))
                fail(std::string(field) + ": " + value[field].get<std::string>() + " not found in the directory");
    }
    if (options.thumbnail) {
        auto &th = *options.thumbnail;
        if (th.width != Limits::thumbnailSize || th.height != Limits::thumbnailSize)
            fail("thumbnail: " + std::to_string(th.width) + " x " + std::to_string(th.height) + ", must be 256 x 256");
        if (th.bytes > Limits::thumbnailBytes)
            fail("thumbnail: " + std::to_string(th.bytes) + " bytes, more than " + std::to_string(Limits::thumbnailBytes));
    }
    auto &measured = options.measured;
    if (measured.bounds)
        for (int end = 0; end < 2; ++end)
            for (int j = 0; j < 3; ++j) {
                double v = at(b[end ? "max" : "min"], j), w = (*measured.bounds)[end][j];
                if (std::abs(v - w) > t)
                    fail(std::string("bounds: ") + (end ? "max " : "min ") + axis(j) + " " + num(b[end ? "max" : "min"][j]) +
                         " is more than 1 cm from the scene's " + fixed(w));
            }
    int texture = has(traits, "character") ? Limits::characterTextureSize : Limits::textureSize;
    if (measured.maxTextureSize && *measured.maxTextureSize > texture)
        fail("textures: " + std::to_string(*measured.maxTextureSize) + " px exceeds " + std::to_string(texture) + " px");
    uint64_t gpu = has(traits, "mover") && traits["mover"].value("item", false) == true ? Limits::itemGpuBytes : Limits::gpuBytes;
    if (measured.gpuBytes && *measured.gpuBytes > gpu)
        fail("gpu: " + std::to_string(*measured.gpuBytes) + " bytes exceeds " + std::to_string(gpu));
    if (options.gltf)
        validateObjectGltf(value, version, *options.gltf, measured, fail, warn, result);
}
// Common to object and library manifests: reserved traits and formatVersion named
// plainly, then the schema, then version prerelease, SPDX and attribution paths.
bool checkCommon(const std::string &kind, const Json &value, const std::string &version, const Fail &fail) {
    bool failed = false;
    auto failing = [&](const std::string &m) { failed = true, fail(m); };
    Json copy = value;
    if (kind == "object" && value.is_object() && value.contains("traits") && value["traits"].is_object())
        for (auto &[name, why] : reservedTraits(version))
            if (value["traits"].contains(name)) {
                failing("traits." + name + ": " + why + "; rejected in object format " + version);
                copy["traits"].erase(name);
            }
    auto &list = formats();
    const Json fv = value.is_object() ? value.value("formatVersion", Json()) : Json();
    if (fv != version) {
        bool known = fv.is_string() && std::find(list.begin(), list.end(), fv.get<std::string>()) != list.end();
        std::string names;
        for (auto &f : list)
            names += (names.empty() ? "" : ", ") + f;
        failing("formatVersion: " + (fv.is_null() ? std::string("undefined") : fv.dump()) + " is not " +
                (known ? "the requested " + version : "an object format (" + names + ")"));
        if (copy.is_object())
            copy["formatVersion"] = version;
    }
    try {
        validateObjectDocument(copy, kind, version);
    } catch (const std::exception &e) {
        std::string message = e.what();
        if (message.starts_with(": "))
            message = "/" + message.substr(2);
        failing(message);
    }
    if (failed)
        return false;
    auto version_ = value["version"].get<std::string>();
    auto core = version_.substr(0, version_.find('+'));
    auto dash = core.find('-');
    if (dash != std::string::npos) {
        std::stringstream parts(core.substr(dash + 1));
        std::string part;
        while (std::getline(parts, part, '.'))
            if (part.size() > 1 && part[0] == '0' && std::all_of(part.begin(), part.end(), ::isdigit))
                fail("version: numeric prerelease identifiers cannot have leading zeros");
    }
    if (!validSpdx(value["license"].get<std::string>()))
        fail("license: invalid SPDX expression");
    std::set<std::string> paths;
    bool duplicate = false;
    for (auto &a : value["attribution"])
        duplicate = !paths.insert(a["path"].get<std::string>()).second || duplicate;
    if (duplicate)
        fail("attribution: duplicate path");
    for (auto &a : value["attribution"])
        if (!validSpdx(a["license"].get<std::string>()))
            fail("attribution " + a["path"].get<std::string>() + ": invalid SPDX expression");
    return true;
}
std::string defaultVersion(const Json &value) {
    auto &list = formats();
    if (value.is_object() && value.contains("formatVersion") && value["formatVersion"].is_string() &&
        std::find(list.begin(), list.end(), value["formatVersion"].get<std::string>()) != list.end())
        return value["formatVersion"];
    return list.back();
}
} // namespace
const std::vector<std::string> &formats() { return versionsList(); }
bool atLeast(const std::string &version, const std::string &minimum) {
    std::stringstream a(version), b(minimum);
    std::string x, y;
    while (true) {
        bool more = bool(std::getline(a, x, '.')), other = bool(std::getline(b, y, '.'));
        if (!more && !other)
            return true;
        long p = more ? std::atol(x.c_str()) : 0, q = other ? std::atol(y.c_str()) : 0;
        if (p != q)
            return p > q;
    }
}
uint64_t primitiveTriangles(const Json &gltf, const Json &p) {
    auto &accessors = arrayOr(gltf, "accessors");
    Json index = has(p, "indices") ? p["indices"] : has(p, "attributes") ? p["attributes"].value("POSITION", Json()) : Json();
    uint64_t count = 0;
    if (index.is_number_unsigned() && index.get<size_t>() < accessors.size())
        count = accessors[index.get<size_t>()].value("count", 0);
    int mode = p.value("mode", 4);
    return mode == 4 ? count / 3 : mode == 5 || mode == 6 ? (count > 2 ? count - 2 : 0) : 0;
}
std::map<size_t, std::array<double, 16>> sceneNodes(const Json &gltf) {
    std::map<size_t, Mat> out;
    auto &nodes = arrayOr(gltf, "nodes");
    auto &scenes = arrayOr(gltf, "scenes");
    size_t index = gltf.value("scene", 0);
    if (index >= scenes.size())
        return out;
    std::function<void(size_t, const Mat &, int)> visit = [&](size_t i, const Mat &parent, int depth) {
        if (out.contains(i) || i >= nodes.size() || depth > 64)
            return;
        auto m = mul(parent, nodeMatrix(nodes[i]));
        out[i] = m;
        for (auto &c : arrayOr(nodes[i], "children"))
            if (c.is_number_unsigned())
                visit(c.get<size_t>(), m, depth + 1);
    };
    for (auto &i : arrayOr(scenes[index], "nodes"))
        if (i.is_number_unsigned())
            visit(i.get<size_t>(), nodeMatrix(Json::object()), 0);
    return out;
}
bool validSpdx(const std::string &source) {
    // Tokens as spdx-expression-parse scans them, then its precedence parser.
    struct Token {
        std::string type, text;
    };
    std::vector<Token> tokens;
    auto isLicense = [](const std::string &id) {
        return std::binary_search(std::begin(spdx::licenses), std::end(spdx::licenses), id,
                                  [](const std::string &a, const std::string &b) { return a < b; });
    };
    auto isException = [](const std::string &id) {
        return std::binary_search(std::begin(spdx::exceptions), std::end(spdx::exceptions), id,
                                  [](const std::string &a, const std::string &b) { return a < b; });
    };
    size_t i = 0;
    auto idstring = [&]() {
        size_t start = i;
        while (i < source.size() && (std::isalnum((unsigned char)source[i]) || source[i] == '-' || source[i] == '.'))
            ++i;
        return source.substr(start, i - start);
    };
    auto readWord = [&](const char *word) {
        size_t n = std::strlen(word);
        if (i + n > source.size())
            return false;
        for (size_t k = 0; k < n; ++k)
            if (std::toupper((unsigned char)source[i + k]) != word[k])
                return false;
        i += n;
        return true;
    };
    while (i < source.size()) {
        while (i < source.size() && source[i] == ' ')
            ++i;
        if (i >= source.size())
            break;
        if (readWord("WITH"))
            tokens.push_back({"OP", "WITH"});
        else if (readWord("AND"))
            tokens.push_back({"OP", "AND"});
        else if (readWord("OR"))
            tokens.push_back({"OP", "OR"});
        else if (source[i] == '(' || source[i] == ')' || source[i] == ':' || source[i] == '+') {
            if (source[i] == '+' && i > 0 && source[i - 1] == ' ')
                return false;
            tokens.push_back({"OP", std::string(1, source[i++])});
        } else if (source.compare(i, 12, "DocumentRef-") == 0) {
            i += 12;
            auto id = idstring();
            if (id.empty())
                return false;
            tokens.push_back({"DOCREF", id});
        } else if (source.compare(i, 11, "LicenseRef-") == 0) {
            i += 11;
            auto id = idstring();
            if (id.empty())
                return false;
            tokens.push_back({"LICREF", id});
        } else {
            size_t start = i;
            auto id = idstring();
            if (!id.empty() && isLicense(id))
                tokens.push_back({"LICENSE", id});
            else if (!id.empty() && isException(id))
                tokens.push_back({"EXCEPTION", id});
            else {
                i = start;
                return false;
            }
        }
    }
    size_t k = 0;
    auto op = [&](const char *s) {
        if (k < tokens.size() && tokens[k].type == "OP" && tokens[k].text == s)
            return ++k, true;
        return false;
    };
    std::function<bool()> expression;
    auto atom = [&]() -> bool {
        if (op("(")) {
            if (!expression() || !op(")"))
                throw std::runtime_error("paren");
            return true;
        }
        size_t begin = k;
        if (k < tokens.size() && tokens[k].type == "DOCREF") {
            ++k;
            if (!op(":"))
                throw std::runtime_error("docref");
        }
        if (k < tokens.size() && tokens[k].type == "LICREF")
            return ++k, true;
        k = begin;
        if (k < tokens.size() && tokens[k].type == "LICENSE") {
            ++k;
            op("+");
            if (op("WITH")) {
                if (k < tokens.size() && tokens[k].type == "EXCEPTION")
                    ++k;
                else
                    throw std::runtime_error("with");
            }
            return true;
        }
        return false;
    };
    std::function<bool(const char *, const std::function<bool()> &)> binary = [&](const char *name, const std::function<bool()> &next) {
        if (!next())
            return false;
        if (!op(name))
            return true;
        if (!binary(name, next))
            throw std::runtime_error("binary");
        return true;
    };
    std::function<bool()> conj = [&]() { return binary("AND", atom); };
    expression = [&]() { return binary("OR", conj); };
    try {
        return expression() && k == tokens.size();
    } catch (const std::runtime_error &) {
        return false;
    }
}
Result checkObject(const Json &value, const Options &options, std::string version) {
    Result result;
    if (version.empty())
        version = defaultVersion(value);
    auto fail = [&](const std::string &m) { result.errors.push_back(m); };
    auto warn = [&](const std::string &m) { result.warnings.push_back(m); };
    if (std::find(formats().begin(), formats().end(), version) == formats().end())
        throw std::invalid_argument("Unknown object format: " + version);
    if (checkCommon("object", value, version, fail))
        validateObjectManifest(value, version, options, fail, warn, result);
    result.valid = result.errors.empty();
    return result;
}
Result checkLibrary(const Json &value, const std::map<std::string, Json> *objects, const std::vector<File> *files,
                    std::string version) {
    Result result;
    if (version.empty())
        version = defaultVersion(value);
    auto fail = [&](const std::string &m) { result.errors.push_back(m); };
    if (checkCommon("library", value, version, fail)) {
        if (objects) {
            for (auto &idJson : value["objects"]) {
                auto id = idJson.get<std::string>();
                if (!objects->contains(id)) {
                    fail("objects: " + id + " has no objects/" + id + "/object.json");
                    continue;
                }
                auto &o = objects->at(id);
                if (o.value("id", Json()) != id)
                    fail("objects: objects/" + id + "/object.json has id " + (o.contains("id") && o["id"].is_string() ? o["id"].get<std::string>() : "undefined"));
                const Json fv = o.value("formatVersion", Json());
                if (!fv.is_string() || std::find(formats().begin(), formats().end(), fv.get<std::string>()) == formats().end())
                    fail("objects: " + id + " has no known object formatVersion (" + (fv.is_null() ? std::string("undefined") : fv.dump()) + ")");
                else if (!atLeast(value["formatVersion"].get<std::string>(), fv.get<std::string>()))
                    fail("objects: " + id + " uses object format " + fv.get<std::string>() + ", newer than the library's " +
                         value["formatVersion"].get<std::string>());
            }
            for (auto &[id, o] : *objects)
                if (std::find(value["objects"].begin(), value["objects"].end(), id) == value["objects"].end())
                    fail("objects: objects/" + id + " is not listed");
        }
        if (files) {
            uint64_t total = 0;
            for (auto &f : *files)
                total += f.size;
            if (files->size() > Limits::libraryFiles)
                fail("library: " + std::to_string(files->size()) + " files, more than " + std::to_string(Limits::libraryFiles));
            if (total > Limits::libraryBytes)
                fail("library: " + std::to_string(total) + " bytes, more than " + std::to_string(Limits::libraryBytes));
            for (auto required : {"library.json", "LICENSE", "ATTRIBUTION.md"})
                if (std::none_of(files->begin(), files->end(), [&](auto &f) { return f.path == required; }))
                    fail(std::string("library: missing ") + required);
            for (auto &f : *files)
                if (!f.path.starts_with("objects/") && !covered(value["attribution"], f.path))
                    fail("attribution: " + f.path + " is not covered");
        }
    }
    result.valid = result.errors.empty();
    return result;
}
} // namespace hyprune::world::objects
