#pragma once
#include <ctime>
#include <json.hpp>
#include <set>
#include <string>
#include <vector>
namespace hyprune::quests {
using Json = nlohmann::json;
// Personal quests (RFC-0012 P1): definitions, states, tracking and objective
// progress. Pure data: no clock, files or IPC; the engine passes timestamps in
// and turns the returned quest.progress parameters into events.
//
// Objective progress is monotonic and counts only while the quest is active and
// the step is open (current, or any step of a non-sequential quest).
class QuestBook {
  public:
    static constexpr size_t MaxPersonal = 64;
    static constexpr const char *Source = "personal";
    // Normalised records: the definition plus state, progress and timestamps.
    Json quests = Json::array();
    Json tracked = nullptr; // questRef or null
    // Validates a PersonalQuestInput and returns the normalised definition
    // (ids filled in, labels generated). Throws ipc::Error -32602.
    static Json normalize(const Json &input);
    static Json validTarget(const Json &target); // throws ipc::Error -32602
    static std::string slug(const std::string &title);
    static std::string targetName(const Json &target);

    Json create(const Json &input, const std::string &now); // returns questRef
    void update(const std::string &id, const Json &input, const std::string &now);
    void remove(const std::string &id);
    void track(const Json &ref);
    std::vector<Json> accept(const std::string &id, const std::string &now);
    std::vector<Json> abandon(const std::string &id, const std::string &now);
    std::vector<Json> resume(const std::string &id, const std::string &now);
    std::vector<Json> mark(const std::string &id, const std::string &stepId, const std::string &objectiveId,
                           const std::string &now);
    // `inside`: keys "<source>/<quest>/<step>/<objective>" whose reach target holds the eye
    // now. Counts each key that was not inside at the previous call.
    std::vector<Json> reached(const std::set<std::string> &inside, const std::string &now);
    // A completed work interval with `creditedMin` credited minutes. `questRef` and
    // `objectiveId` link the session (null/empty when not started from a quest).
    std::vector<Json> focusCompleted(const Json &questRef, const std::string &objectiveId, double creditedMin,
                                     double workMin, const std::string &now);
    std::vector<Json> expire(std::time_t now, const std::string &nowText);

    Json summary(const Json &quest, bool offWorld) const;
    Json detail(const std::string &id) const;
    // `resolved`: full quest IDs with at least one target in the loaded world.
    Json list(const Json &params, const std::set<std::string> &resolved) const;
    Json stateKey() const;
    Json watch() const;   // open reach objectives: [{key, target, radius}]
    Json markers() const; // quest marker requests for the plugin (unresolved targets)
    Json file() const;
    void restore(const Json &);
    const Json *find(const std::string &id) const;
    Json *find(const std::string &id);
    static Json ref(const std::string &id) { return {{"source", Source}, {"id", id}}; }

  private:
    std::set<std::string> inside_;
    static bool stepDone(const Json &quest, const Json &step);
    static bool stepOpen(const Json &quest, size_t index);
    static bool questDone(const Json &quest);
    static const Json *currentStep(const Json &quest);
    static Json stepProgress(const Json &quest, const Json &step);
    static Json questProgress(const Json &quest);
    std::vector<Json> credit(Json &quest, const std::string &stepId, const std::string &objectiveId, int amount,
                             const std::string &now);
};
} // namespace hyprune::quests
