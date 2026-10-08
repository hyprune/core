#include "Bridge.hpp"
#include "offload/Packet.hpp"
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
namespace hyprune::ipc {
Bridge::Bridge(const std::string &helper, Handler handler) : handler_(std::move(handler)), epoch_(randomId()) {
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, pair))
        throw std::runtime_error("private bridge socketpair failed");
    fd_ = pair[0];
    // Duplicate to high fds before constructing child actions: never clobber a
    // source fd while remapping another one to the fixed child ABI (3,4).
    int source = fcntl(pair[1], F_DUPFD_CLOEXEC, 100);
    close(pair[1]);
    const char *launch = getenv("HYPRUNE_LAUNCH_FD");
    int grant = -1;
    if (launch) {
        int inherited = std::stoi(launch);
        grant = fcntl(inherited, F_DUPFD_CLOEXEC, 100);
        close(inherited);
        unsetenv("HYPRUNE_LAUNCH_FD");
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, source, 3);
    posix_spawn_file_actions_addclose(&actions, source);
    if (grant >= 0) {
        posix_spawn_file_actions_adddup2(&actions, grant, 4);
        posix_spawn_file_actions_addclose(&actions, grant);
    }
    std::vector<std::string> envStrings;
    for (char **p = environ; *p; p++)
        envStrings.emplace_back(*p);
    envStrings.emplace_back("HYPRUNE_BRIDGE_FD=3");
    if (grant >= 0)
        envStrings.emplace_back("HYPRUNE_LAUNCH_FD=4");
    std::vector<char *> env;
    for (auto &s : envStrings)
        env.push_back(s.data());
    env.push_back(nullptr);
    char *args[] = {const_cast<char *>(helper.c_str()), nullptr};
    // Hyprland may block termination signals for its own event loop. The helper
    // has independent handlers and must be able to shut down on SIGTERM.
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    sigset_t empty;
    sigemptyset(&empty);
    posix_spawnattr_setsigmask(&attributes, &empty);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK);
    int rc = posix_spawn(&child_, helper.c_str(), &actions, &attributes, args, env.data());
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    close(source);
    if (grant >= 0)
        close(grant);
    if (rc) {
        close(fd_);
        fd_ = -1;
        throw std::runtime_error("cannot start hypruned");
    }
    send({{"type", "hello"}, {"version", 1}, {"epoch", epoch_}});
}
Bridge::~Bridge() {
    if (fd_ >= 0)
        close(fd_);
    if (child_ > 0) {
        kill(child_, SIGTERM);
        waitpid(child_, nullptr, 0);
    }
}
void Bridge::send(Json j) {
    j["epoch"] = epoch_;
    std::string bytes = j.dump();
    if (bytes.size() > 196608 || output_.size() >= 64)
        throw Error(-32008, "private bridge budget exceeded");
    output_.push_back(std::move(bytes));
}
void Bridge::advertise(std::map<std::string, std::string> methods) {
    send({{"type", "methods"}, {"methods", methods}});
}
void Bridge::publish(Json state) {
    if (state == state_)
        return;
    state_ = std::move(state);
    ++revision_;
    send({{"type", "state"}, {"state", state_}});
}
void Bridge::complete(Json result) {
    if (pending_.empty())
        return;
    send({{"type", "reply"}, {"id", pending_}, {"result", std::move(result)}});
    pending_.clear();
}
void Bridge::fail(int code, const std::string &message) {
    if (pending_.empty())
        return;
    send({{"type", "reply"}, {"id", pending_}, {"error", {{"code", code}, {"message", message}}}});
    pending_.clear();
}
bool Bridge::healthy() const {
    return fd_ >= 0 && std::chrono::steady_clock::now() - heartbeat_ < std::chrono::seconds(1);
}
void Bridge::poll() {
    for (int i = 0; i < 16; i++) {
        std::optional<offload::Packet> packet;
        try { packet = offload::receivePacket(fd_); }
        catch (...) { close(fd_); fd_ = -1; return; }
        if (!packet) break;
        auto &j = packet->body;
        if (j.at("epoch") != epoch_)
            throw std::runtime_error("private epoch mismatch");
        heartbeat_ = std::chrono::steady_clock::now();
        if (j.at("type") == "renderer.channel") {
            int received = packet->take();
            auto params = j; params["fd"] = received;
            try { handler_("_renderer.channel", params); } catch (...) {close(received);throw;}
            continue;
        }
        if (!packet->fds.empty()) throw std::runtime_error("unexpected private descriptor");
        if (j.at("type") == "renderer.state") {handler_("_renderer.state", j.at("state"));continue;}
        if (j.at("type") == "layout") {
            handler_("_layout.restore", j.at("layout"));
            send({{"type", "layout.ready"}});
            continue;
        }
        if (j.at("type") == "heartbeat")
            continue;
        if (j.at("type") == "cancel") {
            handler_("_operation.cancel", Json::object());
            continue;
        }
        if (j.at("type") != "command")
            throw std::runtime_error("invalid private message");
        if (!pending_.empty())
            throw std::runtime_error("concurrent bridge command");
        pending_ = j.at("id");
        auto now =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        if (j.at("deadlineMs").get<int64_t>() < now) {
            fail(-32006, "command expired");
            continue;
        }
        try {
            if (auto result = handler_(j.at("method"), j.at("params")))
                complete(*result);
        } catch (const Error &e) {
            fail(e.code, e.what());
        } catch (const Json::exception &) {
            fail(-32602, "invalid params");
        } catch (const std::exception &e) {
            fail(-32603, e.what());
        }
    }
    while (!output_.empty() && fd_ >= 0) {
        auto &bytes = output_.front();
        auto n = ::send(fd_, bytes.data(), bytes.size(), MSG_NOSIGNAL);
        if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                close(fd_);
                fd_ = -1;
            }
            break;
        }
        output_.pop_front();
    }
}
} // namespace hyprune::ipc
