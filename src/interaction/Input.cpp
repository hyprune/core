#include "Input.hpp"
#include "InputDefaults.hpp"
#include <set>
#include <xkbcommon/xkbcommon.h>
namespace hyprune::interaction {
std::string bindingKey(xkb_state *state, uint32_t keycode) {
    const xkb_keysym_t *symbols = nullptr;
    auto count = xkb_keymap_key_get_syms_by_level(xkb_state_get_keymap(state), keycode,
                                                  xkb_state_key_get_layout(state, keycode), 0, &symbols);
    auto symbol = xkb_keysym_to_lower(count ? symbols[0] : xkb_state_key_get_one_sym(state, keycode));
    char name[128];
    return xkb_keysym_get_name(symbol, name, sizeof name) > 0 ? name : "";
}
Trigger parseBinding(const std::string &s) {
    if (s.empty() || s.size() > 96)
        throw std::runtime_error("binding must be 1–96 bytes");
    Trigger t;
    std::string text = s;
    if (text.ends_with("[L]")) {
        t.hold = true;
        text.resize(text.size() - 3);
    }
    size_t start = 0;
    for (;;) {
        auto end = text.find('+', start);
        auto part = text.substr(start, end - start);
        if (end == std::string::npos) {
            t.key = part;
            break;
        }
        unsigned m = part == "Super"   ? Super
                     : part == "Shift" ? Shift
                     : part == "Ctrl"  ? Ctrl
                     : part == "Alt"   ? Alt
                                       : 0;
        if (!m || (t.mods & m))
            throw std::runtime_error("invalid or duplicate modifier");
        t.mods |= m;
        start = end + 1;
    }
    static const std::set<std::string> mouse{"LMB",   "RMB",     "MMB",       "Back",     "Forward",
                                             "Wheel", "WheelUp", "WheelDown", "MouseMove"};
    if (!mouse.contains(t.key) && !t.key.starts_with("Button")) {
        auto sym = xkb_keysym_from_name(t.key.c_str(), XKB_KEYSYM_CASE_INSENSITIVE);
        if (sym == XKB_KEY_NoSymbol)
            throw std::runtime_error("unknown key: " + t.key);
        sym = xkb_keysym_to_lower(sym);
        char name[128];
        xkb_keysym_get_name(sym, name, sizeof name);
        t.key = name;
    }
    if (t.key.starts_with("Button")) {
        auto n = t.key.substr(6);
        if (n.empty() || n.find_first_not_of("0123456789") != std::string::npos || std::stoi(n) < 1 ||
            std::stoi(n) > 767)
            throw std::runtime_error("invalid mouse button");
    }
    if (t.hold && (t.key.starts_with("Wheel") || t.key == "MouseMove"))
        throw std::runtime_error("wheel/motion cannot be long-held");
    return t;
}
std::string bindingName(const Trigger &t) {
    std::string s;
    for (auto [m, name] :
         {std::pair<unsigned, const char *>{Super, "Super"}, {Shift, "Shift"}, {Ctrl, "Ctrl"}, {Alt, "Alt"}})
        if (t.mods & m)
            s += std::string(name) + "+";
    return s + t.key + (t.hold ? "[L]" : "");
}
Json inputDefaults() {
    return Json::parse(defaultInputJSON);
}
Json legacyInputNames(Json j) {
    if (j.is_array()) {
        for (auto &v : j)
            v = legacyInputNames(std::move(v));
    } else if (j.is_object()) {
        if (j.contains("hud.cycle")) {
            if (!j.contains("tool5"))
                j["tool5"] = std::move(j["hud.cycle"]);
            j.erase("hud.cycle");
        }
        for (auto &v : j)
            v = legacyInputNames(std::move(v));
    } else if (j == "hud.cycle")
        j = "tool5";
    return j;
}
Json validateInput(const Json &raw) {
    auto j = raw;
    if (j.is_object() && j.contains("modes") && j["modes"].is_object() &&
        j["modes"].contains("world") && j["modes"]["world"].is_object()) {
        auto &w = j["modes"]["world"];
        if (w.contains("wheel")) {
            if (!w.contains("cycle"))
                w["cycle"] = w["wheel"];
            w.erase("wheel");
        }
        // Migrate the former factory binding, retaining explicitly customised HUD chords.
        if (!w.contains("tool5")) {
            w["tool5"] = Json::array({"5"});
            if (w.value("hud.cycle", Json::array()) == Json::array({"5"}))
                w["hud.cycle"] = Json::array();
        }
        if (w.value("rotate", Json::array()) == Json::array({"Super+Shift+Wheel"}) && !w.contains("content.scale"))
            w["rotate"] = Json::array({"Super+Ctrl+Wheel"});
        for (auto key : {"hud.cycle", "content.scale", "window.reset", "carry.facing", "alternate", "cycle"})
            if (!w.contains(key))
                w[key] = inputDefaults()["modes"]["world"][key];
    }
    if (j.is_object() && !j.contains("show_area_markers"))
        j["show_area_markers"] = false;
    if (j.is_object() && !j.contains("invert_y"))
        j["invert_y"] = false;
    auto defaults = inputDefaults();
    if (j.is_object() && j.contains("modes") && j["modes"].contains("world") && !j["modes"]["world"].contains("exit"))
        j["modes"]["world"]["exit"] = defaults["modes"]["world"]["exit"];
    if (j.is_object() && j.contains("modes") && j["modes"].is_object())
        for (auto &[mode, actions] : j["modes"].items())
            if (actions.is_object() && defaults["modes"].contains(mode) && !actions.contains("record.toggle")) actions["record.toggle"] = Json::array();
    if (j.is_object() && j.contains("modes") && j["modes"].is_object())
        for (auto mode : {"interactive", "focus", "menu"})
            if (j["modes"].contains(mode) && j["modes"][mode].is_object())
                for (auto action : {"debug", "capture"})
                    if (!j["modes"][mode].contains(action))
                        j["modes"][mode][action] = defaults["modes"][mode][action];
    if (!j.is_object() || j.size() != defaults.size())
        throw std::runtime_error("input config requires all version 2 fields");
    for (auto it = j.begin(); it != j.end(); ++it)
        if (!defaults.contains(it.key()))
            throw std::runtime_error("unknown input option");
    auto integer = [&](const char *k, int lo, int hi) {
        if (!j.at(k).is_number_integer() || j.at(k) < lo || j.at(k) > hi)
            throw std::runtime_error(std::string(k) + " out of range");
    };
    integer("version", 2, 2);
    integer("longHoldMs", 100, 5000);
    integer("captureInputLimit", 16, 1024);
    if (!j.at("show_area_markers").is_boolean())
        throw std::runtime_error("show_area_markers must be boolean");
    if (!j.at("invert_y").is_boolean())
        throw std::runtime_error("invert_y must be boolean");
    if (!j.at("scopeVignette").is_boolean())
        throw std::runtime_error("scopeVignette must be boolean");
    for (auto [key, lo, hi] : {std::tuple{"useDistance", .2, 30.}, std::tuple{"useTolerance", 0., .5}}) {
        if (!j.at(key).is_number() || !std::isfinite(j.at(key).get<double>()) || j.at(key) < lo || j.at(key) > hi)
            throw std::runtime_error(std::string(key) + " out of range");
    }
    auto out = j;
    if (!out["modes"].is_object() || out["modes"].size() != defaults["modes"].size())
        throw std::runtime_error("unknown input mode");
    for (auto &[mode, actions] : defaults["modes"].items()) {
        if (!out["modes"].contains(mode) || !out["modes"][mode].is_object() ||
            out["modes"][mode].size() != actions.size())
            throw std::runtime_error("unknown/missing action in " + mode);
        for (auto &[action, b] : out["modes"][mode].items()) {
            if (!actions.contains(action) || !b.is_array() || b.size() > 3)
                throw std::runtime_error("action accepts at most three bindings");
            std::set<std::string> seen;
            for (auto &v : b) {
                if (!v.is_string())
                    throw std::runtime_error("binding must be string");
                auto t = parseBinding(v.get<std::string>());
                if (t.key == "F12")
                    throw std::runtime_error("F12 is the emergency exit");
                if (t.key == "MouseMove" && action != "look")
                    throw std::runtime_error("MouseMove is only a look binding");
                auto name = bindingName(t);
                if (!seen.insert(name).second)
                    throw std::runtime_error("duplicate binding variant");
                v = name;
            }
        }
    }
    return out;
}
Json inputConflicts(const Json &j) {
    Json result = Json::array();
    for (auto &[mode, actions] : j["modes"].items()) {
        struct Binding {
            std::string action, name;
            Trigger trigger;
        };
        std::vector<Binding> all;
        for (auto &[action, bindings] : actions.items())
            for (auto &v : bindings)
                all.push_back({action, v, parseBinding(v)});
        for (size_t i = 0; i < all.size(); i++)
            for (size_t k = i + 1; k < all.size(); k++) {
                auto a = all[i].trigger, b = all[k].trigger;
                bool overlap = a.key == b.key || (a.key == "Wheel" && b.key.starts_with("Wheel")) ||
                               (b.key == "Wheel" && a.key.starts_with("Wheel"));
                if (a.mods == b.mods && overlap)
                    result.push_back(
                        {{"mode", mode}, {"binding", all[k].name}, {"actions", {all[i].action, all[k].action}}});
            }
    }
    return result;
}
std::vector<Action> Input::event(const std::string &mode, const std::string &key, unsigned mods, bool down,
                                 uint64_t now, float value, bool gestureMovement) {
    std::string identity = key;
    recent.push_back(
        {{"timeMs", now}, {"mode", mode}, {"key", key}, {"mods", mods}, {"pressed", down}, {"value", value}});
    while (recent.size() > config["captureInputLimit"].get<size_t>())
        recent.pop_front();
    if (!down) {
        auto it = held.find(identity);
        if (it == held.end())
            return {};
        auto p = it->second;
        held.erase(it);
        bool still = false;
        for (auto &[k, h] : held)
            if (h.action == p.action)
                still = true;
        return {{p.action, still, value, false}};
    }
    if (held.contains(identity))
        return {{held.at(identity).action, !held.at(identity).hold || held.at(identity).fired, value, false}};
    if (!config["modes"].contains(mode))
        return {};
    // Exact modifiers for chords; unmodified held movement actions accept Shift/Ctrl
    // so sprint and crouch can be combined with every movement variant.
    for (int pass = 0; pass < (gestureMovement && mode == "world" ? 2 : 1); ++pass)
    for (auto &[action, bindings] : config["modes"][mode].items())
        for (auto &binding : bindings) {
            // Modifier gestures are global and cannot be shadowed by tool slots.
            if ((action == "primary" || action == "secondary" || action == "alternate" || action == "cycle") &&
                (mods & Super))
                continue;
            auto t = parseBinding(binding);
            bool move = action == "forward" || action == "back" || action == "left" || action == "right" ||
                        action == "up" || action == "down" || action == "sprint";
            bool gesturePass = gestureMovement && mode == "world" && pass == 0;
            if (gesturePass && !move)
                continue;
            unsigned matchMods = (gesturePass || (gestureMovement && action == "carry.facing" && !(t.mods & Super))) ? mods & ~Super : mods;
            unsigned own = t.key == "Shift_L" || t.key == "Shift_R"       ? Shift
                           : t.key == "Control_L" || t.key == "Control_R" ? Ctrl
                                                                          : 0;
            if ((t.key != key && !(t.key == "Wheel" && key.starts_with("Wheel"))) ||
                ((matchMods & ~own) != t.mods && !(move && t.mods == 0 && (matchMods & ~(Shift | Ctrl)) == 0)))
                continue;
            if (!key.starts_with("Wheel") && key != "MouseMove")
                held[identity] = {action, bindingName(t), now, t.hold, false};
            // Wheel notches are impulses: a button-like slot still owes its
            // client/drag handler a release in this same deferred input batch.
            if (key.starts_with("Wheel") && (action == "primary" || action == "secondary" || action == "alternate"))
                return {{action, true, value}, {action, false, value, false}};
            return {{action, t.hold ? false : true, value}};
        }
    return {};
}
Json Input::holds() const {
    Json result = Json::array();
    for (const auto &[key, p] : held)
        if (p.hold && !p.fired && result.size() < 64)
            result.push_back({{"action", p.action}, {"binding", p.binding},
                              {"startedAtMs", p.started}, {"durationMs", config["longHoldMs"]}});
    return result;
}
bool Input::gestureHeld() const {
    for (auto &[key, press] : held)
        if (press.action == "carry" || press.action == "resize" || press.action == "rotate")
            return true;
    return false;
}
std::vector<Action> Input::tick(uint64_t now) {
    std::vector<Action> out;
    for (auto &[key, p] : held)
        if (p.hold && !p.fired && now - p.started >= config["longHoldMs"].get<uint64_t>()) {
            p.fired = true;
            out.push_back({p.action, true});
        }
    return out;
}
void Input::apply(const Json &j) {
    auto next = validateInput(j);
    if (config != next) {
        if (config["modes"] != next["modes"])
            clear();
        config = std::move(next);
    }
}
void Input::clear() {
    held.clear();
}
} // namespace hyprune::interaction
