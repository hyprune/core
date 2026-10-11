#pragma once
#include <cstdint>
#include <ctime>
#include <json.hpp>
#include <optional>
#include <string>
#include <vector>
namespace hyprune::quests {
using Json = nlohmann::json;
// Focus sessions (RFC-0012): work intervals and breaks in cycles. The interval
// clock runs on the monotonic clock; credit is separate and accrues only while
// keyboard focus is on a matching window (or within the grace period after it
// left), with input inside the idle window. Nothing here reads titles or contents.
class FocusSession {
  public:
    struct Settings {
        double workMin = 25, shortBreakMin = 5, longBreakMin = 15;
        int longBreakEvery = 4, cycles = 4;
        bool autoStartBreaks = true, autoStartWork = false, calm = true;
        std::string countMode = "focus";
        double idleS = 180, graceS = 30;
    };
    // What the plugin reports. `focus` is null while the world view holds the
    // keyboard or nothing is focused; otherwise {workspaceId, appClass}.
    struct Facts {
        Json focus = nullptr;
        int64_t inputAtMs = 0;
        bool locked = false;
    };
    struct Outcome {
        Json phase;                 // focus.phase params (without the IPC sessionId/revision)
        std::optional<Json> history; // FocusInterval line
        // A completed work interval: credited minutes and the session's link.
        std::optional<double> completedWorkMin;
    };
    // Every configured duration is divided by `scale` (tests run whole sessions in seconds).
    double scale = 1;
    std::string state = "idle", pausedState, breakKind, next; // next: phase started from "ready"
    std::string id;
    Json target = nullptr, questRef = nullptr;
    std::string objectiveId;
    Settings settings;
    int cycle = 0;
    int64_t phaseStart = 0, phaseMs = 0, remainingAtPause = 0, creditedMs = 0;
    int64_t lastTick = 0, lastMatch = INT64_MIN / 2;
    bool crediting = false;
    std::time_t phaseWallStart = 0;

    static Settings settingsFrom(const Json &guidance, const Json &overrides); // throws std::invalid_argument
    static Json validTarget(const Json &target);                               // throws std::invalid_argument
    bool matches(const Json &focus) const;
    std::vector<Outcome> start(int64_t now, std::time_t wall, Json target, Json questRef, std::string objectiveId,
                               Settings, std::string id);
    std::vector<Outcome> pause(int64_t now, std::time_t wall);
    std::vector<Outcome> resume(int64_t now, std::time_t wall);
    std::vector<Outcome> skip(int64_t now, std::time_t wall);
    std::vector<Outcome> stop(int64_t now, std::time_t wall);
    // Advances credit and phases. Returns phase changes; `creditChanged` is set
    // when credited time or the crediting flag moved since the last tick.
    std::vector<Outcome> tick(int64_t now, std::time_t wall, const Facts &, bool &creditChanged);
    bool running() const { return state == "work" || state == "break"; }
    bool calmNow() const { return state == "work" && settings.calm; }
    Json stateKey(int64_t now) const;
    Json file(int64_t now) const;                                // the running session, for a restart
    void restore(const Json &, int64_t now, std::time_t wall); // restores paused (or ready)
    int64_t workMs() const;
    int64_t breakMs() const;

  private:
    Json phaseEvent(const std::string &phase) const;
    Json historyLine(std::time_t wallEnd, const std::string &phase, int64_t plannedMs, const std::string &outcome) const;
    void begin(const std::string &phase, int64_t now, std::time_t wall, int64_t length);
    std::vector<Outcome> finishPhase(int64_t now, std::time_t wall, const std::string &outcome);
};
} // namespace hyprune::quests
