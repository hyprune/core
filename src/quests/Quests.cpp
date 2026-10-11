#include "Quests.hpp"
#include "Files.hpp"
#include "ipc/Server.hpp"
#include "ipc/Text.hpp"
#include <algorithm>
#include <cmath>
#include <regex>
namespace hyprune::quests {
namespace {
using ipc::Error;
void require(bool ok, const std::string &message, int code = -32602) {
    if (!ok)
        throw Error(code, message);
}
bool isSlug(const Json &v) {
    static const std::regex pattern("[a-z0-9][a-z0-9-]{0,63}");
    return v.is_string() && std::regex_match(v.get<std::string>(), pattern);
}
std::string text(const Json &v, const char *field, size_t lo, size_t hi) {
    require(v.is_string(), std::string(field) + " must be a string");
    auto s = v.get<std::string>();
    require(ipc::boundedText(s, 100000) == s, std::string(field) + " must be valid UTF-8");
    auto n = ipc::codePoints(s);
    require(n >= lo && n <= hi, std::string(field) + " length");
    for (unsigned char c : s)
        require(c >= 32 && c != 127, std::string(field) + " must be plain text");
    return s;
}
void keys(const Json &j, std::initializer_list<const char *> allowed, const char *what) {
    require(j.is_object(), std::string(what) + " must be an object");
    for (auto &[key, value] : j.items())
        require(std::find_if(allowed.begin(), allowed.end(), [&](auto a) { return key == a; }) != allowed.end(),
                std::string("unknown ") + what + " field: " + key);
}
std::string pretty(const std::string &id) {
    std::string out;
    bool start = true;
    for (char c : id) {
        if (c == '-' || c == '_') {
            out += ' ';
            start = true;
        } else {
            out += start ? char(std::toupper((unsigned char)c)) : c;
            start = false;
        }
    }
    return out;
}
std::string label48(const std::string &s) {
    return ipc::boundedText(s, 48);
}
} // namespace
std::string QuestBook::slug(const std::string &title) {
    std::string out;
    for (unsigned char c : title) {
        if (std::isalnum(c) && c < 128)
            out += char(std::tolower(c));
        else if (!out.empty() && out.back() != '-')
            out += '-';
        if (out.size() >= 48)
            break;
    }
    while (!out.empty() && out.back() == '-')
        out.pop_back();
    return out.empty() ? "quest" : out;
}
std::string QuestBook::targetName(const Json &t) {
    if (t.contains("workspaceId"))
        return "workspace " + std::to_string(t["workspaceId"].get<int64_t>());
    for (auto key : {"areaId", "zoneId", "placeId"})
        if (t.contains(key))
            return pretty(t[key].get<std::string>());
    return "the marked spot";
}
Json QuestBook::validTarget(const Json &t) {
    require(t.is_object() && !t.empty(), "target must be an object");
    int kinds = 0;
    for (auto &[key, value] : t.items()) {
        if (key == "position") {
            require(value.is_array() && value.size() == 3, "target position needs [x, y, z]");
            for (auto &v : value)
                require(v.is_number() && std::isfinite(v.get<double>()) && std::abs(v.get<double>()) <= 100000,
                        "target position range");
            ++kinds;
        } else if (key == "workspaceId") {
            require(value.is_number_integer() && value.get<int64_t>() >= 1 && value.get<int64_t>() <= 2147483647,
                    "workspaceId range");
            ++kinds;
        } else if (key == "zoneId" || key == "areaId" || key == "placeId" || key == "worldId") {
            require(value.is_string() && !value.get<std::string>().empty() && value.get<std::string>().size() <= 128,
                    key + " must be a 1-128 byte string");
            if (key != "worldId")
                ++kinds;
        } else
            require(false, "unknown target field: " + key);
    }
    require(kinds == 1, "target needs exactly one of position, zoneId, areaId, placeId, workspaceId");
    require(!t.contains("worldId") || t.contains("position"), "worldId only qualifies a position target");
    return t;
}
Json QuestBook::normalize(const Json &input) {
    keys(input, {"id", "title", "summary", "category", "sequential", "expires", "steps"}, "quest");
    require(input.contains("title"), "quest needs a title");
    Json q = {{"title", text(input["title"], "title", 1, 64)},
              {"summary", input.contains("summary") ? text(input["summary"], "summary", 0, 1000) : ""},
              {"category", "personal"},
              {"sequential", true},
              {"expires", nullptr}};
    if (input.contains("id")) {
        require(isSlug(input["id"]), "quest id must be a slug");
        q["id"] = input["id"];
    }
    if (input.contains("category"))
        require(input["category"] == "personal", "personal quests have category personal");
    if (input.contains("sequential")) {
        require(input["sequential"].is_boolean(), "sequential must be boolean");
        q["sequential"] = input["sequential"];
    }
    if (input.contains("expires") && !input["expires"].is_null()) {
        require(input["expires"].is_string() && parseLocalTime(input["expires"].get<std::string>()),
                "expires must be a local date-time YYYY-MM-DDTHH:MM");
        q["expires"] = input["expires"];
    }
    require(input.contains("steps") && input["steps"].is_array() && !input["steps"].empty() &&
                input["steps"].size() <= 16,
            "quest needs 1-16 steps");
    q["steps"] = Json::array();
    std::set<std::string> stepIds;
    for (size_t i = 0; i < input["steps"].size(); ++i) {
        auto &in = input["steps"][i];
        keys(in, {"id", "title", "optional", "any", "target", "objectives"}, "step");
        require(in.contains("title"), "step needs a title");
        Json step = {{"title", text(in["title"], "step title", 1, 64)}, {"optional", false}, {"any", false},
                     {"target", nullptr}};
        if (in.contains("id")) {
            require(isSlug(in["id"]), "step id must be a slug");
            step["id"] = in["id"];
        } else {
            for (size_t n = i + 1;; ++n)
                if (!stepIds.contains("step-" + std::to_string(n))) {
                    step["id"] = "step-" + std::to_string(n);
                    break;
                }
        }
        require(stepIds.insert(step["id"]).second, "duplicate step id");
        for (auto flag : {"optional", "any"})
            if (in.contains(flag)) {
                require(in[flag].is_boolean(), std::string(flag) + " must be boolean");
                step[flag] = in[flag];
            }
        if (in.contains("target") && !in["target"].is_null())
            step["target"] = validTarget(in["target"]);
        auto objectives = in.value("objectives", Json::array({{{"id", "done"}, {"type", "manual"}}}));
        require(objectives.is_array() && !objectives.empty() && objectives.size() <= 8, "step needs 1-8 objectives");
        step["objectives"] = Json::array();
        std::set<std::string> objectiveIds;
        for (auto &o : objectives) {
            keys(o, {"id", "type", "count", "label", "target", "radius", "sessions", "minutes", "focusTarget"},
                 "objective");
            require(o.contains("type") && (o["type"] == "manual" || o["type"] == "reach" || o["type"] == "focus"),
                    "objective type must be manual, reach or focus (personal quests)");
            std::string type = o["type"];
            Json out = {{"type", type}, {"count", 1}};
            if (o.contains("id")) {
                require(isSlug(o["id"]), "objective id must be a slug");
                out["id"] = o["id"];
            } else
                out["id"] = objectiveIds.contains(type) ? type + "-" + std::to_string(objectiveIds.size() + 1) : type;
            require(objectiveIds.insert(out["id"]).second, "duplicate objective id");
            if (o.contains("count")) {
                require(o["count"].is_number_integer() && o["count"] >= 1 && o["count"] <= 100, "count must be 1-100");
                out["count"] = o["count"];
            }
            if (type == "reach") {
                if (o.contains("target"))
                    out["target"] = validTarget(o["target"]);
                require(out.contains("target") || !step["target"].is_null(),
                        "a reach objective needs a target (its own or the step's)");
                if (o.contains("radius")) {
                    require(o["radius"].is_number() && o["radius"] >= .5 && o["radius"] <= 100, "radius must be 0.5-100");
                    out["radius"] = o["radius"];
                }
            } else
                require(!o.contains("target") && !o.contains("radius"), "only reach objectives take a target or radius");
            if (type == "focus") {
                int sessions = 1;
                if (o.contains("sessions")) {
                    require(o["sessions"].is_number_integer() && o["sessions"] >= 1 && o["sessions"] <= 12,
                            "sessions must be 1-12");
                    sessions = o["sessions"];
                }
                out["sessions"] = sessions;
                out["count"] = sessions;
                if (o.contains("minutes")) {
                    require(o["minutes"].is_number() && o["minutes"] >= 1 && o["minutes"] <= 120,
                            "minutes must be 1-120");
                    out["minutes"] = o["minutes"];
                }
                out["focusTarget"] = "quest";
                if (o.contains("focusTarget")) {
                    require(o["focusTarget"] == "quest" || o["focusTarget"] == "any", "focusTarget must be quest or any");
                    out["focusTarget"] = o["focusTarget"];
                }
            } else
                require(!o.contains("sessions") && !o.contains("minutes") && !o.contains("focusTarget"),
                        "only focus objectives take sessions, minutes or focusTarget");
            if (o.contains("label"))
                out["label"] = text(o["label"], "label", 0, 64);
            if (!out.contains("label") || out["label"] == "") {
                if (type == "manual")
                    out["label"] = "Done";
                else if (type == "reach")
                    out["label"] = ipc::boundedText(
                        "Reach " + targetName(out.contains("target") ? out["target"] : step["target"]), 64);
                else {
                    int n = out["sessions"];
                    out["label"] = std::to_string(n) + (n == 1 ? " focus session" : " focus sessions");
                }
            }
            step["objectives"].push_back(out);
        }
        q["steps"].push_back(step);
    }
    return q;
}
const Json *QuestBook::find(const std::string &id) const {
    for (auto &q : quests)
        if (q["id"] == id)
            return &q;
    return nullptr;
}
Json *QuestBook::find(const std::string &id) {
    for (auto &q : quests)
        if (q["id"] == id)
            return &q;
    return nullptr;
}
bool QuestBook::stepDone(const Json &quest, const Json &step) {
    auto &progress = quest["progress"];
    int satisfied = 0;
    for (auto &o : step["objectives"]) {
        int done = progress.contains(step["id"]) ? progress[step["id"]].value(o["id"].get<std::string>(), 0) : 0;
        satisfied += done >= o["count"].get<int>();
    }
    return step["any"].get<bool>() ? satisfied > 0 : satisfied == int(step["objectives"].size());
}
bool QuestBook::stepOpen(const Json &quest, size_t index) {
    if (quest["state"] != "active")
        return false;
    auto &steps = quest["steps"];
    if (stepDone(quest, steps[index]))
        return false;
    if (!quest["sequential"].get<bool>())
        return true;
    for (size_t i = 0; i < index; ++i)
        if (!steps[i]["optional"].get<bool>() && !stepDone(quest, steps[i]))
            return false;
    return true;
}
bool QuestBook::questDone(const Json &quest) {
    bool anyRequired = false;
    for (auto &s : quest["steps"])
        if (!s["optional"].get<bool>()) {
            anyRequired = true;
            if (!stepDone(quest, s))
                return false;
        }
    if (anyRequired)
        return true;
    for (auto &s : quest["steps"])
        if (!stepDone(quest, s))
            return false;
    return true;
}
const Json *QuestBook::currentStep(const Json &quest) {
    for (size_t i = 0; i < quest["steps"].size(); ++i)
        if (stepOpen(quest, i))
            return &quest["steps"][i];
    if (quest["state"] != "completed")
        for (auto &s : quest["steps"])
            if (!stepDone(quest, s))
                return &s;
    return nullptr;
}
Json QuestBook::stepProgress(const Json &quest, const Json &step) {
    auto &objectives = step["objectives"];
    auto done = [&](const Json &o) {
        int n = quest["progress"].contains(step["id"]) ? quest["progress"][step["id"]].value(o["id"].get<std::string>(), 0)
                                                       : 0;
        return std::min(n, o["count"].get<int>());
    };
    if (objectives.size() == 1)
        return {done(objectives[0]), objectives[0]["count"]};
    int satisfied = 0;
    for (auto &o : objectives)
        satisfied += done(o) >= o["count"].get<int>();
    if (step["any"].get<bool>())
        return {satisfied > 0 ? 1 : 0, 1};
    return {satisfied, objectives.size()};
}
Json QuestBook::questProgress(const Json &quest) {
    int required = 0, done = 0, all = 0, allDone = 0;
    for (auto &s : quest["steps"]) {
        bool d = stepDone(quest, s);
        ++all;
        allDone += d;
        if (!s["optional"].get<bool>()) {
            ++required;
            done += d;
        }
    }
    return required ? Json{done, required} : Json{allDone, all};
}
Json QuestBook::create(const Json &input, const std::string &now) {
    require(quests.size() < MaxPersonal, "at most 64 personal quests", -32008);
    auto q = normalize(input);
    std::string id = q.value("id", slug(q["title"]));
    if (!input.contains("id"))
        for (int n = 2; find(id); ++n)
            id = slug(q["title"]).substr(0, 58) + "-" + std::to_string(n);
    require(!find(id), "a personal quest with this id exists", -32004);
    q["id"] = id;
    q["state"] = "active";
    q["progress"] = Json::object();
    q["credited"] = Json::object();
    q["createdAt"] = q["updatedAt"] = now;
    q["completedAt"] = nullptr;
    quests.push_back(q);
    if (tracked.is_null())
        tracked = ref(id);
    return ref(id);
}
void QuestBook::update(const std::string &id, const Json &input, const std::string &now) {
    auto *q = find(id);
    require(q, "unknown quest", -32003);
    auto next = normalize(input);
    next["id"] = id;
    // Keep the progress of every step and objective ID that still exists (clamped, monotonic).
    Json progress = Json::object(), credited = Json::object();
    for (auto &s : next["steps"])
        for (auto &o : s["objectives"]) {
            std::string sid = s["id"], oid = o["id"];
            if ((*q)["progress"].contains(sid) && (*q)["progress"][sid].contains(oid))
                progress[sid][oid] = std::min((*q)["progress"][sid][oid].get<int>(), o["count"].get<int>());
            if ((*q)["credited"].contains(sid) && (*q)["credited"][sid].contains(oid))
                credited[sid][oid] = (*q)["credited"][sid][oid];
        }
    for (auto key : {"state", "createdAt", "completedAt"})
        next[key] = (*q)[key];
    next["progress"] = progress;
    next["credited"] = credited;
    next["updatedAt"] = now;
    if (next["state"] == "active" && questDone(next)) {
        next["state"] = "completed";
        next["completedAt"] = now;
    } else if (next["state"] == "completed" && !questDone(next)) {
        next["state"] = "active"; // a new step reopens the quest
        next["completedAt"] = nullptr;
    }
    *q = next;
    if (next["state"] != "active" && tracked == ref(id))
        tracked = nullptr;
}
void QuestBook::remove(const std::string &id) {
    require(find(id), "unknown quest", -32003);
    std::erase_if(quests.get_ref<Json::array_t &>(), [&](auto &q) { return q["id"] == id; });
    if (tracked == ref(id))
        tracked = nullptr;
}
void QuestBook::track(const Json &r) {
    if (r.is_null()) {
        tracked = nullptr;
        return;
    }
    require(r.is_object() && r.size() == 2 && r.contains("source") && r.contains("id") && r["source"].is_string() &&
                r["id"].is_string(),
            "questRef needs source and id");
    require(r["source"] == Source && find(r["id"]), "unknown quest", -32003);
    require((*find(r["id"]))["state"] == "active", "only an active quest can be tracked", -32004);
    tracked = ref(r["id"]);
}
std::vector<Json> QuestBook::accept(const std::string &id, const std::string &now) {
    auto *q = find(id);
    require(q, "unknown quest", -32003);
    require((*q)["state"] == "available", "only an available quest can be accepted", -32004);
    (*q)["state"] = "active";
    (*q)["updatedAt"] = now;
    if (tracked.is_null())
        tracked = ref(id);
    return {};
}
std::vector<Json> QuestBook::abandon(const std::string &id, const std::string &now) {
    auto *q = find(id);
    require(q, "unknown quest", -32003);
    require((*q)["state"] == "active" || (*q)["state"] == "available", "only an active quest can be set aside", -32004);
    (*q)["state"] = "abandoned";
    (*q)["updatedAt"] = now;
    if (tracked == ref(id))
        tracked = nullptr;
    return {};
}
std::vector<Json> QuestBook::resume(const std::string &id, const std::string &now) {
    auto *q = find(id);
    require(q, "unknown quest", -32003);
    require((*q)["state"] == "abandoned" || (*q)["state"] == "failed", "only a failed or set-aside quest resumes",
            -32004);
    // A quest resumed after its date passed keeps no date: it would fail again at once.
    if ((*q)["state"] == "failed")
        (*q)["expires"] = nullptr;
    (*q)["state"] = "active";
    (*q)["updatedAt"] = now;
    if (tracked.is_null())
        tracked = ref(id);
    return {};
}
std::vector<Json> QuestBook::credit(Json &quest, const std::string &stepId, const std::string &objectiveId, int amount,
                                    const std::string &now) {
    std::vector<Json> events;
    const Json *step = nullptr, *objective = nullptr;
    size_t index = 0;
    for (size_t i = 0; i < quest["steps"].size(); ++i)
        if (quest["steps"][i]["id"] == stepId) {
            step = &quest["steps"][i];
            index = i;
        }
    if (!step || !stepOpen(quest, index))
        return events;
    for (auto &o : (*step)["objectives"])
        if (o["id"] == objectiveId)
            objective = &o;
    if (!objective)
        return events;
    int count = (*objective)["count"], before = quest["progress"].contains(stepId)
                                                     ? quest["progress"][stepId].value(objectiveId, 0)
                                                     : 0;
    if (before >= count)
        return events;
    int after = std::min(count, before + amount);
    quest["progress"][stepId][objectiveId] = after;
    quest["updatedAt"] = now;
    Json r = ref(quest["id"]);
    std::string title = quest["title"];
    events.push_back({{"questRef", r},
                      {"kind", "objective"},
                      {"title", title},
                      {"stepId", stepId},
                      {"stepTitle", (*step)["title"]},
                      {"objectiveId", objectiveId},
                      {"progress", {after, count}}});
    if (questDone(quest)) {
        quest["state"] = "completed";
        quest["completedAt"] = now;
        events.push_back({{"questRef", r}, {"kind", "completed"}, {"title", title}});
        if (tracked == r)
            tracked = nullptr;
    } else if (stepDone(quest, *step)) {
        Json e = {{"questRef", r}, {"kind", "step"}, {"title", title}, {"stepId", stepId}, {"stepTitle", (*step)["title"]}};
        if (auto *next = currentStep(quest))
            e["nextStepTitle"] = (*next)["title"];
        events.push_back(e);
    }
    return events;
}
std::vector<Json> QuestBook::mark(const std::string &id, const std::string &stepId, const std::string &objectiveId,
                                  const std::string &now) {
    auto *q = find(id);
    require(q, "unknown quest", -32003);
    const Json *step = nullptr;
    size_t index = 0;
    for (size_t i = 0; i < (*q)["steps"].size(); ++i)
        if ((*q)["steps"][i]["id"] == stepId) {
            step = &(*q)["steps"][i];
            index = i;
        }
    require(step, "unknown step", -32003);
    const Json *objective = nullptr;
    for (auto &o : (*step)["objectives"])
        if (o["id"] == objectiveId)
            objective = &o;
    require(objective, "unknown objective", -32003);
    require((*objective)["type"] == "manual", "only manual objectives are marked done", -32004);
    require((*q)["state"] == "active", "the quest is not active", -32004);
    require(stepOpen(*q, index), "the step is not open", -32004);
    return credit(*q, stepId, objectiveId, 1, now);
}
std::vector<Json> QuestBook::reached(const std::set<std::string> &inside, const std::string &now) {
    std::vector<Json> events;
    for (auto &key : inside) {
        if (inside_.contains(key))
            continue;
        // "<source>/<quest>/<step>/<objective>"
        auto a = key.find('/'), b = key.find('/', a + 1), c = key.find('/', b + 1);
        if (a == key.npos || b == key.npos || c == key.npos || key.substr(0, a) != Source)
            continue;
        auto *q = find(key.substr(a + 1, b - a - 1));
        if (!q)
            continue;
        auto stepId = key.substr(b + 1, c - b - 1), objectiveId = key.substr(c + 1);
        bool isReach = false;
        for (auto &s : (*q)["steps"])
            if (s["id"] == stepId)
                for (auto &o : s["objectives"])
                    isReach |= o["id"] == objectiveId && o["type"] == "reach";
        if (!isReach)
            continue;
        auto e = credit(*q, stepId, objectiveId, 1, now);
        events.insert(events.end(), e.begin(), e.end());
    }
    inside_ = inside;
    return events;
}
std::vector<Json> QuestBook::focusCompleted(const Json &questRef, const std::string &objectiveId, double creditedMin,
                                            double workMin, const std::string &now) {
    std::vector<Json> events;
    for (auto &q : quests) {
        if (q["state"] != "active")
            continue;
        bool fromThis = questRef.is_object() && questRef.value("source", "") == Source && Json(questRef.value("id", "")) == q["id"];
        // One event advances at most one objective per quest.
        bool done = false;
        for (size_t i = 0; i < q["steps"].size() && !done; ++i) {
            if (!stepOpen(q, i))
                continue;
            auto step = q["steps"][i];
            for (auto &o : step["objectives"]) {
                if (o["type"] != "focus")
                    continue;
                if (o["focusTarget"] == "quest" && !fromThis)
                    continue;
                if (fromThis && !objectiveId.empty() && o["id"] != objectiveId && o["focusTarget"] == "quest")
                    continue;
                double minimum = o.contains("minutes") ? o["minutes"].get<double>() : workMin * .8;
                std::string sid = step["id"], oid = o["id"];
                double total = (q["credited"].contains(sid) ? q["credited"][sid].value(oid, 0.) : 0.) + creditedMin;
                q["credited"][sid][oid] = std::round(total * 10) / 10;
                if (creditedMin + 1e-6 < minimum)
                    continue;
                auto e = credit(q, sid, oid, 1, now);
                events.insert(events.end(), e.begin(), e.end());
                done = true;
                break;
            }
        }
    }
    return events;
}
std::vector<Json> QuestBook::expire(std::time_t now, const std::string &nowText) {
    std::vector<Json> events;
    for (auto &q : quests)
        if (q["state"] == "active" && q["expires"].is_string()) {
            auto due = parseLocalTime(q["expires"]);
            if (due && *due <= now) {
                q["state"] = "failed";
                q["updatedAt"] = nowText;
                if (tracked == ref(q["id"]))
                    tracked = nullptr;
                events.push_back({{"questRef", ref(q["id"])}, {"kind", "failed"}, {"title", q["title"]}});
            }
        }
    return events;
}
Json QuestBook::summary(const Json &q, bool offWorld) const {
    Json step = nullptr;
    if (q["state"] == "active" || q["state"] == "abandoned" || q["state"] == "failed")
        if (auto *s = currentStep(q))
            step = {{"id", (*s)["id"]}, {"title", (*s)["title"]}, {"progress", stepProgress(q, *s)}};
    return {{"questRef", ref(q["id"])},
            {"title", q["title"]},
            {"category", "personal"},
            {"state", q["state"]},
            {"tracked", tracked == ref(q["id"])},
            {"step", step},
            {"progress", questProgress(q)},
            {"offWorld", offWorld},
            {"updatedAt", q["updatedAt"]}};
}
Json QuestBook::detail(const std::string &id) const {
    auto *q = find(id);
    require(q, "unknown quest", -32003);
    auto *current = currentStep(*q);
    Json steps = Json::array();
    for (size_t i = 0; i < (*q)["steps"].size(); ++i) {
        auto &s = (*q)["steps"][i];
        Json objectives = Json::array();
        for (auto &o : s["objectives"]) {
            Json out = {{"id", o["id"]},
                        {"type", o["type"]},
                        {"label", o["label"]},
                        {"count", o["count"]},
                        {"done", std::min(o["count"].get<int>(),
                                          (*q)["progress"].contains(s["id"])
                                              ? (*q)["progress"][s["id"]].value(o["id"].get<std::string>(), 0)
                                              : 0)}};
            for (auto key : {"target", "radius", "sessions", "minutes", "focusTarget"})
                if (o.contains(key))
                    out[key] = o[key];
            if (o["type"] == "focus")
                out["creditedMinutes"] =
                    (*q)["credited"].contains(s["id"]) ? (*q)["credited"][s["id"]].value(o["id"].get<std::string>(), 0.)
                                                       : 0.;
            objectives.push_back(out);
        }
        steps.push_back({{"id", s["id"]},
                         {"title", s["title"]},
                         {"optional", s["optional"]},
                         {"any", s["any"]},
                         {"state", stepDone(*q, s) ? "done" : current == &s ? "current" : "pending"},
                         {"target", s["target"]},
                         {"objectives", objectives}});
    }
    return {{"questRef", ref(id)},
            {"title", (*q)["title"]},
            {"summary", (*q)["summary"]},
            {"category", "personal"},
            {"state", (*q)["state"]},
            {"tracked", tracked == ref(id)},
            {"sequential", (*q)["sequential"]},
            {"expires", (*q)["expires"]},
            {"createdAt", (*q)["createdAt"]},
            {"updatedAt", (*q)["updatedAt"]},
            {"completedAt", (*q)["completedAt"]},
            {"personal", true},
            {"steps", steps}};
}
Json QuestBook::list(const Json &params, const std::set<std::string> &resolved) const {
    keys(params, {"states", "sources"}, "params");
    std::set<std::string> states, sources;
    for (auto [field, set] : {std::pair{"states", &states}, std::pair{"sources", &sources}})
        if (params.contains(field)) {
            require(params[field].is_array() && params[field].size() <= 8, std::string(field) + " must be a list");
            for (auto &v : params[field]) {
                require(v.is_string(), std::string(field) + " must be strings");
                set->insert(v.get<std::string>());
            }
        }
    for (auto &s : states)
        require(s == "available" || s == "active" || s == "completed" || s == "failed" || s == "abandoned",
                "unknown quest state");
    Json out = Json::array();
    if (!sources.empty() && !sources.contains(Source))
        return {{"quests", out}};
    std::vector<const Json *> order;
    for (auto &q : quests)
        if (states.empty() || states.contains(q["state"]))
            order.push_back(&q);
    auto rank = [&](const Json *q) {
        auto s = (*q)["state"].get<std::string>();
        return tracked == ref((*q)["id"]) ? 0 : s == "active" ? 1 : s == "available" ? 2 : s == "completed" ? 3 : 4;
    };
    std::stable_sort(order.begin(), order.end(), [&](auto *a, auto *b) {
        if (rank(a) != rank(b))
            return rank(a) < rank(b);
        return (*a)["updatedAt"].template get<std::string>() > (*b)["updatedAt"].template get<std::string>();
    });
    for (auto *q : order) {
        bool targeted = false;
        for (auto &s : (*q)["steps"]) {
            targeted |= !s["target"].is_null();
            for (auto &o : s["objectives"])
                targeted |= o.contains("target");
        }
        out.push_back(summary(*q, targeted && !resolved.contains(std::string(Source) + "/" + (*q)["id"].get<std::string>())));
        if (out.size() >= 256)
            break;
    }
    return {{"quests", out}};
}
Json QuestBook::stateKey() const {
    Json active = Json::array();
    int counts[3] = {0, 0, 0};
    std::vector<const Json *> order;
    for (auto &q : quests) {
        auto s = q["state"].get<std::string>();
        counts[0] += s == "available";
        counts[1] += s == "active";
        counts[2] += s == "completed";
        if (s == "active")
            order.push_back(&q);
    }
    std::stable_sort(order.begin(), order.end(), [&](auto *a, auto *b) {
        bool ta = tracked == ref((*a)["id"]), tb = tracked == ref((*b)["id"]);
        if (ta != tb)
            return ta;
        return (*a)["updatedAt"].template get<std::string>() > (*b)["updatedAt"].template get<std::string>();
    });
    for (auto *q : order) {
        if (active.size() >= 16)
            break;
        Json step = nullptr;
        if (auto *s = currentStep(*q))
            step = {{"id", (*s)["id"]}, {"title", (*s)["title"]}, {"progress", stepProgress(*q, *s)}};
        active.push_back({{"id", (*q)["id"]},
                          {"source", Source},
                          {"title", (*q)["title"]},
                          {"step", step},
                          {"progress", questProgress(*q)}});
    }
    return {{"enabled", true},
            {"tracked", tracked},
            {"active", active},
            {"counts", {{"available", counts[0]}, {"active", counts[1]}, {"completed", counts[2]}}},
            {"world", nullptr}};
}
Json QuestBook::watch() const {
    Json out = Json::array();
    for (auto &q : quests) {
        if (q["state"] != "active")
            continue;
        for (size_t i = 0; i < q["steps"].size(); ++i) {
            if (!stepOpen(q, i))
                continue;
            auto &s = q["steps"][i];
            for (auto &o : s["objectives"]) {
                if (o["type"] != "reach")
                    continue;
                int done = q["progress"].contains(s["id"]) ? q["progress"][s["id"]].value(o["id"].get<std::string>(), 0) : 0;
                if (done >= o["count"].get<int>())
                    continue;
                Json w = {{"key", std::string(Source) + "/" + q["id"].get<std::string>() + "/" +
                                      s["id"].get<std::string>() + "/" + o["id"].get<std::string>()},
                          {"target", o.contains("target") ? o["target"] : s["target"]}};
                if (o.contains("radius"))
                    w["radius"] = o["radius"];
                out.push_back(w);
                if (out.size() >= 64)
                    return out;
            }
        }
    }
    return out;
}
Json QuestBook::markers() const {
    Json out = Json::array();
    std::vector<const Json *> order;
    for (auto &q : quests)
        if (q["state"] == "active")
            order.push_back(&q);
    std::stable_sort(order.begin(), order.end(),
                     [&](auto *a, auto *b) { return tracked == ref((*a)["id"]) && tracked != ref((*b)["id"]); });
    for (auto *q : order) {
        bool isTracked = tracked == ref((*q)["id"]);
        for (size_t i = 0; i < (*q)["steps"].size(); ++i) {
            if (!stepOpen(*q, i))
                continue;
            auto &s = (*q)["steps"][i];
            Json target = s["target"];
            std::string objectiveId;
            if (target.is_null())
                for (auto &o : s["objectives"])
                    if (o.contains("target")) {
                        target = o["target"];
                        objectiveId = o["id"];
                        break;
                    }
            if (target.is_null())
                continue;
            std::string full = std::string(Source) + "/" + (*q)["id"].get<std::string>();
            Json source = {{"questId", full}, {"stepId", s["id"]}};
            if (!objectiveId.empty())
                source["objectiveId"] = objectiveId;
            out.push_back({{"id", "quest:" + full + "/" + s["id"].get<std::string>()},
                           {"kind", "quest"},
                           {"tier", isTracked ? 1 : 2},
                           {"label", label48(s["title"])},
                           {"icon", s["optional"].get<bool>() ? "quest-optional" : "quest"},
                           {"target", target},
                           {"tracked", isTracked},
                           {"source", source}});
            if (out.size() >= 32)
                return out;
            if (!isTracked)
                break; // other quests: their first open step only
        }
    }
    return out;
}
Json QuestBook::file() const {
    return {{"version", 1}, {"tracked", tracked}, {"quests", quests}};
}
void QuestBook::restore(const Json &j) {
    if (!j.is_object() || j.value("version", 0) != 1 || !j.contains("quests") || !j["quests"].is_array())
        throw std::runtime_error("unsupported personal quest file");
    Json next = Json::array();
    for (auto &record : j["quests"]) {
        Json input = {{"id", record.at("id")}, {"title", record.at("title")}, {"summary", record.value("summary", "")},
                      {"sequential", record.value("sequential", true)}, {"steps", record.at("steps")}};
        if (record.contains("expires") && !record["expires"].is_null())
            input["expires"] = record["expires"];
        // Stored steps are normalised records: drop derived fields before re-validating.
        for (auto &s : input["steps"]) {
            if (s.contains("target") && s["target"].is_null())
                s.erase("target");
            for (auto &o : s["objectives"])
                if (o["type"] == "focus")
                    o.erase("count");
        }
        auto q = normalize(input);
        q["id"] = record.at("id");
        auto state = record.value("state", "active");
        if (state != "available" && state != "active" && state != "completed" && state != "failed" && state != "abandoned")
            state = "active";
        q["state"] = state;
        q["progress"] = record.value("progress", Json::object());
        q["credited"] = record.value("credited", Json::object());
        q["createdAt"] = record.value("createdAt", "");
        q["updatedAt"] = record.value("updatedAt", "");
        q["completedAt"] = record.value("completedAt", Json(nullptr));
        if (next.size() < MaxPersonal)
            next.push_back(q);
    }
    quests = next;
    tracked = nullptr;
    if (j.contains("tracked") && j["tracked"].is_object() && find(j["tracked"].value("id", "")) &&
        (*find(j["tracked"]["id"]))["state"] == "active")
        tracked = ref(j["tracked"]["id"]);
}
} // namespace hyprune::quests
