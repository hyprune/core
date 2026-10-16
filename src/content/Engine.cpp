#include "Engine.hpp"
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <iostream>
#include <random>
#include <regex>
#include <set>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
namespace hyprune::content {
namespace fs = std::filesystem;
using ipc::Error;

// ------------------------------------------------------------------------------- Process
Process::Process(const std::vector<std::string> &argv, const std::vector<std::string> &env) {
    int out[2], err[2];
    if (pipe2(out, O_CLOEXEC) || pipe2(err, O_CLOEXEC))
        throw std::runtime_error(std::string("pipe: ") + std::strerror(errno));
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, out[1], 1);
    posix_spawn_file_actions_adddup2(&actions, err[1], 2);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK);
    posix_spawnattr_setpgroup(&attr, 0);
    sigset_t none;
    sigemptyset(&none);
    posix_spawnattr_setsigmask(&attr, &none);
    std::vector<char *> args, envp;
    for (auto &a : argv)
        args.push_back(const_cast<char *>(a.c_str()));
    args.push_back(nullptr);
    for (auto &e : env)
        envp.push_back(const_cast<char *>(e.c_str()));
    envp.push_back(nullptr);
    int result = posix_spawnp(&pid_, args[0], &actions, &attr, args.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    close(out[1]);
    close(err[1]);
    out_ = out[0];
    err_ = err[0];
    fcntl(out_, F_SETFL, O_NONBLOCK);
    fcntl(err_, F_SETFL, O_NONBLOCK);
    if (result) {
        pid_ = -1;
        close(out_);
        close(err_);
        out_ = err_ = -1;
        errno = result;
        throw std::system_error(result, std::generic_category(), argv.empty() ? "spawn" : argv[0]);
    }
}
Process::~Process() {
    terminate();
    if (out_ >= 0)
        close(out_);
    if (err_ >= 0)
        close(err_);
}
void Process::terminate() {
    if (pid_ > 0 && !exited_) {
        kill(-pid_, SIGTERM);
        int status = 0;
        for (int i = 0; i < 50 && waitpid(pid_, &status, WNOHANG) == 0; ++i)
            usleep(10000);
        if (waitpid(pid_, &status, WNOHANG) == 0) {
            kill(-pid_, SIGKILL);
            waitpid(pid_, &status, 0);
        }
        exited_ = true;
    }
}
bool Process::poll(std::vector<std::string> &lines) {
    char buffer[65536];
    auto drain = [&](int &fd, std::string *into, bool stdoutPipe) {
        while (fd >= 0) {
            auto n = read(fd, buffer, sizeof buffer);
            if (n > 0) {
                if (stdoutPipe) {
                    // A runaway child can't exhaust the daemon: 64 MiB of output at most.
                    if (all_.size() < (64u << 20))
                        all_.append(buffer, n);
                    partial_.append(buffer, n);
                    for (size_t at; (at = partial_.find('\n')) != std::string::npos;) {
                        lines.push_back(partial_.substr(0, at));
                        partial_.erase(0, at + 1);
                    }
                } else if (into->size() < 65536)
                    into->append(buffer, n);
                continue;
            }
            if (n == 0) {
                close(fd);
                fd = -1;
            }
            return;
        }
    };
    drain(out_, nullptr, true);
    drain(err_, &errors_, false);
    if (!exited_ && pid_ > 0) {
        int status = 0;
        if (waitpid(pid_, &status, WNOHANG) == pid_) {
            exited_ = true;
            exit_ = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
        }
    }
    if (exited_ && out_ < 0 && err_ < 0) {
        if (!partial_.empty()) {
            lines.push_back(partial_);
            partial_.clear();
        }
        return false;
    }
    return true;
}
std::string Process::lastError() const {
    std::string last;
    size_t start = 0;
    while (start < errors_.size()) {
        auto end = errors_.find('\n', start);
        auto line = errors_.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (line.find_first_not_of(" \t\r") != std::string::npos)
            last = line;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return last.substr(0, 2048);
}

// ------------------------------------------------------------------------------- shapes
namespace {
Json pick(const Json &from, std::initializer_list<const char *> keys) {
    Json out = Json::object();
    if (!from.is_object())
        return out;
    for (auto k : keys)
        if (from.contains(k))
            out[k] = from.at(k);
    return out;
}
std::string iso(std::time_t t) {
    char text[32];
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return text;
}
std::string jobId() {
    static std::mt19937_64 random{std::random_device{}()};
    char text[16];
    std::snprintf(text, sizeof text, "j-%08x", unsigned(random()));
    return text;
}
fs::path home(const char *name, const char *fallback) {
    const char *v = std::getenv(name);
    if (v && v[0] == '/')
        return v;
    const char *h = std::getenv("HOME");
    return fs::path(h && h[0] == '/' ? h : "/") / fallback;
}
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}
bool samePlan(const Json &a, const Json &b) {
    auto key = [](const Json &plan) {
        std::vector<std::string> out;
        if (plan.is_object() && plan.contains("packages"))
            for (auto &p : plan["packages"])
                out.push_back(p.value("id", "") + "@" + p.value("version", "") + "#" + (p["sha256"].is_string() ? p["sha256"].get<std::string>() : "") +
                              ":" + p.value("action", ""));
        std::sort(out.begin(), out.end());
        return out;
    };
    return key(a) == key(b);
}
const std::set<std::string> stages{"queued", "resolving", "confirm", "downloading", "verifying", "extracting", "validating",
                                   "committing", "done", "failed", "cancelled"};
} // namespace
Json installItem(const Json &i) {
    auto out = pick(i, {"id", "type", "sourceId", "display", "sourceState", "tag", "version", "previous", "pinned", "managed",
                        "removedFromSource", "newerTag", "license", "missing"});
    if (out.contains("previous") && out["previous"].is_object())
        out["previous"] = pick(out["previous"], {"tag", "version"});
    return out;
}
Json sourceItem(const Json &s) {
    return pick(s, {"id", "kind", "label", "display", "url", "repo", "sha256", "enabled", "builtin", "system", "lastRefresh", "serial",
                    "entries", "error", "successor"});
}
Json registryItem(const Json &e) {
    auto version = [](const Json &v) {
        return v.is_object() ? pick(v, {"tag", "version", "formatVersion", "license", "bytes", "expandedBytes", "published", "status",
                                        "prerelease", "loadable", "requires"})
                             : Json(nullptr);
    };
    auto out = pick(e, {"sourceId", "display", "tier", "id", "type", "name", "summary", "description", "creator", "repo", "tags",
                        "latest", "licenceNote", "thumbnail", "installed", "update", "versions"});
    if (out.contains("latest"))
        out["latest"] = version(out["latest"]);
    if (out.contains("installed") && out["installed"].is_object())
        out["installed"] = pick(out["installed"], {"tag", "sourceId"});
    if (out.contains("versions") && out["versions"].is_array()) {
        Json versions = Json::array();
        for (auto &v : out["versions"])
            versions.push_back(version(v));
        out["versions"] = versions;
    }
    return out;
}
Json planItem(const Json &plan) {
    auto out = pick(plan, {"packages", "sourceChange", "override", "diskFreeAfter"});
    Json packages = Json::array();
    for (auto &p : plan.value("packages", Json::array()))
        packages.push_back(pick(p, {"id", "type", "tag", "version", "formatVersion", "replaces", "sourceId", "sourceLabel", "creator",
                                    "repo", "license", "licenceNote", "bytes", "expandedBytes", "sha256", "url", "action", "requires"}));
    out["packages"] = packages;
    if (!out.contains("sourceChange"))
        out["sourceChange"] = nullptr;
    if (out["sourceChange"].is_object())
        out["sourceChange"] = pick(out["sourceChange"], {"id", "from", "to"});
    if (!out.contains("override"))
        out["override"] = false;
    return out;
}
std::string normalisedRule(const std::string &rule) {
    static const std::regex dotted("^[a-z]+(\\.[a-z-]+)+$");
    return rule.size() <= 64 && std::regex_match(rule, dotted) ? rule : "cli.failed";
}

