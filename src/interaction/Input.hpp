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
inline bool freeCursorToggle(const std::string &key, unsigned mods) {
    return key == "Back" || (key == "Tab" && mods == Super);
}
Json inputDefaults();
Json validateInput(const Json &);
// Project canonical action names into frozen pre-0.9 responses (including capture/conflict metadata).
Json legacyInputNames(Json);
inline Json beforePlacementInput(Json j) {
    if (j.is_object()) {
        if (j.contains("cycle")) {
            j["wheel"] = j["cycle"];
            j.erase("cycle");
        }
        for (auto key : {"tool5", "carry.facing", "content.scale", "window.reset", "alternate"})
            j.erase(key);
        for (auto &v : j)
            v = beforePlacementInput(std::move(v));
    } else if (j.is_array())
        for (auto &v : j)
            v = beforePlacementInput(std::move(v));
    if (j == "cycle")
        j = "wheel";
    return j;
}
inline Json withoutRecordingAction(Json j) {
    if (j.is_object()) {
        j.erase("record.toggle");
        if (j.contains("modes") && j["modes"].contains("world")) j["modes"]["world"].erase("exit");
        for (auto &v : j) v = withoutRecordingAction(std::move(v));
    } else if (j.is_array()) for (auto &v : j) v = withoutRecordingAction(std::move(v));
    return j;
}
// IPC 0.21 added the world action markers.reveal (TAB tap): older peers never see it.
inline Json withoutMarkersReveal(Json j) {
    if (j.is_object()) {
        j.erase("markers.reveal");
        if (j.contains("conflicts") && j["conflicts"].is_array())
            std::erase_if(j["conflicts"].get_ref<Json::array_t &>(), [](const Json &c) {
                return c.is_object() && c.contains("actions") &&
                       std::find(c["actions"].begin(), c["actions"].end(), "markers.reveal") != c["actions"].end();
            });
        for (auto &v : j)
            v = withoutMarkersReveal(std::move(v));
    } else if (j.is_array())
        for (auto &v : j)
            v = withoutMarkersReveal(std::move(v));
    return j;
}
// Tool 9 (the debug tool) as peers before IPC 0.26 see the keymap: no tool9 action, no
// conflict or Hyprland collision naming it.
inline Json withoutDebugTool(Json j) {
    if (j.is_object()) {
        j.erase("tool9");
        for (auto key : {"conflicts", "steals"})
            if (j.contains(key) && j[key].is_array())
                std::erase_if(j[key].get_ref<Json::array_t &>(), [](const Json &c) {
                    if (!c.is_object())
                        return false;
                    if (c.value("action", Json()) == "tool9")
                        return true;
                    return c.contains("actions") && c["actions"].is_array() &&
                           std::find(c["actions"].begin(), c["actions"].end(), "tool9") != c["actions"].end();
                });
        for (auto &v : j)
            v = withoutDebugTool(std::move(v));
    } else if (j.is_array())
        for (auto &v : j)
            v = withoutDebugTool(std::move(v));
    return j;
}
// IPC 0.22 editor deltas (RFC-0011 P1) as older peers see them: scene nodes are
// "object" selections, the universal gizmo mode reads "move", no gizmo state, and the
// pocket lists windows only (a selected object item reads as the first window or null).
inline Json beforeObjectsEditor(Json editor) {
    if (editor.value("selectedKind", "") == "node")
        editor["selectedKind"] = "object";
    if (editor.value("submode", "") == "universal")
        editor["submode"] = "move";
    editor.erase("gizmo");
    if (editor.contains("unplaced") && editor["unplaced"].is_object()) {
        auto &u = editor["unplaced"];
        u.erase("objects");
        auto windows = u.value("windows", Json::array());
        u["count"] = windows.size();
        auto selected = u.value("selectedId", Json(nullptr));
        bool window = std::any_of(windows.begin(), windows.end(), [&](const Json &w) { return w.value("surfaceId", Json()) == selected; });
        if (!window)
            u["selectedId"] = windows.empty() ? Json(nullptr) : windows[0]["surfaceId"];
        if (windows.empty())
            u["preview"] = false;
    }
    return editor;
}
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
    // `mode`: the current input mode; a held tap/hold pair started in another mode fires nothing.
    std::vector<Action> tick(uint64_t now, const std::string &mode = "");
    void apply(const Json &);
    void clear();

  private:
    struct Press {
        std::string action, binding;
        uint64_t started;
        bool hold, fired;
        // Tap/hold pair (RFC-0012): `action` is the hold action, `tap` the plain one.
        std::string tap = "", mode = "";
    };
    std::map<std::string, Press> held;
};
} // namespace hyprune::interaction
