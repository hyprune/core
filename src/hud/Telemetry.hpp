#pragma once
#include "world/World.hpp"
namespace hyprune::hud {
using world::Json;
// Camera-space HUD data is authoritative simulation state, not shell projections.
// Zone bounds are authored world metres. First containing zone wins at shared edges.
inline Json zoneAt(const world::World *world, Vec3 p) {
    if (!world)
        return nullptr;
    for (auto &zone : world->manifest.value("zones", Json::array())) {
        if (!zone.contains("bounds"))
            continue;
        auto &lo = zone.at("bounds").at("min"), &hi = zone.at("bounds").at("max");
        if (p.x < lo[0].get<float>() || p.y < lo[1].get<float>() || p.z < lo[2].get<float>() ||
            p.x > hi[0].get<float>() || p.y > hi[1].get<float>() || p.z > hi[2].get<float>())
            continue;
        std::string name = zone.value("name", zone.at("id").get<std::string>());
        for (auto &area : world->areas)
            if (area.at("id") == zone.at("id"))
                name = area.value("name", name);
        return {{"id", zone.at("id")}, {"homeName", name}, {"worldId", world->id}, {"worldName", world->name}};
    }
    return nullptr;
}
inline Json frame(const Json &timings) {
    Json gpu = 0.;
    for (auto &pass : timings.at("gpuPassMs")) {
        if (pass.is_null()) {
            gpu = nullptr;
            break;
        }
        gpu = gpu.get<double>() + pass.get<double>();
    }
    return {{"fps", timings.at("deliveredFPS")}, {"frameMs", timings.at("frameMs")},
            {"cpuMs", timings.at("cpuRenderMs").get<double>() + timings.at("cpuUpdateMs").get<double>()},
            {"gpuMs", gpu}, {"renderScale", timings.at("renderScale")},
            {"preset", timings.at("graphics").value("quality", "auto")}};
}
} // namespace hyprune::hud
