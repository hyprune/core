#include "World.hpp"
#include "Schema.hpp"
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <fstream>
#include <regex>
#include <set>
#include <stb_image.h>
namespace hyprune::world {
namespace {
void require(bool good, const std::string &why) {
    if (!good)
        throw std::runtime_error(why);
}
Vec3 vec(const Json &j) {
    require(j.is_array() && j.size() == 3, "expected vec3");
    Vec3 v{j[0], j[1], j[2]};
    require(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z), "nonfinite vector");
    return v;
}
void checkFile(const std::filesystem::path &p) {
    require(std::filesystem::file_size(p) <= 128 * 1024 * 1024, "asset exceeds 128 MiB");
}
} // namespace
std::filesystem::path safePath(const std::filesystem::path &root, const std::string &path) {
    require(!path.empty() && path.front() != '/' && path.find_first_of("\\:%?#") == std::string::npos,
            "unsafe package path");
    std::filesystem::path out = root;
    size_t start = 0;
    while (start <= path.size()) {
        auto end = path.find('/', start);
        auto part = path.substr(start, end - start);
        require(!part.empty() && part != "." && part != "..", "unsafe path component");
        out /= part;
        require(!std::filesystem::is_symlink(out), "symlink in package");
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    require(std::filesystem::is_regular_file(out), "missing package asset: " + path);
    checkFile(out);
    return out;
}
const Spawn &World::spawn(const std::string &id) const {
    for (auto &s : spawns)
        if (s.id == (id.empty() ? defaultSpawn : id))
            return s;
    throw std::runtime_error("unknown spawn");
}
std::shared_ptr<World> load(const std::filesystem::path &root) {
    require(!std::filesystem::is_symlink(root), "package root is a symlink");
    auto manifestPath = safePath(root, "world.json");
    require(std::filesystem::file_size(manifestPath) <= 1024 * 1024, "manifest too large");
    std::ifstream in(manifestPath);
    std::vector<std::set<std::string>> objectKeys;
    Json j = Json::parse(in, [&](int depth, Json::parse_event_t event, Json &value) {
        require(depth <= 64, "manifest nesting limit");
        if (event == Json::parse_event_t::object_start)
            objectKeys.emplace_back();
        if (event == Json::parse_event_t::key)
            require(objectKeys.back().insert(value.get<std::string>()).second, "duplicate manifest key");
        if (event == Json::parse_event_t::object_end)
            objectKeys.pop_back();
        return true;
    });
    validateManifest(j);
    require(j.at("formatVersion") == "0.1", "unsupported world format");
    require(std::regex_match(j.at("id").get<std::string>(), std::regex("[a-z][a-z0-9-]*(\\.[a-z][a-z0-9-]*)+")),
            "invalid world ID");
    for (auto &field : {"version", "name", "license", "scene", "defaultSpawn"})
        require(j.at(field).is_string() && !j.at(field).get<std::string>().empty(), "missing manifest field");
    require(j.at("attribution").is_array() && !j["attribution"].empty(), "attribution required");
    bool covered = false;
    for (auto &a : j["attribution"]) {
        require(!a.at("author").get<std::string>().empty() && !a.at("license").get<std::string>().empty(),
                "invalid attribution");
        if (a.at("path") == ".")
            covered = true;
        else
            safePath(root, a.at("path"));
    }
    require(covered, "M0 requires baseline attribution for all assets");
    require(j.value("requiredExtensions", Json::array()).empty(), "unsupported required world extension");
    auto result = std::make_shared<World>();
    result->manifest = j;
    result->id = j["id"];
    result->name = j["name"];
    result->defaultSpawn = j["defaultSpawn"];
    std::set<std::string> spawnIds;
    for (auto &s : j.at("spawns")) {
        Spawn sp;
        sp.id = s.at("id");
        require(spawnIds.insert(sp.id).second, "duplicate spawn");
        sp.position = vec(s.at("pose").at("position"));
        auto q = s.at("pose").at("rotation");
        require(q.is_array() && q.size() == 4, "invalid rotation");
        float x = q[0], y = q[1], z = q[2], w = q[3];
        require(std::isfinite(x + y + z + w) && std::abs(x * x + y * y + z * z + w * w - 1) < .002f,
                "rotation must be normalized");
        Vec3 f{-2 * (x * z + w * y), 2 * (w * x - y * z), -(1 - 2 * (x * x + y * y))};
        sp.yaw = std::atan2(f.x, -f.z);
        sp.pitch = std::asin(std::clamp(f.y, -1.f, 1.f));
        result->spawns.push_back(sp);
    }
    result->spawn();
    auto scene = safePath(root, j["scene"]);
    cgltf_options opts{};
    cgltf_data *raw = nullptr;
    require(cgltf_parse_file(&opts, scene.c_str(), &raw) == cgltf_result_success, "glTF parse failed");
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, cgltf_free);
    require(raw->nodes_count < 10000 && raw->meshes_count < 10000, "scene too complex");
    for (size_t i = 0; i < raw->extensions_required_count; i++)
        require((std::string(raw->extensions_required[i]) == "KHR_materials_unlit" ||
                 std::string(raw->extensions_required[i]) == "KHR_texture_transform"),
                "unsupported required glTF extension");
    auto relative = std::filesystem::relative(scene.parent_path(), root);
    auto asset = [&](const char *uri) {
        std::string p = relative == "." ? uri : (relative / std::string(uri)).generic_string();
        return safePath(root, p);
    };
    size_t bufferBytes = 0;
    for (size_t i = 0; i < raw->buffers_count; i++) {
        require(raw->buffers[i].size <= 128 * 1024 * 1024, "buffer exceeds per-file limit");
        bufferBytes += raw->buffers[i].size;
        require(bufferBytes <= 512 * 1024 * 1024, "CPU buffer budget exceeded");
        if (raw->buffers[i].uri)
            asset(raw->buffers[i].uri);
    }
    // Validate parent chains before cgltf_node_transform_world follows them.
    for (size_t i = 0; i < raw->nodes_count; i++) {
        std::set<cgltf_node *> seen;
        for (auto *n = &raw->nodes[i]; n; n = n->parent)
            require(seen.insert(n).second, "node cycle");
    }
    std::vector<int> parents(raw->nodes_count, 0);
    for (size_t i = 0; i < raw->nodes_count; i++)
        for (size_t c = 0; c < raw->nodes[i].children_count; c++)
            require(++parents[raw->nodes[i].children[c] - raw->nodes] <= 1, "multiple node parents");
    require(cgltf_load_buffers(&opts, raw, scene.c_str()) == cgltf_result_success, "glTF buffers failed");
    require(cgltf_validate(raw) == cgltf_result_success, "invalid glTF");
    size_t memory = 0;
    for (size_t i = 0; i < raw->textures_count; i++) {
        Sampler sampler;
        if (auto *s = raw->textures[i].sampler) {
            sampler = {int(s->wrap_s), int(s->wrap_t), s->min_filter ? int(s->min_filter) : 9987,
                       s->mag_filter ? int(s->mag_filter) : 9729};
        }
        result->samplers.push_back(sampler);
        auto *img = raw->textures[i].image;
        require(img, "missing texture image");
        bool irradiance = false;
        for (auto &l : j.value("lightmaps", Json::array()))
            if (l.at("texture").get<size_t>() == i)
                irradiance = true;
        if (irradiance) {
            static const unsigned char png[8] = {137, 80, 78, 71, 13, 10, 26, 10};
            if (img->uri) {
                auto path = asset(img->uri);
                std::ifstream input(path, std::ios::binary);
                unsigned char magic[8]{};
                input.read(reinterpret_cast<char *>(magic), 8);
                require(input.gcount() == 8 && std::equal(magic, magic + 8, png), "lightmap must be PNG");
            } else {
                require(img->buffer_view && img->buffer_view->size >= 8, "missing PNG lightmap");
                auto *bytes = (const unsigned char *)img->buffer_view->buffer->data + img->buffer_view->offset;
                require(std::equal(bytes, bytes + 8, png), "lightmap must be PNG");
            }
        }
        int w = 0, h = 0, n = 0;
        unsigned char *pixels = nullptr;
        if (img->uri) {
            auto p = asset(img->uri);
            require(stbi_info(p.c_str(), &w, &h, &n), "invalid image");
            require(w <= 8192 && h <= 8192, "image dimensions exceed budget");
            memory += size_t(w) * h * 4;
            require(memory <= 512 * 1024 * 1024, "world GPU budget exceeded");
            pixels = stbi_load(p.c_str(), &w, &h, &n, 4);
        } else {
            require(img->buffer_view, "missing image data");
            auto *b = img->buffer_view;
            const auto *bytes = (const stbi_uc *)b->buffer->data + b->offset;
            require(stbi_info_from_memory(bytes, b->size, &w, &h, &n), "invalid image");
            require(w <= 8192 && h <= 8192, "image dimensions exceed budget");
            memory += size_t(w) * h * 4;
            require(memory <= 512 * 1024 * 1024, "world GPU budget exceeded");
            pixels = stbi_load_from_memory(bytes, b->size, &w, &h, &n, 4);
        }
        require(pixels, "image decode failed");
        Image image{w, h, {pixels, pixels + size_t(w) * h * 4}};
        stbi_image_free(pixels);
        result->images.push_back(std::move(image));
    }
    auto slot = [&](const cgltf_texture_view &view) {
        TextureSlot out;
        if (!view.texture) return out;
        out.texture = view.texture - raw->textures;
        out.texCoord = view.texcoord;
        if (view.has_transform) {
            const auto &t = view.transform;
            std::copy_n(t.offset, 2, out.offset);
            std::copy_n(t.scale, 2, out.scale);
            out.rotation = t.rotation;
            if (t.has_texcoord) out.texCoord = t.texcoord;
        }
        require(out.texCoord == 0 || out.texCoord == 1, "material requires unsupported UV set (only UV0/UV1)");
        require(std::isfinite(out.offset[0]+out.offset[1]+out.scale[0]+out.scale[1]+out.rotation), "nonfinite UV transform");
        return out;
    };
    for (size_t i = 0; i < raw->materials_count; i++) {
        auto &m = raw->materials[i];
        Material out;
        if (m.has_pbr_metallic_roughness) {
            auto &p = m.pbr_metallic_roughness;
            out.color = {p.base_color_factor[0], p.base_color_factor[1], p.base_color_factor[2]};
            out.alpha = p.base_color_factor[3];
            out.slots[0] = slot(p.base_color_texture);
            out.slots[2] = slot(p.metallic_roughness_texture);
            out.metallic = p.metallic_factor;
            out.roughness = p.roughness_factor;
            if (p.base_color_texture.texture)
                out.baseTexture = p.base_color_texture.texture - raw->textures;
        }
        out.emissive = {m.emissive_factor[0], m.emissive_factor[1], m.emissive_factor[2]};
        out.unlit = m.unlit;
        out.slots[1] = slot(m.normal_texture);
        out.slots[3] = slot(m.occlusion_texture);
        out.slots[4] = slot(m.emissive_texture);
        out.normalScale = m.normal_texture.scale;
        out.occlusionStrength = m.occlusion_texture.scale;
        result->materials.push_back(out);
    }
    result->materials.push_back(Material{});
    std::set<unsigned> baked;
    for (auto &l : j.value("lightmaps", Json::array())) {
        unsigned m = l.at("material"), t = l.at("texture");
        require(m < raw->materials_count && t < raw->textures_count, "invalid lightmap binding");
        require(baked.insert(m).second && l.at("texCoord") == 1, "invalid lightmap UV contract");
        auto &mat = result->materials[m];
        mat.lightmap = t;
        mat.intensity = l.at("intensity");
        auto encoding = l.at("encoding").get<std::string>();
        require(encoding == "linear" || encoding == "srgb", "unknown lightmap encoding");
        mat.lightmapSrgb = encoding == "srgb";
        require(mat.intensity >= 0 && std::isfinite(mat.intensity), "invalid lightmap intensity");
    }
    std::set<unsigned> collisionNodes;
    for (auto &c : j.value("collision", Json::array())) {
        unsigned node = c.at("node");
        require(node < raw->nodes_count && c.at("shape") == "mesh" && c.at("layer") == "static",
                "M0 supports static mesh collision only");
        require(raw->nodes[node].mesh, "collision node has no mesh");
        collisionNodes.insert(node);
    }
    std::set<cgltf_node *> visible;
    std::function<void(cgltf_node *)> visit = [&](cgltf_node *node) {
        if (!visible.insert(node).second)
            return;
        for (size_t i = 0; i < node->children_count; i++)
            visit(node->children[i]);
    };
    auto *sceneGraph = raw->scene ? raw->scene : (raw->scenes_count ? raw->scenes : nullptr);
    require(sceneGraph, "glTF requires a scene");
    for (size_t i = 0; i < sceneGraph->nodes_count; i++)
        visit(sceneGraph->nodes[i]);
    for (size_t ni = 0; ni < raw->nodes_count; ni++) {
        auto &node = raw->nodes[ni];
        if (!node.mesh)
            continue;
        Mat4 transform;
        cgltf_node_transform_world(&node, transform.v.data());
        for (float f : transform.v)
            require(std::isfinite(f), "nonfinite node transform");
        for (size_t pi = 0; pi < node.mesh->primitives_count; pi++) {
            auto &p = node.mesh->primitives[pi];
            require(p.type == cgltf_primitive_type_triangles, "M0 requires triangle primitives");
            const cgltf_accessor *pos = nullptr, *uv = nullptr, *uv1 = nullptr, *normal = nullptr;
            for (size_t a = 0; a < p.attributes_count; a++) {
                auto &attr = p.attributes[a];
                if (attr.type == cgltf_attribute_type_position)
                    pos = attr.data;
                if (attr.type == cgltf_attribute_type_normal)
                    normal = attr.data;
                if (attr.type == cgltf_attribute_type_texcoord) {
                    if (attr.index == 0)
                        uv = attr.data;
                    if (attr.index == 1)
                        uv1 = attr.data;
                }
            }
            require(pos, "missing positions");
            Mesh mesh;
            mesh.material = p.material ? p.material - raw->materials : raw->materials_count;
            require(result->materials[mesh.material].lightmap < 0 || uv1, "declared lightmap needs TEXCOORD_1");
            for (const auto &slot : result->materials[mesh.material].slots)
                require(slot.texture < 0 || (slot.texCoord == 0 ? uv : uv1), "material references missing UV set");
            size_t count = p.indices ? p.indices->count : pos->count;
            require(count % 3 == 0 && count < 3000000, "invalid triangle count");
            memory += count * sizeof(Vertex);
            require(memory <= 512 * 1024 * 1024, "world GPU budget exceeded");
            for (size_t i = 0; i < count; i++) {
                size_t index = p.indices ? cgltf_accessor_read_index(p.indices, i) : i;
                float v[3];
                require(cgltf_accessor_read_float(pos, index, v, 3), "invalid position");
                Vertex vert;
                vert.position = transform.point({v[0], v[1], v[2]});
                require(std::isfinite(vert.position.x + vert.position.y + vert.position.z), "nonfinite vertex");
                if (uv)
                    require(cgltf_accessor_read_float(uv, index, vert.uv, 2), "invalid UV0");
                if (uv1)
                    require(cgltf_accessor_read_float(uv1, index, vert.uv1, 2), "invalid UV1");
                require(std::isfinite(vert.uv[0]+vert.uv[1]+vert.uv1[0]+vert.uv1[1]), "nonfinite UV");
                if (normal) {
                    float n[3];
                    require(cgltf_accessor_read_float(normal, index, n, 3), "invalid normal");
                    Vec3 a{transform.v[0], transform.v[1], transform.v[2]},
                        b{transform.v[4], transform.v[5], transform.v[6]},
                        c{transform.v[8], transform.v[9], transform.v[10]};
                    float det = dot(a, cross(b, c));
                    require(std::abs(det) > 1e-8f, "singular node transform");
                    vert.normal = normalized((cross(b, c) * n[0] + cross(c, a) * n[1] + cross(a, b) * n[2]) / det);
                }
                mesh.vertices.push_back(vert);
            }
            for (size_t i = 0; i < count; i += 3) {
                Triangle t{mesh.vertices[i].position, mesh.vertices[i + 1].position, mesh.vertices[i + 2].position};
                Vec3 n = normalized(cross(t.b - t.a, t.c - t.a));
                if (!normal)
                    for (int k = 0; k < 3; k++)
                        mesh.vertices[i + k].normal = n;
                if (collisionNodes.contains(ni) && length(n) > .1f)
                    result->collision.push_back(t);
            }
            if (visible.contains(&node))
                result->meshes.push_back(std::move(mesh));
        }
    }
    require(result->collision.size() <= 20000, "M0 collision triangle budget exceeded");
    // Spawn positions are capsule feet; the camera sits 1.7 m above them.
    for (auto &s : result->spawns) {
        sim::Camera c;
        c.position = s.position + Vec3{0, 1.7f, 0};
        Vec3 before = c.position;
        sim::resolveCapsule(c, result->collision);
        require(length(c.position - before) < .015f, "spawn capsule obstructed");
    }
    require(!result->meshes.empty(), "empty scene");
    return result;
}
} // namespace hyprune::world