// ------------------------------------------------------------------------------- Engine
Engine::Paths Engine::Paths::fromEnvironment() {
    Paths p;
    if (const char *cli = std::getenv("HYPRUNE_CLI")) {
        std::string word;
        for (const char *c = cli;; ++c) {
            if (*c == ' ' || *c == '\t' || *c == 0) {
                if (!word.empty())
                    p.cli.push_back(word);
                word.clear();
                if (!*c)
                    break;
            } else
                word += *c;
        }
    }
    if (p.cli.empty())
        p.cli = {"hyprune"};
    auto data = home("XDG_DATA_HOME", ".local/share") / "hyprune";
    p.watched = {home("XDG_STATE_HOME", ".local/state") / "hyprune/content.json",
                 home("XDG_CONFIG_HOME", ".config") / "hyprune/content-sources.json",
                 home("XDG_CACHE_HOME", ".cache") / "hyprune/sources",
                 std::getenv("HYPRUNE_WORLD_DIR") && *std::getenv("HYPRUNE_WORLD_DIR") ? fs::path(std::getenv("HYPRUNE_WORLD_DIR")) : data / "worlds",
                 data / "libraries"};
    return p;
}
Engine::Engine(Paths paths) : paths_(std::move(paths)) {
    listing_ = true; // the first tick lists what is installed
}
std::map<std::string, std::string> Engine::methods() {
    return {{"content.install", "content.control"}, {"content.confirm", "content.control"}, {"content.cancel", "content.control"},
            {"content.update", "content.control"},  {"content.rollback", "content.control"}, {"content.pin", "content.control"},
            {"content.remove", "content.control"},  {"sources.list", "state.read"},          {"sources.add", "content.control"},
            {"sources.set", "content.control"},     {"sources.remove", "content.control"},   {"sources.refresh", "content.control"},
            {"registry.list", "state.read"},        {"registry.entry", "state.read"}};
}
bool Engine::handles(const std::string &method) {
    return methods().contains(method);
}
std::vector<std::string> Engine::environment() const {
    // Never a session: no socket, token or launch descriptor reaches the CLI.
    static const std::set<std::string> drop{"HYPRUNE_SOCKET", "HYPRUNE_TOKEN_FD", "HYPRUNE_TOKEN", "HYPRUNE_LAUNCH_FD", "HYPRUNE_BRIDGE_FD",
                                            "HYPRUNE_GRANT_FD", "HYPRUNE_PROTOCOL"};
    std::vector<std::string> env;
    for (char **e = environ; e && *e; ++e) {
        std::string entry = *e;
        if (!drop.contains(entry.substr(0, entry.find('='))))
            env.push_back(entry);
    }
    return env;
}
std::vector<std::string> Engine::argv(std::vector<std::string> args, const Job *job) const {
    std::vector<std::string> out = paths_.cli;
    out.insert(out.end(), args.begin(), args.end());
    out.push_back("--json");
    out.push_back("--no-session");
    if (job && job->confirmable && job->step + 1 == job->commands.size()) {
        if (!job->confirmed)
            out.push_back("--plan");
        else {
            out.push_back("--yes");
            if (job->confirmedPlan.is_object() && job->confirmedPlan["sourceChange"].is_object())
                out.push_back("--accept-source-change");
        }
    }
    return out;
}
Json Engine::Job::view() const {
    return {{"jobId", id}, {"kind", kind},     {"stage", stage},   {"target", target},  {"plan", plan},
            {"progress", progress}, {"error", error}, {"result", result}, {"created", created}};
}
Engine::Job *Engine::find(const std::string &id) {
    for (auto &j : jobs_)
        if (j.id == id)
            return &j;
    return nullptr;
}
void Engine::bump() {
    ++revision_;
    changed = true;
}
Json Engine::entryFor(const std::string &sourceId, const std::string &id) const {
    for (auto &e : entries_)
        if (e.value("sourceId", "") == sourceId && e.value("id", "") == id)
            return e;
    return nullptr;
}
Json Engine::installFor(const std::string &id) const {
    for (auto &i : installs_)
        if (i.value("id", "") == id)
            return i;
    return nullptr;
}
std::string Engine::enqueue(Job job) {
    if (int(jobs_.size()) >= jobLimit) // finished jobs make room early
        std::erase_if(jobs_, [](const Job &j) { return j.stage == "done" || j.stage == "failed" || j.stage == "cancelled"; });
    if (int(jobs_.size()) >= jobLimit)
        throw Error(-32008, "too many content jobs", Json{{"rule", "jobs.limit"}});
    job.id = jobId();
    while (find(job.id))
        job.id = jobId();
    job.created = iso(std::time(nullptr));
    jobs_.push_back(std::move(job));
    bump();
    return jobs_.back().id;
}
Json Engine::call(const std::string &method, const Json &input) {
    const Json params = input.is_object() ? input : Json::object();
    auto str = [&](const char *key) -> std::string {
        if (!params.contains(key) || !params[key].is_string() || params[key].get<std::string>().empty())
            throw Error(-32602, std::string("missing ") + key);
        return params[key];
    };
    auto optional = [&](const char *key) -> std::optional<std::string> {
        if (params.contains(key) && params[key].is_string())
            return params[key].get<std::string>();
        return std::nullopt;
    };
    auto flag = [&](const char *key) { return params.contains(key) && params[key].is_boolean() && params[key].get<bool>(); };
    auto requireInstalled = [&](const std::string &id) {
        if (listedOnce_ && installFor(id).is_null())
            throw Error(-32004, id + " is not installed");
    };
    if (method == "sources.list")
        return {{"sources", sources_}};
    if (method == "registry.list") {
        std::string query = lower(optional("query").value_or(""));
        auto type = optional("type");
        std::set<std::string> ids, tags;
        for (auto &s : params.value("sourceIds", Json::array()))
            ids.insert(s.get<std::string>());
        for (auto &t : params.value("tags", Json::array()))
            tags.insert(t.get<std::string>());
        size_t offset = 0;
        if (params.contains("cursor")) {
            auto cursor = str("cursor");
            if (cursor.size() > 9 || cursor.find_first_not_of("0123456789") != std::string::npos)
                throw Error(-32602, "invalid cursor");
            offset = std::stoul(cursor);
        }
        size_t limit = std::clamp<int64_t>(params.value("limit", 50), 1, 100);
        std::vector<const Json *> matched;
        for (auto &e : entries_) {
            if (type && e.value("type", "") != *type)
                continue;
            if (!ids.empty() && !ids.contains(e.value("sourceId", "")))
                continue;
            std::set<std::string> have;
            for (auto &t : e.value("tags", Json::array()))
                have.insert(t.get<std::string>());
            if (!std::all_of(tags.begin(), tags.end(), [&](auto &t) { return have.contains(t); }))
                continue;
            if (!query.empty()) {
                std::string text = lower(e.value("id", "") + "\n" + e.value("name", "") + "\n" +
                                         (e["summary"].is_string() ? e["summary"].get<std::string>() : std::string()));
                for (auto &t : have)
                    text += "\n" + lower(t);
                if (text.find(query) == std::string::npos)
                    continue;
            }
            matched.push_back(&e);
        }
        Json out = Json::array();
        for (size_t i = offset; i < matched.size() && out.size() < limit; ++i)
            out.push_back(*matched[i]);
        size_t next = offset + out.size();
        return {{"entries", out}, {"cursor", next < matched.size() ? Json(std::to_string(next)) : Json(nullptr)}};
    }
    if (method == "registry.entry") {
        auto entry = entryFor(str("sourceId"), str("id"));
        if (entry.is_null())
            throw Error(-32004, "unknown registry entry");
        auto installed = installFor(entry.value("id", ""));
        if (!installed.is_null() && installed.value("type", "") != entry.value("type", ""))
            installed = nullptr;
        return {{"entry", entry}, {"installed", installed}};
    }
    if (method == "content.confirm" || method == "content.cancel") {
        auto *job = find(str("jobId"));
        if (!job)
            throw Error(-32004, "unknown job");
        if (method == "content.confirm") {
            if (!params.contains("accept") || !params["accept"].is_boolean())
                throw Error(-32602, "missing accept");
            if (job->stage != "confirm")
                throw Error(-32005, "the job is not waiting for confirmation");
            if (params["accept"].get<bool>()) {
                job->confirmed = true;
                job->confirmedPlan = job->plan;
                job->stage = "queued";
            } else {
                job->stage = "cancelled";
                job->finished = Clock::now();
            }
        } else {
            if (job->stage != "queued" && job->stage != "confirm")
                throw Error(-32005, "only a queued job or one waiting for confirmation can be cancelled");
            job->stage = "cancelled";
            job->finished = Clock::now();
            listing_ = true;
        }
        bump();
        return {{"revision", revision_}};
    }
    Job job;
    if (method == "content.install") {
        if (!params.contains("source") || !params["source"].is_object())
            throw Error(-32602, "missing source");
        auto &source = params["source"];
        auto type = optional("type");
        std::string spec;
        std::vector<std::string> extra;
        if (source.contains("entry")) {
            auto &e = source["entry"];
            auto entry = entryFor(e.value("sourceId", ""), e.value("id", ""));
            if (entry.is_null())
                throw Error(-32004, "unknown registry entry");
            if (type && *type != entry.value("type", ""))
                throw Error(-32602, "type does not match the entry");
            type = entry.value("type", "");
            spec = e.value("id", "");
            job.target = spec;
            extra = {"--from", e.value("sourceId", "")};
        } else {
            if (!type || (*type != "world" && *type != "library"))
                throw Error(-32602, "type is required unless source is an entry");
            if (source.contains("github"))
                spec = "github:" + source.value("github", "");
            else if (source.contains("url"))
                spec = source.value("url", "");
            else if (source.contains("file"))
                spec = source.value("file", "");
            else
                throw Error(-32602, "invalid source");
            job.target = spec;
        }
        if (auto tag = optional("tag")) {
            if (source.contains("url") || source.contains("file"))
                throw Error(-32602, "a URL or file install has no tag");
            spec += "@" + *tag;
        }
        std::vector<std::string> args{*type, "add", spec};
        args.insert(args.end(), extra.begin(), extra.end());
        if (auto sha = optional("sha256"))
            args.insert(args.end(), {"--sha256", *sha});
        if (flag("pin"))
            args.push_back("--pin");
        if (flag("override"))
            args.push_back("--override");
        job.kind = "install";
        job.commands = {args};
        job.confirmable = true;
        job.autoConfirm = flag("autoConfirm");
    } else if (method == "content.update") {
        job.kind = "update";
        job.confirmable = true;
        if (params.contains("all") && params["all"] == true)
            job.commands = {{"update", "--all"}};
        else {
            auto id = str("id");
            requireInstalled(id);
            job.target = id;
            job.commands = {{"update", id}};
        }
    } else if (method == "content.rollback") {
        auto id = str("id");
        requireInstalled(id);
        job.kind = "rollback";
        job.target = id;
        job.commands = {{"rollback", id}};
    } else if (method == "content.pin") {
        auto id = str("id");
        if (!params.contains("pinned") || !params["pinned"].is_boolean())
            throw Error(-32602, "missing pinned");
        requireInstalled(id);
        job.kind = params["pinned"].get<bool>() ? "pin" : "unpin";
        job.target = id;
        job.commands = {{job.kind, id}};
    } else if (method == "content.remove") {
        auto id = str("id");
        requireInstalled(id);
        job.kind = "remove";
        job.target = id;
        std::vector<std::string> args{"remove", optional("version") ? id + "@" + *optional("version") : id};
        if (flag("force"))
            args.push_back("--force");
        job.commands = {args};
    } else if (method == "sources.add") {
        std::string spec = params.contains("github") ? "github:" + str("github") : str("url");
        std::vector<std::string> args{"source", "add", spec};
        for (auto key : {"label", "id", "sha256"})
            if (auto v = optional(key))
                args.insert(args.end(), {std::string("--") + key, *v});
        job.kind = "source.add";
        job.target = spec;
        job.commands = {args};
    } else if (method == "sources.set" || method == "sources.remove") {
        auto id = str("id");
        if (listedOnce_ && std::none_of(sources_.begin(), sources_.end(), [&](const Json &s) { return s.value("id", "") == id; }))
            throw Error(-32004, "unknown source");
        job.target = id;
        job.documents = true;
        if (method == "sources.remove") {
            job.kind = "source.remove";
            job.commands = {{"source", "remove", id}};
        } else {
            job.kind = "source.set";
            if (auto label = optional("label"))
                job.commands.push_back({"source", "label", id, *label});
            if (params.contains("enabled") && params["enabled"].is_boolean())
                job.commands.push_back({"source", params["enabled"].get<bool>() ? "enable" : "disable", id});
            if (job.commands.empty())
                throw Error(-32602, "nothing to set");
        }
    } else if (method == "sources.refresh") {
        job.kind = "source.refresh";
        std::vector<std::string> args{"source", "refresh"};
        for (auto &id : params.value("ids", Json::array()))
            args.push_back(id.get<std::string>());
        job.commands = {args};
    } else
        throw Error(-32601, "unknown content method");
    return {{"jobId", enqueue(std::move(job))}};
}
void Engine::fail(Job &job, const std::string &rule, const std::string &message) {
    job.error = {{"rule", normalisedRule(rule)}, {"message", message.substr(0, 2048)}};
    job.stage = "failed";
    job.finished = Clock::now();
    notices.push_back({"runtime.notice",
                       {{"level", "warning"},
                        {"code", job.kind.starts_with("source.") ? "source.failed" : "content.failed"},
                        {"message", (job.target.is_string() ? job.target.get<std::string>() + ": " : std::string()) + message.substr(0, 3000)}}});
    listing_ = true;
    bump();
}
void Engine::start(Job &job) {
    try {
        process_ = std::make_unique<Process>(argv(job.commands.at(job.step), &job), environment());
        running_ = job.id;
        if (job.stage == "queued")
            job.stage = "resolving";
        bump();
    } catch (const std::exception &e) {
        process_.reset();
        running_.clear();
        fail(job, "cli.missing", std::string("the hyprune command didn't run: ") + e.what());
    }
}
void Engine::handleLine(Job &job, const std::string &line) {
    if (line.empty() || line[0] != '{')
        return;
    Json e;
    try {
        e = Json::parse(line);
    } catch (...) {
        return;
    }
    if (job.documents) {
        job.result = e;
        return;
    }
    auto event = e.value("event", "");
    if (event == "stage") {
        auto stage = e.value("stage", "");
        if (stages.contains(stage) && stage != "done" && stage != "failed" && stage != "cancelled" && stage != "queued")
            job.stage = stage;
    } else if (event == "plan" && e.contains("plan")) {
        auto plan = planItem(e["plan"]);
        if (job.confirmed && !samePlan(plan, job.confirmedPlan)) {
            if (process_)
                process_->terminate();
            fail(job, "plan.changed", "the plan changed after it was confirmed; review it again");
            return;
        }
        if (job.plan.is_object() && !job.confirmed) // update --all: one plan per package
            for (auto &p : plan["packages"])
                job.plan["packages"].push_back(p);
        else
            job.plan = plan;
    } else if (event == "progress") {
        job.progress = {{"bytes", e.value("bytes", 0)}, {"totalBytes", e.contains("total") ? e["total"] : Json(nullptr)}};
    } else if (event == "done") {
        job.result = e.value("result", Json::object());
    } else if (event == "error" && e.contains("error")) {
        job.error = {{"rule", normalisedRule(e["error"].value("rule", "cli.failed"))}, {"message", e["error"].value("message", "").substr(0, 2048)}};
    }
    bump();
}
void Engine::finishJob(Job &job) {
    int code = process_ ? process_->exitCode() : 1;
    std::string stderrLine = process_ ? process_->lastError() : "";
    process_.reset();
    running_.clear();
    if (job.stage == "failed")
        return; // plan.changed already handled
    if (job.error.is_object()) {
        auto error = job.error;
        job.error = nullptr;
        fail(job, error.value("rule", "cli.failed"), error.value("message", ""));
        return;
    }
    if (code != 0) {
        // Non-job commands report on stderr: "hyprune: <rule>: <message>".
        static const std::regex shaped("^hyprune: ([a-z]+(?:\\.[a-z-]+)+): (.*)$");
        std::smatch m;
        if (std::regex_match(stderrLine, m, shaped))
            fail(job, m[1], m[2]);
        else
            fail(job, "cli.failed", stderrLine.empty() ? "the hyprune command failed (exit " + std::to_string(code) + ")" : stderrLine);
        return;
    }
    if (job.confirmable && !job.confirmed) {
        bool nothing = !job.plan.is_object() || std::all_of(job.plan["packages"].begin(), job.plan["packages"].end(),
                                                           [](const Json &p) { return p.value("action", "") == "already-installed"; });
        if (nothing) {
            job.stage = "done";
            job.finished = Clock::now();
            if (job.result.is_null())
                job.result = {{"action", "already-installed"}};
        } else if (job.autoConfirm && !job.plan["sourceChange"].is_object() && !job.plan.value("override", false)) {
            job.confirmed = true;
            job.confirmedPlan = job.plan;
            job.stage = "queued";
        } else
            job.stage = "confirm";
        bump();
        return;
    }
    if (job.step + 1 < job.commands.size()) {
        ++job.step;
        job.stage = "queued";
        bump();
        return;
    }
    job.stage = "done";
    job.finished = Clock::now();
    static const std::map<std::string, std::pair<std::string, std::string>> words{
        {"install", {"content.installed", "Installed"}},  {"update", {"content.updated", "Updated"}},
        {"rollback", {"content.rolled-back", "Rolled back"}}, {"remove", {"content.removed", "Removed"}},
        {"source.add", {"source.added", "Source added"}}};
    if (auto w = words.find(job.kind); w != words.end())
        notices.push_back({"runtime.notice",
                           {{"level", "info"},
                            {"code", w->second.first},
                            {"message", w->second.second + (job.target.is_string() ? ": " + job.target.get<std::string>() : std::string())}}});
    listing_ = true;
    bump();
}
void Engine::startListing() {
    listingSteps_ = {{"installed"}, {"source", "list"}, {"browse"}};
    listingStep_ = 0;
    try {
        process_ = std::make_unique<Process>(argv(listingSteps_[0], nullptr), environment());
        running_ = "";
    } catch (const std::exception &e) {
        process_.reset();
        listing_ = false;
        static bool told = false;
        if (!told)
            std::cerr << "hypruned: content listings unavailable: " << e.what() << '\n';
        told = true;
    }
}
void Engine::finishListing(bool ok) {
    // The three listings land together: a client that re-reads registry.list when sources or
    // installs change must find the matching entries.
    Json doc;
    if (ok)
        try {
            doc = Json::parse(process_->output());
        } catch (...) {
            ok = false;
        }
    process_.reset();
    if (listingStep_ == 0)
        pending_ = {installs_, sources_, entries_};
    if (ok) {
        Json out = Json::array();
        static const char *keys[] = {"installs", "sources", "entries"};
        for (auto &item : doc.value(keys[listingStep_], Json::array()))
            out.push_back(listingStep_ == 0 ? installItem(item) : listingStep_ == 1 ? sourceItem(item) : registryItem(item));
        pending_[listingStep_] = out;
    }
    if (++listingStep_ < listingSteps_.size()) {
        try {
            process_ = std::make_unique<Process>(argv(listingSteps_[listingStep_], nullptr), environment());
            return;
        } catch (...) {
            process_.reset();
        }
    }
    if (pending_[0] != installs_ || pending_[1] != sources_ || pending_[2] != entries_) {
        installs_ = pending_[0];
        sources_ = pending_[1];
        entries_ = pending_[2];
        bump();
    }
    listedOnce_ = true;
    running_.clear();
}
bool Engine::watchChanged() {
    bool moved = false;
    auto stamp = [&](const fs::path &p) {
        std::error_code ec;
        auto t = fs::last_write_time(p, ec);
        if (ec)
            t = {};
        auto it = stamps_.find(p);
        if (it == stamps_.end() || it->second != t) {
            moved = moved || it != stamps_.end();
            stamps_[p] = t;
        }
    };
    for (auto &p : paths_.watched) {
        stamp(p);
        std::error_code ec;
        if (fs::is_directory(p, ec) && p.filename() == "sources")
            for (auto &d : fs::directory_iterator(p, ec))
                stamp(d.path() / "meta.json");
    }
    return moved;
}
void Engine::tick() {
    auto now = Clock::now();
    if (process_) {
        std::vector<std::string> lines;
        bool alive = process_->poll(lines);
        if (running_.empty()) { // a listing step
            if (!alive) {
                finishListing(process_->exitCode() == 0);
                if (!process_)
                    listing_ = false;
            }
        } else if (auto *job = find(running_)) {
            for (auto &l : lines) {
                handleLine(*job, l);
                if (job->stage == "failed")
                    break;
            }
            if (job->stage == "failed") { // plan.changed: the process was stopped
                process_.reset();
                running_.clear();
            } else if (!alive)
                finishJob(*job);
        } else {
            process_.reset();
            running_.clear();
        }
        if (process_)
            return;
    }
    // Finished jobs stay listed for a while.
    auto before = jobs_.size();
    std::erase_if(jobs_, [&](const Job &j) {
        return (j.stage == "done" || j.stage == "failed" || j.stage == "cancelled") && now - j.finished > keepFinished;
    });
    if (jobs_.size() != before)
        bump();
    if (now - lastWatch_ >= watchEvery) {
        lastWatch_ = now;
        if (watchChanged())
            listing_ = true;
    }
    // A confirmed job runs first, then queued ones in order; a listing refresh waits for none.
    Job *next = nullptr;
    for (auto &j : jobs_)
        if (j.stage == "queued" && (j.confirmed || !next) && (!next || !next->confirmed))
            next = &j;
    if (listing_) {
        startListing();
        if (!process_)
            listing_ = false;
        return;
    }
    if (next)
        start(*next);
}
Json Engine::decorate(Json state) const {
    Json jobs = Json::array();
    for (auto &j : jobs_)
        jobs.push_back(j.view());
    int updates = 0;
    for (auto &i : installs_)
        if (i["newerTag"].is_string() && !i.value("pinned", false))
            ++updates;
    state["content"] = {{"revision", revision_}, {"installs", installs_}, {"updates", updates}, {"sources", sources_}, {"jobs", jobs}};
    return state;
}
} // namespace hyprune::content
