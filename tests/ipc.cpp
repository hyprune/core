#include "ipc/Server.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
using namespace hyprune::ipc;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Client {
    int fd;
    explicit Client(const std::string &path) {
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strcpy(addr.sun_path, path.c_str());
        check(connect(fd, (sockaddr *)&addr, sizeof addr) == 0, "connect");
        timeval timeout{2, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    }
    ~Client() { close(fd); }
    void sendBytes(std::string bytes, bool split = false) {
        uint32_t n = htonl(bytes.size());
        std::string frame((char *)&n, 4);
        frame += bytes;
        for (size_t offset = 0; offset < frame.size();) {
            size_t size = split ? 1 : frame.size() - offset;
            auto n = send(fd, frame.data() + offset, size, MSG_NOSIGNAL);
            check(n > 0, "send");
            offset += n;
        }
    }
    void sendJson(Json j, bool split = false) { sendBytes(j.dump(), split); }
    Json receive() {
        auto exact = [&](char *b, size_t size) {
            size_t got = 0;
            while (got < size) {
                auto n = recv(fd, b + got, size - got, 0);
                check(n > 0, "response timed out/closed");
                got += n;
            }
        };
        uint32_t size;
        exact((char *)&size, 4);
        size = ntohl(size);
        check(size <= 1048576, "oversized response");
        std::string bytes(size, '\0');
        exact(bytes.data(), size);
        return Json::parse(bytes);
    }
};
Json req(std::string id, std::string method, Json params = Json::object()) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
}
Json hello(const std::string &token = "") {
    Json p = {{"protocols", {"0.1"}},
              {"client", {{"id", "org.hyprune.test"}, {"name", "Test"}}},
              {"capabilities", {"state.read"}}};
    if (!token.empty())
        p["token"] = token;
    return req("hello", "session.hello", p);
}
} // namespace
int main() {
    char pattern[] = "/tmp/hn-ipc-XXXXXX";
    char *dir = mkdtemp(pattern);
    if (!dir)
        return 1;
    std::atomic<bool> stop = false;
    std::thread loop;
    try {
        auto token = randomId();
        Server server(dir, "test", {{token, "org.hyprune.test", {"state.read"}}},
                      [](auto &, auto &) { return Json{}; });
        server.publish({{"runtime", {{"active", false}}}});
        loop = std::thread([&] {
            while (!stop) {
                server.poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        {
            Client c(server.path());
            c.sendJson(hello(token), true);
            auto h = c.receive();
            check(h["result"]["protocol"] == "0.1", "split hello");
            c.sendJson(req("sub", "state.subscribe", {{"topics", {"state"}}}));
            auto result = c.receive();
            check(result.contains("result"), "subscribe");
            auto sub = result["result"]["subscriptionId"];
            c.sendJson(req("sub2", "state.subscribe", {{"topics", {"state"}}}));
            check(c.receive()["error"]["code"] == -32004, "duplicate subscription");
            c.sendJson(req("snap", "state.snapshot", {{"subscriptionId", sub}}));
            check(c.receive()["result"]["snapshot"]["revision"] == 1, "snapshot baseline");
            c.sendJson(req("ping1", "session.ping"));
            c.sendJson(req("ping2", "session.ping"));
            check(c.receive()["id"] == "ping1", "coalesced first");
            check(c.receive()["id"] == "ping2", "coalesced second");
            c.sendJson(req("ping1", "session.ping"));
            check(c.receive()["error"]["code"] == -32600, "reused request ID");
            c.sendJson(req("unknown", "camera.set"));
            check(c.receive()["error"]["code"] == -32601, "unadvertised method");
        }
        {
            Client c(server.path());
            c.sendJson(hello(token));
            check(c.receive()["error"]["code"] == -32002, "token replay");
        }
        {
            Client c(server.path());
            c.sendJson(hello());
            check(c.receive()["result"]["capabilities"].empty(), "anonymous granted capability");
            c.sendJson(req("read", "state.subscribe", {{"topics", {"state"}}}));
            check(c.receive()["error"]["code"] == -32601, "anonymous state leak");
        }
        {
            Client c(server.path());
            c.sendBytes("{\"id\":\"a\",\"id\":\"b\"}");
            check(c.receive()["error"]["code"] == -32700, "duplicate JSON key");
        }
        {
            Client c(server.path());
            uint32_t size = htonl(1048577);
            send(c.fd, &size, 4, MSG_NOSIGNAL);
            char b;
            check(recv(c.fd, &b, 1, 0) == 0, "oversized frame not closed");
        }
        {
            Client c(server.path());
            c.sendJson(req("first", "session.ping"));
            check(c.receive()["error"]["code"] == -32600, "hello sequencing");
        }
        stop = true;
        loop.join();
        std::filesystem::remove_all(dir);
        std::cout << "PASS: framing, auth, token replay, snapshots, IDs, malformed input, limits\n";
        return 0;
    } catch (const std::exception &e) {
        stop = true;
        if (loop.joinable())
            loop.join();
        std::filesystem::remove_all(dir);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
