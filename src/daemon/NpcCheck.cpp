// hyprune-npc-check: check an NPC character (.glb) against the artist spec (RFC-0010).
#include "world/Character.hpp"
#include <format>
#include <iostream>
#include <optional>
int main(int argc, char **argv) {
    using namespace hyprune::world;
    std::string file, against;
    bool json = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--json")
            json = true;
        else if (a == "--against" && i + 1 < argc)
            against = argv[++i];
        else if (file.empty() && !a.starts_with("--"))
            file = a;
        else {
            std::cerr << "usage: hyprune-npc-check CHARACTER.glb [--against BASE.glb] [--json]\n";
            return 2;
        }
    }
    if (file.empty()) {
        std::cerr << "usage: hyprune-npc-check CHARACTER.glb [--against BASE.glb] [--json]\n";
        return 2;
    }
    try {
        auto c = loadCharacter(file);
        std::optional<Character> base;
        if (!against.empty())
            base = loadCharacter(against);
        auto problems = checkCharacter(c, base ? &*base : nullptr);
        int errors = 0;
        Json list = Json::array();
        for (auto &p : problems) {
            const char *level = p.level == CharacterProblem::Error ? "error" : p.level == CharacterProblem::Warning ? "warning" : "info";
            errors += p.level == CharacterProblem::Error;
            list.push_back({{"level", level}, {"message", p.message}});
        }
        Json clips = Json::array();
        for (auto &clip : c.clips)
            clips.push_back({{"name", clip.name}, {"seconds", clip.duration}, {"speed", clip.speed}});
        Json summary{{"file", file},          {"bones", c.joints.size()},       {"triangles", c.triangles},
                     {"materials", c.materialNames}, {"height", c.hi.y - c.lo.y}, {"clips", clips},
                     {"problems", list},      {"ok", errors == 0}};
        if (json)
            std::cout << summary.dump(2) << '\n';
        else {
            std::cout << file << ": " << c.joints.size() << " bones, " << c.triangles << " triangles, "
                      << c.materials.size() << " material(s), " << std::format("{:.2f}", c.hi.y - c.lo.y) << " m tall, clips:";
            for (auto &clip : c.clips)
                std::cout << ' ' << clip.name;
            std::cout << '\n';
            for (auto &p : list)
                std::cout << "  " << p["level"].get<std::string>() << ": " << p["message"].get<std::string>() << '\n';
            std::cout << (errors ? std::format("FAIL: {} error(s)", errors) : std::string("OK: meets the NPC character spec")) << '\n';
        }
        return errors ? 1 : 0;
    } catch (const std::exception &e) {
        std::cerr << file << ": cannot load: " << e.what() << '\n';
        return 1;
    }
}
