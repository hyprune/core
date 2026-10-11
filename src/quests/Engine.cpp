#include "Engine.hpp"
#include "Files.hpp"
#include "ipc/Server.hpp"
#include <cmath>
#include <ctime>
#include <sstream>
#include <unistd.h>
namespace hyprune::quests {
namespace {
using ipc::Error;
void require(bool ok, const std::string &message, int code = -32602) {
    if (!ok)
        throw Error(code, message);
}
void exact(const Json &j, std::initializer_list<const char *> required, std::initializer_list<const char *> optional = {}) {
    require(j.is_object(), "params must be an object");
    for (auto k : required)
        require(j.contains(k), std::string("missing parameter: ") + k);
    for (auto &[key, value] : j.items()) {
        bool known = false;
        for (auto k : required)
            known |= key == k;
        for (auto k : optional)
            known |= key == k;
        require(known, "unknown parameter: " + key);
    }
}
std::string personalId(const Json &ref, bool forEdit = false) {
    require(ref.is_object() && ref.size() == 2 && ref.contains("source") && ref.contains("id") &&
                ref["source"].is_string() && ref["id"].is_string(),
            "questRef needs source and id");
    if (ref["source"] != QuestBook::Source)
        throw Error(forEdit ? -32007 : -32003, forEdit ? "only personal quests can be edited" : "unknown quest");
    return ref["id"];
}
int64_t clockMs(clockid_t id) {
    timespec t{};
    clock_gettime(id, &t);
    return int64_t(t.tv_sec) * 1000 + t.tv_nsec / 1000000;
}
std::string sessionId() {
    std::string id = ipc::randomId();
    return "focus-" + id.substr(0, 16);
}
} // namespace
Engine::Paths Engine::Paths::fromEnvironment() {
    auto state = stateHome(), config = configHome();
    return {state / "quests/personal.json", state / "focus/history.jsonl", state / "focus/session.json",
            config / "guidance.json"};
}
Engine::Engine(Paths paths, double timeScale) : guidance(paths.guidance), paths_(std::move(paths)), scale_(timeScale) {
    focus.scale = scale_;
    guidance.load();
    now_ = localTime(std::time(nullptr));
    try {
        if (auto data = readPrivate(paths_.personal, 1 << 20))
            book.restore(Json::parse(*data));
    } catch (const std::exception &e) {
        // Never overwrite a file we could not read: set it aside and start empty.
        std::fprintf(stderr, "hypruned: personal quests unreadable (%s); kept as .unreadable\n", e.what());
        std::error_code ignored;
        std::filesystem::rename(paths_.personal, paths_.personal.string() + ".unreadable", ignored);
    }
    lastBoot_ = clockMs(CLOCK_BOOTTIME);
    lastMono_ = clockMs(CLOCK_MONOTONIC);
    try {
        if (auto data = readPrivate(paths_.session, 65536))
            focus.restore(Json::parse(*data), lastMono_, std::time(nullptr));
    } catch (const std::exception &) {
        focus = FocusSession{};
        focus.scale = scale_;
    }
    // History keeps focus.history_days of lines; 0 keeps nothing.
    try {
        int days = guidance.get<int>("focus.history_days");
        if (days == 0)
            std::filesystem::remove(paths_.history);
        else if (auto data = readPrivate(paths_.history, 8 << 20)) {
            auto cutoff = std::time(nullptr) - std::time_t(days) * 86400;
            std::string kept, line;
            std::istringstream in(*data);
            while (std::getline(in, line))
                try {
                    auto j = Json::parse(line);
                    if (auto t = parseLocalTime(j.at("start")); t && *t >= cutoff)
                        kept += line + "\n";
                } catch (...) {
                }
            if (kept.size() != data->size())
                writeAtomic(paths_.history, kept);
        }
    } catch (const std::exception &) {
    }
    changed = true;
}
std::map<std::string, std::string> Engine::methods() {
    return {{"guidance.get", "state.read"},     {"guidance.set", "quest.control"}, {"quests.list", "state.read"},
            {"quests.get", "state.read"},       {"quests.track", "quest.control"}, {"quests.accept", "quest.control"},
            {"quests.abandon", "quest.control"}, {"quests.resume", "quest.control"}, {"quests.create", "quest.control"},
            {"quests.update", "quest.control"}, {"quests.delete", "quest.control"}, {"quests.mark", "quest.control"},
            {"focus.start", "quest.control"},   {"focus.pause", "quest.control"},  {"focus.resume", "quest.control"},
            {"focus.skip", "quest.control"},    {"focus.stop", "quest.control"},   {"focus.history", "state.read"}};
}
bool Engine::handles(const std::string &method) {
    return methods().contains(method);
}
void Engine::questEvents(const std::vector<Json> &list) {
    for (auto &e : list)
        events.push_back({"quest.progress", e});
}
void Engine::questsChanged(int64_t now) {
    changed = true;
    if (questsDirtyAt_ < 0)
        questsDirtyAt_ = now;
}
Json Engine::pickBreak() const {
    if (!eye_.is_array() || eye_.size() != 3)
        return nullptr;
    const Json *best = nullptr;
    double bestDistance = 0;
    for (const char *kind : {"place", "home"}) {
        for (auto &m : markers_) {
            if (m.value("kind", "") != kind || !m.contains("position") || !m["position"].is_array())
                continue;
            double dx = m["position"][0].get<double>() - eye_[0].get<double>(),
                   dy = m["position"][1].get<double>() - eye_[1].get<double>(),
                   dz = m["position"][2].get<double>() - eye_[2].get<double>();
            double d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d >= 30 && d <= 150 && (!best || d < bestDistance)) {
                best = &m;
                bestDistance = d;
            }
        }
        if (best)
            break;
    }
    if (!best)
        return nullptr;
    std::string label = (*best)["label"];
    return {{"markerId", "break:" + (*best)["id"].get<std::string>()}, {"label", label}, {"target", (*best)["target"]}};
}
void Engine::focusOutcomes(const std::vector<FocusSession::Outcome> &outcomes, int64_t now) {
    for (auto o : outcomes) {
        if (o.history && guidance.get<int>("focus.history_days") > 0)
            try {
                appendPrivate(paths_.history, o.history->dump() + "\n");
            } catch (const std::exception &e) {
                std::fprintf(stderr, "hypruned: focus history: %s\n", e.what());
            }
        if (o.completedWorkMin) {
            auto e = book.focusCompleted(focus.questRef, focus.objectiveId, *o.completedWorkMin, focus.settings.workMin, now_);
            if (!e.empty())
                questsChanged(now);
            questEvents(e);
        }
        if (o.phase.value("phase", "") == "break") {
            breakMarker_ = pickBreak();
            if (!breakMarker_.is_null())
                o.phase["suggestion"] = {{"markerId", breakMarker_["markerId"]}, {"label", breakMarker_["label"]}};
        } else if (o.phase.value("phase", "") != "paused")
            breakMarker_ = nullptr;
        events.push_back({"focus.phase", o.phase});
    }
    sessionDirty_ = true;
    changed = true;
}
Json Engine::history(int days, std::time_t wall) const {
    Json out = Json::array();
    auto data = readPrivate(paths_.history, 8 << 20);
    if (!data)
        return out;
    auto cutoff = wall - std::time_t(days) * 86400;
    std::vector<Json> lines;
    std::string line;
    std::istringstream in(*data);
    while (std::getline(in, line))
        try {
            auto j = Json::parse(line);
            if (auto t = parseLocalTime(j.at("start")); t && *t >= cutoff)
                lines.push_back(j);
        } catch (...) {
        }
    for (auto it = lines.rbegin(); it != lines.rend() && out.size() < 512; ++it)
        out.push_back(*it);
    return out;
}
Json Engine::call(const std::string &method, const Json &raw, int64_t now, std::time_t wall) {
    Json params = Json::object();
    for (auto &[key, value] : raw.items())
        if (!key.starts_with("_"))
            params[key] = value;
    now_ = localTime(wall);
    auto mutated = [&](std::vector<Json> e = {}) {
        questEvents(e);
        questsChanged(now);
        return Json{{"revision", 0}};
    };
    if (method == "guidance.get") {
        exact(params, {});
        return guidance.snapshot();
    }
    if (method == "guidance.set") {
        exact(params, {"patch"});
        try {
            guidance.set(params["patch"]);
        } catch (const std::invalid_argument &e) {
            throw Error(-32602, e.what());
        } catch (const std::exception &e) {
            throw Error(-32603, std::string("guidance not saved: ") + e.what());
        }
        changed = true;
        return guidance.snapshot();
    }
    if (method == "quests.list")
        return book.list(params, resolved_);
    if (method == "quests.get") {
        exact(params, {"questRef"});
        return {{"quest", book.detail(personalId(params["questRef"]))}};
    }
    if (method == "quests.track") {
        exact(params, {"questRef"});
        if (!params["questRef"].is_null())
            personalId(params["questRef"]);
        book.track(params["questRef"]);
        return mutated();
    }
    if (method == "quests.accept" || method == "quests.abandon" || method == "quests.resume") {
        exact(params, {"questRef"});
        auto id = personalId(params["questRef"]);
        return mutated(method == "quests.accept"    ? book.accept(id, now_)
                       : method == "quests.abandon" ? book.abandon(id, now_)
                                                    : book.resume(id, now_));
    }
    if (method == "quests.create") {
        exact(params, {"quest"});
        auto ref = book.create(params["quest"], now_);
        auto result = mutated();
        result["questRef"] = ref;
        return result;
    }
    if (method == "quests.update") {
        exact(params, {"questRef", "quest"});
        book.update(personalId(params["questRef"], true), params["quest"], now_);
        return mutated();
    }
    if (method == "quests.delete") {
        exact(params, {"questRef"});
        book.remove(personalId(params["questRef"], true));
        return mutated();
    }
    if (method == "quests.mark") {
        exact(params, {"questRef", "stepId", "objectiveId"});
        require(params["stepId"].is_string() && params["objectiveId"].is_string(), "stepId and objectiveId are strings");
        return mutated(book.mark(personalId(params["questRef"]), params["stepId"], params["objectiveId"], now_));
    }
    if (method == "focus.start") {
        exact(params, {}, {"target", "questRef", "objectiveId", "overrides"});
        require(focus.state == "idle", "a focus session is running", -32004);
        Json target, questRef = nullptr;
        std::string objectiveId;
        FocusSession::Settings settings;
        try {
            target = FocusSession::validTarget(params.value("target", Json(nullptr)));
            settings = FocusSession::settingsFrom(guidance.config(), params.value("overrides", Json(nullptr)));
        } catch (const std::invalid_argument &e) {
            throw Error(-32602, e.what());
        }
        if (params.contains("questRef") && !params["questRef"].is_null()) {
            auto id = personalId(params["questRef"]);
            require(book.find(id), "unknown quest", -32003);
            questRef = QuestBook::ref(id);
        }
        if (params.contains("objectiveId")) {
            require(!questRef.is_null() && params["objectiveId"].is_string(), "objectiveId needs a questRef");
            objectiveId = params["objectiveId"];
            bool found = false;
            for (auto &s : (*book.find(questRef["id"]))["steps"])
                for (auto &o : s["objectives"])
                    found |= o["id"] == objectiveId;
            require(found, "unknown objective", -32003);
        }
        auto id = sessionId();
        focusOutcomes(focus.start(now, wall, target, questRef, objectiveId, settings, id), now);
        return {{"revision", 0}, {"sessionId", id}};
    }
    if (method == "focus.pause" || method == "focus.resume" || method == "focus.skip" || method == "focus.stop") {
        exact(params, {});
        try {
            focusOutcomes(method == "focus.pause"    ? focus.pause(now, wall)
                          : method == "focus.resume" ? focus.resume(now, wall)
                          : method == "focus.skip"   ? focus.skip(now, wall)
                                                     : focus.stop(now, wall),
                          now);
        } catch (const std::logic_error &e) {
            throw Error(-32004, e.what());
        }
        return {{"revision", 0}};
    }
    if (method == "focus.history") {
        exact(params, {}, {"days"});
        int days = 30;
        if (params.contains("days")) {
            require(params["days"].is_number_integer() && params["days"] >= 1 && params["days"] <= 365, "days must be 1-365");
            days = params["days"];
        }
        return {{"intervals", history(days, wall)}};
    }
    throw Error(-32601, "method not advertised");
}
void Engine::facts(const Json &j) {
    if (!j.is_object())
        return;
    facts_.focus = j.value("focus", Json(nullptr));
    facts_.inputAtMs = j.value("inputAtMs", int64_t(0));
    facts_.locked = j.value("locked", false);
    std::set<std::string> inside;
    for (auto &k : j.value("inside", Json::array()))
        if (k.is_string())
            inside.insert(k.get<std::string>());
    now_ = localTime(std::time(nullptr));
    auto e = book.reached(inside, now_);
    if (!e.empty()) {
        questEvents(e);
        questsChanged(clockMs(CLOCK_MONOTONIC));
    }
}
void Engine::pluginState(const Json &state) {
    if (state.contains("movement") && state["movement"].is_object())
        eye_ = state["movement"].value("position", Json(nullptr));
    resolved_.clear();
    markers_ = Json::array();
    if (state.contains("markers") && state["markers"].is_object())
        markers_ = state["markers"].value("list", Json::array());
    for (auto &m : markers_)
        if (m.value("kind", "") == "quest" && m.contains("position") && !m["position"].is_null() &&
            m.contains("source") && m["source"].contains("questId"))
            resolved_.insert(m["source"]["questId"].get<std::string>());
}
void Engine::tick(int64_t now, std::time_t wall) {
    now_ = localTime(wall);
    auto boot = clockMs(CLOCK_BOOTTIME), mono = clockMs(CLOCK_MONOTONIC);
    bool suspended = (boot - lastBoot_) - (mono - lastMono_) > 5000;
    lastBoot_ = boot;
    lastMono_ = mono;
    if (suspended && focus.running())
        focusOutcomes(focus.pause(now, wall), now); // suspend pauses; it resumes paused
    bool creditChanged = false;
    auto outcomes = focus.tick(now, wall, facts_, creditChanged);
    if (!outcomes.empty())
        focusOutcomes(outcomes, now);
    if (creditChanged)
        changed = true;
    if (now - lastExpire_ >= 30000) {
        lastExpire_ = now;
        auto e = book.expire(wall, now_);
        if (!e.empty()) {
            questEvents(e);
            questsChanged(now);
        }
    }
    if (questsDirtyAt_ >= 0 && now - questsDirtyAt_ >= 5000)
        flush(now, false);
    if (sessionDirty_)
        writeSession(now);
}
void Engine::writeSession(int64_t now) {
    sessionDirty_ = false;
    try {
        auto j = focus.file(now);
        if (j.is_null())
            std::filesystem::remove(paths_.session);
        else
            writeAtomic(paths_.session, j.dump() + "\n");
    } catch (const std::exception &e) {
        std::fprintf(stderr, "hypruned: focus session: %s\n", e.what());
    }
}
void Engine::flush(int64_t now, bool force) {
    if (questsDirtyAt_ >= 0) {
        try {
            auto bytes = book.file().dump(1) + "\n";
            if (bytes.size() > (1 << 20))
                throw std::runtime_error("personal quests exceed 1 MiB");
            writeAtomic(paths_.personal, bytes);
            questsDirtyAt_ = -1;
        } catch (const std::exception &e) {
            std::fprintf(stderr, "hypruned: personal quests not saved: %s\n", e.what());
            questsDirtyAt_ = now; // retry after the debounce
        }
    }
    if (force && focus.running()) {
        // Core exit pauses the session; the file restores it paused.
        focus.pause(now, std::time(nullptr));
        sessionDirty_ = true;
    }
    if (sessionDirty_ || force)
        writeSession(now);
}
Json Engine::decorate(Json state, int64_t now) const {
    if (!state.is_object())
        return state;
    state["quests"] = book.stateKey();
    state["focus"] = focus.stateKey(now);
    return state;
}
std::optional<Json> Engine::sync() {
    auto markers = book.markers();
    if (focus.state != "idle" && focus.target.is_object() && focus.target.contains("workspaceId")) {
        auto ws = focus.target["workspaceId"].get<int64_t>();
        markers.push_back({{"id", "focus:workspace-" + std::to_string(ws)},
                           {"kind", "focus"},
                           {"tier", 1},
                           {"label", focus.target["label"]},
                           {"icon", "focus"},
                           {"target", {{"workspaceId", ws}}},
                           {"tracked", false},
                           {"source", {{"focus", true}, {"workspaceId", ws}}}});
    }
    if (!breakMarker_.is_null() && (focus.state == "break" || (focus.state == "paused" && focus.pausedState == "break")))
        markers.push_back({{"id", breakMarker_["markerId"]},
                           {"kind", "break"},
                           {"tier", 1},
                           {"label", "Break: " + breakMarker_["label"].get<std::string>().substr(0, 40)},
                           {"icon", "break"},
                           {"target", breakMarker_["target"]},
                           {"tracked", false},
                           {"source", {{"break", true}}}});
    auto config = guidance.config();
    Json message = {{"markers", markers},
                    {"watch", book.watch()},
                    {"calm", focus.calmNow()},
                    {"guidance",
                     {{"revealMs", config["markers.reveal_ms"]},
                      {"revealMode", config["markers.reveal_mode"]},
                      {"maxRevealed", config["markers.max_revealed"]},
                      {"labels", config["markers.labels"]},
                      {"persistent", config["markers.persistent"]},
                      {"coreDraw", config["markers.core_draw"]}}}};
    if (message == lastSync_)
        return std::nullopt;
    lastSync_ = message;
    return message;
}
} // namespace hyprune::quests
