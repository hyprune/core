// The IPC wire audit (ipc/WireAudit.hpp) under signals: the host process (the compositor)
// gets signals such as SIGCHLD that may land on the audit thread. An interrupted send or
// recv used to count as a rejected frame and abort the process; it must be retried.
#include "ipc/WireAudit.hpp"
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <pthread.h>
#include <unistd.h>
using namespace hyprune;
namespace {
volatile std::sig_atomic_t delivered = 0;
void count(int) { delivered = delivered + 1; }
} // namespace
int main() {
    setenv("HYPRUNE_VALIDATE_IPC", "1", 1);
    struct sigaction action {};
    action.sa_handler = count; // no SA_RESTART: blocking calls return EINTR
    sigemptyset(&action.sa_mask);
    sigaction(SIGUSR1, &action, nullptr);
    int frames = 0;
    {
        ipc::WireAudit audit; // its thread starts with SIGUSR1 unblocked
        sigset_t usr1;
        sigemptyset(&usr1);
        sigaddset(&usr1, SIGUSR1);
        pthread_sigmask(SIG_BLOCK, &usr1, nullptr); // so the process's SIGUSR1 goes to it
        auto frame = ipc::Json::parse(R"({"jsonrpc":"2.0","id":"a","error":{"code":-32602,"message":"invalid parameter"}})");
        for (; frames < 400; ++frames) {
            audit.check("0.1", frame, "session.ping");
            kill(getpid(), SIGUSR1);
            usleep(1000);
        }
    } // joins after every queued frame is checked; a failure aborts before this line
    if (delivered < 100) {
        std::cerr << "FAIL only " << delivered << " signals reached the audit thread\n";
        return 1;
    }
    std::cout << "PASS wire audit: " << frames << " frames checked with " << delivered
              << " signals on its thread (interrupted calls retried)\n";
}
