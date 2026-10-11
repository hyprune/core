#include "Objects.hpp"
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
void loadObjects(World &w, const std::filesystem::path &root, const std::string &formatVersion, LoadOptions options,
                 size_t &memory) {
    const auto &j = w.manifest;
    const auto assets = j.value("objectAssets", Json::array()), objects = j.value("objects", Json::array());
    if (assets.empty() && objects.empty())
        return;
    require(format::atLeast(formatVersion, "0.8"), "world objects need world format 0.8");
    require(assets.size() <= 32 && objects.size() <= 256, "at most 32 object assets and 256 objects");
    std::map<std::string, unsigned> ids;
    for (const auto &entry : assets) {
        ObjectAsset a;
        a.id = entry.at("id").get<std::string>();
        const std::string label = "objectAssets " + a.id;
        require(ids.emplace(a.id, unsigned(w.objectAssets.size())).second, "duplicate object asset id " + a.id);
        // Reserved for shared object libraries (a later world format).
        require(!entry.contains("library"), label + ": library references are reserved for a later world format");
        require(entry.contains("scene"), label + ": scene required");
        a.unlit = entry.value("lighting", Json::object()).value("mode", "probe") == "unlit";
        std::string proxy = entry.contains("collision") ? entry["collision"].at("node").get<std::string>() : "";
        size_t firstMaterial = w.materials.size();
        // Same extension rules as scene.gltf for this format; never lightmapped.
        GltfScene scene;
        try {
            scene = loadGltf(w, root, entry.at("scene").get<std::string>(), formatVersion, options, Json::array(),
                             Json::array(), proxy, memory);
        } catch (const std::exception &e) {
            throw std::runtime_error(label + ": " + e.what());
        }
        require(!scene.meshes.empty(), label + ": no rendered meshes");
        a.meshes = std::move(scene.meshes);
        a.nodeParents = scene.parents;
        a.collision = std::move(scene.collision);
        if (a.unlit)
            for (size_t m = firstMaterial; m < w.materials.size(); ++m)
                w.materials[m].unlit = true;
        std::map<unsigned, int> targets;
        const auto animations = entry.value("animations", Json::array());
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
        for (const auto &mesh : a.meshes) {
            int i = a.animationOf(mesh.node);
            for (const auto &v : mesh.vertices)
                a.radius = std::max(a.radius, i < 0 ? length(v.position - a.center)
                                                    : animation::sweptDistance(a.animations[i], v.position, a.center));
        }
        w.objectAssets.push_back(std::move(a));
    }
    std::set<std::string> objectIds;
    for (const auto &entry : objects) {
        ObjectInstance o;
        o.id = entry.at("id").get<std::string>();
        require(objectIds.insert(o.id).second, "duplicate object id " + o.id);
        auto asset = ids.find(entry.at("asset").get<std::string>());
        require(asset != ids.end(), "objects " + o.id + ": unknown asset " + entry.at("asset").get<std::string>());
        o.asset = asset->second;
        auto position = vec(entry.at("pose").at("position"));
        auto q = entry.at("pose").at("rotation").get<std::array<float, 4>>();
        require(std::isfinite(q[0] + q[1] + q[2] + q[3]) &&
                    std::abs(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3] - 1) < .002f,
                "objects " + o.id + ": rotation must be normalized");
        o.scale = entry.value("scale", 1.f);
        o.speed = entry.value("speed", 1.f);
        o.phase = entry.value("phase", 0.f);
        require(o.scale >= .01f && o.scale <= 100 && o.speed >= 0 && o.speed <= 10 && o.phase >= 0 && o.phase < 1,
                "objects " + o.id + ": scale, speed or phase out of range");
        if (entry.contains("ambient")) {
            o.ambientUp = o.ambientDown = vec(entry["ambient"]);
            o.authoredAmbient = true;
        }
        o.model = instanceModel(position, q, o.scale);
        const auto &a = w.objectAssets[o.asset];
        o.center = o.model.point(a.center);
        o.radius = a.radius * o.scale;
        // The static collision proxy at the instance pose; it never moves.
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
void lightObjects(World &w) {
    if (w.objectInstances.empty())
        return;
    // An object shares the visibility of the static geometry around it: the areas
    // owning scene meshes its swept sphere overlaps. Bounds alone say nothing:
    // derived regions are camera cells that own no nodes and are visible from
    // nowhere, so an object placed by position in one was never drawn.
    std::vector<std::pair<Vec3, Vec3>> bounds(w.meshes.size(), {Vec3{1e30f, 1e30f, 1e30f}, Vec3{-1e30f, -1e30f, -1e30f}});
    for (size_t m = 0; m < w.meshes.size(); ++m)
        for (const auto &v : w.meshes[m].vertices) {
            auto &[lo, hi] = bounds[m];
            lo = {std::min(lo.x, v.position.x), std::min(lo.y, v.position.y), std::min(lo.z, v.position.z)};
            hi = {std::max(hi.x, v.position.x), std::max(hi.y, v.position.y), std::max(hi.z, v.position.z)};
        }
    for (auto &o : w.objectInstances) {
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
    // Static triangle -> (mesh, first vertex) as visualTriangles was built.
    std::vector<std::pair<unsigned, unsigned>> source;
    for (unsigned m = 0; m < w.meshes.size(); ++m)
        for (unsigned i = 0; i + 2 < w.meshes[m].vertices.size(); i += 3)
            source.push_back({m, i});
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
    for (auto &o : w.objectInstances) {
        if (w.objectAssets[o.asset].unlit)
            continue;
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
        if (!o.authoredAmbient) {
            o.ambientUp = upWeight > 0 ? up / upWeight : Vec3{.24f, .24f, .24f};
            o.ambientDown = downWeight > 0 ? down / downWeight : o.ambientUp;
        }
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
        o.sunVisibility = open / 5.f;
        o.sun = strength * o.sunVisibility;
    }
}
} // namespace hyprune::world
