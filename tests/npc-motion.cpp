// Character motion policy (RFC-0010, sim/NpcMotion.hpp): the pause reason for every setting
// and world state, and which requests the user's settings refuse.
#include "sim/NpcMotion.hpp"
#include <format>
#include <iostream>
#include <stdexcept>
using namespace hyprune;
namespace {
void check(bool value, const std::string &why) {
    if (!value)
        throw std::runtime_error(why);
}
std::string describe(const sim::NpcMotionInput &in) {
    return std::format("motion {} reduced {} shown {} focus {}", in.motion, in.reducedMotion, in.shown, in.focus);
}
} // namespace
int main() {
    try {
        // The pause table: setting x reduced motion x world state.
        struct Row {
            std::string motion;
            bool reduced, shown, focus;
            std::string reason;
        };
        const Row rows[] = {
            // auto and always are the same: they animate whenever the world is shown.
            {"auto", false, true, false, ""},
            {"always", false, true, false, ""},
            {"auto", false, true, true, "focus"},
            {"always", false, true, true, "focus"},
            {"auto", false, false, false, "inactive"},
            {"always", false, false, true, "inactive"},
            // Reduced motion wins over the world state; still and off win over everything.
            {"auto", true, true, false, "reduced motion"},
            {"always", true, false, true, "reduced motion"},
            {"still", false, true, false, "still"},
            {"still", true, false, true, "still"},
            {"off", false, true, false, "off"},
            {"off", true, false, true, "off"},
        };
        for (auto &r : rows) {
            sim::NpcMotionInput in{r.motion, r.reduced, r.shown, r.focus};
            auto got = sim::npcPauseReason(in);
            check(got == r.reason, describe(in) + ": paused '" + got + "', want '" + r.reason + "'");
        }
        // Nothing else pauses them: there is no power, idle, typing or free-cursor input at all,
        // so a shown world with auto or always animates.
        for (auto *m : {"auto", "always"})
            check(sim::npcPauseReason({m, false, true, false}).empty(), std::string(m) + " animates whenever the world is shown");

        // Requests: guide (walks) and talk/waypoint/panel/companion (no walking needed).
        struct Request {
            std::string motion;
            bool reduced, walks;
            std::string refusal;
        };
        const Request requests[] = {
            {"auto", false, true, ""},
            {"always", false, true, ""},
            {"auto", false, false, ""},
            {"still", false, true, "characters are paused (motion still)"},
            {"still", false, false, ""},
            {"auto", true, true, "characters are paused (reduced motion)"},
            {"always", true, false, ""},
            {"off", false, true, "characters are paused (motion off)"},
            {"off", false, false, "characters are paused (motion off)"},
            {"off", true, false, "characters are paused (motion off)"},
        };
        for (auto &r : requests) {
            sim::NpcMotionInput in{r.motion, r.reduced, true, false};
            auto got = sim::npcRefusal(in, r.walks);
            check(got == r.refusal, describe(in) + std::format(" walks {}: refused '{}', want '{}'", r.walks, got, r.refusal));
        }
        // A world that isn't shown or a focused window pauses characters but refuses no request:
        // they carry it out once the world is shown again (core refuses interactions while a
        // window is in use on its own).
        for (bool shown : {false, true})
            for (bool focus : {false, true})
                check(sim::npcRefusal({"auto", false, shown, focus}, true).empty(), "only the user's settings refuse a guide");
    } catch (const std::exception &e) {
        std::cerr << "FAIL npc-motion: " << e.what() << "\n";
        return 1;
    }
    std::cout << "PASS npc-motion\n";
    return 0;
}
