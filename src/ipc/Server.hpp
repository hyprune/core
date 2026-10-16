#pragma once
#include <chrono>
#include <deque>
#include <functional>
#include <json.hpp>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
namespace hyprune::ipc {
using Json = nlohmann::json;
struct Error : std::runtime_error {
    int code;
    Json data; // optional JSON-RPC error data (IPC 0.22: {retryable, rule}); null = none
    Error(int c, const std::string &s, Json d = nullptr) : std::runtime_error(s), code(c), data(std::move(d)) {}
};
struct LaunchGrant {
    std::string token, client;
    std::set<std::string> capabilities;
    int recipientPid = 0; // Zero only for legacy initial one-shot grants.
    std::chrono::steady_clock::time_point expires = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    std::string profile; // IPC 0.33 (RFC-0016): "assistant" for a token from session.pair
};
class WireAudit;
class Server {
  public:
    using Handler = std::function<Json(const std::string &, const Json &)>;
    Server(const std::string &runtime, const std::string &instance, std::vector<LaunchGrant>, Handler);
    ~Server();
    Server(const Server &) = delete;
    void flush();
    bool reissue(const LaunchGrant &); // Private supervisor channel only; never a public IPC method.
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
        // IPC 0.33 (RFC-0016): a pairing request waiting for the user, the assistant profile.
        std::string pairRequest, profile, action;
        Json agent, releaseDraft; // releaseDraft: a release the assistant prepared (null: none)
        bool paused = false, remembered = false;
        uint64_t changes = 0, mutationsTotal = 0;
        std::deque<std::chrono::steady_clock::time_point> mutations;
        std::chrono::steady_clock::time_point connected{}, lastPreview{};
    };
    struct Pair {
        std::string requestId, clientId, clientName;
        Json agent, rpcId;
        int fd = -1;
        std::chrono::steady_clock::time_point expires;
    };
    std::vector<Pair> pairs_;
    std::deque<std::chrono::steady_clock::time_point> pairTimes_;
    Json approvals_;
    bool approvalsLoaded_ = false;
    void pair(Peer &, const Json &params, const Json &id);
    Json answer(const Json &params);
    void expirePairs();
    Json connectionsState() const;
    Json assistantState(const Json &state) const;
    void loadApprovals();
    void saveApprovals() const;
    void grantPair(Peer &, const Pair &, bool remembered);
    std::unique_ptr<WireAudit> audit_;
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
    void queue(Peer &, const Json &, const std::string &method = "");
};
std::string randomId();
} // namespace hyprune::ipc
