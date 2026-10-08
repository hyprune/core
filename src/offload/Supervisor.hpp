#pragma once
#include "Packet.hpp"
#include "Policy.hpp"
#include <chrono>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
namespace hyprune::offload {
inline double milliseconds() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline std::string readText(const std::filesystem::path &p) {
    std::ifstream f(p);
    std::string s;
    std::getline(f, s);
    return s;
}
inline double readNumber(const std::filesystem::path &p) {
    try {
        return std::stod(readText(p));
    } catch (...) {
        return 0;
    }
}
inline Supply supply() {
    Supply s;
    std::error_code error;
    for (auto &entry : std::filesystem::directory_iterator("/sys/class/power_supply", error)) {
        auto p = entry.path();
        auto type = readText(p / "type");
        if (type == "Battery") {
            s.known = true;
            s.discharging |= readText(p / "status") == "Discharging";
        }
        if (readText(p / "online") != "1")
            continue;
        if (type == "Mains" || type.starts_with("USB")) {
            s.known = s.ac = true;
            s.usb |= type.starts_with("USB");
            s.watts = std::max(s.watts, std::max(readNumber(p / "power_max") / 1e6,
                                                 readNumber(p / "voltage_max") * readNumber(p / "current_max") / 1e12));
        }
    }
    return s;
}
struct Device {
    std::string node, pci;
};
inline Device discreteDevice() {
    std::error_code error;
    for (auto &entry : std::filesystem::directory_iterator("/sys/class/drm", error))
        if (entry.path().filename().string().starts_with("renderD") &&
            readText(entry.path() / "device/vendor") == "0x10de")
            return {"/dev/dri/" + entry.path().filename().string(),
                    std::filesystem::canonical(entry.path() / "device").filename().string()};
    return {};
}
class Meter {
    void *library_ = nullptr, *device_ = nullptr;
    using Sample = int (*)(void *, unsigned *);
    Sample power_ = nullptr;
    using Temperature = int (*)(void *, unsigned, unsigned *);
    Temperature temperature_ = nullptr;
    bool initialized_ = false;

  public:
    ~Meter() { reset(); }
    void reset() {
        if (initialized_ && library_)
            if (auto shutdown = reinterpret_cast<int (*)()>(dlsym(library_, "nvmlShutdown"))) shutdown();
        if (library_) dlclose(library_);
        library_ = device_ = nullptr; initialized_ = false;
    }
    bool init(const std::string &pci) {
        if (device_)
            return true;
        if (!library_)
            library_ = dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!library_)
            return false;
        auto init = reinterpret_cast<int (*)()>(dlsym(library_, "nvmlInit_v2"));
        auto get =
            reinterpret_cast<int (*)(const char *, void **)>(dlsym(library_, "nvmlDeviceGetHandleByPciBusId_v2"));
        power_ = reinterpret_cast<Sample>(dlsym(library_, "nvmlDeviceGetPowerUsage"));
        temperature_ = reinterpret_cast<Temperature>(dlsym(library_, "nvmlDeviceGetTemperature"));
        if (!init || !get || !power_ || !temperature_) return false;
        if (!initialized_) initialized_ = init() == 0;
        return initialized_ && !get(pci.c_str(), &device_);
    }
    bool sample(double &watts, double &celsius) {
        unsigned mw = 0, t = 0;
        if (!device_ || power_(device_, &mw) || temperature_(device_, 0, &t))
            return false;
        watts = mw / 1000.;
        celsius = t;
        return true;
    }
};
// Only hypruned owns process lifetime and renews the worker's short power lease.
// The worker also has an independent watchdog thread, including during GPU stalls.
class Supervisor {
    std::string executable_;
    std::function<void(Json, int)> send_;
    Json request_, lastState_;
    pid_t child_ = -1;
    int control_ = -1;
    bool failed_ = false;
    double next_ = 0, nextMeter_ = 0, measured_ = 0, watts_ = -1, temperature_ = -1, killAt_ = 0;
    Meter meter_;
    std::function<Supply()> supply_;
    std::function<Device()> discover_;
    std::function<bool(double &, double &)> sample_;
    Device device_;
    Policy policy_;
    std::string reason_ = "off";
    void stop() {
        if (control_ >= 0) {
            close(control_);
            control_ = -1;
        }
        if (child_ > 0) {
            kill(child_, SIGTERM);
            if (!killAt_) killAt_ = milliseconds() + 500;
        }
    }
    void publish() {
        Json state = {{"profile", policy_.profile},
                      {"powerW", watts_ >= 0 ? Json(watts_) : Json(nullptr)},
                      {"capW", policy_.cap},
                      {"targetFPS", policy_.fps},
                      {"reason", reason_},
                      {"pid", child_ > 0 ? Json(child_) : Json(nullptr)},
                      {"generation", request_.value("generation", uint64_t(0))}};
        if (state != lastState_) {
            send_({{"type", "renderer.state"}, {"state", state}}, -1);
            lastState_ = state;
        }
    }

