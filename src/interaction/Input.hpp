#pragma once
#include "world/World.hpp"
#include <deque>
#include <map>
struct xkb_state;
namespace hyprune::interaction {
using Json = world::Json;
enum Mod : unsigned { Shift = 1, Ctrl = 4, Alt = 8, Super = 64 };
struct Trigger {
    std::string key;
    unsigned mods = 0;
    bool hold = false;
};
Trigger parseBinding(const std::string &);
std::string bindingKey(xkb_state *, uint32_t keycode);
std::string bindingName(const Trigger &);
Json inputDefaults();
Json validateInput(const Json &);
// Project canonical action names into frozen pre-0.9 responses (including capture/conflict metadata).
Json legacyInputNames(Json);
Json inputConflicts(const Json &);
struct Action {
    std::string name;
    bool pressed;
    float value = 0;
    bool edge = true;
};
// A consumed press owns its release regardless of a mode switch/reload. Input
// matching is pure; native keybind dispatch and all effects belong to the adapter.
class Input {
  public:
    Json config = inputDefaults();
    std::deque<Json> recent;
    std::vector<Action> event(const std::string &mode, const std::string &key, unsigned mods, bool down, uint64_t now,
                              float value = 0, bool gestureMovement = false);
    bool gestureHeld() const;
    Json holds() const;
    std::vector<Action> tick(uint64_t now);
    void apply(const Json &);
    void clear();

  private:
    struct Press {
        std::string action, binding;
        uint64_t started;
        bool hold, fired;
    };
    std::map<std::string, Press> held;
};
} // namespace hyprune::interaction
