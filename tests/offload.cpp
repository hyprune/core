#include "interaction/Config.hpp"
#include "offload/Packet.hpp"
#include "offload/Policy.hpp"
#include "offload/Sandbox.hpp"
#include "offload/Supervisor.hpp"
#include "offload/Watchdog.hpp"
#include "render/Resolution.hpp"
#include <cassert>
#include <fcntl.h>
#include <sys/wait.h>
#include <thread>
using namespace hyprune;
int main(int argc, char **argv) {
    if (argc == 8 && std::string(argv[3]) == "/dev/null") {
        offload::Watchdog watchdog(std::stoi(argv[7]));
        for (;;)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    using namespace offload;
    Supply usb{true, true, true, false, 0}, battery{true, false, false, true, 0}, full{true, true, false, false, 240};
    assert(!policy(usb, "auto", 60, 80).admitted);
    auto p = policy(usb, "on", 60, 80);
    assert(p.admitted && p.cap == 50 && p.fps == 45 && p.profile == "usb-c");
    p = policy(battery, "on", 60, 80);
    assert(p.admitted && p.cap == 50 && p.fps == 45);
    assert(!policy(battery, "auto", 60, 80).admitted);
    p = policy(full, "on", 90, 80);
    assert(p.admitted && p.cap == 80 && p.fps == 60);
    assert(policy(full, "auto", 60, 80).admitted && !policy(full, "off", 60, 80).admitted);
    assert(!policy({}, "on", 60, 80).admitted);
    full.discharging = true;
    assert(!policy(full, "on", 60, 80).admitted);
    assert(safeSample(49, 70, 500, 50));
    for (auto v : {51., -1.})
        assert(!safeSample(v, 70, 500, 50));
    assert(!safeSample(40, 76, 500, 50) && !safeSample(40, 70, 1501, 50));
    auto defaults = interaction::defaultConfig();
    assert(defaults["graphics"]["offload"] == "off");
    for (auto patch :
         {Json{{"scale_min", .8}, {"scale_max", .5}}, Json{{"offload_power_cap_w", 200}}, Json{{"offload", "yes"}}}) {
        bool threw = false;
        try {
            interaction::configPatch(defaults, {{"graphics", patch}});
        } catch (...) {
            threw = true;
        }
        assert(threw);
    }
    // Exercise supervision and an AC -> USB policy withdrawal without waking a GPU.
    full.discharging = false;
    Json supervisorState;
    {
        Supervisor supervisor(
            std::filesystem::canonical(argv[0]),
            [&](Json j, int channel) {
                if (channel < 0)
                    supervisorState = j.at("state");
            },
            [&] { return full; }, [] { return Device{"/dev/null", "fake"}; },
            [](double &w, double &t) {
                w = 30;
                t = 40;
                return true;
            });
        auto config = defaults["graphics"];
        config["offload"] = "auto";
        supervisor.request({{"enabled", true},
                            {"graphics", config},
                            {"worldPath", "/unused"},
                            {"width", 16},
                            {"height", 16},
                            {"generation", 1}});
        supervisor.tick();
        assert(supervisorState["pid"].is_number_integer() && supervisorState["profile"] == "full-ac");
        full.usb = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(110));
        supervisor.tick();
        assert(supervisorState["reason"] == "auto requires qualified full AC");
        std::this_thread::sleep_for(std::chrono::milliseconds(110));
        supervisor.tick();
        assert(supervisorState["pid"].is_null());
    }
    render::Resolution resolution;
    resolution.minimum = .75f;
    resolution.maximum = .9f;
    resolution.reset(.5f, 60, true, 3840 * 2160);
    assert(resolution.scale == .5f);
    resolution.reset(0, 60, true, 3840 * 2160);
    assert(resolution.scale == .75f);
    auto directory = std::filesystem::temp_directory_path() / ("hyprune-sandbox-" + std::to_string(getpid()));
    std::filesystem::create_directory(directory);
    std::ofstream(directory / "world.json") << "{}";
    auto sandboxPid = fork();
    assert(sandboxPid >= 0);
    if (!sandboxPid) {
        sandbox(directory);
        int allowed = open((directory / "world.json").c_str(), O_RDONLY);
        assert(allowed >= 0);
        close(allowed);
        assert(open("/etc/passwd", O_RDONLY) < 0);
        assert(open((directory / "forbidden").c_str(), O_WRONLY | O_CREAT, 0600) < 0);
        int sock = socket(AF_UNIX, SOCK_STREAM, 0);
        assert(sock >= 0);
        assert(connect(sock, nullptr, 0) < 0 && errno == EPERM);
        close(sock);
        _exit(0);
    }
    int sandboxStatus = 0;
    waitpid(sandboxPid, &sandboxStatus, 0);
    assert(WIFEXITED(sandboxStatus) && WEXITSTATUS(sandboxStatus) == 0);
    std::filesystem::remove_all(directory);
    int pair[2];
    assert(!socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair));
    int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    assert(sendPacket(pair[0], {{"type", "fd"}}, {fd}));
    close(fd);
    {
        auto p = receivePacket(pair[1]);
        assert(p && p->fds.size() == 1 && (fcntl(p->fds[0], F_GETFD) & FD_CLOEXEC));
    }
    assert(!receivePacket(pair[1]));
    // The independent worker thread terminates even if the renderer thread stalls.
    auto pid = fork();
    assert(pid >= 0);
    if (!pid) {
        close(pair[0]);
        Watchdog watchdog(pair[1]);
        std::this_thread::sleep_for(std::chrono::seconds(3));
        _exit(99);
    }
    close(pair[1]);
    int status;
    waitpid(pid, &status, 0);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 72);
    close(pair[0]);
}
