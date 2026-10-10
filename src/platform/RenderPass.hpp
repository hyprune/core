#pragma once
#include <cstdint>
namespace hyprune::platform {
// Compositor render-pass tracking that survives a pass without RENDER_POST.
// Hyprland 0.56.2 emits RENDER_PRE and returns early when beginRender fails
// (e.g. buffer allocation on hotplug/DPMS), so PRE/POST cannot be trusted to
// pair. A pass lives inside one synchronous renderMonitor call, so:
//  - a new RENDER_PRE while a pass is still open means the old one leaked;
//  - an event-loop entry point (timer, doLater, IPC fd) can never run inside a
//    real pass, so a pass still open there leaked too.
// Rule 2 is unchanged: while a pass is genuinely open, deferred work waits.
class RenderPass {
  public:
    // RENDER_PRE. Returns true when the previous pass never ended.
    bool begin(const void *monitor) {
        bool leaked = open_;
        if (leaked)
            ++leaked_;
        open_ = true;
        monitor_ = monitor;
        ++passes_;
        return leaked;
    }
    // RENDER_POST.
    void end() {
        open_ = false;
        monitor_ = nullptr;
    }
    // Event-loop entry point, outside any renderMonitor call stack. Returns true
    // when it closed a pass that had leaked.
    bool settle() {
        if (!open_)
            return false;
        ++leaked_;
        end();
        return true;
    }
    bool active() const { return open_; }
    const void *monitor() const { return monitor_; }
    uint64_t passes() const { return passes_; }
    uint64_t leaked() const { return leaked_; }

  private:
    bool open_ = false;
    const void *monitor_ = nullptr;
    uint64_t passes_ = 0, leaked_ = 0;
};
} // namespace hyprune::platform
