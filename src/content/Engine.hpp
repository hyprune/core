#pragma once
// RFC-0015 content, sources and registry over IPC (0.32): hypruned runs the SDK pipeline (the
// `hyprune` CLI, the fetch helper's role) as its job engine and publishes the `content` state
// key. Core opens no network connection and keeps no pipeline of its own; listings come from
// the CLI's no-network commands and are cached here, so reads never spawn a process.
#include "ipc/Server.hpp"
#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace hyprune::content {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

// One child process: argv without a shell, stdout and stderr captured, its own process group.
class Process {
  public:
    Process(const std::vector<std::string> &argv, const std::vector<std::string> &env);
    ~Process();
    Process(const Process &) = delete;
    Process &operator=(const Process &) = delete;
    // Reads what is available; complete lines go to `lines` (stdout). False once it has exited
    // and both pipes are drained.
    bool poll(std::vector<std::string> &lines);
    void terminate();
    bool started() const { return pid_ > 0; }
    int exitCode() const { return exit_; }
    const std::string &output() const { return all_; }
    std::string lastError() const; // the last non-empty stderr line

  private:
    int pid_ = -1, out_ = -1, err_ = -1, exit_ = -1;
    std::string partial_, all_, errors_;
    bool exited_ = false;
};

class Engine {
  public:
    struct Paths {
        std::vector<std::string> cli;              // $HYPRUNE_CLI split on spaces, else {"hyprune"}
        std::vector<std::filesystem::path> watched; // content.json, content-sources.json, the source cache
        static Paths fromEnvironment();
    };
    explicit Engine(Paths = Paths::fromEnvironment());
    static std::map<std::string, std::string> methods(); // method -> grant
    static bool handles(const std::string &method);
    Json call(const std::string &method, const Json &params); // params: an object (anything else is {})
    void tick();
    Json decorate(Json state) const; // adds "content"
    bool changed = false;                                // the caller republishes
    std::vector<std::pair<std::string, Json>> notices;   // runtime.notice params, in order
    // Test hooks: the listing documents the CLI last returned.
    const Json &installs() const { return installs_; }
    const Json &sources() const { return sources_; }
    const Json &entries() const { return entries_; }
    int jobLimit = 16;
    std::chrono::milliseconds keepFinished{60000}, watchEvery{2000};

  private:
    struct Job {
        std::string id, kind, stage = "queued", created;
        Json target = nullptr, plan = nullptr, progress = nullptr, error = nullptr, result = nullptr;
        std::vector<std::vector<std::string>> commands; // run in order; the last one's done result counts
        bool confirmable = false, autoConfirm = false, confirmed = false, documents = false;
        Json confirmedPlan = nullptr;
        size_t step = 0;
        Clock::time_point finished{};
        Json view() const;
    };
    Paths paths_;
    std::deque<Job> jobs_;
    std::unique_ptr<Process> process_;
    std::string running_;                       // job id, "" for a listing step, or none
    bool listing_ = false;                      // a listing refresh is running or wanted
    std::vector<std::vector<std::string>> listingSteps_;
    size_t listingStep_ = 0;
    Json installs_ = Json::array(), sources_ = Json::array(), entries_ = Json::array();
    int64_t revision_ = 0;
    std::map<std::filesystem::path, std::filesystem::file_time_type> stamps_;
    Clock::time_point lastWatch_{};
    bool listedOnce_ = false;

    Job *find(const std::string &id);
    std::string enqueue(Job job);
    void start(Job &job);
    void startListing();
    void finishListing(bool ok);
    void handleLine(Job &job, const std::string &line);
    void finishJob(Job &job);
    void fail(Job &job, const std::string &rule, const std::string &message);
    void bump();
    std::vector<std::string> environment() const;
    std::vector<std::string> argv(std::vector<std::string> args, const Job *job) const;
    Json entryFor(const std::string &sourceId, const std::string &id) const;
    Json installFor(const std::string &id) const;
    bool watchChanged();
};
// The listing shapes trimmed to the IPC 0.32 fields (the CLI adds paths and hashes).
Json installItem(const Json &);
Json sourceItem(const Json &);
Json registryItem(const Json &);
Json planItem(const Json &);
std::string normalisedRule(const std::string &);
} // namespace hyprune::content
