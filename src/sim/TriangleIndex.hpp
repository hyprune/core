#pragma once
#include "Math.hpp"
#include <limits>
#include <numeric>
#include <span>
#include <vector>
namespace hyprune::sim {
// Immutable, pointer-free broad phase. Copies remain valid; callers rebuild after
// changing positions/topology. Original triangle order preserves contact/tie rules.
class TriangleIndex {
    struct Box {
        Vec3 low{INFINITY, INFINITY, INFINITY}, high{-INFINITY, -INFINITY, -INFINITY};
        void add(Vec3 p) {
            low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
            high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
        }
        bool overlaps(Vec3 a, Vec3 b) const {
            return low.x <= b.x && high.x >= a.x && low.y <= b.y && high.y >= a.y && low.z <= b.z && high.z >= a.z;
        }
        bool ray(Vec3 origin, Vec3 direction) const {
            float near = 0, far = INFINITY;
            const float o[]{origin.x, origin.y, origin.z}, d[]{direction.x, direction.y, direction.z};
            const float lo[]{low.x, low.y, low.z}, hi[]{high.x, high.y, high.z};
            for (int a = 0; a < 3; ++a) {
                if (std::abs(d[a]) < 1e-12f) {
                    if (o[a] < lo[a] || o[a] > hi[a])
                        return false;
                } else {
                    float x = (lo[a] - o[a]) / d[a], y = (hi[a] - o[a]) / d[a];
                    near = std::max(near, std::min(x, y));
                    far = std::min(far, std::max(x, y));
                    if (near > far)
                        return false;
                }
            }
            return true;
        }
    };
    struct Node {
        Box bounds;
        unsigned begin = 0, end = 0, left = 0, right = 0;
    };
    std::vector<Node> nodes;
    std::vector<unsigned> order;
    unsigned build(std::span<const Triangle> mesh, unsigned begin, unsigned end) {
        unsigned id = nodes.size();
        nodes.emplace_back();
        Node n;
        n.begin = begin;
        n.end = end;
        for (unsigned i = begin; i < end; ++i) {
            auto &t = mesh[order[i]];
            n.bounds.add(t.a);
            n.bounds.add(t.b);
            n.bounds.add(t.c);
        }
        if (end - begin > 8) {
            auto extent = n.bounds.high - n.bounds.low;
            int axis = extent.y > extent.x ? 1 : 0;
            if (extent.z > (axis == 1 ? extent.y : extent.x))
                axis = 2;
            auto centroid = [&](unsigned i) {
                auto &t = mesh[i];
                auto c = t.a + t.b + t.c;
                return axis == 0 ? c.x : axis == 1 ? c.y : c.z;
            };
            unsigned mid = begin + (end - begin) / 2;
            std::nth_element(order.begin() + begin, order.begin() + mid, order.begin() + end,
                             [&](unsigned a, unsigned b) { return centroid(a) < centroid(b); });
            n.left = build(mesh, begin, mid);
            n.right = build(mesh, mid, end);
        }
        nodes[id] = n;
        return id;
    }
    template <class Test> std::vector<unsigned> query(Test test) const {
        std::vector<unsigned> result;
        if (nodes.empty())
            return result;
        std::vector<unsigned> stack{0};
        while (!stack.empty()) {
            auto id = stack.back();
            stack.pop_back();
            auto &n = nodes[id];
            if (!test(n.bounds))
                continue;
            if (n.left) {
                stack.push_back(n.right);
                stack.push_back(n.left);
            } else
                result.insert(result.end(), order.begin() + n.begin, order.begin() + n.end);
        }
        std::sort(result.begin(), result.end());
        return result;
    }

  public:
    TriangleIndex() = default;
    explicit TriangleIndex(std::span<const Triangle> mesh) { rebuild(mesh); }
    void rebuild(std::span<const Triangle> mesh) {
        nodes.clear();
        order.resize(mesh.size());
        std::iota(order.begin(), order.end(), 0u);
        if (!mesh.empty())
            build(mesh, 0, mesh.size());
    }
    size_t size() const { return order.size(); }
    std::vector<unsigned> overlap(Vec3 low, Vec3 high) const {
        return query([&](const Box &b) { return b.overlaps(low, high); });
    }
    std::vector<unsigned> ray(Vec3 o, Vec3 d) const {
        return query([&](const Box &b) { return b.ray(o, d); });
    }
};
} // namespace hyprune::sim
