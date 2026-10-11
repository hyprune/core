#include "Character.hpp"
#include <algorithm>
#include <cgltf.h>
#include <cmath>
#include <format>
#include <set>
#include <stb_image.h>
#include <stdexcept>
namespace hyprune::world {
namespace {
Mat4 trs(Vec3 t, const std::array<float, 4> &q, Vec3 s) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    Mat4 m = Mat4::identity();
    m.v[0] = (1 - 2 * (y * y + z * z)) * s.x;
    m.v[1] = (2 * (x * y + z * w)) * s.x;
    m.v[2] = (2 * (x * z - y * w)) * s.x;
    m.v[4] = (2 * (x * y - z * w)) * s.y;
    m.v[5] = (1 - 2 * (x * x + z * z)) * s.y;
    m.v[6] = (2 * (y * z + x * w)) * s.y;
    m.v[8] = (2 * (x * z + y * w)) * s.z;
    m.v[9] = (2 * (y * z - x * w)) * s.z;
    m.v[10] = (1 - 2 * (x * x + y * y)) * s.z;
    m.v[12] = t.x, m.v[13] = t.y, m.v[14] = t.z;
    return m;
}
Mat4 affineInverse(const Mat4 &m) {
    const auto &v = m.v; // column-major, affine
    float a = v[0], b = v[4], c = v[8], d = v[1], e = v[5], f = v[9], g = v[2], h = v[6], i = v[10];
    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    Mat4 r = Mat4::identity();
    if (std::abs(det) < 1e-12f)
        return r;
    float k = 1 / det;
    r.v[0] = (e * i - f * h) * k, r.v[4] = (c * h - b * i) * k, r.v[8] = (b * f - c * e) * k;
    r.v[1] = (f * g - d * i) * k, r.v[5] = (a * i - c * g) * k, r.v[9] = (c * d - a * f) * k;
    r.v[2] = (d * h - e * g) * k, r.v[6] = (b * g - a * h) * k, r.v[10] = (a * e - b * d) * k;
    for (int row = 0; row < 3; ++row)
        r.v[12 + row] = -(r.v[row] * v[12] + r.v[4 + row] * v[13] + r.v[8 + row] * v[14]);
    return r;
}
std::array<float, 4> nlerp(std::array<float, 4> a, std::array<float, 4> b, float u) {
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (d < 0)
        for (auto &c : b)
            c = -c;
    std::array<float, 4> r;
    float l = 0;
    for (int i = 0; i < 4; ++i)
        l += (r[i] = a[i] + (b[i] - a[i]) * u) * r[i];
    l = l > 0 ? 1 / std::sqrt(l) : 0;
    for (auto &c : r)
        c *= l;
    return r;
}
// Sample a channel at t (clamped to its keys); out gets 3 or 4 components.
void sample(const Character::Channel &c, float t, float *out) {
    int width = c.path == 1 ? 4 : 3;
    size_t n = c.times.size();
    if (!n)
        return;
    if (t <= c.times.front() || n == 1) {
        std::copy_n(c.values.data(), width, out);
        return;
    }
    if (t >= c.times.back()) {
        std::copy_n(c.values.data() + (n - 1) * width, width, out);
        return;
    }
    size_t k = std::upper_bound(c.times.begin(), c.times.end(), t) - c.times.begin() - 1;
    float u = c.step ? 0 : (t - c.times[k]) / std::max(1e-6f, c.times[k + 1] - c.times[k]);
    const float *a = c.values.data() + k * width, *b = a + width;
    if (width == 4) {
        auto r = nlerp({a[0], a[1], a[2], a[3]}, {b[0], b[1], b[2], b[3]}, u);
        std::copy_n(r.data(), 4, out);
    } else
        for (int i = 0; i < 3; ++i)
            out[i] = a[i] + (b[i] - a[i]) * u;
}
} // namespace
const Character::Clip *Character::clip(std::string_view name) const {
    for (auto &c : clips)
        if (c.name == name)
            return &c;
    return nullptr;
}
namespace {
std::string boneKey(std::string name) {
    // mixamorig:Hips, mixamorig1:Hips, Armature_Hips -> hips
    if (auto colon = name.rfind(':'); colon != std::string::npos)
        name = name.substr(colon + 1);
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    return name;
}
bool fingerBone(const std::string &name) {
    auto k = boneKey(name);
    for (auto part : {"finger", "thumb", "index", "middle", "ring", "pinky"}) // Mixamo: LeftHandMiddle1 ...
        if (k.find(part) != std::string::npos)
            return true;
    return false;
}
} // namespace
int Character::role(std::string_view r) const {
    for (size_t i = 0; i < joints.size(); ++i) {
        auto k = boneKey(joints[i].name);
        if (k == r || (r == "hips" && k == "pelvis"))
            return int(i);
    }
    return -1;
}
int Character::joint(std::string_view name) const {
    for (size_t i = 0; i < joints.size(); ++i)
        if (joints[i].name == name)
            return int(i);
    return -1;
}
namespace {
struct Locals {
    std::vector<Vec3> T, S;
    std::vector<std::array<float, 4>> R;
};
Locals locals(const Character &ch, const Character::Clip *c, float t, bool loop) {
    Locals l;
    for (auto &j : ch.joints)
        l.T.push_back(j.translation), l.R.push_back(j.rotation), l.S.push_back(j.scale);
    if (c && c->duration > 0) {
        t = loop ? std::fmod(std::fmod(t, c->duration) + c->duration, c->duration) : std::clamp(t, 0.f, c->duration);
        for (auto &k : c->channels) {
            float v[4];
            sample(k, t, v);
            if (k.path == 0)
                l.T[k.joint] = {v[0], v[1], v[2]};
            else if (k.path == 1)
                l.R[k.joint] = {v[0], v[1], v[2], v[3]};
            else
                l.S[k.joint] = {v[0], v[1], v[2]};
        }
    }
    return l;
}
void skin(const Character &ch, const Locals &l, std::vector<Mat4> &out) {
    std::vector<Mat4> global(ch.joints.size());
    out.resize(ch.joints.size());
    for (size_t i = 0; i < ch.joints.size(); ++i) { // parents precede children (sorted at load)
        auto local = trs(l.T[i], l.R[i], l.S[i]);
        global[i] = ch.joints[i].parent < 0 ? ch.armature * local : global[ch.joints[i].parent] * local;
        out[i] = global[i] * ch.joints[i].inverseBind;
    }
}
} // namespace
void Character::pose(const Clip *c, float t, bool loop, std::vector<Mat4> &out) const { skin(*this, locals(*this, c, t, loop), out); }
void Character::blend(const Clip *a, float ta, const Clip *b, float tb, float wb, std::vector<Mat4> &out) const {
    auto la = locals(*this, a, ta, true);
    if (wb <= 0 || !b)
        return skin(*this, la, out);
    auto lb = locals(*this, b, tb, true);
    wb = std::min(wb, 1.f);
    for (size_t i = 0; i < joints.size(); ++i) {
        la.T[i] = la.T[i] + (lb.T[i] - la.T[i]) * wb;
        la.S[i] = la.S[i] + (lb.S[i] - la.S[i]) * wb;
        la.R[i] = nlerp(la.R[i], lb.R[i], wb);
    }
    skin(*this, la, out);
}
Character loadCharacter(const std::filesystem::path &path) {
    cgltf_options opts{};
    cgltf_data *raw = nullptr;
    if (cgltf_parse_file(&opts, path.c_str(), &raw) != cgltf_result_success)
        throw std::runtime_error("not a glTF/GLB file");
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, cgltf_free);
    if (cgltf_load_buffers(&opts, raw, path.c_str()) != cgltf_result_success)
        throw std::runtime_error("glTF buffers could not be loaded");
    if (cgltf_validate(raw) != cgltf_result_success)
        throw std::runtime_error("invalid glTF (run a glTF validator)");
    decodeMeshopt(raw); // EXT_meshopt_compression, as world packages (format 0.7)
    Character c;
    c.skins = int(raw->skins_count);
    if (!raw->skins_count)
        throw std::runtime_error("no skin: an NPC character needs a skinned mesh");
    const cgltf_skin &skin = raw->skins[0];
    // Joints sorted so that parents come first.
    std::vector<const cgltf_node *> nodes(skin.joints, skin.joints + skin.joints_count);
    auto indexOf = [&](const cgltf_node *n) {
        auto it = std::find(nodes.begin(), nodes.end(), n);
        return it == nodes.end() ? -1 : int(it - nodes.begin());
    };
    auto jointParent = [&](const cgltf_node *n) -> const cgltf_node * {
        for (auto p = n->parent; p; p = p->parent)
            if (indexOf(p) >= 0)
                return p;
        return nullptr;
    };
    std::vector<const cgltf_node *> order;
    std::set<const cgltf_node *> placed;
    while (order.size() < nodes.size()) {
        size_t before = order.size();
        for (auto n : nodes)
            if (!placed.contains(n) && (!jointParent(n) || placed.contains(jointParent(n))))
                order.push_back(n), placed.insert(n);
        if (order.size() == before)
            throw std::runtime_error("skin joints form a cycle");
    }
    std::vector<int> remap(nodes.size());
    for (size_t i = 0; i < order.size(); ++i)
        remap[indexOf(order[i])] = int(i);
    for (auto n : order) {
        Character::Joint j;
        j.name = n->name ? n->name : "";
        auto p = jointParent(n);
        j.parent = p ? remap[indexOf(p)] : -1;
        if (n->has_matrix)
            ++c.matrixJoints;
        if (n->has_translation)
            j.translation = {n->translation[0], n->translation[1], n->translation[2]};
        if (n->has_rotation)
            j.rotation = {n->rotation[0], n->rotation[1], n->rotation[2], n->rotation[3]};
        if (n->has_scale)
            j.scale = {n->scale[0], n->scale[1], n->scale[2]};
        if (skin.inverse_bind_matrices)
            cgltf_accessor_read_float(skin.inverse_bind_matrices, indexOf(n), j.inverseBind.v.data(), 16);
        if (j.parent < 0) {
            ++c.skeletonRoots;
            if (n->parent) // armature nodes above the skeleton root (e.g. an axis-conversion node)
                cgltf_node_transform_world(n->parent, c.armature.v.data());
        }
        c.joints.push_back(j);
    }
    // Images and materials (base colour, factors).
    for (size_t i = 0; i < raw->images_count; ++i) {
        auto &img = raw->images[i];
        Image out;
        int w = 0, h = 0, n = 0;
        unsigned char *pixels = nullptr;
        bool ktx2 = img.mime_type && std::string_view(img.mime_type) == "image/ktx2";
        if (ktx2 && img.buffer_view && cgltf_buffer_view_data(img.buffer_view)) { // KHR_texture_basisu: transcode to RGBA
            auto decoded = decodeKtx2(cgltf_buffer_view_data(img.buffer_view), img.buffer_view->size, 0);
            c.images.push_back(std::move(decoded));
            continue;
        }
        if (img.buffer_view && cgltf_buffer_view_data(img.buffer_view))
            pixels = stbi_load_from_memory(cgltf_buffer_view_data(img.buffer_view), int(img.buffer_view->size), &w, &h, &n, 4);
        else if (img.uri && !std::string_view(img.uri).starts_with("data:"))
            pixels = stbi_load((path.parent_path() / img.uri).c_str(), &w, &h, &n, 4);
        if (!pixels)
            throw std::runtime_error(std::format("image {} could not be decoded (PNG or JPEG expected)", i));
        out.width = w, out.height = h;
        out.rgba.assign(pixels, pixels + size_t(w) * h * 4);
        stbi_image_free(pixels);
        c.images.push_back(std::move(out));
    }
    for (size_t i = 0; i < raw->materials_count; ++i) {
        auto &m = raw->materials[i];
        Material out;
        out.color = {m.pbr_metallic_roughness.base_color_factor[0], m.pbr_metallic_roughness.base_color_factor[1],
                     m.pbr_metallic_roughness.base_color_factor[2]};
        out.metallic = m.pbr_metallic_roughness.metallic_factor;
        out.roughness = m.pbr_metallic_roughness.roughness_factor;
        auto image = [&](const cgltf_texture *tex) {
            return !tex ? -1 : tex->basisu_image ? int(tex->basisu_image - raw->images) : tex->image ? int(tex->image - raw->images) : -1;
        };
        if (int i = image(m.pbr_metallic_roughness.base_color_texture.texture); i >= 0) {
            out.slots[0].texture = i;
            out.baseTexture = i;
        }
        out.emissive = {m.emissive_factor[0], m.emissive_factor[1], m.emissive_factor[2]};
        if (m.has_emissive_strength)
            out.emissive = out.emissive * m.emissive_strength.emissive_strength;
        if (int i = image(m.emissive_texture.texture); i >= 0)
            out.slots[4].texture = i;
        c.emissive.push_back(length(out.emissive) > 0 || out.slots[4].texture >= 0);
        out.doubleSided = m.double_sided;
        c.materials.push_back(out);
        c.materialNames.push_back(m.name ? m.name : "");
    }
    if (c.materials.empty()) {
        c.materials.push_back({});
        c.materialNames.push_back("");
        c.emissive.push_back(false);
    }
    // Skinned primitives of the mesh nodes using this skin.
    bool first = true;
    for (size_t ni = 0; ni < raw->nodes_count; ++ni) {
        auto &node = raw->nodes[ni];
        if (!node.mesh || node.skin != &skin)
            continue;
        c.meshNodes.push_back(node.name ? node.name : "");
        for (size_t pi = 0; pi < node.mesh->primitives_count; ++pi) {
            auto &prim = node.mesh->primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles)
                continue;
            const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr, *jn = nullptr, *wt = nullptr;
            for (size_t a = 0; a < prim.attributes_count; ++a) {
                auto &at = prim.attributes[a];
                if (at.type == cgltf_attribute_type_position) pos = at.data;
                else if (at.type == cgltf_attribute_type_normal) nrm = at.data;
                else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) uv = at.data;
                else if (at.type == cgltf_attribute_type_joints && at.index == 0) jn = at.data;
                else if (at.type == cgltf_attribute_type_weights && at.index == 0) wt = at.data;
                else if ((at.type == cgltf_attribute_type_joints || at.type == cgltf_attribute_type_weights) && at.index > 0)
                    ++c.extraWeightSets;
            }
            if (!pos || !jn || !wt)
                throw std::runtime_error("a skinned primitive lacks POSITION, JOINTS_0 or WEIGHTS_0");
            Character::Primitive out;
            out.material = prim.material ? unsigned(prim.material - raw->materials) : 0;
            out.vertices.resize(pos->count);
            for (size_t v = 0; v < pos->count; ++v) {
                auto &o = out.vertices[v];
                cgltf_accessor_read_float(pos, v, &o.position.x, 3);
                if (nrm)
                    cgltf_accessor_read_float(nrm, v, &o.normal.x, 3);
                if (uv)
                    cgltf_accessor_read_float(uv, v, o.uv, 2);
                cgltf_uint j[4]{};
                cgltf_accessor_read_uint(jn, v, j, 4);
                cgltf_accessor_read_float(wt, v, o.weights, 4);
                float sum = 0;
                for (int k = 0; k < 4; ++k) {
                    o.joints[k] = float(j[k] < remap.size() ? remap[j[k]] : 0);
                    sum += o.weights[k];
                }
                c.weightError = std::max(c.weightError, std::abs(sum - 1));
                if (sum > 0)
                    for (auto &w : o.weights)
                        w /= sum;
            }
            if (prim.indices) {
                out.indices.resize(prim.indices->count);
                for (size_t k = 0; k < prim.indices->count; ++k)
                    out.indices[k] = uint32_t(cgltf_accessor_read_index(prim.indices, k));
            } else
                for (size_t k = 0; k < pos->count; ++k)
                    out.indices.push_back(uint32_t(k));
            c.triangles += out.indices.size() / 3;
            c.primitives.push_back(std::move(out));
        }
    }
    if (c.primitives.empty())
        throw std::runtime_error("no triangle mesh uses the skin");
    // Rest-pose bounds as drawn: skinned with the rest pose (armature included).
    std::vector<Mat4> rest;
    c.pose(nullptr, 0, false, rest);
    for (auto &prim : c.primitives)
        for (auto &v : prim.vertices) {
            Vec3 q{};
            for (int k = 0; k < 4; ++k)
                q = q + rest[size_t(v.joints[k])].point(v.position) * v.weights[k];
            if (first)
                c.lo = c.hi = q, first = false;
            c.lo = {std::min(c.lo.x, q.x), std::min(c.lo.y, q.y), std::min(c.lo.z, q.z)};
            c.hi = {std::max(c.hi.x, q.x), std::max(c.hi.y, q.y), std::max(c.hi.z, q.z)};
        }
    // Animation clips: channels on skin joints only (others are ignored, as core moves the NPC).
    for (size_t ai = 0; ai < raw->animations_count; ++ai) {
        auto &anim = raw->animations[ai];
        Character::Clip clip;
        clip.name = anim.name ? anim.name : "";
        if (anim.extras.data)
            if (auto at = std::string_view(anim.extras.data).find("\"speed\""); at != std::string_view::npos)
                clip.speed = std::strtof(anim.extras.data + at + 7 + std::string_view(anim.extras.data + at + 7).find_first_not_of(" :"), nullptr);
        for (size_t ci = 0; ci < anim.channels_count; ++ci) {
            auto &ch = anim.channels[ci];
            int j = indexOf(ch.target_node);
            if (j < 0 || ch.target_path == cgltf_animation_path_type_weights)
                continue;
            Character::Channel out;
            out.joint = remap[j];
            out.path = ch.target_path == cgltf_animation_path_type_translation ? 0
                       : ch.target_path == cgltf_animation_path_type_rotation  ? 1
                                                                               : 2;
            out.step = ch.sampler->interpolation == cgltf_interpolation_type_step;
            bool cubic = ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline;
            int width = out.path == 1 ? 4 : 3;
            out.times.resize(ch.sampler->input->count);
            for (size_t k = 0; k < out.times.size(); ++k)
                cgltf_accessor_read_float(ch.sampler->input, k, &out.times[k], 1);
            out.values.resize(out.times.size() * width);
            for (size_t k = 0; k < out.times.size(); ++k) // cubic splines: the value, not the tangents
                cgltf_accessor_read_float(ch.sampler->output, cubic ? k * 3 + 1 : k, &out.values[k * width], width);
            if (!out.times.empty())
                clip.duration = std::max(clip.duration, out.times.back());
            clip.channels.push_back(std::move(out));
        }
        c.clips.push_back(std::move(clip));
    }
    return c;
}
std::vector<CharacterProblem> checkCharacter(const Character &c, const Character *reference) {
    std::vector<CharacterProblem> p;
    auto error = [&](std::string m) { p.push_back({CharacterProblem::Error, std::move(m)}); };
    auto warn = [&](std::string m) { p.push_back({CharacterProblem::Warning, std::move(m)}); };
    auto info = [&](std::string m) { p.push_back({CharacterProblem::Info, std::move(m)}); };
    if (c.skins != 1)
        warn(std::format("{} skins: one shared skeleton is expected; only the first is used", c.skins));
    // Skeleton: at most 40 bones, one root named `root` at the feet.
    // Bones: 40 is a performance target (a warning); the hard limit is the renderer's 64-joint
    // skinning block. Finger and accessory bones are fine.
    size_t fingers = std::count_if(c.joints.begin(), c.joints.end(), [](auto &j) { return fingerBone(j.name); });
    if (c.joints.size() > 64)
        error(std::format("{} bones: core skins at most 64 (tools/npc-import --strip-fingers removes finger bones)", c.joints.size()));
    else if (c.joints.size() > 40)
        warn(std::format("{} bones ({} finger bones): over the 40-bone performance target", c.joints.size(), fingers));
    int root = c.role("root");
    if (root < 0)
        error(c.role("hips") >= 0 ? "no `root` bone above the hips: run tools/npc-import, which adds one at the feet"
                                  : "no bone named `root`: add a root bone at the feet, parent of the hips");
    if (c.role("hips") < 0)
        error("no hips bone (`Hips`, `mixamorig:Hips` or `pelvis`)");
    if (c.role("head") < 0)
        warn("no head bone (`Head` or `mixamorig:Head`): core can't turn the head towards the player");
    if (root >= 0 && c.joints[root].parent != -1)
        error("`root` is not the top of the skeleton: every other bone must descend from it");
    if (c.skeletonRoots > 1)
        error(std::format("{} top-level bones: the skeleton must have the single root `root`", c.skeletonRoots));
    if (c.matrixJoints)
        error(std::format("{} bones use a matrix transform: export translation/rotation/scale", c.matrixJoints));
    // Units and axes: metres, Y up (a humanoid is 0.5-3 m tall and taller than deep).
    std::vector<Mat4> bind;
    c.pose(nullptr, 0, false, bind);
    float height = c.hi.y - c.lo.y, width = c.hi.x - c.lo.x, depth = c.hi.z - c.lo.z;
    if (height > 30)
        error(std::format("{:.1f} units tall: export in metres (looks like centimetres)", height));
    else if (height < .5f || height > 3)
        error(std::format("{:.2f} m tall: a humanoid NPC is 0.5-3 m tall (Y up, metres)", height));
    bool upright = height >= .5f && depth <= height && width <= height * 1.5f;
    if (depth > height || width > height * 1.5f)
        error(std::format("bounds {:.2f} x {:.2f} x {:.2f} m (x y z): the character must stand along +Y", width, height, depth));
    if (root >= 0 && upright) {
        std::vector<Mat4> rest(c.joints.size()); // rest-pose joint globals
        for (size_t i = 0; i < c.joints.size(); ++i) {
            auto &j = c.joints[i];
            auto local = trs(j.translation, j.rotation, j.scale);
            rest[i] = j.parent < 0 ? c.armature * local : rest[j.parent] * local;
        }
        Vec3 at{rest[root].v[12], rest[root].v[13], rest[root].v[14]};
        if (std::abs(at.y - c.lo.y) > .05f * std::max(1.f, height) || std::hypot(at.x, at.z) > .25f * std::max(1.f, height))
            error(std::format("`root` is at ({:.2f}, {:.2f}, {:.2f}): put it on the ground between the feet, at the origin",
                              at.x, at.y, at.z));
        // Forward +Z: toes (or feet) in front of the ankles.
        float toes = 0;
        int count = 0;
        for (size_t i = 0; i < c.joints.size(); ++i) {
            std::string n = c.joints[i].name;
            std::transform(n.begin(), n.end(), n.begin(), ::tolower);
            if (n.find("toe") != std::string::npos && c.joints[i].parent >= 0) {
                toes += rest[i].v[14] - rest[c.joints[i].parent].v[14];
                ++count;
            }
        }
        if (count && toes < 0)
            error("the toes point to -Z: the character must face +Z");
        else if (!count)
            info("no `toe` bones: facing +Z could not be checked; check it in a viewer");
        // Rest pose vs bind: at rest every skinning matrix is the same (identity, or the
        // dequantization transform of a quantized mesh).
        float worst = 0;
        for (auto &m : bind)
            for (int k = 0; k < 16; ++k)
                worst = std::max(worst, std::abs(m.v[k] - bind[0].v[k]));
        if (worst > .01f)
            warn("the rest pose differs from the bind pose: export with the skeleton in its bind pose");
    }
    // Weights.
    if (c.extraWeightSets)
        error("more than 4 weights per vertex (JOINTS_1/WEIGHTS_1): limit influences to 4");
    if (c.weightError > .01f)
        warn(std::format("vertex weights don't sum to 1 (off by up to {:.3f}): normalise them", c.weightError));
    // Budget and materials.
    if (c.triangles > 16000)
        error(std::format("{} triangles: the budget is 4,000-8,000 (16,000 at most)", c.triangles));
    else if (c.triangles > 8000 || c.triangles < 4000)
        warn(std::format("{} triangles: aim for 4,000-8,000", c.triangles));
    // Materials: the base, optionally one `accent` (recoloured per area) and one emissive
    // (visor/eyes). One baked material is valid: area variants are then separate characters.
    size_t emissive = std::count(c.emissive.begin(), c.emissive.end(), true);
    if (c.materials.size() > 3 || (c.materials.size() == 3 && emissive == 0))
        error(std::format("{} materials ({} emissive): use the base, an optional `accent` and one emissive visor/eyes material",
                          c.materials.size(), emissive));
    if (emissive > 1)
        warn(std::format("{} emissive materials: keep one (visor/eyes)", emissive));
    if (c.materials.size() - std::min(emissive, c.materials.size()) >= 2 &&
        std::none_of(c.materialNames.begin(), c.materialNames.end(), [](auto &n) {
            std::string l = n;
            std::transform(l.begin(), l.end(), l.begin(), ::tolower);
            return l.find("accent") != std::string::npos;
        }))
        warn("two base materials and none named `accent`: name the per-area recolourable one `accent`");
    for (auto &n : c.meshNodes) {
        std::string l = n;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        if (l != "body" && l != "head" && l != "visor")
            warn(std::format("mesh node `{}`: name the mesh nodes body, head and visor", n));
    }
    // Clips.
    static const std::set<std::string> loops{"idle", "walk", "run", "sit_idle"},
        known{"idle", "idle_fidget", "walk", "run", "turn_left", "turn_right", "look_around", "wave", "nod", "sit", "sit_idle"};
    for (auto name : {"idle", "walk"})
        if (!c.clip(name))
            error(std::format("no `{}` clip: idle and walk are required", name));
    for (auto &clip : c.clips) {
        if (!known.contains(clip.name))
            warn(std::format("clip `{}` is not in the spec (idle, idle_fidget, walk, run, turn_left, turn_right, look_around, wave, nod, sit, sit_idle): core ignores it",
                             clip.name));
        if (clip.duration <= 0) {
            error(std::format("clip `{}` has no keyframes on the skeleton", clip.name));
            continue;
        }
        // No root motion: `root` never moves; the hips don't travel horizontally.
        std::vector<Mat4> a, b;
        c.pose(&clip, 0, false, a);
        c.pose(&clip, clip.duration, false, b);
        for (auto &ch : clip.channels)
            if (ch.joint == root && root >= 0) {
                int width = ch.path == 1 ? 4 : 3;
                for (size_t k = 1; k < ch.times.size(); ++k)
                    for (int i = 0; i < width; ++i)
                        if (std::abs(ch.values[k * width + i] - ch.values[i]) > 1e-3f) {
                            error(std::format("clip `{}` animates `root`: no root motion (core moves the NPC)", clip.name));
                            k = ch.times.size();
                            break;
                        }
            }
        int hips = c.role("hips");
        if (hips >= 0) {
            // The hips joint itself: its rest global position carried by each pose's skin matrix.
            Mat4 rest = Mat4::identity();
            for (int j = hips; j >= 0; j = c.joints[j].parent)
                rest = trs(c.joints[j].translation, c.joints[j].rotation, c.joints[j].scale) * rest;
            rest = c.armature * rest;
            Vec3 head{rest.v[12], rest.v[13], rest.v[14]}, from = affineInverse(bind[hips]).point(head);
            Vec3 p0 = a[hips].point(from), p1 = b[hips].point(from);
            float travel = std::hypot(p1.x - p0.x, p1.z - p0.z);
            if (travel > .05f * std::max(1.f, height))
                error(std::format("clip `{}` moves the hips {:.2f} m: keep it in place (no root motion)", clip.name, travel));
        }
        if (loops.contains(clip.name)) {
            float worst = 0;
            for (size_t j = 0; j < a.size(); ++j)
                for (int k = 0; k < 16; ++k)
                    worst = std::max(worst, std::abs(a[j].v[k] - b[j].v[k]));
            if (worst > .02f)
                warn(std::format("clip `{}` should loop: its last pose differs from its first", clip.name));
        }
        if ((clip.name == "walk" || clip.name == "run") && clip.speed <= 0)
            info(std::format("clip `{}` has no extras.speed: core assumes {} m/s", clip.name, clip.name == "walk" ? "1.3" : "3.0"));
        // Stylised characters walk slower than people; core always plays the clip at its
        // measured (planted-foot) speed, so this only flags implausible measurements.
        if (clip.name == "walk" && clip.speed > 0 && (clip.speed < .7f || clip.speed > 1.4f))
            warn(std::format("walk speed {:.2f} m/s: the spec expects 0.7-1.4", clip.speed));
    }
    // Shared skeleton across variants.
    if (reference) {
        bool same = reference->joints.size() == c.joints.size();
        for (size_t i = 0; same && i < c.joints.size(); ++i)
            same = c.joints[i].name == reference->joints[i].name && c.joints[i].parent == reference->joints[i].parent;
        if (!same)
            error("the skeleton differs from the reference: variants must share bone names and hierarchy");
        else {
            float worst = 0;
            for (size_t i = 0; i < c.joints.size(); ++i)
                worst = std::max(worst, length(c.joints[i].translation - reference->joints[i].translation));
            if (worst > .02f)
                warn(std::format("bone positions differ from the reference by up to {:.2f} m", worst));
        }
    }
    return p;
}
} // namespace hyprune::world
