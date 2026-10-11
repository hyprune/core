// RFC-0012 P1: personal quests, focus sessions, guidance, markers and the TAB tap/hold pair.
#include "interaction/Input.hpp"
#include "ipc/Server.hpp"
#include "quests/Engine.hpp"
#include "quests/Files.hpp"
#include "quests/Markers.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
using namespace hyprune;
using namespace hyprune::quests;
namespace {
int failures = 0;
void check(bool ok, const std::string &what) {
    if (!ok) {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}
template <class F> int code(F f) {
    try {
        f();
    } catch (const ipc::Error &e) {
        return e.code;
    } catch (const std::invalid_argument &) {
        return -32602;
    } catch (const std::logic_error &) {
        return -32004;
    }
    return 0;
}
const std::string now = "2026-10-12T10:00:00";

void guidance(const std::filesystem::path &dir) {
    Guidance g(dir / "guidance.json");
    g.load();
    check(g.get<int>("markers.reveal_ms") == 4000 && g.get<std::string>("markers.persistent") == "edge" &&
              g.get<int>("focus.idle_s") == 180 && g.get<int>("focus.grace_s") == 30,
          "guidance defaults");
    for (auto &[key, value] : Guidance::defaults().items())
        check(value.is_boolean() || Guidance::ranges().contains(key) || Guidance::options().contains(key),
              "every guidance key has a range or options: " + key);
    check(code([&] { g.set({{"markers.reveal_ms", 999}}); }) == -32602, "reveal_ms below range");
    check(code([&] { g.set({{"markers.labels", 2.5}}); }) == -32602, "fractional count");
    check(code([&] { g.set({{"markers.persistent", "sometimes"}}); }) == -32602, "unknown option");
    check(code([&] { g.set({{"nope", 1}}); }) == -32602, "unknown key");
    g.set({{"markers.reveal_ms", 6000}, {"focus.calm", false}});
    struct stat st{};
    check(stat((dir / "guidance.json").c_str(), &st) == 0 && (st.st_mode & 077) == 0, "guidance file private");
    Guidance h(dir / "guidance.json");
    h.load();
    check(h.get<int>("markers.reveal_ms") == 6000 && !h.get<bool>("focus.calm") && h.error.empty(), "guidance round trip");
    h.set({{"markers.reveal_ms", nullptr}});
    check(h.get<int>("markers.reveal_ms") == 4000 && !h.overrides.contains("markers.reveal_ms"), "null removes override");
    auto snap = h.snapshot();
    check(snap.contains("config") && snap.contains("defaults") && snap.contains("ranges") && snap.contains("options") &&
              snap["ranges"]["focus.work_min"] == Json({5, 120}),
          "guidance snapshot shape");
}

void questBook() {
    QuestBook b;
    // Normalisation: a title and steps are enough; ids and labels are generated.
    auto n = QuestBook::normalize({{"title", "Write the release notes"}, {"steps", {{{"title", "Draft"}}, {{"title", "Review"}}}}});
    check(n["steps"][0]["id"] == "step-1" && n["steps"][1]["objectives"][0]["type"] == "manual" &&
              n["steps"][0]["objectives"][0]["label"] == "Done" && n["sequential"] == true,
          "normalised defaults");
    for (Json bad : {Json{{"steps", {{{"title", "x"}}}}}, Json{{"title", ""}, {"steps", {{{"title", "x"}}}}},
                     Json{{"title", std::string(65, 'a')}, {"steps", {{{"title", "x"}}}}},
                     Json{{"title", "t"}, {"steps", Json::array()}}, Json{{"title", "t"}, {"steps", Json::array_t(17, {{"title", "s"}})}},
                     Json{{"title", "t"}, {"steps", {{{"title", "s"}, {"objectives", {{{"type", "reach"}}}}}}}},
                     Json{{"title", "t"}, {"steps", {{{"title", "s"}, {"target", {{"areaId", "a"}, {"zoneId", "b"}}}}}}},
                     Json{{"title", "t"}, {"steps", {{{"title", "s"}, {"objectives", {{{"type", "signal"}}}}}}}},
                     Json{{"title", "t"}, {"steps", {{{"title", "s"}}}}, {"expires", "tomorrow"}},
                     Json{{"title", "t\n"}, {"steps", {{{"title", "s"}}}}}, Json{{"title", "t"}, {"bogus", 1}, {"steps", {{{"title", "s"}}}}}})
        check(code([&] { QuestBook::normalize(bad); }) == -32602, "rejects " + bad.dump());
    auto ref = b.create({{"title", "Write the release notes"},
                         {"steps",
                          {{{"title", "Draft"}},
                           {{"title", "Two focus sessions"}, {"objectives", {{{"id", "work"}, {"type", "focus"}, {"sessions", 2}}}}},
                           {{"title", "Publish at the hall"}, {"target", {{"areaId", "array-hall"}}},
                            {"objectives", {{{"id", "go"}, {"type", "reach"}}}}}}}},
                        now);
    check(ref == QuestBook::ref("write-the-release-notes") && b.tracked == ref, "create tracks the first quest");
    auto second = b.create({{"title", "Write the release notes"}, {"steps", {{{"title", "Again"}}}}}, now);
    check(second["id"] == "write-the-release-notes-2" && b.tracked == ref, "unique ids; tracking kept");
    std::string id = ref["id"];
    // Sequential: only the current step is open.
    check(code([&] { b.mark(id, "step-2", "work", now); }) == -32004, "focus objectives are not marked");
    check(code([&] { b.mark(id, "step-3", "go", now); }) == -32004, "later step closed");
    check(code([&] { b.mark(id, "nope", "done", now); }) == -32003, "unknown step");
    auto e = b.mark(id, "step-1", "done", now);
    check(e.size() == 2 && e[0]["kind"] == "objective" && e[1]["kind"] == "step" && e[1]["nextStepTitle"] == "Two focus sessions",
          "step completion events");
    check(code([&] { b.mark(id, "step-1", "done", now); }) == -32004, "a done step is not marked again");
    // Focus objective: sessions from this quest only, with enough credited minutes.
    check(b.focusCompleted(nullptr, "", 25, 25, now).empty(), "unlinked session does not count for focusTarget quest");
    check(b.focusCompleted(ref, "work", 10, 25, now).empty(), "too few credited minutes");
    e = b.focusCompleted(ref, "work", 21, 25, now);
    check(e.size() == 1 && e[0]["progress"] == Json({1, 2}), "first focus session counts");
    e = b.focusCompleted(ref, "work", 25, 25, now);
    check(e.size() == 2 && e[1]["kind"] == "step", "second session completes the step");
    check(b.detail(id)["steps"][1]["objectives"][0]["creditedMinutes"] == 56.0, "credited minutes kept");
    // Reach: counts on entering; the watch lists the open objective.
    auto watch = b.watch();
    check(watch.size() == 1 && watch[0]["key"] == "personal/" + id + "/step-3/go" && watch[0]["target"]["areaId"] == "array-hall",
          "watch list");
    auto markers = b.markers();
    check(!markers.empty() && markers[0]["tier"] == 1 && markers[0]["tracked"] == true &&
              markers[0]["id"] == "quest:personal/" + id + "/step-3",
          "tracked quest marker");
    e = b.reached({"personal/" + id + "/step-3/go"}, now);
    check(e.size() == 2 && e[1]["kind"] == "completed", "reaching completes the quest");
    check(b.tracked.is_null(), "completing the tracked quest untracks it");
    check(b.detail(id)["state"] == "completed" && b.watch().empty(), "completed state");
    check(code([&] { b.track(ref); }) == -32004, "cannot track a completed quest");
    check(code([&] { b.accept(id, now); }) == -32004, "cannot accept a completed quest");
    // Reach with count 2 needs leaving between counts.
    auto r2 = b.create({{"title", "Laps"},
                        {"steps", {{{"title", "Lap"}, {"objectives", {{{"id", "lap"}, {"type", "reach"}, {"count", 2},
                                                                        {"target", {{"position", {0, 0, 0}}}}}}}}}}},
                       now);
    std::string key = "personal/laps/step-1/lap";
    check(b.reached({key}, now).size() == 1, "first lap");
    check(b.reached({key}, now).empty(), "staying inside does not count");
    b.reached({}, now);
    check(b.reached({key}, now).size() == 2, "re-entering counts and completes");
    // Abandon / resume, update keeps progress, delete.
    auto r3 = b.create({{"title", "Tidy"}, {"steps", {{{"id", "a"}, {"title", "A"}}, {{"id", "b"}, {"title", "B"}}}}}, now);
    b.mark("tidy", "a", "done", now);
    b.track(r3);
    b.abandon("tidy", now);
    check(b.detail("tidy")["state"] == "abandoned" && b.tracked.is_null(), "abandon untracks");
    check(code([&] { b.mark("tidy", "b", "done", now); }) == -32004, "abandoned quests do not count");
    b.resume("tidy", now);
    check(b.detail("tidy")["state"] == "active" && b.tracked == r3, "resume tracks when nothing is tracked");
    b.update("tidy", {{"title", "Tidy up"}, {"steps", {{{"id", "a"}, {"title", "A"}}, {{"id", "c"}, {"title", "C"}}}}}, now);
    auto d = b.detail("tidy");
    check(d["title"] == "Tidy up" && d["steps"][0]["state"] == "done" && d["steps"][1]["state"] == "current",
          "update keeps progress by id");
    // Expiry fails an active quest; resume clears the date.
    b.create({{"title", "Due"}, {"expires", "2026-01-01T09:00"}, {"steps", {{{"title", "x"}}}}}, now);
    e = b.expire(std::time(nullptr), now);
    check(e.size() == 1 && e[0]["kind"] == "failed" && b.detail("due")["state"] == "failed", "expiry fails");
    b.resume("due", now);
    check(b.detail("due")["expires"].is_null() && b.expire(std::time(nullptr), now).empty(), "resumed after the date");
    // List order and filters; state key bounds.
    auto list = b.list({{"states", {"active"}}}, {});
    check(list["quests"].size() == 3 && list["quests"][0]["tracked"] == true, "list filters and puts tracked first");
    check(b.list({{"sources", {"org.example"}}}, {})["quests"].empty(), "source filter");
    check(code([&] { b.list({{"states", {"weird"}}}, {}); }) == -32602, "unknown state filter");
    auto key2 = b.stateKey();
    check(key2["active"].size() == 3 && key2["counts"]["completed"] == 2 && key2["world"].is_null(), "state key");
    b.remove("tidy");
    check(!b.find("tidy") && b.tracked.is_null(), "delete untracks");
    check(code([&] { b.remove("tidy"); }) == -32003, "delete unknown");
    // Persistence round trip.
    QuestBook c;
    c.restore(Json::parse(b.file().dump()));
    check(c.file() == b.file() && c.quests == b.quests, "file round trip");
    // Limit.
    QuestBook full;
    for (int i = 0; i < 64; ++i)
        full.create({{"title", "q" + std::to_string(i)}, {"steps", {{{"title", "s"}}}}}, now);
    check(code([&] { full.create({{"title", "one more"}, {"steps", {{{"title", "s"}}}}}, now); }) == -32008, "64 quest limit");
    check(full.stateKey()["active"].size() == 16, "state key holds 16 active quests");
}

void focus() {
    FocusSession f;
    FocusSession::Settings s;
    s.workMin = 1;
    s.shortBreakMin = 1;
    s.longBreakMin = 2;
    s.longBreakEvery = 2;
    s.cycles = 3;
    s.idleS = 60;
    s.graceS = 10;
    auto target = FocusSession::validTarget({{"kind", "workspace"}, {"workspaceId", 3}});
    check(target["label"] == "Workspace 3", "target label");
    check(code([&] { FocusSession::validTarget({{"kind", "workspace"}}); }) == -32602, "workspace target needs an id");
    check(code([&] { FocusSession::validTarget({{"kind", "window"}, {"workspaceId", 1}}); }) == -32602, "window needs a class");
    int64_t t = 1000000;
    auto out = f.start(t, 0, target, nullptr, "", s, "focus-1");
    check(f.state == "work" && out[0].phase["phase"] == "work" && f.phaseMs == 60000, "start");
    FocusSession::Facts facts;
    facts.focus = {{"workspaceId", 3}, {"appClass", "foot"}};
    bool changed = false;
    auto step = [&](int64_t ms, int64_t every = 500) {
        std::vector<FocusSession::Outcome> all;
        for (int64_t k = 0; k < ms; k += every) {
            t += every;
            facts.inputAtMs = facts.inputAtMs ? facts.inputAtMs : t;
            auto o = f.tick(t, 0, facts, changed);
            all.insert(all.end(), o.begin(), o.end());
        }
        return all;
    };
    facts.inputAtMs = t;
    step(10000);
    check(f.creditedMs >= 9500 && f.creditedMs <= 10000 && f.crediting, "credit while focused with input");
    // Grace: 10 s on another workspace still credits, beyond it not.
    facts.focus = {{"workspaceId", 4}, {"appClass", "browser"}};
    facts.inputAtMs = t;
    auto before = f.creditedMs;
    step(8000);
    check(f.creditedMs - before >= 7500, "grace keeps crediting");
    facts.inputAtMs = t;
    step(6000);
    check(f.creditedMs - before <= 10500 && !f.crediting, "beyond grace stops crediting");
    // Back on target but idle for over 60 s: no credit.
    facts.focus = {{"workspaceId", 3}, {"appClass", "foot"}};
    facts.inputAtMs = t - 61000;
    before = f.creditedMs;
    step(2000);
    check(f.creditedMs == before && !f.crediting, "idle stops crediting");
    // World view holds the keyboard: no credit.
    facts.focus = nullptr;
    facts.inputAtMs = t;
    step(12000);
    check(f.creditedMs - before <= 10500 && !f.crediting, "world view credits only inside the grace period");
    // Pause keeps remaining time; resume continues.
    auto remaining = f.phaseStart + f.phaseMs - t;
    f.pause(t, 0);
    t += 30000;
    f.resume(t, 0);
    check(std::abs(f.phaseStart + f.phaseMs - t - remaining) < 2, "pause keeps remaining");
    check(code([&] { f.resume(t, 0); }) == -32004, "resume while running");
    // Finish the work interval: completed, under the 80 % credit floor -> not counted.
    facts.focus = {{"workspaceId", 3}, {"appClass", "foot"}};
    facts.inputAtMs = t;
    auto o = step(f.phaseStart + f.phaseMs - t + 500);
    check(!o.empty() && o.back().phase["phase"] == "break" && o.back().phase["breakKind"] == "short" &&
              o.back().completedWorkMin.has_value() && o.back().history.has_value() &&
              o.back().history->at("outcome") == "completed",
          "work completes into a short break");
    check(f.state == "break" && f.cycle == 1, "break after cycle 1");
    // Skip the break; auto_start_work off -> skipping starts work anyway.
    o = f.skip(t, 0);
    check(f.state == "work" && f.cycle == 2 && o[0].history->at("outcome") == "skipped", "skip break");
    // Skip work: no credit, history says skipped; cycle 2 of every-2 -> long break.
    o = f.skip(t, 0);
    check(!o[0].completedWorkMin && o[0].phase["breakKind"] == "long" && o[0].history->at("outcome") == "skipped",
          "skipped work does not count; long break");
    // Break ends on time -> ready (auto_start_work off).
    o = step(f.phaseMs + 1000);
    check(f.state == "ready" && f.cycle == 3 && o.back().phase["phase"] == "ready", "ready after the break");
    auto key = f.stateKey(t);
    check(key["state"] == "ready" && key["remainingMs"] == f.workMs() && key["phaseStartedAtMs"].is_null(), "ready state key");
    // Lock pauses; it never resumes on its own.
    f.resume(t, 0);
    facts.locked = true;
    o = step(1000);
    check(f.state == "paused" && f.pausedState == "work", "lock pauses");
    facts.locked = false;
    step(1000);
    check(f.state == "paused", "stays paused after unlock");
    // Restore after a core restart: paused.
    FocusSession g;
    g.restore(f.file(t), t + 5000, 0);
    check(g.state == "paused" && g.pausedState == "work" && g.remainingAtPause == f.remainingAtPause && g.cycle == 3,
          "restore paused");
    // Stop: ended, stopped, nothing counted.
    o = f.stop(t, 0);
    check(f.state == "idle" && o[0].phase["phase"] == "ended" && o[0].phase["outcome"] == "stopped" &&
              o[0].history->at("outcome") == "stopped" && !o[0].history->contains("title"),
          "stop");
    check(code([&] { f.stop(t, 0); }) == -32004 && code([&] { f.pause(t, 0); }) == -32004, "idle refuses");
    // Last cycle ends the session; wall mode credits without focus.
    FocusSession w;
    s.cycles = 1;
    s.countMode = "wall";
    w.start(t, 0, FocusSession::validTarget(nullptr), nullptr, "", s, "focus-2");
    FocusSession::Facts none;
    bool c2;
    std::vector<FocusSession::Outcome> end;
    for (int i = 0; i < 130 && end.empty(); ++i) {
        t += 500;
        end = w.tick(t, 0, none, c2);
    }
    check(!end.empty() && end[0].phase["phase"] == "ended" && end[0].phase["outcome"] == "completed" &&
              end[0].phase["counted"] == true && w.state == "idle",
          "wall mode; last cycle ends the session");
    // Scaled clock (tests): durations divide by the scale.
    FocusSession scaled;
    scaled.scale = 60;
    s.workMin = 25;
    scaled.start(t, 0, FocusSession::validTarget(nullptr), nullptr, "", s, "focus-3");
    check(scaled.phaseMs == 25000, "time scale");
    // Overrides validated.
    check(code([&] { FocusSession::settingsFrom(Guidance::defaults(), {{"work_min", 0}}); }) == -32602, "override range");
    check(FocusSession::settingsFrom(Guidance::defaults(), {{"work_min", 50}, {"cycles", 1}}).workMin == 50, "override applies");
}

void engine(const std::filesystem::path &dir) {
    Engine::Paths paths{dir / "state/quests/personal.json", dir / "state/focus/history.jsonl",
                        dir / "state/focus/session.json", dir / "config/guidance.json"};
    int64_t t = 5000000;
    auto wall = std::time(nullptr);
    {
        Engine e(paths, 60);
        auto r = e.call("quests.create",
                        {{"quest", {{"title", "Notes"}, {"steps", {{{"title", "Draft"}, {"target", {{"workspaceId", 21}}}}}}}},
                         {"_actor", "x"}},
                        t, wall);
        check(r["questRef"] == QuestBook::ref("notes") && r.contains("revision"), "engine create");
        check(e.changed, "create changes state");
        auto state = e.decorate(Json::object(), t);
        check(state["quests"]["tracked"] == QuestBook::ref("notes") && state["focus"]["state"] == "idle", "decorated state");
        auto sync = e.sync();
        check(sync && (*sync)["markers"].size() == 1 && (*sync)["markers"][0]["target"]["workspaceId"] == 21 &&
                  (*sync)["guidance"]["revealMs"] == 4000,
              "sync carries the tracked marker and guidance");
        check(!e.sync(), "sync only on change");
        check(code([&] { e.call("quests.get", {{"questRef", {{"source", "org.x"}, {"id", "y"}}}}, t, wall); }) == -32003,
              "unknown source");
        check(code([&] { e.call("quests.update", {{"questRef", {{"source", "org.x"}, {"id", "y"}}}, {"quest", Json::object()}}, t, wall); }) ==
                  -32007,
              "editing a non-personal quest");
        check(code([&] { e.call("quests.mark", {{"questRef", QuestBook::ref("notes")}}, t, wall); }) == -32602, "missing params");
        e.call("focus.start", {{"target", {{"kind", "workspace"}, {"workspaceId", 21}}}, {"questRef", QuestBook::ref("notes")}}, t,
               wall);
        check(code([&] { e.call("focus.start", Json::object(), t, wall); }) == -32004, "one session at a time");
        check(e.events.size() == 1 && e.events[0].first == "focus.phase" && e.events[0].second["phase"] == "work",
              "focus.phase event");
        auto s2 = e.sync();
        check(s2 && (*s2)["calm"] == true && (*s2)["markers"].size() == 2 && (*s2)["markers"][1]["kind"] == "focus",
              "focus target marker and calm mode");
        e.events.clear();
        e.facts({{"focus", {{"workspaceId", 21}, {"appClass", "foot"}}}, {"inputAtMs", t}});
        for (int i = 0; i < 54; ++i) {
            t += 500;
            e.facts({{"focus", {{"workspaceId", 21}, {"appClass", "foot"}}}, {"inputAtMs", t}});
            e.tick(t, wall);
        }
        check(e.focus.state == "break" && !e.events.empty(), "scaled work interval ends: " + e.focus.state + " " + std::to_string(e.focus.phaseMs));
        auto hist = e.call("focus.history", Json::object(), t, wall);
        check(hist["intervals"].size() == 1 && hist["intervals"][0]["phase"] == "work" &&
                  hist["intervals"][0]["plannedMin"] == 25.0 && hist["intervals"][0]["workspaceId"] == 21 &&
                  hist["intervals"][0]["questId"] == "personal/notes",
              "history line");
        e.call("focus.pause", Json::object(), t, wall);
        e.call("guidance.set", {{"patch", {{"markers.max_revealed", 8}}}}, t, wall);
        check(code([&] { e.call("guidance.set", {{"patch", {{"markers.max_revealed", 99}}}}, t, wall); }) == -32602,
              "guidance range");
        e.call("quests.mark", {{"questRef", QuestBook::ref("notes")}, {"stepId", "step-1"}, {"objectiveId", "done"}}, t, wall);
        auto onDisk = [&] { return Json::parse(*readPrivate(paths.personal, 1 << 20))["quests"][0]["state"]; };
        check(onDisk() == "active", "personal quests debounced");
        e.tick(t + 6000, wall);
        check(onDisk() == "completed", "written after 5 s");
        struct stat st{};
        stat(paths.personal.c_str(), &st);
        check((st.st_mode & 077) == 0, "personal.json private");
        e.call("quests.create", {{"quest", {{"title", "Later"}, {"steps", {{{"title", "x"}}}}}}}, t, wall);
        e.flush(t + 6000, true);
    }
    Engine again(paths, 60);
    check(again.book.find("later") && again.book.detail("notes")["state"] == "completed", "flushed on exit and reloaded");
    check(again.focus.state == "paused" && again.focus.pausedState == "break", "session restored paused");
    check(again.guidance.get<int>("markers.max_revealed") == 8, "guidance persisted");
    std::ifstream history(paths.history);
    std::string all((std::istreambuf_iterator<char>(history)), {});
    check(all.find("title") == std::string::npos && all.find("Notes") == std::string::npos, "history holds no titles");
}

void input() {
    interaction::Input in;
    in.config["longHoldMs"] = 1000;
    auto release = [&](uint64_t at, const std::string &mode = "world") {
        in.event("world", "Tab", 0, true, 0);
        return in.event(mode, "Tab", 0, false, at);
    };
    auto r = release(50);
    check(r.size() == 2 && r[0].name == "markers.reveal" && r[0].pressed && !r[1].pressed, "tap at 50 ms");
    r = release(999);
    check(r.size() == 2 && r[0].name == "markers.reveal", "tap at 999 ms");
    r = release(1001);
    check(r.size() == 2 && r[0].name == "menu" && r[0].pressed, "release at 1001 ms is the hold");
    // Press fires nothing; the tick fires the hold; the release then fires nothing new.
    auto p = in.event("world", "Tab", 0, true, 0);
    check(p.size() == 1 && p[0].name == "menu" && !p[0].pressed, "press fires nothing");
    check(in.holds().size() == 1 && in.holds()[0]["action"] == "menu", "hold reported for the ring");
    check(in.tick(500, "world").empty(), "no hold before longHoldMs");
    auto fired = in.tick(1000, "world");
    check(fired.size() == 1 && fired[0].name == "menu" && fired[0].pressed, "hold fires at longHoldMs");
    r = in.event("world", "Tab", 0, false, 1200);
    check(r.size() == 1 && r[0].name == "menu" && !r[0].pressed, "release after the hold fires only its release");
    // Mode change while held: nothing.
    in.event("world", "Tab", 0, true, 0);
    check(in.tick(300, "menu").empty() && in.event("menu", "Tab", 0, false, 400).empty(), "mode change cancels");
    in.event("world", "Tab", 0, true, 0);
    check(in.event("interactive", "Tab", 0, false, 100).empty(), "release in another mode fires nothing");
    // Shift (sprint) held still taps.
    r = in.event("world", "Tab", interaction::Shift, true, 0);
    r = in.event("world", "Tab", interaction::Shift, false, 80);
    check(r.size() == 2 && r[0].name == "markers.reveal", "tap with Shift held");
    // Cleared input fires nothing.
    in.event("world", "Tab", 0, true, 0);
    in.clear();
    check(in.event("world", "Tab", 0, false, 50).empty(), "clear cancels");
    // Pairs are not conflicts; a second plain Tab is.
    check(interaction::inputConflicts(in.config).empty(), "tap/hold pair is not a conflict");
    auto conflicting = in.config;
    conflicting["modes"]["world"]["noclip"] = {"Tab"};
    check(!interaction::inputConflicts(conflicting).empty(), "two plain Tab bindings conflict");
    // Migration: files without markers.reveal get Tab unless plain Tab is taken.
    auto old = interaction::inputDefaults();
    old["modes"]["world"].erase("markers.reveal");
    check(interaction::validateInput(old)["modes"]["world"]["markers.reveal"] == Json({"Tab"}), "migration adds Tab");
    old["modes"]["world"]["noclip"] = {"Tab"};
    check(interaction::validateInput(old)["modes"]["world"]["markers.reveal"] == Json::array(), "migration respects plain Tab");
    // Older peers never see the action.
    auto legacy = interaction::withoutMarkersReveal(Json{{"config", in.config}});
    check(!legacy["config"]["modes"]["world"].contains("markers.reveal"), "pre-0.21 projection");
}

std::shared_ptr<world::World> lumenLike() {
    auto w = std::make_shared<world::World>();
    w->id = "com.example.test";
    w->areas = Json::parse(R"([
      {"id":"court","primaryAnchor":"court-wall","spawnId":"court","playable":{"volumes":[{"type":"box","min":[-20,-5,-20],"max":[20,30,20]}]}},
      {"id":"hall","primaryAnchor":"hall-wall","spawnId":"hall","playable":{"volumes":[{"type":"box","min":[-20,-5,-80],"max":[20,30,-20]}]}}])");
    w->anchors = Json::parse(R"([
      {"id":"court-wall","pose":{"position":[0,5,-10],"rotation":[0,0,0,1]},"size":[16,9]},
      {"id":"hall-wall","pose":{"position":[0,5,-70],"rotation":[0,0,0,1]},"size":[16,9]}])");
    w->manifest = Json::parse(R"({"zones":[{"id":"court","name":"Sun Court","bounds":{"min":[-20,-1,-20],"max":[20,20,20]}},
      {"id":"pond","name":"Pond","bounds":{"min":[30,-1,0],"max":[40,5,10]}}]})");
    w->places = Json::array();
    return w;
}

