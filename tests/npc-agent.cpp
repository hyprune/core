// NPC agent (RFC-0010 prototype): route following, idling and the occasional fidget.
#include "sim/Npc.hpp"
#include <iostream>
#include <stdexcept>
using namespace hyprune;
namespace {
void check(bool value, const std::string &why) {
    if (!value)
        throw std::runtime_error(why);
}
sim::NpcAgent agent(float fidgetSeconds) {
    sim::NpcAgent a;
    a.route = {{0, 0, 0}, {0, 0, 4}};
    a.walkSpeed = 1, a.idleSeconds = 3, a.fidgetSeconds = fidgetSeconds, a.fidgetEvery = 20;
    a.start();
    return a;
}
// Runs until the agent reaches its next route point and starts idling.
void arrive(sim::NpcAgent &a, Vec3 player) {
    for (int i = 0; i < 2000 && a.wait <= 0; ++i)
        a.update(.01f, player, 1);
    check(a.wait > 0 && a.clip == "idle", "arrives and idles");
}
} // namespace
int main() {
    try {
        Vec3 far{30, 1.7f, 30}, near{2, 1.7f, 4};
        // Walks the route at the clip's speed.
        auto a = agent(0);
        a.update(.5f, far, 1);
        check(a.clip == "walk" && a.position.z > .45f && a.position.z < .55f, "walks at walkSpeed");
        // Without an idle_fidget clip it never fidgets.
        a = agent(0);
        a.sinceFidget = 100;
        arrive(a, near);
        for (int i = 0; i < 200; ++i)
            a.update(.01f, near, 1), check(a.clip != "idle_fidget", "no fidget clip, no fidget");
        // Player far: idles plainly even when a fidget is due.
        a = agent(2);
        a.sinceFidget = 100;
        arrive(a, far);
        for (int i = 0; i < 200; ++i)
            a.update(.01f, far, 1), check(a.clip != "idle_fidget", "no fidget with nobody near");
        // Player near and a fidget due: plays it through once, holds the stop for it, then idles.
        a = agent(2);
        a.sinceFidget = 100;
        arrive(a, near);
        a.update(.01f, near, 1);
        check(a.clip == "idle_fidget" && a.wait >= 1.9f, "fidgets once when the player is near");
        float played = 0;
        while (a.clip == "idle_fidget" && played < 5)
            a.update(.01f, near, 1), played += .01f;
        check(played > 1.6f && played < 1.9f && a.clip == "idle" && a.wait > 0, "plays the fidget through, then idles");
        // Rarely: not again within fidgetEvery.
        for (int i = 0; i < 1500; ++i) {
            a.update(.01f, near, 1);
            check(a.clip != "idle_fidget", "at most one fidget per fidgetEvery");
        }
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
    std::cout << "PASS NPC agent: walks at clip speed, idles at points, fidgets rarely and only with the player near\n";
}
