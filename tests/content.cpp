// RFC-0015 content engine (IPC 0.32): jobs over the SDK CLI (a fake one here), listings, the
// confirm flow and error mapping.
#include "content/Engine.hpp"
#include "ipc/Protocol.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>
using namespace hyprune;
using content::Engine;
using content::Json;
namespace fs = std::filesystem;
namespace {
void check(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void until(Engine &e, F done, const std::string &what, int ms = 15000) {
    for (int i = 0; i < ms / 5; ++i) {
        e.tick();
        if (done())
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("timed out: " + what);
}
Json job(Engine &e, const std::string &id) {
    for (auto &j : e.decorate(Json::object())["content"]["jobs"])
        if (j["jobId"] == id)
            return j;
    return nullptr;
}
int rejects(Engine &e, const std::string &method, const Json &params) {
    try {
        e.call(method, params);
    } catch (const ipc::Error &error) {
        return error.code;
    }
    return 0;
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    auto tmp = fs::temp_directory_path() / ("hyprune-content-" + std::to_string(getpid()));
    fs::create_directories(tmp);
    auto log = tmp / "cli.log";
    setenv("FAKE_CLI_LOG", log.c_str(), 1);
    setenv("HYPRUNE_SOCKET", "/nowhere/socket", 1); // never handed to the CLI
    Engine::Paths paths{{"python3", "-I", argv[1]}, {tmp / "content.json"}};
    Engine e(paths);
    e.watchEvery = std::chrono::milliseconds(50);
    // Listings at start, trimmed to the IPC fields.
    until(e, [&] { return e.entries().size() == 2 && e.sources().size() == 2 && e.installs().size() == 1; }, "first listing");
    check(!e.installs()[0].contains("path") && !e.installs()[0].contains("sha256") && !e.installs()[0].contains("pending"),
          "installs carry only the IPC fields");
    auto state = e.decorate(Json::object())["content"];
    check(state["updates"] == 1 && state["jobs"].empty() && state["sources"].size() == 2, "content state");
    check(e.call("sources.list", {})["sources"].size() == 2, "sources.list");
    auto list = e.call("registry.list", {{"query", "TIDE"}});
    check(list["entries"].size() == 1 && list["entries"][0]["id"] == "io.github.ada.tidepool" && list["cursor"].is_null(), "query");
    check(e.call("registry.list", {{"tags", {"sand"}}})["entries"][0]["id"] == "io.github.ada.dunes", "tags");
    auto page = e.call("registry.list", {{"limit", 1}});
    check(page["entries"].size() == 1 && page["cursor"] == "1", "first page");
    check(e.call("registry.list", {{"limit", 1}, {"cursor", "1"}})["cursor"].is_null(), "last page");
    check(e.call("registry.entry", {{"sourceId", "community"}, {"id", "io.github.ada.dunes"}})["installed"].is_null(), "entry");
    check(rejects(e, "registry.entry", {{"sourceId", "community"}, {"id", "io.github.ada.none"}}) == -32004, "unknown entry");
    check(rejects(e, "registry.list", {{"cursor", "x"}}) == -32602, "bad cursor");

    // Install: plan, confirm, the pipeline's stages, done and a notice; listings refresh.
    auto id = e.call("content.install", {{"source", {{"entry", {{"sourceId", "community"}, {"id", "io.github.ada.tidepool"}}}}}})["jobId"]
                  .get<std::string>();
    until(e, [&] { return job(e, id)["stage"] == "confirm"; }, "plan");
    auto j = job(e, id);
    check(j["kind"] == "install" && j["target"] == "io.github.ada.tidepool" && j["plan"]["packages"].size() == 1 &&
              !j["plan"]["packages"][0].contains("extraCliField") && j["plan"]["override"] == false,
          "the plan, trimmed");
    check(rejects(e, "content.cancel", {{"jobId", "j-00000000"}}) == -32004, "unknown job");
    e.call("content.confirm", {{"jobId", id}, {"accept", true}});
    until(e, [&] { return job(e, id)["stage"] == "done"; }, "install");
    check(job(e, id)["progress"]["bytes"] == 100 && job(e, id)["result"]["action"] == "install", "progress and result");
    bool told = false;
    for (auto &[name, params] : e.notices)
        told = told || (params["code"] == "content.installed" && params["level"] == "info");
    check(told, "content.installed notice");
    e.notices.clear();
    {
        std::ifstream in(log);
        std::string line, all;
        while (std::getline(in, line))
            all += line + "\n";
        check(all.find("[\"world\", \"add\", \"io.github.ada.tidepool\", \"--from\", \"community\", \"--json\", \"--no-session\", \"--plan\"]") !=
                      std::string::npos &&
                  all.find("\"--yes\"]") != std::string::npos && all.find("SESSION SOCKET LEAKED") == std::string::npos,
              "plan run, then --yes: " + all);
    }
    // autoConfirm runs straight through; a source change still waits.
    auto quick = e.call("content.install", {{"source", {{"github", "ada/tidepool-world"}}}, {"type", "world"}, {"autoConfirm", true}})["jobId"]
                     .get<std::string>();
    until(e, [&] { return job(e, quick)["stage"] == "done"; }, "auto confirm");
    auto moved = e.call("content.install", {{"source", {{"url", "https://example.org/io.github.ada.moved.tar.zst"}}}, {"type", "world"},
                                            {"autoConfirm", true}})["jobId"].get<std::string>();
    until(e, [&] { return job(e, moved)["stage"] == "confirm"; }, "source change waits");
    check(job(e, moved)["plan"]["sourceChange"].is_object(), "source change in the plan");
    e.call("content.confirm", {{"jobId", moved}, {"accept", false}});
    check(job(e, moved)["stage"] == "cancelled", "declined");
    check(rejects(e, "content.confirm", {{"jobId", moved}, {"accept", true}}) == -32005, "confirm only in confirm");
    // The plan changed between confirm and the --yes run.
    setenv("FAKE_PLAN_CHANGE", "1", 1);
    auto changed = e.call("content.install", {{"source", {{"entry", {{"sourceId", "community"}, {"id", "io.github.ada.tidepool"}}}}}})["jobId"]
                       .get<std::string>();
    until(e, [&] { return job(e, changed)["stage"] == "confirm"; }, "plan 2");
    e.call("content.confirm", {{"jobId", changed}, {"accept", true}});
    until(e, [&] { return job(e, changed)["stage"] == "failed"; }, "plan changed");
    check(job(e, changed)["error"]["rule"] == "plan.changed", "plan.changed");
    unsetenv("FAKE_PLAN_CHANGE");
    // Errors: a job's error event, a document command's stderr, an unshaped failure.
    auto roll = e.call("content.rollback", {{"id", "com.hyprune.street"}})["jobId"].get<std::string>();
    until(e, [&] { return job(e, roll)["stage"] == "failed"; }, "rollback refused");
    check(job(e, roll)["error"]["rule"] == "world.active", "the CLI's rule");
    auto label = e.call("sources.set", {{"id", "community"}, {"label", "Shared"}})["jobId"].get<std::string>();
    until(e, [&] { return job(e, label)["stage"] == "done"; }, "label");
    check(job(e, label)["result"]["label"] == "Shared", "document result");
    auto gone = e.call("sources.remove", {{"id", "official"}})["jobId"].get<std::string>();
    until(e, [&] { return job(e, gone)["stage"] == "failed"; }, "remove refused");
    check(job(e, gone)["error"]["rule"] == "not.found", "stderr rule");
    auto pin = e.call("content.pin", {{"id", "com.hyprune.street"}, {"pinned", true}})["jobId"].get<std::string>();
    until(e, [&] { return job(e, pin)["stage"] == "failed"; }, "pin failure");
    check(job(e, pin)["error"]["rule"] == "cli.failed" && job(e, pin)["error"]["message"] == "something unexpected", "unshaped failure");
    check(rejects(e, "content.rollback", {{"id", "io.github.ada.none"}}) == -32004, "not installed");
    check(rejects(e, "sources.remove", {{"id", "nope"}}) == -32004, "unknown source");
    check(rejects(e, "content.install", {{"source", {{"github", "a/b"}}}}) == -32602, "type required");
    // Nothing to update: done without a confirm step.
    auto none = e.call("content.update", {{"all", true}})["jobId"].get<std::string>();
    until(e, [&] { return job(e, none)["stage"] == "done"; }, "update all");
    // Cancel a queued job; the limit.
    e.jobLimit = 30;
    // The CLI can't run.
    Engine missing(Engine::Paths{{"/nonexistent/hyprune"}, {}});
    auto lost = missing.call("sources.refresh", {})["jobId"].get<std::string>();
    until(missing, [&] { return job(missing, lost)["stage"] == "failed"; }, "cli.missing");
    check(job(missing, lost)["error"]["rule"] == "cli.missing", "cli.missing");
    missing.jobLimit = 2;
    missing.call("sources.refresh", {});
    check(rejects(missing, "sources.refresh", {}) == 0, "a finished job makes room");
    check(rejects(missing, "sources.refresh", {}) == -32008, "too many jobs");
    auto queued = missing.decorate(Json::object())["content"]["jobs"].back()["jobId"].get<std::string>();
    missing.call("content.cancel", {{"jobId", queued}});
    check(job(missing, queued)["stage"] == "cancelled", "cancel a queued job");
    // Protocol floors.
    check(!ipc::protocol::methodAvailable("0.31", "content.install") && ipc::protocol::methodAvailable("0.32", "registry.list") &&
              ipc::protocol::methodAvailable("0.32", "sources.refresh"),
          "IPC 0.32 floor");
    fs::remove_all(tmp);
    std::cout << "content tests passed\n";
}
