// Character occlusion (world/Occlusion.hpp): rays from the eye to a character's bounds
// against opaque scene geometry only; conservative (one clear ray: visible).
#include "world/Occlusion.hpp"
#include <iostream>
#include <stdexcept>
using namespace hyprune;
namespace {
void check(bool value, const std::string &why) {
    if (!value)
        throw std::runtime_error(why);
}
// An axis-aligned quad in the plane z = z0 from (x0, y0) to (x1, y1), two triangles.
world::Mesh quad(float x0, float x1, float y0, float y1, float z0, unsigned material) {
    world::Mesh m;
    m.material = material;
    auto v = [](float x, float y, float z) {
        world::Vertex out;
        out.position = {x, y, z};
        return out;
    };
    m.vertices = {v(x0, y0, z0), v(x1, y0, z0), v(x1, y1, z0), v(x0, y0, z0), v(x1, y1, z0), v(x0, y1, z0)};
    return m;
}
} // namespace
int main() {
    try {
        world::World w;
        w.materials.resize(2);
        w.materials[1].alphaMode = 2, w.materials[1].alpha = .3f; // glass
        w.meshes = {quad(-2, 2, 0, 4, -5, 0),                     // opaque wall 4 m wide, 4 m high, at z -5
                    quad(4, 8, 0, 4, -5, 1),                      // a glass pane beside it
                    quad(10, 14, 0, 1, -5, 0)};                   // a low opaque wall, 1 m high
        check(!world::characterOccluded(w, {0, 1.7f, 0}, Mat4::identity(), {-.3f, 0, -10.3f}, {.3f, 1.8f, -9.7f}),
              "no occluders built: nothing is hidden");
        world::buildOccluders(w);
        check(w.occluderTriangles.size() == 4, "only the opaque meshes occlude (glass is left out)");
        check(world::opaqueMesh(w, w.meshes[0]) && !world::opaqueMesh(w, w.meshes[1]),
              "opaqueMesh: alpha mode and alpha");
        Vec3 eye{0, 1.7f, 0}, lo{-.3f, 0, -.3f}, hi{.3f, 1.8f, .3f};
        auto at = [](float x, float z) {
            auto m = Mat4::identity();
            m.v[12] = x, m.v[14] = z;
            return m;
        };
        check(world::characterOccluded(w, eye, at(0, -10), lo, hi), "behind the wall: every ray blocked");
        check(!world::characterOccluded(w, eye, at(5, -10), lo, hi), "beside the wall (rays pass its edge): visible");
        check(!world::characterOccluded(w, {0, 1.7f, 3}, at(2.8f, -10), lo, hi),
              "seen head-on near the wall's edge: one side ray passes, so it is drawn (conservative)");
        check(!world::characterOccluded(w, {6, 1.7f, 0}, at(6, -10), lo, hi), "behind glass: visible");
        check(!world::characterOccluded(w, {12, 1.7f, 0}, at(12, -10), lo, hi), "behind a low wall: the head shows");
        check(!world::characterOccluded(w, eye, at(0, -.2f), lo, hi), "the eye inside its bounds: not hidden");
        // A moved wall (a tool committed its node): the occluders follow the transform.
        w.meshes[0].node = 7;
        auto moved = Mat4::identity();
        moved.v[12] = 20;
        world::buildOccluders(w, {{7, moved}});
        check(!world::characterOccluded(w, eye, at(0, -10), lo, hi), "the wall moved away: visible again");
        check(world::characterOccluded(w, {20, 1.7f, 0}, at(20, -10), lo, hi),
              "and hides what is behind its new place");
        // Hits near the eye or the character never hide it (if in doubt, draw).
        world::World near;
        near.materials.resize(1);
        near.meshes = {quad(-3, 3, -1, 5, -.2f, 0),   // a wall 0.2 m in front of the eye
                       quad(17, 23, -1, 5, -9.6f, 0)}; // a wall the character stands against
        world::buildOccluders(near);
        check(!world::characterOccluded(near, eye, at(0, -10), lo, hi), "a wall at the eye (near plane): drawn");
        check(!world::characterOccluded(near, {20, 1.7f, 0}, at(20, -10), lo, hi),
              "a wall within 0.3 m of the character: drawn");
        check(world::characterOccluded(near, {20, 1.7f, 0}, at(20, -10.8f), lo, hi), "a step further back it hides");
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
    std::cout << "PASS character occlusion: opaque occluders, all rays blocked, glass, low walls, edges, moved nodes, near hits\n";
}
