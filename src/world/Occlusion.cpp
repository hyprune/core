#include "Occlusion.hpp"
#include "interaction/Hands.hpp"
#include <cmath>
namespace hyprune::world {
bool opaqueMesh(const World &w, const Mesh &mesh) {
    if (mesh.material >= w.materials.size())
        return false;
    auto &m = w.materials[mesh.material];
    return m.alphaMode == 0 && m.alpha >= 1;
}
void buildOccluders(World &w, const std::map<unsigned, Mat4> &transforms) {
    w.occluderTriangles.clear();
    for (const auto &mesh : w.meshes) {
        if (!opaqueMesh(w, mesh))
            continue;
        auto it = transforms.find(mesh.node);
        auto matrix = it == transforms.end() ? Mat4::identity() : it->second;
        for (size_t i = 0; i + 2 < mesh.vertices.size(); i += 3)
            w.occluderTriangles.push_back({matrix.point(mesh.vertices[i].position),
                                           matrix.point(mesh.vertices[i + 1].position),
                                           matrix.point(mesh.vertices[i + 2].position)});
    }
    w.occluderIndex.rebuild(w.occluderTriangles);
}
bool characterOccluded(const World &w, Vec3 eye, const Mat4 &model, Vec3 lo, Vec3 hi, float margin) {
    if (w.occluderTriangles.empty() || w.occluderIndex.size() != w.occluderTriangles.size())
        return false;
    Vec3 low = model.point(lo), high = model.point(hi);
    Vec3 a{std::min(low.x, high.x), std::min(low.y, high.y), std::min(low.z, high.z)};
    Vec3 b{std::max(low.x, high.x), std::max(low.y, high.y), std::max(low.z, high.z)};
    Vec3 centre = (a + b) * .5f;
    // Sideways from the eye's point of view: the widest the character can appear.
    Vec3 view{centre.x - eye.x, 0, centre.z - eye.z};
    float flat = std::hypot(view.x, view.z);
    if (flat < .5f)
        return false; // standing in it
    Vec3 side{-view.z / flat, 0, view.x / flat};
    float half = std::max(b.x - a.x, b.z - a.z) * .5f + margin;
    std::array<Vec3, 5> points{centre, Vec3{centre.x, b.y + margin * .5f, centre.z},
                               Vec3{centre.x, a.y + .1f, centre.z}, centre + side * half, centre - side * half};
    // Hits this close to the eye or the character, or inside its bounds grown by as much, are
    // not trusted to hide it (the near plane, a surface it touches, its own floor).
    constexpr float near = .3f;
    Vec3 ga = a - Vec3{near, near, near}, gb = b + Vec3{near, near, near};
    auto inside = [&](Vec3 p) {
        return p.x >= ga.x && p.x <= gb.x && p.y >= ga.y && p.y <= gb.y && p.z >= ga.z && p.z <= gb.z;
    };
    for (auto p : points) {
        Vec3 d = p - eye;
        float distance = length(d);
        if (distance < 2 * near)
            return false;
        auto hit = interaction::raycast(eye, d * (1 / distance), w.occluderTriangles, &w.occluderIndex);
        if (!hit || hit->distance < near || hit->distance > distance - near || inside(hit->point))
            return false;
    }
    return true;
}
} // namespace hyprune::world
