// hyprune-npc-check: check an NPC character (.glb) against the artist spec (RFC-0010).
#include "world/Character.hpp"
#include "world/Motion.hpp"
#include "world/Retarget.hpp"
#include <format>
#include <iostream>
#include <optional>
int main(int argc, char **argv) {
    using namespace hyprune::world;
    std::string file, against;
    std::vector<std::string> motionDirs; // --motion DIR: motion packs bound onto it (shared clip library)
    bool json = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--json")
            json = true;
        else if (a == "--against" && i + 1 < argc)
            against = argv[++i];
        else if (a == "--motion" && i + 1 < argc)
            motionDirs.push_back(argv[++i]);
        else if (file.empty() && !a.starts_with("--"))
            file = a;
        else {
            std::cerr << "usage: hyprune-npc-check CHARACTER.glb [--against BASE.glb] [--motion PACK_DIR]... [--json]\n";
            return 2;
        }
    }
    if (file.empty()) {
        std::cerr << "usage: hyprune-npc-check CHARACTER.glb [--against BASE.glb] [--motion PACK_DIR]... [--json]\n";
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
        // Motion packs: the clips they fill on this character, foot slip of the bound locomotion
        // clips (in place, at their scaled speed), and the seat fit below with a bound sit.
        if (!motionDirs.empty()) {
            std::vector<std::shared_ptr<MotionPack>> packs;
            for (auto &d : motionDirs)
                packs.push_back(loadMotionPack(d, "check"));
            auto binding = bindMotion(c, Json::object(), packs);
            Json bound = Json::array();
            for (auto &b : binding.clips) {
                auto *clip = c.clip(b.name);
                float slip = clip && (b.role == "walk" || b.role == "run") ? footSlip(c, *clip) : -1;
                bound.push_back({{"name", b.name}, {"role", b.role}, {"pack", b.pack}, {"speed", clip ? clip->speed : 0},
                                 {"footSlip", slip < 0 ? Json(nullptr) : Json(std::round(slip * 1000) / 1000)}});
            }
            summary["motion"] = {{"skeleton", binding.skeleton.empty() ? Json(nullptr) : Json(binding.skeleton)}, {"bound", bound}};
            if (!json) {
                std::cout << "motion: skeleton " << (binding.skeleton.empty() ? "none" : binding.skeleton) << ", " << bound.size() << " clips bound\n";
                for (auto &b : bound)
                    std::cout << "  " << b["name"].get<std::string>() << " (" << b["pack"].get<std::string>() << ")"
                              << (b["footSlip"].is_null() ? std::string() : std::format(", speed {:.2f} m/s, foot slip {:.1f} %", b["speed"].get<float>(), b["footSlip"].get<float>() * 100))
                              << '\n';
            }
        }
        // The sit clip's seat height (RFC-0011 clips.sit.seatHeight), measured as core does.
        auto seat = c.measureSeat();
        summary["seatHeight"] = seat.valid ? Json(std::round(seat.seatHeight * 1000) / 1000) : Json(nullptr);
        summary["sitSink"] = seat.valid ? Json(std::round(seat.sink * 1000) / 1000) : Json(nullptr);
        if (json)
            std::cout << summary.dump(2) << '\n';
        else {
            std::cout << file << ": " << c.joints.size() << " bones, " << c.triangles << " triangles, "
                      << c.materials.size() << " material(s), " << std::format("{:.2f}", c.hi.y - c.lo.y) << " m tall, clips:";
            for (auto &clip : c.clips)
                std::cout << ' ' << clip.name;
            std::cout << '\n';
            if (seat.valid)
                std::cout << "  sit: seat height " << std::format("{:.3f}", seat.seatHeight) << " m, ankles sink "
                          << std::format("{:.3f}", seat.sink) << " m below idle\n";
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
