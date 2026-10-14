#include "Motion.hpp"
#include "Retarget.hpp"
#include <algorithm>
#include <fstream>
#include <regex>
#include <set>
#include <stdexcept>
namespace hyprune::world {
namespace {
void need(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
}
bool slug(const std::string &s, size_t max = 64) {
    static const std::regex re("^[a-z0-9][a-z0-9-]*$");
    return !s.empty() && s.size() <= max && std::regex_match(s, re);
}
} // namespace
std::string motionClipName(const std::string &role, const std::string &style) {
    std::string base = role == "sit-down" ? "sit" : role == "stand-up" ? "stand" : role == "seated-idle" ? "sit_idle" : role;
    if (style.empty())
        return base;
    std::string s = style;
    std::replace(s.begin(), s.end(), '-', '_');
    return base + "_" + s;
}
std::shared_ptr<MotionPack> loadMotionPack(const std::filesystem::path &directory, const std::string &origin) {
    auto file = directory / "motion.json";
    need(std::filesystem::file_size(file) <= 256 * 1024, "motion.json is larger than 256 KiB");
    std::ifstream in(file);
    Json j = Json::parse(in);
    auto pack = std::make_shared<MotionPack>();
    pack->origin = origin;
    pack->directory = directory;
    need(j.value("motionVersion", std::string()) == "0.1", "motion.json: motionVersion 0.1 expected");
    pack->id = j.value("id", std::string());
    need(slug(pack->id), "motion.json: id must be a slug");
    pack->name = j.value("name", pack->id);
    pack->skeleton = j.at("skeleton").at("id").get<std::string>();
    need(slug(pack->skeleton), "motion.json: skeleton.id must be a slug");
    std::map<std::string, int> rigs;
    auto rigList = j.at("rigs");
    need(rigList.is_array() && !rigList.empty() && int(rigList.size()) <= motionRigs, "motion.json: 1-8 rigs");
    for (auto &r : rigList) {
        auto id = r.at("id").get<std::string>(), scene = r.at("scene").get<std::string>();
        need(slug(id) && !rigs.contains(id), "motion.json: rig ids are unique slugs");
        auto path = safePath(directory, scene);
        need(std::filesystem::file_size(path) <= 32u * 1024 * 1024, "rig " + id + ": larger than 32 MiB");
        auto rig = std::make_shared<Character>(loadCharacter(path));
        need(rig->role("hips") >= 0, "rig " + id + ": no hips bone");
        rigs[id] = int(pack->rigs.size());
        pack->rigs.push_back(rig);
    }
    auto clips = j.at("clips");
    need(clips.is_array() && !clips.empty() && int(clips.size()) <= motionClips, "motion.json: 1-128 clips");
    std::set<std::string> names, played;
    for (auto &c : clips) {
        MotionPack::Clip clip;
        clip.name = c.at("name").get<std::string>();
        need(slug(clip.name) && names.insert(clip.name).second, "motion.json: clip names are unique slugs");
        clip.role = c.at("role").get<std::string>();
        need(std::find(motionRoles.begin(), motionRoles.end(), clip.role) != motionRoles.end(),
             "clip " + clip.name + ": unknown role " + clip.role);
        clip.style = c.value("style", std::string());
        need(clip.style.empty() || slug(clip.style, 32), "clip " + clip.name + ": style must be a slug");
        need(clip.role != "dance" || !clip.style.empty(), "clip " + clip.name + ": a dance needs a style");
        need(played.insert(motionClipName(clip.role, clip.style)).second,
             "clip " + clip.name + ": another clip already has role " + clip.role + (clip.style.empty() ? "" : " style " + clip.style));
        clip.loop = c.value("loop", true);
        auto rig = c.at("rig").get<std::string>();
        need(rigs.contains(rig), "clip " + clip.name + ": unknown rig " + rig);
        clip.rig = rigs[rig];
        auto source = c.value("clip", clip.name);
        clip.source = pack->rigs[clip.rig]->clip(source);
        need(clip.source, "clip " + clip.name + ": rig " + rig + " has no animation " + source);
        pack->clips.push_back(clip);
    }
    return pack;
}
std::vector<std::shared_ptr<MotionPack>> findMotionPacks(const std::vector<std::pair<std::filesystem::path, std::string>> &roots,
                                                         std::vector<std::string> *problems) {
    std::vector<std::shared_ptr<MotionPack>> out;
    std::set<std::string> ids;
    for (auto &[root, origin] : roots) {
        std::error_code ec;
        auto dir = root / "motion";
        if (!std::filesystem::is_directory(dir, ec))
            continue;
        std::vector<std::filesystem::path> packs;
        for (auto &e : std::filesystem::directory_iterator(dir, ec))
            if (e.is_directory() && std::filesystem::is_regular_file(e.path() / "motion.json", ec))
                packs.push_back(e.path());
        std::sort(packs.begin(), packs.end());
        for (auto &p : packs) {
            if (int(out.size()) >= motionPacksMax)
                break;
            try {
                auto pack = loadMotionPack(p, origin);
                if (ids.insert(pack->id).second) // the first root's pack of an id wins
                    out.push_back(pack);
            } catch (const std::exception &e) {
                if (problems)
                    problems->push_back(p.string() + ": " + e.what());
            }
        }
    }
    return out;
}
std::string characterSkeleton(const Character &c, const Json &trait) {
    if (trait.contains("skeleton") && trait["skeleton"].contains("id"))
        return trait["skeleton"]["id"].get<std::string>();
    for (auto bone : {"hips", "spine", "head", "leftarm", "rightarm", "leftupleg", "leftleg", "leftfoot", "rightupleg", "rightleg",
                      "rightfoot"})
        if (c.role(bone) < 0)
            return "";
    return "mixamo-v1";
}
bool validCharacterStyle(const Json &style) {
    if (!style.is_object() || style.empty())
        return false;
    for (auto &[role, value] : style.items())
        if ((role != "walk" && role != "run" && role != "idle") || !value.is_string() || !slug(value.get<std::string>(), 32))
            return false;
    return true;
}
Json characterMotion(const Character &c, const Json &binding) {
    Json styles = {{"walk", Json::array()}, {"run", Json::array()}, {"idle", Json::array()}}, dances = Json::array();
    auto style = [](const std::string &rest) {
        std::string s = rest;
        std::replace(s.begin(), s.end(), '_', '-');
        return s;
    };
    for (auto &clip : c.clips) {
        for (auto role : {"walk", "run", "idle"}) {
            std::string prefix = std::string(role) + "_";
            if (clip.name.starts_with(prefix) && styles[role].size() < 32 && slug(style(clip.name.substr(prefix.size())), 32))
                styles[role].push_back(style(clip.name.substr(prefix.size())));
        }
        if (clip.name.starts_with("dance_") && dances.size() < 64 && slug(style(clip.name.substr(6)), 32))
            dances.push_back(style(clip.name.substr(6)));
    }
    Json packs = Json::array();
    std::set<std::string> seen;
    if (binding.is_object())
        for (auto &b : binding.value("clips", Json::array()))
            if (seen.insert(b["pack"].get<std::string>()).second && packs.size() < 16)
                packs.push_back(b["pack"]);
    Json skeleton = binding.is_object() ? binding.value("skeleton", Json(nullptr)) : Json(nullptr);
    return {{"skeleton", skeleton}, {"styles", styles}, {"dances", dances}, {"packs", packs}};
}
MotionBinding bindMotion(Character &c, const Json &trait, const std::vector<std::shared_ptr<MotionPack>> &packs) {
    MotionBinding binding;
    binding.skeleton = characterSkeleton(c, trait);
    if (binding.skeleton.empty())
        return binding;
    std::map<std::string, std::string> aliases;
    if (trait.contains("skeleton") && trait["skeleton"].contains("aliases"))
        for (auto &[key, name] : trait["skeleton"]["aliases"].items())
            aliases[key] = name.get<std::string>();
    for (auto &pack : packs) {
        if (pack->skeleton != binding.skeleton)
            continue;
        for (auto &clip : pack->clips) {
            auto name = motionClipName(clip.role, clip.style);
            if (c.clip(name))
                continue; // embedded (or an earlier pack's) clip wins
            auto moved = retargetClip(c, *pack->rigs[clip.rig], *clip.source, aliases);
            moved.name = name;
            c.clips.push_back(std::move(moved));
            binding.clips.push_back({name, clip.role, clip.style, pack->id});
        }
    }
    return binding;
}
} // namespace hyprune::world
