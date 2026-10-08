#include "ipc/Bridge.hpp"
#include <cassert>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>
using namespace hyprune;
int main(int, char **argv) {
    if (getenv("HYPRUNE_BRIDGE_FD")) {
        sigset_t mask;
        sigprocmask(SIG_SETMASK, nullptr, &mask);
        std::ofstream report(getenv("HYPRUNE_TEST_SIGNAL_REPORT"));
        report << sigismember(&mask, SIGTERM);
        report.close();
        raise(SIGTERM);
        return 1;
    }
    auto path = std::filesystem::temp_directory_path() / ("hyprune-signal-" + std::to_string(getpid()));
    setenv("HYPRUNE_TEST_SIGNAL_REPORT", path.c_str(), 1);
    sigset_t blocked, previous;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGTERM);
    assert(sigprocmask(SIG_BLOCK, &blocked, &previous) == 0);
    {
        ipc::Bridge bridge(std::filesystem::canonical(argv[0]),
                           [](auto &, auto &) -> std::optional<ipc::Json> { return {}; });
        assert(sigprocmask(SIG_SETMASK, &previous, nullptr) == 0);
        for (int i = 0; i < 100 && !std::filesystem::exists(path); i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        int termBlocked = -1;
        std::ifstream report(path);
        report >> termBlocked;
        assert(termBlocked == 0);
        for (int i = 0; i < 100 && bridge.healthy(); i++) {
            bridge.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        assert(!bridge.healthy());
    }
    std::filesystem::remove(path);
    unsetenv("HYPRUNE_TEST_SIGNAL_REPORT");
}