  public:
    Supervisor(std::string executable, std::function<void(Json, int)> send, std::function<Supply()> power = supply,
               std::function<Device()> discover = discreteDevice, std::function<bool(double &, double &)> sample = {})
        : executable_(std::move(executable)), send_(std::move(send)), supply_(std::move(power)),
          discover_(std::move(discover)), sample_(std::move(sample)) {}
    ~Supervisor() {
        stop();
        if (child_ > 0) {
            kill(child_, SIGKILL);
            waitpid(child_, nullptr, 0);
        }
    }
    void request(Json j) {
        if (j == request_)
            return;
        stop();
        request_ = std::move(j);
        failed_ = false;
        next_ = 0;
    }
    void tick() {
        try {
            tickImpl();
        } catch (const std::exception &e) {
            reason_ = e.what();
            failed_ = true;
            stop();
            publish();
        }
    }
    void tickImpl() {
        auto now = milliseconds();
        if (child_ > 0) {
            int status = 0;
            auto done = waitpid(child_, &status, WNOHANG);
            if (done == child_) {
                child_ = -1;
                if (control_ >= 0) {
                    close(control_);
                    control_ = -1;
                    failed_ = true;
                    reason_ = "renderer exited";
                }
            } else if (killAt_ && now >= killAt_) {
                kill(child_, SIGKILL);
                killAt_ = 0;
            }
        }
        if (now < next_ || request_.is_null())
            return;
        next_ = now + 100;
        auto g = request_.at("graphics");
        auto previous = policy_;
        policy_ = policy(supply_(), g.at("offload"), g.at("target_fps"), g.at("offload_power_cap_w"));
        if (!request_.value("enabled", false))
            policy_.admitted = false;
        if (!policy_.admitted) {
            reason_ = request_.value("enabled", false) ? policy_.reason : "inactive";
            if (child_ <= 0) {meter_.reset();watts_ = -1;measured_ = nextMeter_ = 0;}
            stop();
            publish();
            return;
        }
        if (previous.profile != policy_.profile && child_ > 0) {
            reason_ = "power profile changed";
            stop();
            publish();
            return;
        }
        if (failed_) {
            if (child_ <= 0) {meter_.reset();watts_ = -1;measured_ = nextMeter_ = 0;}
            publish();
            return;
        }
        if (device_.node.empty())
            device_ = discover_();
        if (device_.node.empty() || (!sample_ && !meter_.init(device_.pci))) {
            reason_ = "GPU/telemetry unavailable";
            failed_ = true;
            stop();
            publish();
            return;
        }
        if (now >= nextMeter_) {
            nextMeter_ = now + 500;
            if (sample_ ? sample_(watts_, temperature_) : meter_.sample(watts_, temperature_))
                measured_ = now;
        }
        if (!safeSample(watts_, temperature_, now - measured_, policy_.cap)) {
            reason_ = "power/temperature watchdog";
            failed_ = true;
            stop();
            publish();
            return;
        }
        if (child_ <= 0) {
            int data[2], ctl[2];
            if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, data))
                throw std::runtime_error("renderer socketpair");
            if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, ctl)) {
                close(data[0]);
                close(data[1]);
                throw std::runtime_error("renderer control socketpair");
            }
            std::vector<std::string> args = {executable_,
                                             "198",
                                             request_.at("worldPath"),
                                             device_.node,
                                             std::to_string(request_.at("width").get<int>()),
                                             std::to_string(request_.at("height").get<int>()),
                                             std::to_string(policy_.fps),
                                             "199"};
            std::vector<char *> av;
            for (auto &v : args)
                av.push_back(v.data());
            av.push_back(nullptr);
            posix_spawn_file_actions_t actions;
            posix_spawn_file_actions_init(&actions);
            posix_spawn_file_actions_adddup2(&actions, data[1], 198);
            posix_spawn_file_actions_adddup2(&actions, ctl[1], 199);
            for (auto &entry : std::filesystem::directory_iterator("/proc/self/fd")) {
                int openfd = std::stoi(entry.path().filename().string());
                if (openfd >= 3 && openfd != 198 && openfd != 199)
                    posix_spawn_file_actions_addclose(&actions, openfd);
            }
            // Only the two private channels survive; no Wayland/session/launch grants.

            posix_spawnattr_t attr;
            posix_spawnattr_init(&attr);
            sigset_t empty;
            sigemptyset(&empty);
            posix_spawnattr_setsigmask(&attr, &empty);
            posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK);
            char path[] = "PATH=/usr/bin", cache[] = "__GL_SHADER_DISK_CACHE=0";
            char *env[] = {path, cache, nullptr};
            int rc = posix_spawn(&child_, executable_.c_str(), &actions, &attr, av.data(), env);
            posix_spawnattr_destroy(&attr);
            posix_spawn_file_actions_destroy(&actions);
            close(data[1]);
            close(ctl[1]);
            if (rc) {
                close(data[0]);
                close(ctl[0]);
                child_ = -1;
                failed_ = true;
                reason_ = "renderer spawn failed";
                publish();
                return;
            }
            control_ = ctl[0];
            killAt_ = 0;
            reason_ = "warming";
            // Before startup uploads begin, grant an initial, bounded power lease.
            sendPacket(control_, {{"untilMs", now + 500}, {"fps", policy_.fps}});
            try {
                send_({{"type", "renderer.channel"}, {"generation", request_.at("generation")}}, data[0]);
            } catch (...) {
                close(data[0]);
                stop();
                throw;
            }
            close(data[0]);
        }
        if (control_ >= 0 && !sendPacket(control_, {{"untilMs", now + 500}, {"fps", policy_.fps}})) {
            reason_ = "renderer lease backpressure";
            failed_ = true;
            stop();
        }
        publish();
    }
};
} // namespace hyprune::offload
