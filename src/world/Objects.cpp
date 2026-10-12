#include "Objects.hpp"
#include "Catalog.hpp"
#include "Character.hpp"
#include "Paths.hpp"
#include "ipc/Protocol.hpp"
#include "Format.hpp"
#include "Gltf.hpp"
#include "Mounts.hpp"
#include <map>
#include <set>
namespace hyprune::world {
namespace {
void require(bool good, const std::string &why) {
    if (!good)
        throw std::runtime_error(why);
}
Vec3 vec(const Json &j) {
    require(j.is_array() && j.size() == 3, "expected vec3");
    Vec3 v{j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
    require(std::isfinite(v.x + v.y + v.z), "nonfinite vector");
    return v;
}
Vec3 mul(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
float luminance(Vec3 c) { return .2126f * c.x + .7152f * c.y + .0722f * c.z; }
} // namespace
Mat4 instanceModel(Vec3 p, const std::array<float, 4> &q, float scale) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    auto m = Mat4::identity();
    m.v[0] = (1 - 2 * (y * y + z * z)) * scale, m.v[1] = 2 * (x * y + w * z) * scale, m.v[2] = 2 * (x * z - w * y) * scale;
    m.v[4] = 2 * (x * y - w * z) * scale, m.v[5] = (1 - 2 * (x * x + z * z)) * scale, m.v[6] = 2 * (y * z + w * x) * scale;
    m.v[8] = 2 * (x * z + w * y) * scale, m.v[9] = 2 * (y * z - w * x) * scale, m.v[10] = (1 - 2 * (x * x + y * y)) * scale;
    m.v[12] = p.x, m.v[13] = p.y, m.v[14] = p.z;
    return m;
}
ObjectAsset buildObjectAsset(World &w, const std::filesystem::path &root, const std::string &id, const std::string &scenePath,
                             const std::string &formatVersion, LoadOptions options, const Json &animations,
                             const std::string &proxy, bool unlit, size_t &memory) {
    ObjectAsset a;
    a.id = id;
    const std::string label = "objectAssets " + a.id;
    a.unlit = unlit;
    size_t firstMaterial = w.materials.size();
    // Same extension rules as scene.gltf for this format; never lightmapped.
    GltfScene scene;
    try {
        scene = loadGltf(w, root, scenePath, formatVersion, options, Json::array(), Json::array(), proxy, memory);
    } catch (const std::exception &e) {
        throw std::runtime_error(label + ": " + e.what());
    }
    require(!scene.meshes.empty(), label + ": no rendered meshes");
    a.meshes = std::move(scene.meshes);
    a.nodeParents = scene.parents;
    a.nodeNames = scene.names;
    a.nodeOrigins = scene.origins;
    a.materialNames = scene.materialNames;
    a.firstMaterial = scene.materialBase;
    a.collision = std::move(scene.collision);
    if (a.unlit)
        for (size_t m = firstMaterial; m < w.materials.size(); ++m)
            w.materials[m].unlit = true;
    std::map<unsigned, int> targets;
    require(animations.size() <= 16, label + ": at most 16 animations");
    for (const auto &e : animations) {
        Animation an;
        an.name = e.at("node").get<std::string>();
        an.node = nodeNamed(scene.names, an.name, label);
        require(targets.emplace(an.node, int(a.animations.size())).second,
                label + ": two animations target node \"" + an.name + "\"");
        auto type = e.at("type").get<std::string>();
        require(type == "spin" || type == "orbit" || type == "bob", label + ": unknown animation type");
        an.type = type == "spin" ? Animation::Spin : type == "orbit" ? Animation::Orbit : Animation::Bob;
        auto axis = vec(e.at("axis"));
        require(std::abs(length(axis) - 1) <= .001f, label + ": animation \"" + an.name + "\" axis must be a unit vector");
        an.axis = normalized(axis);
        bool bob = an.type == Animation::Bob;
        require(bob ? e.contains("amplitude") && !e.contains("pivot") : e.contains("pivot") && !e.contains("amplitude"),
                label + ": " + type + " animation \"" + an.name + "\" " +
                    (bob ? "needs an amplitude and takes no pivot" : "needs a pivot and takes no amplitude"));
        if (bob) {
            an.amplitude = e.at("amplitude").get<float>();
            require(an.amplitude > 0 && an.amplitude <= 10, label + ": bob amplitude must be in (0, 10] m");
        } else
            an.pivot = vec(e.at("pivot"));
        an.period = e.at("period").get<float>();
        require(an.period >= .5f && an.period <= 3600, label + ": animation period must be 0.5-3600 s");
        an.phase = e.value("phase", 0.f);
        require(an.phase >= 0 && an.phase < 1, label + ": animation phase must be in [0, 1)");
        an.origin = scene.origins[an.node];
        require(std::isfinite(an.origin.x + an.origin.y + an.origin.z), label + ": nonfinite node transform");
        a.animations.push_back(std::move(an));
    }
    for (auto &[node, index] : targets)
        for (int p = a.nodeParents[node]; p >= 0; p = a.nodeParents[p])
            require(!targets.contains(unsigned(p)), label + ": animation \"" + a.animations[index].name +
                                                        "\" is inside animated node \"" + scene.names[p] + "\"");
    a.nodeAnimation.assign(a.nodeParents.size(), -1);
    for (size_t n = 0; n < a.nodeParents.size(); ++n)
        for (int i = int(n); i >= 0; i = a.nodeParents[i])
            if (auto it = targets.find(unsigned(i)); it != targets.end()) {
                a.nodeAnimation[n] = it->second;
                break;
            }
    if (!proxy.empty())
        require(a.animationOf(nodeNamed(scene.names, proxy, label)) < 0,
                label + ": collision node \"" + proxy + "\" is inside an animated subtree");
    // Swept bounds about the rest-pose box centre: every pose of every node.
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (const auto &mesh : a.meshes)
        for (const auto &v : mesh.vertices)
            lo = {std::min(lo.x, v.position.x), std::min(lo.y, v.position.y), std::min(lo.z, v.position.z)},
            hi = {std::max(hi.x, v.position.x), std::max(hi.y, v.position.y), std::max(hi.z, v.position.z)};
    a.center = (lo + hi) * .5f;
    a.low = lo, a.high = hi;
    for (const auto &mesh : a.meshes) {
        int i = a.animationOf(mesh.node);
        for (const auto &v : mesh.vertices)
            a.radius = std::max(a.radius, i < 0 ? length(v.position - a.center)
                                                : animation::sweptDistance(a.animations[i], v.position, a.center));
    }
    return a;
}
namespace {
// A skinned character asset (RFC-0010 spec, checked as hyprune-npc-check does): no static
// meshes; bounds are the rest pose's, the swept sphere that grown by 0.6 m.
ObjectAsset characterAsset(const std::filesystem::path &glb, const std::string &id, const Json &traits, size_t &memory) {
    const std::string label = "objectAssets " + id;
    ObjectAsset a;
    a.id = id;
    a.traits = traits;
    std::shared_ptr<Character> c;
    try {
        c = std::make_shared<Character>(loadCharacter(glb));
    } catch (const std::exception &e) {
        throw std::runtime_error(label + ": " + e.what());
    }
    for (auto &p : checkCharacter(*c))
        require(p.level != CharacterProblem::Error, label + ": " + p.message);
    for (auto &image : c->images)
        memory += image.rgba.size() * 4 / 3;
    for (auto &prim : c->primitives)
        memory += prim.vertices.size() * sizeof(Character::Vertex) + prim.indices.size() * 4;
    a.low = c->lo, a.high = c->hi;
    Vec3 grown{.6f, .6f, .6f};
    a.center = (c->lo + c->hi) * .5f;
    a.radius = length((c->hi + grown) - (c->lo - grown)) * .5f;
    a.character = std::move(c);
    return a;
}
// A validated object directory (object.json + scene) as an asset of w: package dirs,
// installed library objects and catalog objects share it.
ObjectAsset manifestAsset(World &w, const std::filesystem::path &dir, const std::string &id, const Json &manifest,
                          LoadOptions options, size_t &memory) {
    const auto traits = manifest.value("traits", Json::object());
    if (traits.contains("character")) {
        auto a = characterAsset(safePath(dir, manifest.at("scene").get<std::string>()), id, traits, memory);
        a.manifest = manifest;
        a.directory = dir;
        return a;
    }
    const Json animations = traits.contains("decorative") ? traits["decorative"].value("animations", Json::array()) : Json::array();
    std::string proxy = traits.contains("collidable") ? traits["collidable"].value("node", std::string()) : "";
    bool unlit = manifest.value("lighting", Json::object()).value("mode", "probe") == "unlit";
    // Object formats (0.9 on) take the glTF extension rules of the newest world format
    // they extend: world 0.8's (KTX2, meshopt and quantisation included).
    auto asset = buildObjectAsset(w, dir, id, manifest.at("scene").get<std::string>(), "0.8", options, animations, proxy, unlit,
                                  memory);
    // RFC-0011: core recomputes the rest bounds; more than 1 cm off on any face rejects the object.
    if (manifest.contains("bounds")) {
        auto &b = manifest["bounds"];
        Vec3 lo = vec(b.at("min")), hi = vec(b.at("max"));
        float off = std::max({std::abs(lo.x - asset.low.x), std::abs(lo.y - asset.low.y), std::abs(lo.z - asset.low.z),
                              std::abs(hi.x - asset.high.x), std::abs(hi.y - asset.high.y), std::abs(hi.z - asset.high.z)});
        require(off <= .0105f, "bounds: the decoded scene differs from object.json bounds by " + std::to_string(off) + " m");
    }
    asset.traits = traits;
    asset.manifest = manifest;
    asset.directory = dir;
    return asset;
}
// Installed library object directory for "<id>@<version>" / object, else empty.
std::filesystem::path libraryObject(const LoadOptions &options, const std::string &library, const std::string &object) {
    for (auto &root : options.libraryRoots) {
        auto dir = root / library / "objects" / object;
        std::error_code ec;
        if (std::filesystem::is_regular_file(dir / "object.json", ec))
            return dir;
    }
    return {};
}
} // namespace
void loadObjects(World &w, const std::filesystem::path &root, const std::string &formatVersion, LoadOptions options,
                 size_t &memory) {
    const auto &j = w.manifest;
    const auto assets = j.value("objectAssets", Json::array()), objects = j.value("objects", Json::array());
    bool v09 = format::atLeast(formatVersion, "0.9");
    if (assets.empty() && objects.empty()) {
        loadPaths(w, formatVersion);
        return;
    }
    require(format::atLeast(formatVersion, "0.8"), "world objects need world format 0.8");
    require(assets.size() <= 32 && objects.size() <= 256, "at most 32 object assets and 256 objects");
    std::map<std::string, unsigned> ids;
    for (const auto &entry : assets) {
        auto id = entry.at("id").get<std::string>();
        const std::string label = "objectAssets " + id;
        require(ids.emplace(id, unsigned(w.objectAssets.size())).second, "duplicate object asset id " + id);
        if (entry.contains("library") || entry.contains("dir")) {
            // World 0.9: a package object directory or an installed library object, validated
            // with the shared object rules (as the catalog does).
            require(v09, label + (entry.contains("library") ? ": library references need world format 0.9"
                                                            : ": object directories need world format 0.9"));
            std::filesystem::path dir;
            std::string ref, source;
            if (entry.contains("dir")) {
                dir = safePath(root, entry["dir"].get<std::string>() + "/object.json").parent_path();
                ref = "package/" + id, source = "package";
            } else {
                auto library = entry.at("library").get<std::string>(), object = entry.at("object").get<std::string>();
                dir = libraryObject(options, library, object);
                if (dir.empty())
                    throw std::runtime_error("world.library.missing: " + library + " (" + object + ") is not installed");
                ref = "library/" + library + "/" + object, source = "library";
            }
            auto e = scanObject(dir, ref, source);
            require(e.state == "ready", label + ": " + (e.errors.empty() ? std::string("invalid object") : e.errors[0]));
            auto objectFormat = e.manifest.value("formatVersion", std::string());
            require(ipc::protocol::atLeast(formatVersion, objectFormat),
                    label + ": object format " + objectFormat + " is newer than the world's " + formatVersion);
            auto asset = manifestAsset(w, dir, id, e.manifest, options, memory);
            asset.ref = ref;
            w.objectAssets.push_back(std::move(asset));
            continue;
        }
        require(entry.contains("scene"), label + ": scene required");
        // Inline assets: the 0.8 shorthand (animations, collision) or 0.9 traits, not both.
        auto traits = entry.value("traits", Json::object());
        require(v09 || !entry.contains("traits"), label + ": traits need world format 0.9");
        require(!(entry.contains("animations") && traits.contains("decorative")),
                label + ": animations and traits.decorative are two forms of one thing; give one");
        require(!(entry.contains("collision") && traits.contains("collidable")),
                label + ": collision and traits.collidable are two forms of one thing; give one");
        for (auto &[name, value] : traits.items())
            require(name == "decorative" || name == "collidable" || name == "movable" || name == "character" || name == "seat",
                    label + ": trait " + name + " is not part of world format " + formatVersion);
        require(!traits.contains("character") || traits.size() == 1, label + ": a character combines with no other trait");
        auto scenePath = entry.at("scene").get<std::string>();
        if (traits.contains("character")) {
            auto asset = characterAsset(safePath(root, scenePath), id, traits, memory);
            asset.ref = "package/" + id;
            w.objectAssets.push_back(std::move(asset));
            continue;
        }
        Json animations = entry.contains("animations") ? entry["animations"]
                          : traits.contains("decorative") ? traits["decorative"].value("animations", Json::array())
                                                          : Json::array();
        std::string proxy = entry.contains("collision") ? entry["collision"].at("node").get<std::string>()
                            : traits.contains("collidable") ? traits["collidable"].value("node", std::string())
                                                            : "";
        auto asset = buildObjectAsset(w, root, id, scenePath, formatVersion, options, animations, proxy,
                                      entry.value("lighting", Json::object()).value("mode", "probe") == "unlit", memory);
        if (entry.contains("animations"))
            traits["decorative"] = {{"animations", entry["animations"]}};
        if (entry.contains("collision"))
            traits["collidable"] = entry["collision"];
        asset.traits = traits;
        asset.ref = "package/" + id;
        w.objectAssets.push_back(std::move(asset));
    }
    // World 0.9 paths, seats and keepClear, and the instance fields that refer to them.
    loadPaths(w, formatVersion);
    std::set<std::string> objectIds;
    for (const auto &entry : objects) {
        ObjectInstance o;
        o.id = entry.at("id").get<std::string>();
        require(objectIds.insert(o.id).second, "duplicate object id " + o.id);
        auto asset = ids.find(entry.at("asset").get<std::string>());
        require(asset != ids.end(), "objects " + o.id + ": unknown asset " + entry.at("asset").get<std::string>());
        o.asset = asset->second;
        const auto &a = w.objectAssets[o.asset];
        o.character = a.character != nullptr;
        o.scale = entry.value("scale", 1.f);
        o.speed = entry.value("speed", 1.f);
        o.phase = entry.value("phase", 0.f);
        require(o.scale >= .01f && o.scale <= 100 && o.speed >= 0 && o.speed <= 10 && o.phase >= 0 && o.phase < 1,
                "objects " + o.id + ": scale, speed or phase out of range");
        Vec3 position{};
        std::array<float, 4> q{0, 0, 0, 1};
        if (entry.contains("path")) {
            // Bound: starts at phase along its route, facing along it.
            o.bound = true;
            for (auto &p : w.paths)
                if (p["id"] == entry["path"]) {
                    auto route = routeOf(p);
                    float s = route.wrap(o.phase * route.length());
                    position = route.at(s);
                    Vec3 t = route.tangent(s) * (entry.value("reverse", false) ? -1.f : 1.f);
                    float yaw = std::atan2(t.x, t.z);
                    q = {0, std::sin(yaw * .5f), 0, std::cos(yaw * .5f)};
                }
        } else {
            position = vec(entry.at("pose").at("position"));
            q = entry.at("pose").at("rotation").get<std::array<float, 4>>();
        }
        require(std::isfinite(q[0] + q[1] + q[2] + q[3]) &&
                    std::abs(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3] - 1) < .002f,
                "objects " + o.id + ": rotation must be normalized");
        if (entry.contains("ambient")) {
            o.ambientUp = o.ambientDown = vec(entry["ambient"]);
            o.authoredAmbient = true;
        }
        o.position = position, o.rotation = q, o.authoredScale = o.scale;
        o.model = instanceModel(position, q, o.scale);
        o.center = o.model.point(a.center);
        o.radius = a.radius * o.scale;
        // The static collision proxy at the instance pose (only an editor overlay moves it).
        o.collisionFirst = w.collision.size(), o.collisionCount = a.collision.size();
        for (const auto &t : a.collision) {
            w.collision.push_back({o.model.point(t.a), o.model.point(t.b), o.model.point(t.c)});
            w.collisionNodes.push_back(objectCollisionNode);
        }
        w.objectInstances.push_back(std::move(o));
    }
}
namespace {
// Möller-Trumbore against the static visual triangles: nearest hit with its
// triangle index and barycentrics (u for b, v for c).
struct Hit {
    float distance;
    unsigned triangle;
    float u, v;
};
std::optional<Hit> cast(const World &w, Vec3 o, Vec3 d, float maxDistance) {
    std::optional<Hit> best;
    auto test = [&](unsigned id) {
        const auto &t = w.visualTriangles[id];
        auto e = t.b - t.a, f = t.c - t.a, p = cross(d, f);
        float det = dot(e, p);
        if (std::abs(det) < 1e-9f)
            return;
        auto s = o - t.a;
        float u = dot(s, p) / det;
        auto q = cross(s, e);
        float v = dot(d, q) / det, distance = dot(f, q) / det;
        if (u < 0 || v < 0 || u + v > 1 || distance <= 1e-3f || distance > maxDistance || (best && distance >= best->distance))
            return;
        best = Hit{distance, id, u, v};
    };
    if (w.visualIndex.size() == w.visualTriangles.size())
        for (auto id : w.visualIndex.ray(o, d))
            test(id);
    else
        for (unsigned id = 0; id < w.visualTriangles.size(); ++id)
            test(id);
    return best;
}
Vec3 decodeSrgb(Vec3 c) {
    auto f = [](float x) { return x <= .04045f ? x / 12.92f : std::pow((x + .055f) / 1.055f, 2.4f); };
    return {f(c.x), f(c.y), f(c.z)};
}
} // namespace
void prepareLighting(World &w) {
    if (w.meshBounds.size() != w.meshes.size()) {
        w.meshBounds.assign(w.meshes.size(), {Vec3{1e30f, 1e30f, 1e30f}, Vec3{-1e30f, -1e30f, -1e30f}});
        for (size_t m = 0; m < w.meshes.size(); ++m)
            for (const auto &v : w.meshes[m].vertices) {
                auto &[lo, hi] = w.meshBounds[m];
                lo = {std::min(lo.x, v.position.x), std::min(lo.y, v.position.y), std::min(lo.z, v.position.z)};
                hi = {std::max(hi.x, v.position.x), std::max(hi.y, v.position.y), std::max(hi.z, v.position.z)};
            }
    }
    // Static triangle -> (mesh, first vertex) as visualTriangles was built.
    if (w.visualSource.size() != w.visualTriangles.size()) {
        w.visualSource.clear();
        for (unsigned m = 0; m < w.meshes.size(); ++m)
            for (unsigned i = 0; i + 2 < w.meshes[m].vertices.size(); i += 3)
                w.visualSource.push_back({m, i});
    }
}
void objectRegions(World &w, std::optional<size_t> only) {
    if (w.objectInstances.empty() || (only && *only >= w.objectInstances.size()))
        return;
    prepareLighting(w);
    auto skip = [&](const ObjectInstance &o) { return only && &o != &w.objectInstances[*only]; };
    // An object shares the visibility of the static geometry around it: the areas
    // owning scene meshes its swept sphere overlaps. Bounds alone say nothing:
    // derived regions are camera cells that own no nodes and are visible from
    // nowhere, so an object placed by position in one was never drawn.
    const auto &bounds = w.meshBounds;
    for (auto &o : w.objectInstances) {
        if (skip(o))
            continue;
        std::set<int> owners;
        for (size_t m = 0; m < w.meshes.size(); ++m) {
            unsigned node = w.meshes[m].node;
            int owner = node < w.nodeVisibility.size() ? w.nodeVisibility[node] : -1;
            if (owner < 0 || owners.contains(owner))
                continue;
            auto &[lo, hi] = bounds[m];
            float dx = std::max({lo.x - o.center.x, 0.f, o.center.x - hi.x}), dy = std::max({lo.y - o.center.y, 0.f, o.center.y - hi.y}),
                  dz = std::max({lo.z - o.center.z, 0.f, o.center.z - hi.z});
            if (dx * dx + dy * dy + dz * dz <= o.radius * o.radius)
                owners.insert(owner);
        }
        o.regions.assign(owners.begin(), owners.end());
    }
}
ProbeLight probeLight(const World &w, const ObjectInstance &o) {
    const auto &source = w.visualSource; // prepareLighting built it
    const bool mapped = source.size() == w.visualTriangles.size();
    auto profile = w.atmosphere.is_object() ? w.atmosphere : defaultAtmosphere();
    auto colour = [](const Json &c) { return Vec3{c[0].get<float>(), c[1].get<float>(), c[2].get<float>()}; };
    const auto sky = profile.at("sky");
    const Vec3 zenith = colour(sky.at("zenith")), horizon = colour(sky.at("horizon")), ground = colour(sky.at("ground"));
    const Vec3 sun = normalized(colour(sky.at("sun").at("direction")));
    // Average base colour of an image (sRGB-decoded), for bounce albedo.
    std::map<int, Vec3> averages;
    auto average = [&](int image) -> Vec3 {
        if (image < 0 || size_t(image) >= w.images.size() || w.images[image].rgba.empty())
            return {1, 1, 1};
        if (auto it = averages.find(image); it != averages.end())
            return it->second;
        const auto &im = w.images[image];
        Vec3 sum{};
        size_t n = 0, pixels = im.rgba.size() / 4, step = std::max<size_t>(1, pixels / 4096);
        for (size_t i = 0; i < pixels; i += step, ++n)
            sum += decodeSrgb(Vec3{float(im.rgba[i * 4]), float(im.rgba[i * 4 + 1]), float(im.rgba[i * 4 + 2])} / 255.f);
        return averages[image] = n ? sum / float(n) : Vec3{1, 1, 1};
    };
    // Baked irradiance (lightmap texel x intensity) at a static hit, if lightmapped.
    auto baked = [&](const Hit &h, const Material &m, const Mesh &mesh, unsigned first) -> std::optional<Vec3> {
        if (m.lightmap < 0 || size_t(m.lightmap) >= w.images.size() || w.images[m.lightmap].rgba.empty())
            return std::nullopt;
        const auto &a = mesh.vertices[first], &b = mesh.vertices[first + 1], &c = mesh.vertices[first + 2];
        float s = a.uv1[0] + (b.uv1[0] - a.uv1[0]) * h.u + (c.uv1[0] - a.uv1[0]) * h.v;
        float t = a.uv1[1] + (b.uv1[1] - a.uv1[1]) * h.u + (c.uv1[1] - a.uv1[1]) * h.v;
        const auto &im = w.images[m.lightmap];
        int x = std::clamp(int(s * im.width), 0, im.width - 1), y = std::clamp(int(t * im.height), 0, im.height - 1);
        const auto *px = &im.rgba[(size_t(y) * im.width + x) * 4];
        Vec3 e = Vec3{float(px[0]), float(px[1]), float(px[2])} / 255.f;
        return (m.lightmapSrgb ? decodeSrgb(e) : e) * m.intensity;
    };
    constexpr int rays = 64;
    Vec3 up{}, down{};
    float upWeight = 0, downWeight = 0;
    // Sun calibration from baked surfaces the probe sees: sunlit E ≈ A + D·cos, shaded E ≈ A.
    float litSum = 0, litCos = 0, shadeSum = 0;
    int lit = 0, shade = 0;
    for (int i = 0; i < rays; ++i) {
        // Fibonacci sphere: even directions, deterministic.
        float y = 1 - 2 * (i + .5f) / rays, r = std::sqrt(std::max(0.f, 1 - y * y)), phi = 2.39996323f * i;
        Vec3 d{r * std::cos(phi), y, r * std::sin(phi)};
        Vec3 radiance;
        auto hit = mapped ? cast(w, o.center, d, 100) : std::nullopt;
        if (!hit)
            radiance = y >= 0 ? horizon + (zenith - horizon) * y : horizon + (ground - horizon) * -y;
        else {
            auto [m, first] = source[hit->triangle];
            const auto &mesh = w.meshes[m];
            const auto &mat = w.materials[mesh.material];
            Vec3 albedo = mat.color;
            if (mat.slots[0].texture >= 0)
                albedo = mul(albedo, average(mat.slots[0].texture));
            const auto &tri = w.visualTriangles[hit->triangle];
            Vec3 n = normalized(cross(tri.b - tri.a, tri.c - tri.a));
            if (dot(n, d) > 0)
                n = n * -1;
            if (mat.unlit)
                radiance = albedo;
            else if (auto e = baked(*hit, mat, mesh, first)) {
                radiance = mul(albedo, *e);
                // Is this baked texel in sunlight? (shadow ray toward the sun)
                float c = dot(n, sun);
                Vec3 point = o.center + d * hit->distance + n * .02f;
                bool open = c > 0 && !cast(w, point, sun, 200);
                if (open && c > .3f)
                    litSum += luminance(*e), litCos += c, ++lit;
                else if (!open)
                    shadeSum += luminance(*e), ++shade;
            } else
                radiance = albedo * (.24f + .76f * std::max(dot(n, sun), 0.f)); // world shader, unbaked
            radiance += mat.emissive;
        }
        if (y > 0)
            up += radiance * y, upWeight += y;
        else
            down += radiance * -y, downWeight += -y;
    }
    ProbeLight result;
    result.ambientUp = upWeight > 0 ? up / upWeight : Vec3{.24f, .24f, .24f};
    result.ambientDown = downWeight > 0 ? down / downWeight : result.ambientUp;
    // Direct sun: visibility from a few points of the instance's bounds,
    // strength calibrated to the baked sunlight nearby (else the unbaked
    // world shader's 0.76).
    // A bake whose shaded texels are as bright as its sunlit ones carries no
    // usable sun signal: then assume the unbaked convention (76% direct).
    float strength = .76f;
    if (lit >= 4 && shade >= 4 && litSum / lit - shadeSum / shade > .05f * litSum / lit)
        strength = (litSum / lit - shadeSum / shade) / (litCos / lit);
    else if (lit >= 4)
        strength = .76f * (litSum / lit) / (litCos / lit);
    strength = std::clamp(strength, 0.f, 16.f);
    int open = 0;
    float spread = std::min(o.radius * .5f, 1.f);
    const Vec3 offsets[5]{{0, 0, 0}, {spread, 0, 0}, {-spread, 0, 0}, {0, 0, spread}, {0, 0, -spread}};
    for (auto offset : offsets)
        open += mapped ? !cast(w, o.center + offset, sun, 200) : 1;
    result.sunVisibility = open / 5.f;
    result.sun = strength * result.sunVisibility;
    return result;
}
void applyProbeLight(ObjectInstance &o, const ProbeLight &l) {
    if (!o.authoredAmbient)
        o.ambientUp = l.ambientUp, o.ambientDown = l.ambientDown;
    o.sun = l.sun, o.sunVisibility = l.sunVisibility;
}
void lightObjects(World &w, std::optional<size_t> only) {
    if (w.objectInstances.empty() || (only && *only >= w.objectInstances.size()))
        return;
    objectRegions(w, only);
    for (size_t k = 0; k < w.objectInstances.size(); ++k)
        if ((!only || k == *only) && !w.objectAssets[w.objectInstances[k].asset].unlit)
            applyProbeLight(w.objectInstances[k], probeLight(w, w.objectInstances[k]));
}
std::shared_ptr<World> loadObjectAsset(const std::filesystem::path &dir, const Json &manifest, LoadOptions options) {
    auto w = std::make_shared<World>();
    size_t memory = 0;
    w->objectAssets.push_back(manifestAsset(*w, dir, manifest.at("id").get<std::string>(), manifest, options, memory));
    return w;
}
unsigned mergeObjectAsset(World &into, World &scratch) {
    require(scratch.objectAssets.size() == 1, "one object asset per scratch world");
    const size_t materialBase = into.materials.size(), imageBase = into.images.size();
    // Samplers parallel the images (an image without one uses the default sampler).
    into.samplers.resize(imageBase);
    for (auto m : scratch.materials) {
        for (auto &slot : m.slots)
            if (slot.texture >= 0)
                slot.texture += int(imageBase);
        if (m.baseTexture >= 0)
            m.baseTexture += int(imageBase);
        if (m.lightmap >= 0)
            m.lightmap += int(imageBase);
        into.materials.push_back(m);
    }
    scratch.samplers.resize(scratch.images.size());
    for (size_t i = 0; i < scratch.images.size(); ++i) {
        into.images.push_back(std::move(scratch.images[i]));
        into.samplers.push_back(scratch.samplers[i]);
    }
    auto asset = std::move(scratch.objectAssets[0]);
    for (auto &mesh : asset.meshes)
        mesh.material += unsigned(materialBase);
    asset.firstMaterial += materialBase;
    into.objectAssets.push_back(std::move(asset));
    return unsigned(into.objectAssets.size() - 1);
}
} // namespace hyprune::world
