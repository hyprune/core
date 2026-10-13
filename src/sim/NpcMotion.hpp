// Character motion policy (RFC-0010 "Frames only when someone is looking"): whether residents
// and the companion animate, and which requests the user's settings refuse. Pure, so the
// policy table is unit-tested (tests/npc-motion.cpp).
#pragma once
#include <string>
namespace hyprune::sim {
struct NpcMotionInput {
    std::string motion = "auto"; // npc.motion: auto, always (the same), still, off
    bool reducedMotion = false;  // graphics.reduced_motion
    bool shown = true;           // the world is active and loaded
    bool focus = false;          // the desktop layout (or a fullscreen transition) covers the world
};
// Why characters don't animate, or "" when they do. `auto` animates whenever the world is
// shown: no pause for power, idle, typing or the free cursor (render on demand and occlusion
// keep the cost to characters in view). Only the user's settings and a hidden world stop them.
inline std::string npcPauseReason(const NpcMotionInput &in) {
    return in.motion == "off" || in.motion == "still" ? in.motion
           : in.reducedMotion                         ? "reduced motion"
           : !in.shown                                ? "inactive"
           : in.focus                                 ? "focus"
                                                      : "";
}
// A request to a character (an interaction; `walks`: a guide) is refused under the user's
// settings instead of being accepted and never carried out. The refusal, or "" when it runs.
// Talk, waypoints and panels need no motion: still and reduced motion keep them.
inline std::string npcRefusal(const NpcMotionInput &in, bool walks) {
    if (in.motion == "off")
        return "characters are paused (motion off)";
    if (!walks)
        return "";
    return in.motion == "still" ? "characters are paused (motion still)"
           : in.reducedMotion   ? "characters are paused (reduced motion)"
                                : "";
}
} // namespace hyprune::sim
