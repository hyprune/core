#include "Focus.hpp"
#include "Files.hpp"
#include <cmath>
#include <stdexcept>
namespace hyprune::quests {
namespace {
double tenth(double v) {
    return std::round(v * 10) / 10;
}
void need(bool ok, const std::string &message) {
    if (!ok)
        throw std::invalid_argument(message);
}
} // namespace
FocusSession::Settings FocusSession::settingsFrom(const Json &g, const Json &overrides) {
    Settings s;
    s.workMin = g.at("focus.work_min").get<double>();
    s.shortBreakMin = g.at("focus.short_break_min").get<double>();
    s.longBreakMin = g.at("focus.long_break_min").get<double>();
    s.longBreakEvery = g.at("focus.long_break_every").get<int>();
    s.cycles = g.at("focus.cycles").get<int>();
    s.autoStartBreaks = g.at("focus.auto_start_breaks").get<bool>();
    s.autoStartWork = g.at("focus.auto_start_work").get<bool>();
    s.countMode = g.at("focus.count_mode").get<std::string>();
    s.idleS = g.at("focus.idle_s").get<double>();
    s.graceS = g.at("focus.grace_s").get<double>();
    s.calm = g.at("focus.calm").get<bool>();
    if (overrides.is_null())
        return s;
    need(overrides.is_object(), "overrides must be an object");
    auto number = [&](const char *key, double lo, double hi, auto &field, bool integer) {
        if (!overrides.contains(key))
            return;
        auto &v = overrides[key];
        need(v.is_number() && std::isfinite(v.get<double>()) && v >= lo && v <= hi &&
                 (!integer || v.get<double>() == std::floor(v.get<double>())),
             std::string(key) + " out of range");
        field = v.get<std::remove_reference_t<decltype(field)>>();
    };
    auto flag = [&](const char *key, bool &field) {
        if (!overrides.contains(key))
            return;
        need(overrides[key].is_boolean(), std::string(key) + " must be boolean");
        field = overrides[key];
    };
    for (auto &[key, value] : overrides.items())
        need(key == "work_min" || key == "short_break_min" || key == "long_break_min" || key == "long_break_every" ||
                 key == "cycles" || key == "count_mode" || key == "idle_s" || key == "grace_s" || key == "calm" ||
                 key == "auto_start_breaks" || key == "auto_start_work",
             "unknown focus override: " + key);
    number("work_min", 1, 120, s.workMin, false);
    number("short_break_min", 1, 30, s.shortBreakMin, false);
    number("long_break_min", 1, 60, s.longBreakMin, false);
    number("long_break_every", 2, 12, s.longBreakEvery, true);
    number("cycles", 1, 12, s.cycles, true);
    number("idle_s", 60, 900, s.idleS, false);
    number("grace_s", 0, 120, s.graceS, false);
    flag("calm", s.calm);
    flag("auto_start_breaks", s.autoStartBreaks);
    flag("auto_start_work", s.autoStartWork);
    if (overrides.contains("count_mode")) {
        need(overrides["count_mode"] == "focus" || overrides["count_mode"] == "wall", "count_mode must be focus or wall");
        s.countMode = overrides["count_mode"];
    }
    return s;
}
Json FocusSession::validTarget(const Json &t) {
    if (t.is_null())
        return {{"kind", "any"}, {"label", "Any window"}};
    need(t.is_object() && t.contains("kind") && t["kind"].is_string(), "focus target needs a kind");
    std::string kind = t["kind"];
    need(kind == "any" || kind == "workspace" || kind == "window" || kind == "app",
         "focus target kind must be any, workspace, window or app");
    for (auto &[key, value] : t.items())
        need(key == "kind" || key == "label" || key == "workspaceId" || key == "appClass", "unknown focus target field: " + key);
    Json out = {{"kind", kind}};
    bool wantsWorkspace = kind == "workspace" || kind == "window", wantsClass = kind == "window" || kind == "app";
    need(wantsWorkspace == t.contains("workspaceId"), kind + " targets " + (wantsWorkspace ? "need" : "take no") + " workspaceId");
    need(wantsClass == t.contains("appClass"), kind + " targets " + (wantsClass ? "need" : "take no") + " appClass");
    if (wantsWorkspace) {
        need(t["workspaceId"].is_number_integer() && t["workspaceId"] >= 1 && t["workspaceId"] <= 2147483647,
             "workspaceId range");
        out["workspaceId"] = t["workspaceId"];
    }
    if (wantsClass) {
        need(t["appClass"].is_string() && !t["appClass"].get<std::string>().empty() &&
                 t["appClass"].get<std::string>().size() <= 128,
             "appClass must be 1-128 bytes");
        out["appClass"] = t["appClass"];
    }
    std::string label = kind == "any"         ? "Any window"
                        : kind == "workspace" ? "Workspace " + std::to_string(t["workspaceId"].get<int64_t>())
                        : kind == "window"    ? t["appClass"].get<std::string>() + " on " +
                                                 std::to_string(t["workspaceId"].get<int64_t>())
                                              : t["appClass"].get<std::string>();
    if (t.contains("label")) {
        need(t["label"].is_string() && t["label"].get<std::string>().size() <= 256, "label must be a short string");
        if (!t["label"].get<std::string>().empty())
            label = t["label"];
    }
    if (label.size() > 64) {
        label.resize(64);
        while (!label.empty() && (label.back() & 0xc0) == 0x80)
            label.pop_back();
        if (!label.empty() && (label.back() & 0x80))
            label.pop_back();
    }
    out["label"] = label;
    return out;
}
bool FocusSession::matches(const Json &focus) const {
    if (!focus.is_object() || target.is_null())
        return false;
    std::string kind = target["kind"];
    if (kind == "any")
        return true;
    bool sameWorkspace = target.contains("workspaceId") && focus.value("workspaceId", -1) == target["workspaceId"];
    bool sameClass = target.contains("appClass") && Json(focus.value("appClass", "")) == target["appClass"];
    return kind == "workspace" ? sameWorkspace : kind == "window" ? sameWorkspace && sameClass : sameClass;
}
int64_t FocusSession::workMs() const {
    return int64_t(settings.workMin * 60000 / scale);
}
int64_t FocusSession::breakMs() const {
    return int64_t((breakKind == "long" ? settings.longBreakMin : settings.shortBreakMin) * 60000 / scale);
}
void FocusSession::begin(const std::string &phase, int64_t now, std::time_t wall, int64_t length) {
    state = phase;
    pausedState.clear();
    next.clear();
    phaseStart = lastTick = now;
    phaseMs = length;
    phaseWallStart = wall;
    if (phase == "work") {
        creditedMs = 0;
        crediting = false;
        lastMatch = INT64_MIN / 2;
    }
}
Json FocusSession::phaseEvent(const std::string &phase) const {
    Json e = {{"focusSessionId", id}, {"phase", phase}, {"cycle", cycle}, {"cycles", settings.cycles},
              {"creditedMs", creditedMs}};
    if (!breakKind.empty() && (phase == "break" || (phase == "ready" && next == "break")))
        e["breakKind"] = breakKind;
    return e;
}
Json FocusSession::historyLine(std::time_t wallEnd, const std::string &phase, int64_t plannedMs,
                               const std::string &outcome) const {
    Json line = {{"start", localTime(phaseWallStart)},
                 {"end", localTime(std::max(wallEnd, phaseWallStart))},
                 {"phase", phase},
                 {"plannedMin", tenth(plannedMs * scale / 60000.)},
                 {"creditedMin", phase == "work" ? tenth(creditedMs * scale / 60000.) : 0.},
                 {"targetKind", target.is_null() ? "any" : target["kind"]},
                 {"outcome", outcome}};
    if (target.is_object() && target.contains("appClass"))
        line["appClass"] = target["appClass"];
    if (target.is_object() && target.contains("workspaceId"))
        line["workspaceId"] = target["workspaceId"];
    if (questRef.is_object())
        line["questId"] = questRef["source"].get<std::string>() + "/" + questRef["id"].get<std::string>();
    return line;
}
std::vector<FocusSession::Outcome> FocusSession::start(int64_t now, std::time_t wall, Json t, Json quest,
                                                       std::string objective, Settings s, std::string sessionId) {
    target = std::move(t);
    questRef = std::move(quest);
    objectiveId = std::move(objective);
    settings = s;
    id = std::move(sessionId);
    cycle = 1;
    breakKind.clear();
    begin("work", now, wall, workMs());
    return {{phaseEvent("work"), std::nullopt, std::nullopt}};
}
std::vector<FocusSession::Outcome> FocusSession::finishPhase(int64_t now, std::time_t wall, const std::string &outcome) {
    std::vector<Outcome> out;
    if (state == "work") {
        double credited = creditedMs * scale / 60000.;
        Outcome o{nullptr, historyLine(wall, "work", phaseMs, outcome), std::nullopt};
        if (outcome == "completed")
            o.completedWorkMin = credited;
        bool counted = outcome == "completed" && credited + 1e-6 >= settings.workMin * .8;
        if (cycle >= settings.cycles) {
            Json e = phaseEvent("ended");
            e["outcome"] = outcome;
            e["counted"] = counted;
            o.phase = e;
            state = "idle";
            crediting = false;
            out.push_back(o);
            return out;
        }
        breakKind = cycle % settings.longBreakEvery == 0 ? "long" : "short";
        if (settings.autoStartBreaks || outcome == "skipped")
            begin("break", now, wall, breakMs());
        else {
            state = "ready";
            next = "break";
            phaseMs = breakMs();
        }
        crediting = false;
        Json e = phaseEvent(state);
        e["outcome"] = outcome;
        e["counted"] = counted;
        o.phase = e;
        out.push_back(o);
        return out;
    }
    if (state == "break") {
        Outcome o{nullptr, historyLine(wall, "break", phaseMs, outcome), std::nullopt};
        ++cycle;
        auto kind = breakKind;
        if (settings.autoStartWork || outcome == "skipped")
            begin("work", now, wall, workMs());
        else {
            state = "ready";
            next = "work";
            phaseMs = workMs();
            creditedMs = 0;
        }
        Json e = phaseEvent(state);
        e["outcome"] = outcome;
        o.phase = e;
        out.push_back(o);
    }
    return out;
}
std::vector<FocusSession::Outcome> FocusSession::pause(int64_t now, std::time_t) {
    if (!running())
        throw std::logic_error("no running interval to pause");
    remainingAtPause = std::max<int64_t>(0, phaseStart + phaseMs - now);
    pausedState = state;
    state = "paused";
    crediting = false;
    return {{phaseEvent("paused"), std::nullopt, std::nullopt}};
}
std::vector<FocusSession::Outcome> FocusSession::resume(int64_t now, std::time_t wall) {
    if (state == "paused") {
        state = pausedState;
        pausedState.clear();
        phaseStart = now - (phaseMs - remainingAtPause);
        lastTick = now;
        // Pausing never moves the wall start: history keeps the interval's real start.
        return {{phaseEvent(state), std::nullopt, std::nullopt}};
    }
    if (state == "ready") {
        if (next == "break")
            begin("break", now, wall, breakMs());
        else
            begin("work", now, wall, workMs());
        return {{phaseEvent(state), std::nullopt, std::nullopt}};
    }
    throw std::logic_error("nothing to resume");
}
std::vector<FocusSession::Outcome> FocusSession::skip(int64_t now, std::time_t wall) {
    if (state == "idle")
        throw std::logic_error("no focus session");
    if (state == "ready") {
        if (next == "break") {
            ++cycle; // skipping a waiting break starts the next work interval
            begin("work", now, wall, workMs());
        } else
            begin("work", now, wall, workMs());
        return {{phaseEvent("work"), std::nullopt, std::nullopt}};
    }
    if (state == "paused") {
        state = pausedState;
        pausedState.clear();
    }
    return finishPhase(now, wall, "skipped");
}
std::vector<FocusSession::Outcome> FocusSession::stop(int64_t, std::time_t wall) {
    if (state == "idle")
        throw std::logic_error("no focus session");
    Outcome o{nullptr, std::nullopt, std::nullopt};
    auto phase = state == "paused" ? pausedState : state;
    if (phase == "work" || phase == "break")
        o.history = historyLine(wall, phase, phaseMs, "stopped");
    Json e = phaseEvent("ended");
    e["outcome"] = "stopped";
    e["counted"] = false;
    o.phase = e;
    state = "idle";
    pausedState.clear();
    next.clear();
    crediting = false;
    return {o};
}
std::vector<FocusSession::Outcome> FocusSession::tick(int64_t now, std::time_t wall, const Facts &facts,
                                                      bool &creditChanged) {
    creditChanged = false;
    std::vector<Outcome> out;
    if (!running()) {
        lastTick = now;
        return out;
    }
    if (facts.locked)
        return pause(now, wall);
    auto dt = std::clamp<int64_t>(now - lastTick, 0, 2000);
    lastTick = now;
    if (state == "work") {
        bool was = crediting;
        auto previous = creditedMs;
        if (settings.countMode == "wall")
            crediting = true;
        else {
            if (matches(facts.focus))
                lastMatch = now;
            bool recentInput = now - facts.inputAtMs < int64_t(settings.idleS * 1000 / scale);
            bool focused = now - lastMatch <= int64_t(settings.graceS * 1000 / scale);
            crediting = recentInput && focused;
        }
        if (crediting)
            creditedMs = std::min(phaseMs, creditedMs + dt);
        // State publishes credit at most every 10 s (scaled) and when crediting flips.
        auto step = std::max<int64_t>(1, int64_t(10000 / scale));
        creditChanged = was != crediting || previous / step != creditedMs / step;
    }
    if (now >= phaseStart + phaseMs)
        return finishPhase(now, wall, "completed");
    return out;
}
Json FocusSession::stateKey(int64_t now) const {
    bool run = running();
    Json t = target;
    if (t.is_object() && questRef.is_object())
        t["questRef"] = questRef;
    std::string kind = state == "break" || (state == "paused" && pausedState == "break") ||
                               (state == "ready" && next == "break")
                           ? breakKind
                           : "";
    return {{"state", state},
            {"pausedState", state == "paused" ? Json(pausedState) : Json(nullptr)},
            {"sessionId", state == "idle" ? Json(nullptr) : Json(id)},
            {"phaseStartedAtMs", run ? Json(phaseStart) : Json(nullptr)},
            {"phaseEndsAtMs", run ? Json(phaseStart + phaseMs) : Json(nullptr)},
            {"phaseMs", state == "idle" ? 0 : phaseMs},
            {"remainingMs", run                ? std::max<int64_t>(0, phaseStart + phaseMs - now)
                            : state == "paused" ? remainingAtPause
                            : state == "ready"  ? phaseMs
                                                : 0},
            {"creditedMs", state == "idle" ? 0 : creditedMs},
            {"crediting", crediting && state == "work"},
            {"cycle", state == "idle" ? 0 : cycle},
            {"cycles", settings.cycles},
            {"breakKind", kind.empty() ? Json(nullptr) : Json(kind)},
            {"countMode", settings.countMode},
            {"target", state == "idle" ? Json(nullptr) : t},
            {"calm", calmNow()}};
}
Json FocusSession::file(int64_t now) const {
    if (state == "idle")
        return nullptr;
    return {{"version", 1},
            {"id", id},
            {"state", state},
            {"pausedState", pausedState},
            {"next", next},
            {"breakKind", breakKind},
            {"cycle", cycle},
            {"phaseMs", phaseMs},
            {"remainingMs", running() ? std::max<int64_t>(0, phaseStart + phaseMs - now) : remainingAtPause},
            {"creditedMs", creditedMs},
            {"phaseWallStart", int64_t(phaseWallStart)},
            {"scale", scale},
            {"target", target},
            {"questRef", questRef},
            {"objectiveId", objectiveId},
            {"settings",
             {{"work_min", settings.workMin},
              {"short_break_min", settings.shortBreakMin},
              {"long_break_min", settings.longBreakMin},
              {"long_break_every", settings.longBreakEvery},
              {"cycles", settings.cycles},
              {"auto_start_breaks", settings.autoStartBreaks},
              {"auto_start_work", settings.autoStartWork},
              {"count_mode", settings.countMode},
              {"idle_s", settings.idleS},
              {"grace_s", settings.graceS},
              {"calm", settings.calm}}}};
}
void FocusSession::restore(const Json &j, int64_t now, std::time_t) {
    if (!j.is_object() || j.value("version", 0) != 1)
        return;
    std::string saved = j.value("state", "idle");
    if (saved == "idle")
        return;
    auto s = j.at("settings");
    settings.workMin = s.at("work_min");
    settings.shortBreakMin = s.at("short_break_min");
    settings.longBreakMin = s.at("long_break_min");
    settings.longBreakEvery = s.at("long_break_every");
    settings.cycles = s.at("cycles");
    settings.autoStartBreaks = s.at("auto_start_breaks");
    settings.autoStartWork = s.at("auto_start_work");
    settings.countMode = s.at("count_mode");
    settings.idleS = s.at("idle_s");
    settings.graceS = s.at("grace_s");
    settings.calm = s.at("calm");
    id = j.at("id");
    target = j.at("target");
    questRef = j.at("questRef");
    objectiveId = j.value("objectiveId", "");
    breakKind = j.value("breakKind", "");
    cycle = j.at("cycle");
    // A restored session's clock follows this process's scale; stored lengths keep theirs.
    phaseMs = j.at("phaseMs");
    creditedMs = j.at("creditedMs");
    phaseWallStart = j.value("phaseWallStart", int64_t(0));
    lastTick = now;
    if (saved == "ready") {
        state = "ready";
        next = j.value("next", "work");
        return;
    }
    // Core exit pauses a running session; it resumes paused, never on its own.
    state = "paused";
    pausedState = saved == "paused" ? j.value("pausedState", "work") : saved;
    remainingAtPause = j.at("remainingMs");
}
} // namespace hyprune::quests
