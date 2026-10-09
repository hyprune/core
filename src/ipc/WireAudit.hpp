#pragma once
#include "Server.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
namespace hyprune::ipc {
// Test/debug only. Validation stays off the compositor and IPC event loop.
class WireAudit {
  public:
    WireAudit();
    ~WireAudit();
    void check(const std::string &protocol, const Json &frame, const std::string &method);

  private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::string> pending_;
    std::thread worker_;
    bool stopping_ = false;
    void run();
};
} // namespace hyprune::ipc
