#pragma once
#include "Focus.hpp"
#include "Guidance.hpp"
#include "Quests.hpp"
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
namespace hyprune::quests {
// The quest engine hosted by hypruned (RFC-0012 P1): personal quests, focus
// sessions and guidance settings. It owns persistence and the `quests` and
// `focus` state keys, and consumes facts the plugin reports over the private
// bridge (keyboard focus as app class + workspace, an input timestamp, the
// session lock, reach-target containment). It never sees titles or contents.
class Engine {
  public:
    struct Paths {
        std::filesystem::path personal, history, session, guidance;
        static Paths fromEnvironment();
    };
    explicit Engine(Paths = Paths::fromEnvironment(), double timeScale = 1);
    // Method -> grant, advertised next to the plugin's (IPC 0.21).
    static std::map<std::string, std::string> methods();
    static bool handles(const std::string &method);
    // Applies a request; throws ipc::Error. Results that carry a revision get
    // the post-publication revision from the caller.
    Json call(const std::string &method, const Json &params, int64_t now, std::time_t wall);
    void facts(const Json &);
    void pluginState(const Json &state);
    void tick(int64_t now, std::time_t wall);
    // Adds `quests` and `focus` to the plugin's state.
    Json decorate(Json state, int64_t now) const;
    // A sync message for the plugin, when its marker/watch/guidance inputs changed.
    std::optional<Json> sync();
    void flush(int64_t now, bool force);
    bool changed = false;                              // state keys moved; the caller republishes
    std::vector<std::pair<std::string, Json>> events; // quest.progress / focus.phase, in order
    Guidance guidance;
    QuestBook book;
    FocusSession focus;

  private:
    Paths paths_;
    double scale_;
    int64_t questsDirtyAt_ = -1, lastExpire_ = INT64_MIN / 2, lastBoot_ = 0, lastMono_ = 0;
    bool sessionDirty_ = false;
    FocusSession::Facts facts_;
    std::set<std::string> inside_, resolved_;
    Json markers_ = Json::array(), eye_ = nullptr, lastSync_ = nullptr, breakMarker_ = nullptr;
    std::string now_;
    void questEvents(const std::vector<Json> &);
    void focusOutcomes(const std::vector<FocusSession::Outcome> &, int64_t now);
    void questsChanged(int64_t now);
    void writeSession(int64_t now);
    Json history(int days, std::time_t wall) const;
    Json pickBreak() const;
};
} // namespace hyprune::quests
