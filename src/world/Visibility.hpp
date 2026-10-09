#pragma once
#include "World.hpp"
#include <set>
#include <stdexcept>
namespace hyprune::world {
// Conservative authored visibility: outside all regions, draw everything.
// Regions are independent of workspace homes. Unassigned nodes are always drawn.
inline void loadVisibility(World &w) {
    w.visibility.clear();
    const auto areas = w.manifest.value("visibility", Json::object()).value("areas", Json::array());
    std::map<std::string,int> ids;
    for (const auto &a : areas) {
        int id = ids.size();
        if (!ids.emplace(a.at("id"),id).second) throw std::runtime_error("duplicate visibility area");
        auto lo=a.at("bounds").at("min"), hi=a.at("bounds").at("max");
        for (int i=0;i<3;++i) if (lo[i]>=hi[i]) throw std::runtime_error("inverted visibility bounds");
        w.visibility.push_back({{lo[0],lo[1],lo[2]},{hi[0],hi[1],hi[2]}, {id}});
    }
    w.nodeVisibility.assign(w.nodeParents.size(),-1);
    for (size_t i=0;i<areas.size();++i) {
        for (const auto &source:areas[i].at("visibleFrom")) {
            if (!ids.contains(source)) throw std::runtime_error("unknown visibility source");
            w.visibility[i].from.push_back(ids.at(source));
        }
        for (auto node:areas[i].at("nodes")) {
            auto n=node.get<size_t>();
            if (n>=w.nodeVisibility.size() || w.nodeVisibility[n]!=-1) throw std::runtime_error("invalid visibility node");
            w.nodeVisibility[n]=i;
        }
    }
    // Root assignment applies to descendants. Reject conflicting nested owners.
    const auto roots=w.nodeVisibility;
    for (size_t n=0;n<roots.size();++n) {
        int owner=roots[n];
        for (int parent=w.nodeParents[n];parent>=0;parent=w.nodeParents[parent])
            if (roots[parent]>=0) {
                if (owner>=0 && owner!=roots[parent]) throw std::runtime_error("conflicting visibility subtree");
                owner=roots[parent];
            }
        w.nodeVisibility[n]=owner;
    }
}
inline std::vector<bool> visibleAreas(const World &w, Vec3 eye) {
    std::set<int> current;
    for (size_t i=0;i<w.visibility.size();++i) {
        const auto &a=w.visibility[i];
        if (eye.x>=a.min.x && eye.x<=a.max.x && eye.y>=a.min.y && eye.y<=a.max.y && eye.z>=a.min.z && eye.z<=a.max.z)
            current.insert(i);
    }
    std::vector<bool> result(w.visibility.size(),current.empty());
    for (size_t i=0;i<result.size();++i)
        for (auto id:w.visibility[i].from) if (current.contains(id)) result[i]=true;
    return result;
}
inline bool nodeVisible(const World &w, unsigned node, const std::vector<bool> &visible) {
    return node>=w.nodeVisibility.size() || w.nodeVisibility[node]<0 || visible[w.nodeVisibility[node]];
}
} // namespace hyprune::world
