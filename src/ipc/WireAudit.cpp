#include "WireAudit.hpp"
#include <cstdio>
#include <cstdlib>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
namespace hyprune::ipc {
namespace {
[[noreturn]] void failed(const char *reason) {
    std::fprintf(stderr, "IPC SCHEMA ASSERTION: %s\n", reason);
    std::fflush(stderr);
    std::abort(); // The helper fails the nested run; never abort the compositor.
}
} // namespace
WireAudit::WireAudit() {
#ifdef HYPRUNE_WIRE_AUDIT_SCRIPT
    const char *requested = std::getenv("HYPRUNE_VALIDATE_IPC");
#ifdef NDEBUG
    bool enabled = requested && std::string(requested) == "1";
#else
    bool enabled = !requested || std::string(requested) != "0";
#endif
    if (enabled)
        worker_ = std::thread([this] { run(); });
#endif
}
WireAudit::~WireAudit() {
    if (!worker_.joinable())
        return;
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    ready_.notify_one();
    worker_.join();
}
void WireAudit::check(const std::string &protocol, const Json &frame, const std::string &method) {
    if (!worker_.joinable())
        return;
    std::lock_guard lock(mutex_);
    if (pending_.size() >= 2048)
        failed("validator backlog exceeded (no frames skipped)");
    pending_.push_back(
        Json{{"protocol", protocol.empty() ? "0.1" : protocol}, {"method", method}, {"frame", frame}}.dump(
            -1, ' ', false, Json::error_handler_t::replace) +
        "\n");
    ready_.notify_one();
}
void WireAudit::run() {
#ifdef HYPRUNE_WIRE_AUDIT_SCRIPT
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair))
        failed("validator socketpair");
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pair[1], 0);
    posix_spawn_file_actions_adddup2(&actions, pair[1], 1);
    char *args[] = {const_cast<char *>("python3"), const_cast<char *>(HYPRUNE_WIRE_AUDIT_SCRIPT), nullptr};
    pid_t child;
    int result = posix_spawnp(&child, "python3", &actions, nullptr, args, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pair[1]);
    if (result)
        failed("cannot launch schema validator");
    size_t checked = 0;
    // A forced stop (systemctl stop, SIGTERM to the process group) kills the
    // validator too: that ends the audit, it is not a schema failure.
    auto killed = [&] {
        int status = 0;
        pid_t done = 0;
        for (int i = 0; i < 100 && done != child; ++i) {
            done = waitpid(child, &status, WNOHANG);
            if (done != child) usleep(10000);
        }
        if (done != child || !WIFSIGNALED(status))
            return false;
        std::fprintf(stderr, "IPC schema audit: validator stopped by signal %d after %zu frames\n", WTERMSIG(status), checked);
        return true;
    };
    timeval timeout{15, 0};
    setsockopt(pair[0], SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    setsockopt(pair[0], SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
    while (true) {
        std::string item;
        {
            std::unique_lock lock(mutex_);
            ready_.wait(lock, [&] { return stopping_ || !pending_.empty(); });
            if (pending_.empty())
                break;
            item = std::move(pending_.front());
            pending_.pop_front();
        }
        size_t at = 0;
        while (at < item.size()) {
            auto n = send(pair[0], item.data() + at, item.size() - at, MSG_NOSIGNAL);
            if (n <= 0) {
                if (killed()) {
                    close(pair[0]);
                    return;
                }
                failed("validator write");
            }
            at += n;
        }
        char reply;
        auto got = recv(pair[0], &reply, 1, 0);
        if (got != 1 && killed()) {
            close(pair[0]);
            return;
        }
        if (got != 1 || reply != '.')
            failed("outgoing frame rejected (see validator path above)");
        ++checked;
    }
    close(pair[0]);
    int status = 0;
    waitpid(child, &status, 0);
    if (WIFSIGNALED(status)) {
        std::fprintf(stderr, "IPC schema audit: validator stopped by signal %d after %zu frames\n", WTERMSIG(status), checked);
        return;
    }
    if (status)
        failed("validator exit");
    std::fprintf(stderr, "IPC schema audit: %zu outgoing frames validated\n", checked);
#endif
}
} // namespace hyprune::ipc
