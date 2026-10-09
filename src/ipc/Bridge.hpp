#pragma once
#include "Server.hpp"
#include "Text.hpp"
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
    // `derived` lists binding IDs that are recomputed from the world; hypruned
    // publishes them but never persists them as user layout.
    void publish(Json state, Json derived = Json::array());
    void notify(const std::string &event, Json params, const std::string &actor = "") {
        if (params.is_object() && params.contains("message") && params["message"].is_string())
            params["message"] = boundedText(params["message"].get<std::string>(), MessageLimit);
        send({{"type", "event"}, {"event", event}, {"params", std::move(params)}, {"actor", actor}});
    }
    void renderer(Json request) { send({{"type", "renderer.request"}, {"request", std::move(request)}}); }
    void poll();
    void complete(Json result);
    void fail(int code, const std::string &message);
    uint64_t revision() const { return revision_; }
    bool healthy() const;

  private:
    int fd_ = -1;
    pid_t child_ = -1;
    Handler handler_;
    Json state_, derived_ = Json::array();
    uint64_t revision_ = 0;
    std::string pending_, epoch_;
    std::deque<std::string> output_;
    std::chrono::steady_clock::time_point heartbeat_ = std::chrono::steady_clock::now();
    void send(Json);
};
} // namespace hyprune::ipc
