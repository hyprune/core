#include "World.hpp"
#include "Format.hpp"
#include <meshoptimizer/meshoptimizer.h>
#include <basisu/transcoder/basisu_transcoder.h>
#include <mutex>
#include "Visibility.hpp"
#include "Environment.hpp"
#include "Mounts.hpp"
#include "Schema.hpp"
#include "Spatial.hpp"
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
void markCollisionOwners(World &w) {
    w.ownsCollision.assign(w.nodeParents.size(), false);
    for (auto node : std::set<unsigned>(w.collisionNodes.begin(), w.collisionNodes.end()))
        for (int i = int(node); i >= 0 && size_t(i) < w.nodeParents.size() && !w.ownsCollision[i]; i = w.nodeParents[i])
            w.ownsCollision[i] = true;
}
bool movableNode(const World &w, unsigned node) { return node < w.ownsCollision.size() && w.ownsCollision[node]; }
const Spawn &World::spawn(const std::string &id) const {
    for (auto &s : spawns)
        if (s.id == (id.empty() ? defaultSpawn : id))
            return s;
    throw std::runtime_error("unknown spawn");
}
Image decodeKtx2(const unsigned char *data, size_t size, unsigned formats) {
    static std::once_flag once;
    std::call_once(once, [] { basist::basisu_transcoder_init(); });
    basist::ktx2_transcoder ktx;
    if (size > 0xffffffffu || !ktx.init(data, uint32_t(size)) || !ktx.start_transcoding())
        throw std::runtime_error("invalid KTX2 texture");
    if (ktx.get_faces() != 1 || ktx.get_layers() > 1 || ktx.is_hdr() || !(ktx.is_etc1s() || ktx.is_uastc()))
        throw std::runtime_error("unsupported KTX2 texture (2D LDR ETC1S/UASTC only)");
    Image image;
    image.width = int(ktx.get_width());
    image.height = int(ktx.get_height());
    if (image.width < 1 || image.height < 1 || image.width > 8192 || image.height > 8192)
        throw std::runtime_error("image dimensions exceed budget");
    // GL internal formats (linear variants); BC1 cannot carry alpha.
    struct Target {
        basist::transcoder_texture_format format;
        unsigned gl;
    };
    std::optional<Target> target;
    if (formats & BC7)
        target = Target{basist::transcoder_texture_format::cTFBC7_RGBA, 0x8E8C};
    else if (formats & BC1BC3)
        target = ktx.get_has_alpha() ? Target{basist::transcoder_texture_format::cTFBC3_RGBA, 0x83F3}
                                     : Target{basist::transcoder_texture_format::cTFBC1_RGB, 0x83F0};
    else if (formats & ASTC4x4)
        target = Target{basist::transcoder_texture_format::cTFASTC_4x4_RGBA, 0x93B0};
    uint32_t levels = std::max(1u, ktx.get_levels());
    for (uint32_t level = 0; level < (target ? levels : 1); ++level) {
        basist::ktx2_image_level_info info;
        if (!ktx.get_image_level_info(info, level, 0, 0))
            throw std::runtime_error("invalid KTX2 level");
        auto bytesPerBlock = target ? basist::basis_get_bytes_per_block_or_pixel(target->format) : 4u;
        auto units = target ? info.m_total_blocks : info.m_orig_width * info.m_orig_height;
        std::vector<unsigned char> out(size_t(units) * bytesPerBlock);
        if (!ktx.transcode_image_level(level, 0, 0, out.data(), units,
                                       target ? target->format : basist::transcoder_texture_format::cTFRGBA32))
            throw std::runtime_error("KTX2 transcode failed");
        if (target)
            image.levels.push_back(std::move(out));
        else
            image.rgba = std::move(out);
    }
    if (target)
        image.compressedFormat = target->gl;
    return image;
}
void decodeMeshopt(cgltf_data *raw) {
    for (size_t i = 0; i < raw->buffer_views_count; ++i) {
        auto &view = raw->buffer_views[i];
        if (!view.has_meshopt_compression)
            continue;
        auto &mc = view.meshopt_compression;
        if (!mc.buffer || !mc.buffer->data || mc.offset + mc.size > mc.buffer->size || mc.count * mc.stride > view.size ||
            mc.stride == 0 || mc.stride > 256)
            throw std::runtime_error("invalid meshopt buffer view");
        auto *source = static_cast<const unsigned char *>(mc.buffer->data) + mc.offset;
        void *target = malloc(mc.count * mc.stride);
        if (!target)
            throw std::bad_alloc();
        int result = -1;
        if (mc.mode == cgltf_meshopt_compression_mode_attributes)
            result = meshopt_decodeVertexBuffer(target, mc.count, mc.stride, source, mc.size);
        else if (mc.mode == cgltf_meshopt_compression_mode_triangles)
            result = meshopt_decodeIndexBuffer(target, mc.count, mc.stride, source, mc.size);
        else if (mc.mode == cgltf_meshopt_compression_mode_indices)
            result = meshopt_decodeIndexSequence(target, mc.count, mc.stride, source, mc.size);
        if (result == 0) {
            if (mc.filter == cgltf_meshopt_compression_filter_octahedral)
                meshopt_decodeFilterOct(target, mc.count, mc.stride);
            else if (mc.filter == cgltf_meshopt_compression_filter_quaternion)
                meshopt_decodeFilterQuat(target, mc.count, mc.stride);
            else if (mc.filter == cgltf_meshopt_compression_filter_exponential)
                meshopt_decodeFilterExp(target, mc.count, mc.stride);
        }
        if (result != 0) {
            free(target);
            throw std::runtime_error("meshopt decode failed");
        }
        view.data = target;
    }
}
void buildGpuMesh(Mesh &mesh, std::span<const Vertex> unique, std::vector<uint32_t> indices, bool keepOrder) {
    // Exact duplicates merged, then meshoptimizer's vertex-cache and fetch
    // orders. Blended materials keep their authored triangle order (blending
    // within one batch depends on it).
    std::vector<unsigned> remap(unique.size());
    size_t used = meshopt_generateVertexRemap(remap.data(), indices.data(), indices.size(), unique.data(),
                                              unique.size(), sizeof(Vertex));
    mesh.gpuVertices.resize(used);
    meshopt_remapVertexBuffer(mesh.gpuVertices.data(), unique.data(), unique.size(), sizeof(Vertex), remap.data());
    meshopt_remapIndexBuffer(indices.data(), indices.data(), indices.size(), remap.data());
    if (!keepOrder)
        meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), used);
    meshopt_optimizeVertexFetch(mesh.gpuVertices.data(), indices.data(), indices.size(), mesh.gpuVertices.data(), used,
                                sizeof(Vertex));
    mesh.indices = std::move(indices);
}
std::vector<ImageUse> imageUses(const World &world) {
    std::vector<ImageUse> uses(world.images.size());
    auto mark = [&](int image, bool color) {
        if (image >= 0 && size_t(image) < uses.size())
            (color ? uses[image].color : uses[image].data) = true;
    };
    for (const auto &m : world.materials) {
        for (int slot = 0; slot < 5; ++slot)
            mark(m.slots[slot].texture, slot == 0 || slot == 4);
        mark(m.lightmap, false);
    }
    return uses;
}
size_t gpuBytes(const World &world, bool srgbDecode, std::span<const CubeLevel> environment) {
    size_t bytes = 0;
    for (const auto &mesh : world.meshes)
        bytes += mesh.indices.empty() ? mesh.vertices.size() * sizeof(Vertex)
                                      : mesh.gpuVertices.size() * sizeof(Vertex) + mesh.indices.size() * 4;
    auto uses = imageUses(world);
    for (size_t i = 0; i < world.images.size(); ++i) {
        size_t chain = 0;
        for (auto &level : world.images[i].levels)
            chain += level.size();
        for (int w = world.images[i].width, h = world.images[i].height; world.images[i].levels.empty();
             w = std::max(1, w / 2), h = std::max(1, h / 2)) {
            chain += size_t(w) * h * 4;
            if (w == 1 && h == 1)
                break;
        }
        bytes += chain * (uses[i].color && uses[i].data && !srgbDecode ? 2 : 1);
    }
    for (const auto &level : environment)
        bytes += size_t(level.size) * level.size * 6 * 8;
    return bytes;
}
std::shared_ptr<World> load(const std::filesystem::path &root, LoadOptions options) {
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
    const std::string formatVersion = j.at("formatVersion");
    require(format::known(formatVersion), "unsupported world format");
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
    for (const auto &ext : j.value("requiredExtensions", Json::array()))
        require(ext == "com.hyprune.lumen-reach.mounts", "unsupported required world extension");
    auto zones = j.value("zones", Json::array());
    require(zones.size() <= 128 && zones.dump().size() <= 65536, "zone metadata exceeds IPC budget");
    std::set<std::string> zoneIds;
    for (const auto &zone : zones) {
        require(zoneIds.insert(zone.at("id").get<std::string>()).second, "duplicate zone ID");
        for (int axis = 0; axis < 3; axis++)
            require(zone.at("bounds").at("min")[axis].get<double>() < zone.at("bounds").at("max")[axis].get<double>(),
                    "inverted zone bounds");
    }
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
    std::set<std::string> climbIds;
    for (const auto &entry : j.value("climbables", Json::array())) {
        sim::Climbable c;
        c.id = entry.at("id");
        c.type = entry.at("type");
        c.bottom = vec(entry.at("bottom"));
        c.top = vec(entry.at("top"));
        c.facing = vec(entry.at("facing"));
        c.bottomExit = vec(entry.at("exits").at("bottom"));
        c.topExit = vec(entry.at("exits").at("top"));
        c.width = entry.at("width");
        c.depth = entry.at("depth");
        c.speed = entry.value("speed", 2.f);
        require(climbIds.insert(c.id).second, "duplicate climbable id");
        require(c.extent() >= .5f && c.extent() <= 100 && c.axis().y >= .25f, "invalid ascending climb segment");
        require(std::abs(length(c.facing) - 1) <= .001f && std::abs(dot(c.facing, c.axis())) <= .001f,
                "climb facing must be unit and perpendicular to segment");
        require(length(c.bottomExit - c.bottom) <= 3 && length(c.topExit - c.top) <= 3, "climb exit too far away");
        result->climbables.push_back(c);
    }
    auto scene = safePath(root, j["scene"]);
    cgltf_options opts{};
    cgltf_data *raw = nullptr;
    require(cgltf_parse_file(&opts, scene.c_str(), &raw) == cgltf_result_success, "glTF parse failed");
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, cgltf_free);
    require(raw->nodes_count < 10000 && raw->meshes_count < 10000, "scene too complex");
    // RFC-0004: world 0.7 adds compressed/optimised assets; earlier formats keep the base set.
    auto allowedExtension = [&](std::string_view id) {
        for (auto base : {"KHR_materials_unlit", "KHR_texture_transform", "KHR_materials_emissive_strength"})
            if (id == base)
                return true;
        for (auto compressed : {"KHR_texture_basisu", "EXT_meshopt_compression", "KHR_mesh_quantization"})
            if (id == compressed)
                return format::atLeast(formatVersion, "0.7");
        return false;
    };
    for (size_t i = 0; i < raw->extensions_required_count; i++)
        require(allowedExtension(raw->extensions_required[i]), "unsupported required glTF extension for this world format");
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
    for (size_t i = 0; i < raw->buffer_views_count; i++)
        require(!raw->buffer_views[i].has_meshopt_compression || format::atLeast(formatVersion, "0.7"),
                "EXT_meshopt_compression needs world format 0.7");
    decodeMeshopt(raw);
    require(cgltf_validate(raw) == cgltf_result_success, "invalid glTF");
    size_t memory = 0;
    for (size_t i = 0; i < raw->textures_count; i++) {
        Sampler sampler;
        if (auto *s = raw->textures[i].sampler) {
            sampler = {int(s->wrap_s), int(s->wrap_t), s->min_filter ? int(s->min_filter) : 9987,
                       s->mag_filter ? int(s->mag_filter) : 9729};
        }
        result->samplers.push_back(sampler);
        bool irradiance = false;
        for (auto &l : j.value("lightmaps", Json::array()))
            if (l.at("texture").get<size_t>() == i)
                irradiance = true;
        if (raw->textures[i].has_basisu && raw->textures[i].basisu_image) {
            require(format::atLeast(formatVersion, "0.7"), "KHR_texture_basisu needs world format 0.7");
            // v0 lightmaps stay PNG; material textures may be KTX2.
            require(!irradiance, "lightmap must be PNG");
            auto *ktx = raw->textures[i].basisu_image;
            std::vector<unsigned char> bytes;
            if (ktx->uri) {
                auto path = asset(ktx->uri);
                require(std::filesystem::file_size(path) <= 128 * 1024 * 1024, "asset exceeds 128 MiB");
                std::ifstream input(path, std::ios::binary);
                bytes.assign(std::istreambuf_iterator<char>(input), {});
            } else {
                require(ktx->buffer_view && cgltf_buffer_view_data(ktx->buffer_view), "missing KTX2 data");
                auto *b = cgltf_buffer_view_data(ktx->buffer_view);
                bytes.assign(b, b + ktx->buffer_view->size);
            }
            auto image = decodeKtx2(bytes.data(), bytes.size(), options.compressedFormats);
            memory += image.rgba.size();
            for (auto &level : image.levels)
                memory += level.size();
            require(memory <= 512 * 1024 * 1024, "world GPU budget exceeded");
            result->images.push_back(std::move(image));
            continue;
        }
        auto *img = raw->textures[i].image;
        require(img, "missing texture image");
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
        if (!view.texture)
            return out;
        out.texture = view.texture - raw->textures;
        out.texCoord = view.texcoord;
        if (view.has_transform) {
            const auto &t = view.transform;
            std::copy_n(t.offset, 2, out.offset);
            std::copy_n(t.scale, 2, out.scale);
            out.rotation = t.rotation;
            if (t.has_texcoord)
                out.texCoord = t.texcoord;
        }
        require(out.texCoord == 0 || out.texCoord == 1, "material requires unsupported UV set (only UV0/UV1)");
        require(std::isfinite(out.offset[0] + out.offset[1] + out.scale[0] + out.scale[1] + out.rotation),
                "nonfinite UV transform");
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
        // Legacy glTF diffuse inputs remain meaningful even when no metallic-
        // roughness block exists. Preserve transforms/UV sets through slot().
        if (m.has_pbr_specular_glossiness && !m.has_pbr_metallic_roughness) {
            auto &p = m.pbr_specular_glossiness;
            out.color = {p.diffuse_factor[0], p.diffuse_factor[1], p.diffuse_factor[2]};
            out.alpha = p.diffuse_factor[3];
            out.slots[0] = slot(p.diffuse_texture);
            out.baseTexture = out.slots[0].texture;
            out.metallic = 0;
            out.roughness = 1 - p.glossiness_factor;
        }
        out.emissive = {m.emissive_factor[0], m.emissive_factor[1], m.emissive_factor[2]};
        out.unlit = m.unlit;
        out.alphaMode = m.alpha_mode == cgltf_alpha_mode_blend ? 2 : m.alpha_mode == cgltf_alpha_mode_mask ? 1 : 0;
        out.alphaCutoff = m.alpha_cutoff;
        out.doubleSided = m.double_sided;
        if (m.has_emissive_strength)
            out.emissive = out.emissive * m.emissive_strength.emissive_strength;
        require(std::isfinite(out.emissive.x + out.emissive.y + out.emissive.z), "invalid emission");
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
            mesh.node = ni;
            mesh.material = p.material ? p.material - raw->materials : raw->materials_count;
            require(result->materials[mesh.material].lightmap < 0 || uv1, "declared lightmap needs TEXCOORD_1");
            for (const auto &slot : result->materials[mesh.material].slots)
                require(slot.texture < 0 || (slot.texCoord == 0 ? uv : uv1), "material references missing UV set");
            size_t count = p.indices ? p.indices->count : pos->count;
            require(count % 3 == 0 && count < 3000000, "invalid triangle count");
            require(pos->count < 3000000, "invalid vertex count");
            memory += count * sizeof(Vertex);
            require(memory <= 512 * 1024 * 1024, "world GPU budget exceeded");
            // Each accessor vertex is decoded once; the flat triangle list used by
            // collision, editing and picking is expanded from these.
            Vec3 a{transform.v[0], transform.v[1], transform.v[2]}, b{transform.v[4], transform.v[5], transform.v[6]},
                c{transform.v[8], transform.v[9], transform.v[10]};
            float det = dot(a, cross(b, c));
            require(!normal || std::abs(det) > 1e-8f, "singular node transform");
            std::vector<Vertex> unique(pos->count);
            for (size_t index = 0; index < pos->count; ++index) {
                float v[3];
                require(cgltf_accessor_read_float(pos, index, v, 3), "invalid position");
                Vertex &vert = unique[index];
                vert.position = transform.point({v[0], v[1], v[2]});
                require(std::isfinite(vert.position.x + vert.position.y + vert.position.z), "nonfinite vertex");
                if (uv)
                    require(cgltf_accessor_read_float(uv, index, vert.uv, 2), "invalid UV0");
                if (uv1)
                    require(cgltf_accessor_read_float(uv1, index, vert.uv1, 2), "invalid UV1");
                require(std::isfinite(vert.uv[0] + vert.uv[1] + vert.uv1[0] + vert.uv1[1]), "nonfinite UV");
                if (normal) {
                    float n[3];
                    require(cgltf_accessor_read_float(normal, index, n, 3), "invalid normal");
                    vert.normal = normalized((cross(b, c) * n[0] + cross(c, a) * n[1] + cross(a, b) * n[2]) / det);
                }
            }
            std::vector<uint32_t> indices(count);
            for (size_t i = 0; i < count; i++) {
                size_t index = p.indices ? cgltf_accessor_read_index(p.indices, i) : i;
                require(index < unique.size(), "index out of range");
                indices[i] = uint32_t(index);
                mesh.vertices.push_back(unique[index]);
            }
            // Without authored normals the flat list carries per-face normals: draw it.
            if (visible.contains(&node) && normal)
                buildGpuMesh(mesh, unique, std::move(indices), result->materials[mesh.material].alphaMode == 2);
            for (size_t i = 0; i < count; i += 3) {
                Triangle t{mesh.vertices[i].position, mesh.vertices[i + 1].position, mesh.vertices[i + 2].position};
                Vec3 n = normalized(cross(t.b - t.a, t.c - t.a));
                if (!normal)
                    for (int k = 0; k < 3; k++)
                        mesh.vertices[i + k].normal = n;
                if (collisionNodes.contains(ni) && length(n) > .1f) {
                    result->collision.push_back(t);
                    result->collisionNodes.push_back(ni);
                }
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
    for (const auto &ladder : result->climbables) {
        // Endpoints are safe standing foot positions. A bad optional climbable
        // rejects activation instead of letting a traversal place us in a wall.
        for (auto feet : {ladder.bottom, ladder.top, ladder.bottomExit, ladder.topExit}) {
            sim::Camera c;
            c.position = feet + Vec3{0, c.eyeHeight, 0};
            auto expected = c.position;
            sim::resolveCapsule(c, result->collision);
            require(length(c.position - expected) < .015f, "climb endpoint capsule obstructed");
        }
    }
    require(!result->meshes.empty(), "empty scene");
    loadSpatial(*result, root);
    result->nodeParents.resize(raw->nodes_count, -1);
    for (size_t i = 0; i < raw->nodes_count; i++)
        if (raw->nodes[i].parent)
            result->nodeParents[i] = raw->nodes[i].parent - raw->nodes;
    loadMounts(*result, raw->nodes_count);
    result->atmosphere = j.value("atmosphere", defaultAtmosphere());
    validateAtmosphere(result->atmosphere);
    loadVisibility(*result);
    result->playable = loadPlayable(j);
    for (auto &card : j.value("backdrop", Json::object()).value("farCards", Json::array())) {
        if (card.get<uint64_t>() >= raw->nodes_count)
            throw std::runtime_error("backdrop farCards: unknown node");
        unsigned node = card.get<unsigned>();
        for (auto &mesh : result->meshes) {
            int ancestor = mesh.node;
            while (ancestor >= 0 && unsigned(ancestor) != node)
                ancestor = result->nodeParents[ancestor];
            if (ancestor < 0)
                continue;
            for (auto &slot : result->materials[mesh.material].slots) {
                if (slot.texture < 0)
                    continue;
                auto &sampler = result->samplers.at(slot.texture);
                if (sampler.wrapS == 33071 || sampler.wrapT == 33071) {
                    auto warning =
                        "backdrop far card node " + std::to_string(node) + " texture " + std::to_string(slot.texture) +
                        " uses CLAMP_TO_EDGE (" + (sampler.wrapS == 33071 ? std::string("S") : "") +
                        (sampler.wrapT == 33071 ? std::string("T") : "") + "); far cards may smear at their edges";
                    if (std::find(result->warnings.begin(), result->warnings.end(), warning) == result->warnings.end())
                        result->warnings.push_back(warning);
                }
            }
        }
    }
    if (result->atmosphere.contains("environment")) {
        int i = 0;
        for (const auto &face : result->atmosphere["environment"]["faces"]) {
            auto path = safePath(root, face);
            int w, h, n;
            require(stbi_info(path.c_str(), &w, &h, &n) && w == h && w <= 2048, "environment face dimensions");
            auto pixels = stbi_load(path.c_str(), &w, &h, &n, 4);
            require(pixels, "environment face decode");
            result->environment[i++] = {w, h, {pixels, pixels + size_t(w) * h * 4}};
            stbi_image_free(pixels);
        }
    }
    int resolution = result->atmosphere.contains("environment")
                         ? std::min(128, result->atmosphere["environment"]["maxResolution"].get<int>())
                         : 128;
    result->environmentHigh = prefilterEnvironment(*result, resolution);
    result->environmentLow = prefilterEnvironment(*result, std::min(64, resolution));
    result->collisionIndex.rebuild(result->collision);
    for (const auto &mesh : result->meshes)
        for (size_t i = 0; i + 2 < mesh.vertices.size(); i += 3)
            result->visualTriangles.push_back(
                {mesh.vertices[i].position, mesh.vertices[i + 1].position, mesh.vertices[i + 2].position});
    result->visualIndex.rebuild(result->visualTriangles);
    markCollisionOwners(*result);
    return result;
}
} // namespace hyprune::world
