#pragma once
#include "Server.hpp"
#include <deque>
#include <optional>
#include <sys/types.h>
namespace hyprune::ipc {
// Private, build-pinned transport. Only a socketpair inherited by hypruned.
class Bridge {
  public:
    using Handler = std::function<std::optional<Json>(const std::string &, const Json &)>;
    Bridge(const std::string &helper, Handler);
    ~Bridge();
    void advertise(std::map<std::string, std::string>);
    void publish(Json);
    void poll();
    void complete(Json result);
    void fail(int code, const std::string &message);
    uint64_t revision() const { return revision_; }
    bool healthy() const;

  private:
    int fd_ = -1;
    pid_t child_ = -1;
    Handler handler_;
    Json state_;
    uint64_t revision_ = 0;
    std::string pending_, epoch_;
    std::deque<std::string> output_;
    std::chrono::steady_clock::time_point heartbeat_ = std::chrono::steady_clock::now();
    void send(Json);
};
} // namespace hyprune::ipc
