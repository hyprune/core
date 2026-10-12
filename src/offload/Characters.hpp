#pragma once
// Characters for the offload renderer (RFC-0009, RFC-0010): the worker loads character
// assets with the package and catalog assets (Objects.hpp), and every pose carries each
// drawn character's pose inputs (asset, model, clips, times, crossfade, look-at, accent,
// probe light), so the worker evaluates the same skinning (Character::evaluate) as the
// in-process renderer. Internal to core: not part of the IPC contract.
#include "render/Renderer.hpp"
#include "world/Character.hpp"
#include "world/World.hpp"
#include <json.hpp>
namespace hyprune::offload {
inline nlohmann::json characterRecord(unsigned asset, const Mat4 &model, const world::Character::PoseInput &in,
                                      const std::optional<Vec3> &accent, const std::array<float, 8> &probe, bool moving) {
    using Json = nlohmann::json;
    auto name = [](const world::Character::Clip *c) { return c ? Json(c->name) : Json(nullptr); };
    return Json{{"asset", asset},
                {"model", model.v},
                {"from", name(in.from)},
                {"fromTime", in.fromTime},
                {"fromLoop", in.fromLoop},
                {"to", name(in.to)},
                {"toTime", in.toTime},
                {"toLoop", in.toLoop},
                {"weight", in.weight},
                {"look", {in.lookYaw, in.lookPitch}},
                {"accent", accent ? Json{accent->x, accent->y, accent->z} : Json(nullptr)},
                {"probe", probe},
                {"moving", moving}};
}
// Worker side: the renderer's character list from a pose's records (unknown assets and
// clips are skipped).
inline void applyCharacters(const nlohmann::json &records, const world::World &w, render::Renderer &r) {
    r.npcs.clear();
    if (!records.is_array())
        return;
    for (auto &c : records) {
        size_t asset = c.at("asset");
        if (asset >= w.objectAssets.size() || !w.objectAssets[asset].character || r.npcs.size() >= 64)
            continue;
        auto &ch = w.objectAssets[asset].character;
        world::Character::PoseInput in;
        auto clip = [&](const nlohmann::json &name) { return name.is_string() ? ch->clip(name.get<std::string>()) : nullptr; };
        in.from = clip(c.at("from"));
        in.to = clip(c.at("to"));
        if (!in.to)
            in.to = ch->clip("idle");
        in.fromTime = c.at("fromTime"), in.toTime = c.at("toTime");
        in.fromLoop = c.at("fromLoop"), in.toLoop = c.at("toLoop");
        in.weight = c.at("weight");
        in.lookYaw = c.at("look").at(0), in.lookPitch = c.at("look").at(1);
        render::Renderer::Npc npc;
        npc.character = ch;
        npc.model.v = c.at("model").get<std::array<float, 16>>();
        ch->evaluate(in, npc.skin);
        if (c.at("accent").is_array())
            npc.accent = Vec3{c["accent"][0], c["accent"][1], c["accent"][2]};
        npc.probe = c.at("probe").get<std::array<float, 8>>();
        npc.moving = c.at("moving");
        r.npcs.push_back(std::move(npc));
    }
}
} // namespace hyprune::offload
