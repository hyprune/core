#pragma once
#include "Server.hpp"
#include <sys/socket.h>
#include <unistd.h>
namespace hyprune::ipc {
// Inherited private SEQPACKET channel, established by the trusted bootstrap.
// No listener and no public issuance method. Capabilities cannot exceed the
// original launch manifest. All new grants are PID-bound, expiring and one-use.
class GrantIssuer {
    int fd_ = -1;
    std::map<std::string, std::set<std::string>> grants_;
    std::chrono::steady_clock::time_point refill_ = std::chrono::steady_clock::now();
    unsigned budget_ = 8, total_ = 0;

  public:
    GrantIssuer(int fd, const std::vector<LaunchGrant> &grants) : fd_(fd) {
        for (auto &g : grants)
            grants_[g.client] = g.capabilities;
    }
    ~GrantIssuer() {
        if (fd_ >= 0)
            close(fd_);
    }
    void poll(Server &server) {
        if (fd_ < 0)
            return;
        char bytes[1025];
        auto n = recv(fd_, bytes, sizeof bytes, MSG_DONTWAIT | MSG_TRUNC);
        if (n < 0)
            return;
        if (n == 0) {
            close(fd_);
            fd_ = -1;
            return;
        }
        Json reply = {{"ok", false}};
        auto now = std::chrono::steady_clock::now();
        if (now - refill_ >= std::chrono::seconds(1)) {
            budget_ = 8;
            refill_ = now;
        }
        try {
            if (n > 1024 || !budget_ || total_ >= 4096)
                throw std::runtime_error("renewal budget");
            --budget_;
            ++total_;
            auto request = Json::parse(bytes, bytes + n);
            if (request.size() != 3 || !request.contains("client") || !request.contains("token") ||
                !request.contains("pid"))
                throw std::runtime_error("renewal shape");
            auto client = request.at("client").get<std::string>();
            if (!grants_.contains(client))
                throw std::runtime_error("unknown launch identity");
            LaunchGrant grant{request.at("token"), client, grants_.at(client), request.at("pid").get<int>()};
            reply["ok"] = server.reissue(grant);
        } catch (const std::exception &) {
        }
        auto response = reply.dump();
        if (send(fd_, response.data(), response.size(), MSG_DONTWAIT | MSG_NOSIGNAL) != (ssize_t)response.size()) {
            close(fd_);
            fd_ = -1;
        }
    }
};
} // namespace hyprune::ipc
