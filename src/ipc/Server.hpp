#pragma once
#include <chrono>
#include <functional>
#include <json.hpp>
#include <map>
#include <set>
#include <string>
#include <vector>
namespace hyprune::ipc {
using Json = nlohmann::json;
struct Error : std::runtime_error {
    int code;
    Error(int c, const std::string &s) : std::runtime_error(s), code(c) {}
};
struct LaunchGrant {
    std::string token, client;
    std::set<std::string> capabilities;
    std::chrono::steady_clock::time_point expires = std::chrono::steady_clock::now() + std::chrono::seconds(30);
};
class Server {
  public:
    using Handler = std::function<Json(const std::string &, const Json &)>;
    Server(const std::string &runtime, const std::string &instance, std::vector<LaunchGrant>, Handler);
    ~Server();
    Server(const Server &) = delete;
    void flush();
    bool connected(const std::string &actor) const;
    void poll(); // Called only outside the render pass; bounded nonblocking I/O.
    void publish(Json state);
    void notify(const std::string &event, Json params, const std::string &actor = "");
    void contextual(bool enabled) { contextual_ = enabled; }
    Json snapshot() const;
    void advertise(std::map<std::string, std::string> methods) { methods_ = std::move(methods); }
    const std::string &path() const { return path_; }
    uint64_t revision() const { return revision_; }

  private:
    struct Peer {
        int fd = -1, pid = -1;
        std::string input, output;
        size_t written = 0;
        bool hello = false, closing = false;
        std::string subscription, protocol, actor, client;
        std::set<std::string> ids, grants, hudTopics;
        std::string hudSubscription;
        std::map<std::string, std::chrono::steady_clock::time_point> hudLast;
        uint64_t baseline = 0;
        std::chrono::steady_clock::time_point accepted = std::chrono::steady_clock::now(), frameStart = accepted,
                                              lastDelta = accepted;
    };
    int fd_ = -1;
    std::string path_, session_;
    std::vector<Peer> peers_;
    std::vector<LaunchGrant> tokens_;
    std::map<std::string, std::string> methods_;
    Handler handler_;
    bool contextual_ = false;
    Json snapshot(const Peer &) const;
    Json visibleState(const Peer &) const;
    Json state_;
    uint64_t revision_ = 0, subscriptions_ = 0;
    void frame(Peer &, const std::string &);
    void queue(Peer &, const Json &);
};
std::string randomId();
} // namespace hyprune::ipc
