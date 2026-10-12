#pragma once
#include <cstring>
#include <string>
#include <json.hpp>
#include <optional>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
namespace hyprune::offload {
using Json = nlohmann::json;
struct Packet {
    Json body;
    std::vector<int> fds;
    Packet() = default;
    Packet(const Packet &) = delete;
    Packet(Packet &&other) noexcept : body(std::move(other.body)), fds(std::move(other.fds)) { other.fds.clear(); }
    Packet &operator=(Packet &&other) noexcept {
        if (this != &other) {
            for (int fd : fds)
                close(fd);
            body = std::move(other.body);
            fds = std::move(other.fds);
            other.fds.clear();
        }
        return *this;
    }
    ~Packet() {
        for (int fd : fds)
            close(fd);
    }
    int take() {
        if (fds.size() != 1)
            throw std::runtime_error("expected one fd");
        int fd = fds[0];
        fds.clear();
        return fd;
    }
};
inline bool sendPacket(int fd, const Json &body, const std::vector<int> &fds = {}) {
    auto bytes = body.dump();
    if (bytes.size() > 196608 || fds.size() > 4)
        throw std::runtime_error("packet limit");
    iovec io{bytes.data(), bytes.size()};
    msghdr m{};
    m.msg_iov = &io;
    m.msg_iovlen = 1;
    alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int) * 4)]{};
    if (!fds.empty()) {
        m.msg_control = control;
        m.msg_controllen = CMSG_SPACE(sizeof(int) * fds.size());
        auto c = CMSG_FIRSTHDR(&m);
        c->cmsg_level = SOL_SOCKET;
        c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN(sizeof(int) * fds.size());
        std::memcpy(CMSG_DATA(c), fds.data(), sizeof(int) * fds.size());
    }
    auto n = sendmsg(fd, &m, MSG_NOSIGNAL | MSG_DONTWAIT);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return false;
    if (n != ssize_t(bytes.size()))
        throw std::runtime_error("renderer transport write");
    return true;
}
inline std::optional<Packet> receivePacket(int fd) {
    char bytes[196609];
    alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int) * 4)]{};
    iovec io{bytes, sizeof(bytes)};
    msghdr m{};
    m.msg_iov = &io;
    m.msg_iovlen = 1;
    m.msg_control = control;
    m.msg_controllen = sizeof(control);
    auto n = recvmsg(fd, &m, MSG_CMSG_CLOEXEC | MSG_DONTWAIT);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return {};
    Packet p;
    for (auto c = CMSG_FIRSTHDR(&m); c; c = CMSG_NXTHDR(&m, c))
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
            auto f = reinterpret_cast<int *>(CMSG_DATA(c));
            for (size_t i = 0; i < (c->cmsg_len - CMSG_LEN(0)) / sizeof(int); ++i)
                p.fds.push_back(f[i]);
        }
    if (n == 0)
        throw std::runtime_error("renderer transport EOF");
    if (n < 0)
        throw std::runtime_error(std::string("renderer transport read: ") + std::strerror(errno));
    if (n > 196608 || (m.msg_flags & (MSG_TRUNC | MSG_CTRUNC)))
        throw std::runtime_error("renderer transport truncation (" + std::to_string(n) + " bytes" +
                                 (m.msg_flags & MSG_CTRUNC ? ", descriptors" : "") + ")");
    p.body = Json::parse(bytes, bytes + n);
    return p;
}
} // namespace hyprune::offload
