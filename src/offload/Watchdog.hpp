#pragma once
#include "Packet.hpp"
#include <atomic>
#include <chrono>
#include <poll.h>
#include <thread>
namespace hyprune::offload {
// A stalled driver on the render thread cannot prevent this thread withdrawing
// the process. Hypruned renews a 500 ms lease only with fresh power telemetry.
class Watchdog {
    std::atomic<int> fps_{30};
    std::jthread thread_;

  public:
    explicit Watchdog(int fd)
        : thread_([this, fd](std::stop_token stop) {
              auto now = [] {
                  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch())
                      .count();
              };
              double until = now() + 500;
              while (!stop.stop_requested()) {
                  try {
                      pollfd p{fd, POLLIN, 0};
                      poll(&p, 1, 50);
                      for (int i = 0; i < 16; ++i) {
                          auto packet = receivePacket(fd);
                          if (!packet)
                              break;
                          if (!packet->fds.empty())
                              _exit(70);
                          auto deadline = packet->body.at("untilMs").get<double>();
                          int fps = packet->body.at("fps");
                          if (deadline > now() + 550 || fps < 1 || fps > 60)
                              _exit(70);
                          until = deadline;
                          fps_ = fps;
                      }
                  } catch (...) {
                      _exit(71);
                  }
                  if (now() > until)
                      _exit(72);
              }
              close(fd);
          }) {}
    int fps() const { return fps_; }
};
} // namespace hyprune::offload