void markers() {
    auto w = lumenLike();
    Json bindings = Json::parse(R"([{"id":21,"worldId":"com.example.test","areaId":"court"},
                                    {"id":22,"worldId":"com.example.test","areaId":"hall"}])");
    auto top = resolveTarget(*w, {{"areaId", "court"}}, bindings);
    check(top && std::abs(top->y - (5 + 4.5f + .3f)) < 1e-4f && std::abs(top->z + 10) < 1e-4f, "area home: wall top + 0.3 m");
    auto ws = resolveTarget(*w, {{"workspaceId", 22}}, bindings);
    check(ws && std::abs(ws->z + 70) < 1e-4f, "workspace resolves through its home");
    check(!resolveTarget(*w, {{"workspaceId", 9}}, bindings), "workspace without a home here");
    auto zone = resolveTarget(*w, {{"zoneId", "pond"}}, bindings);
    check(zone && std::abs(zone->y - .7f) < 1e-4f && std::abs(zone->x - 35) < 1e-4f, "zone: centre, min y + 1.7");
    check(reaches(*w, {{"areaId", "hall"}}, std::nullopt, {0, 1.7f, -50}, bindings), "reach area by bounds");
    check(reaches(*w, {{"workspaceId", 22}}, std::nullopt, {0, 1.7f, -50}, bindings), "reach workspace by its area");
    check(!reaches(*w, {{"position", {0, 0, 0}}}, std::nullopt, {5, 0, 0}, bindings) &&
              reaches(*w, {{"position", {0, 0, 0}}}, 6.f, {5, 0, 0}, bindings),
          "reach radius");
    auto list = buildMarkers(w.get(), Json{{"id", "wp"}, {"label", "Here"}, {"position", {10, 1, 10}}}, Json::array(), bindings,
                             Json::array({Json{{"id", "quest:personal/a/s"}, {"kind", "quest"}, {"tier", 1}, {"label", "Go"},
                                               {"target", {{"workspaceId", 22}}}, {"tracked", true},
                                               {"source", {{"questId", "personal/a"}}}}}));
    std::set<std::string> ids;
    for (auto &m : list)
        ids.insert(m.id);
    check(ids.contains("waypoint:wp") && ids.contains("quest:personal/a/s") && ids.contains("home:court") &&
              ids.contains("home:hall") && ids.contains("place:zone-pond") && !ids.contains("place:zone-court"),
          "marker sources");
    check(list[0].tier == 1 && list.back().tier == 4, "sorted by tier");
    check(list[0].json().contains("range") && list[0].json()["label"].get<std::string>().size() <= 48, "marker record");

    // Reveal timing: tier-staggered 120 ms fade in, hold, 600 ms fade out; reduced motion instant.
    Reveal r;
    r.start(0, 4000, false, false, "tap");
    check(r.alpha(60, 1) > .4f && r.alpha(60, 1) < .6f && r.alpha(60, 3) == 0, "staggered fade in");
    check(r.alpha(300, 4) == 1 && r.alpha(4200, 1) == 1, "hold");
    check(r.alpha(4210 + 300, 1) > .4f && r.alpha(4210 + 300, 1) < .6f, "fade out halfway");
    check(!r.expired(4800) && r.expired(4210 + 600), "expires after the fade");
    r.start(4500, 4000, false, false, "tap");
    check(r.alpha(4500, 4) == 1 && r.alpha(8400, 1) == 1, "tap again restarts the hold");
    Reveal reduced;
    reduced.start(0, 4000, false, true, "tap");
    check(reduced.alpha(0, 4) == 1 && reduced.alpha(3999, 1) == 1 && reduced.alpha(4000, 1) == 0 && reduced.expired(4000) &&
              reduced.json()["fadeMs"] == 0,
          "reduced motion is instant");
    Reveal toggle;
    toggle.start(0, 4000, true, false, "tap");
    check(toggle.alpha(100000, 1) == 1 && toggle.json()["holdMs"] == 0, "toggle mode holds");
    toggle.start(5, 4000, true, false, "tap");
    check(!toggle.active, "second tap hides in toggle mode");

    // Layout under the clutter rules with a crowded scene.
    MarkerRuntime m;
    m.mode = "world";
    m.reveal.start(0, 4000, false, true, "tap");
    auto add = [&](std::string id, int tier, Vec3 p, std::string label = "Label") {
        Marker k;
        k.id = id;
        k.kind = tier == 1 ? "quest" : tier == 3 ? "home" : "place";
        k.icon = k.kind;
        k.tier = tier;
        k.label = label;
        k.position = p;
        k.rangeMin = 2;
        k.rangeMax = 1000;
        k.labelRange = 200;
        k.occlusion = "through";
        m.list.push_back(k);
    };
    for (int i = 0; i < 20; ++i) // in front, spread across the view, 20-58 m
        add("quest:in-" + std::to_string(i), 2, {float(i % 5 - 2) * 6, float(i / 5) * 3, -20.f - i * 2});
    for (int i = 0; i < 10; ++i) // behind
        add("quest:behind-" + std::to_string(i), 2, {float(i - 5) * 3, 0, 30.f + i});
    add("quest:tracked", 1, {0, 0, -40}, "Tracked objective");
    MarkerView v;
    v.camera.position = {0, 1.7f, 0};
    v.width = 1920;
    v.height = 1080;
    v.now = 1000;
    auto layout = m.layout(v);
    check(layout.inView == 12, "at most 12 in view (got " + std::to_string(layout.inView) + ")");
    check(layout.arrows >= 1 && layout.arrows <= 6, "at most 6 edge arrows (got " + std::to_string(layout.arrows) + ")");
    check(layout.labels <= 5, "at most 5 labels");
    bool trackedLabelled = false;
    for (auto &i : layout.items) {
        if (i.id == "quest:tracked")
            trackedLabelled = i.label.empty(); // within 120 px of the reticle: collapses to an icon
        if (!i.arrow)
            check(std::hypot(i.x - 960, i.y - 540) >= 120 || i.label.empty(), "labels collapse near the reticle");
        if (i.arrow && i.tier == 2)
            check(i.label.empty(), "tier 2 arrows are icon only");
    }
    check(trackedLabelled, "tracked marker at the reticle has no label");
    // Stacking: two markers 10 px apart merge.
    MarkerRuntime st;
    st.mode = "world";
    st.reveal.start(0, 4000, false, true, "tap");
    st.list = {};
    m.list.clear();
    add("place:a", 4, {10, 1.7f, -20});
    add("place:b", 4, {10.1f, 1.7f, -20});
    st.list = m.list;
    layout = st.layout(v);
    check(layout.items.size() == 1 && layout.items[0].stack == 2 && layout.stacks == 1, "stacking within 32 px");
    // Straight behind: bottom centre; behind and right: the right edge.
    m.list.clear();
    add("quest:behind", 2, {0, 2.5f, 30});
    add("quest:behind-right", 2, {25, 1.7f, 20});
    st.list = m.list;
    layout = st.layout(v);
    check(layout.arrows == 2 && std::abs(layout.items[0].x - 960) < 1 && layout.items[0].y > 900 &&
              layout.items[1].x > 1800,
          "behind arrows: bottom centre and the near side");
    // Edge arrows toward one direction stack.
    m.list.clear();
    add("quest:back-a", 2, {.3f, 1.7f, 20});
    add("quest:back-b", 2, {.35f, 1.7f, 25});
    st.list = m.list;
    layout = st.layout(v);
    check(layout.arrows == 1 && layout.items[0].arrow && layout.items[0].stack == 2, "arrows in one direction stack");
    m.list.clear();
    add("place:a", 4, {10, 1.7f, -20});
    add("place:b", 4, {10.1f, 1.7f, -20});
    st.list = m.list;
    // The aimed window hides markers; others dim them to 50 %.
    v.windows = {{1000, 0, 1920, 1080, true}};
    check(st.layout(v).items.empty(), "never over the aimed window");
    v.windows = {{1000, 0, 1920, 1080, false}};
    layout = st.layout(v);
    check(layout.items.size() == 1 && layout.items[0].dimmed && std::abs(layout.items[0].alpha - .5f) < 1e-3f,
          "50 % over other windows");
    v.windows.clear();
    // Calm mode: tier 1 only.
    st.calm = true;
    check(st.layout(v).items.empty(), "calm mode reveals tier 1 only");
    st.calm = false;
    // Tier distance rules: a place at 120 m is not revealed; arrival hides.
    st.list.clear();
    m.list.clear();
    add("place:far", 4, {0, 1.7f, -120});
    add("quest:here", 1, {0, 1.7f, -1});
    st.list = m.list;
    check(st.layout(v).items.empty(), "tier 4 beyond 80 m and arrival inside range[0]");
    // Persistent tracked marker: small diamond beyond 25 m, edge arrow off-screen, nothing in focus mode.
    MarkerRuntime p;
    p.mode = "interactive";
    m.list.clear();
    add("quest:ahead", 1, {0, 1.7f, -60}, "Ahead");
    m.list.back().tracked = true;
    add("quest:untracked", 1, {5, 1.7f, -60}, "Other");
    p.list = m.list;
    layout = p.layout(v);
    check(layout.items.size() == 1 && layout.items[0].small && layout.items[0].label.empty(), "persistent diamond, unlabelled");
    p.list[0].position = Vec3{0, 1.7f, 60};
    layout = p.layout(v);
    check(layout.items.size() == 1 && layout.items[0].arrow && layout.items[0].tracked, "persistent edge arrow");
    // Arrow sits on the inset ellipse.
    auto &a = layout.items[0];
    float ex = (a.x - 960) / (960 - 48), ey = (a.y - 540) / (540 - 48);
    check(std::abs(ex * ex + ey * ey - 1) < 1e-3f, "arrow on the 48 px inset ellipse");
    p.settings.persistent = "off";
    check(p.layout(v).items.empty(), "persistent off");
    p.mode = "hidden";
    p.settings.persistent = "edge";
    check(p.layout(v).items.empty(), "hidden in focus, menu and free cursor");
    // Level chevrons.
    MarkerRuntime lv;
    lv.mode = "world";
    lv.reveal.start(0, 4000, false, true, "tap");
    m.list.clear();
    add("quest:up", 1, {8, 12, -30});
    lv.list = m.list;
    layout = lv.layout(v);
    check(layout.items.size() == 1 && layout.items[0].chevron == 1, "chevron above 4 m");
    // update(): a mode change ends the reveal.
    MarkerRuntime u;
    MarkerRuntime::Context c;
    c.world = w.get();
    c.bindings = bindings;
    c.mode = "world";
    c.active = true;
    c.now = 0;
    u.update(c);
    u.tap(0);
    check(u.reveal.active, "tap reveals in world mode");
    c.mode = "interactive";
    c.now = 100;
    u.update(c);
    check(!u.reveal.active, "entering use mode ends the reveal");
    u.tap(200);
    check(!u.reveal.active, "no tap reveal outside world mode");
    check(u.stateKey()["list"].size() == u.list.size() && u.stateKey()["reveal"].is_null(), "state key");
}
} // namespace
int main() {
    char pattern[] = "/tmp/hn-quests-XXXXXX";
    std::filesystem::path dir = mkdtemp(pattern);
    try {
        guidance(dir);
        questBook();
        focus();
        engine(dir);
        input();
        markers();
    } catch (const std::exception &e) {
        std::cerr << "FAIL: exception " << e.what() << "\n";
        ++failures;
    }
    std::filesystem::remove_all(dir);
    if (failures)
        return 1;
    std::cout << "PASS: quests, focus sessions, guidance, markers, TAB tap/hold\n";
    return 0;
}
