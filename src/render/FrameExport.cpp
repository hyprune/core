#include "FrameExport.hpp"
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
namespace hyprune::render {
void FrameExport::start(const std::string &target, int w, int h, size_t limitBytes, int n) {
    stop();
    join();
    every = std::max(n, 1);
    presented = 0;
    path = target;
    width = w;
    height = h;
    maxQueue = std::max<size_t>(limitBytes / (size_t(std::max(w, 1)) * std::max(h, 1) * 4), 4);
    error.clear();
    closing = false;
    grabbed = written = skipped = stalls = 0;
    running = true;
    writing = true;
    writer = std::thread([this] {
        write();
        writing = false;
    });
}
void FrameExport::write() {
    // A reader that goes away must not kill the compositor: EPIPE instead of SIGPIPE here.
    sigset_t pipe;
    sigemptyset(&pipe);
    sigaddset(&pipe, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &pipe, nullptr);
    // A FIFO opens once its reader has: retry without blocking so stop() never hangs.
    int fd = -1;
    for (;;) {
        fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC | O_NONBLOCK, 0644);
        if (fd >= 0 || errno != ENXIO)
            break;
        std::unique_lock g(lock);
        if (changed.wait_for(g, std::chrono::milliseconds(50), [&] { return closing; }))
            break;
    }
    if (fd >= 0)
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
    else {
        std::lock_guard g(lock);
        error = "open " + path + ": " + std::strerror(errno);
    }
    for (;;) {
        std::vector<uint8_t> frame;
        {
            std::unique_lock g(lock);
            changed.wait(g, [&] { return closing || !queue.empty(); });
            if (queue.empty())
                break;
            frame = std::move(queue.front());
            queue.pop_front();
        }
        changed.notify_all();
        if (fd >= 0) {
            size_t done = 0;
            while (done < frame.size()) {
                auto n = ::write(fd, frame.data() + done, frame.size() - done);
                if (n < 0 && errno == EINTR)
                    continue;
                if (n <= 0) {
                    std::lock_guard g(lock);
                    error = std::string("write: ") + std::strerror(errno);
                    ::close(fd);
                    fd = -1;
                    break;
                }
                done += size_t(n);
            }
            if (fd >= 0)
                ++written;
        }
        std::lock_guard g(lock);
        if (spare.size() < 8)
            spare.push_back(std::move(frame));
    }
    if (fd >= 0)
        ::close(fd);
}
void FrameExport::collect(bool wait) {
    while (inFlight) {
        auto &s = ring[oldest];
        auto state = glClientWaitSync(s.fence, wait ? GL_SYNC_FLUSH_COMMANDS_BIT : 0, wait ? 1000000000ull : 0);
        if (state != GL_ALREADY_SIGNALED && state != GL_CONDITION_SATISFIED)
            return;
        glDeleteSync(s.fence);
        s.fence = nullptr;
        size_t size = size_t(width) * height * 4;
        std::vector<uint8_t> frame;
        bool full = false;
        {
            std::unique_lock g(lock);
            if (queue.size() >= maxQueue) {
                ++stalls; // the encoder is behind: wait rather than drop a frame, but never hang
                full = !changed.wait_for(g, std::chrono::seconds(2), [&] { return queue.size() < maxQueue; });
                if (full)
                    error = "the reader stopped taking frames";
            }
            if (!spare.empty()) {
                frame = std::move(spare.back());
                spare.pop_back();
            }
        }
        frame.resize(size);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, s.pbo);
        if (auto *pixels = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, GLsizeiptr(size), GL_MAP_READ_BIT)) {
            std::memcpy(frame.data(), pixels, size);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
        if (full)
            ++skipped;
        else {
            std::lock_guard g(lock);
            queue.push_back(std::move(frame));
        }
        changed.notify_all();
        oldest = (oldest + 1) % ring.size();
        --inFlight;
        wait = false; // only the slot that had to be freed is waited for
    }
}
void FrameExport::grab(GLuint fbo, int w, int h) {
    if (!running || presented++ % uint64_t(every))
        return;
    if (w != width || h != height) {
        ++skipped;
        return;
    }
    size_t size = size_t(width) * height * 4;
    for (auto &s : ring)
        if (!s.pbo) {
            glGenBuffers(1, &s.pbo);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, s.pbo);
            glBufferData(GL_PIXEL_PACK_BUFFER, GLsizeiptr(size), nullptr, GL_STREAM_READ);
        }
    collect(false);
    if (inFlight == ring.size())
        collect(true);
    GLint alignment = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
    auto &s = ring[next];
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, s.pbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    // Hyprland's target convention: row zero is the logical top.
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glPixelStorei(GL_PACK_ALIGNMENT, alignment);
    s.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    next = (next + 1) % ring.size();
    ++inFlight;
    ++grabbed;
}
void FrameExport::release() {
    for (auto &s : ring) {
        if (s.fence)
            glDeleteSync(s.fence);
        if (s.pbo)
            glDeleteBuffers(1, &s.pbo);
        s = {};
    }
    next = oldest = inFlight = 0;
}
void FrameExport::stop() {
    if (!running)
        return;
    while (inFlight)
        collect(true);
    release();
    {
        std::lock_guard g(lock);
        closing = true;
    }
    changed.notify_all();
    running = false;
}
void FrameExport::join() {
    if (writer.joinable())
        writer.join();
    queue.clear();
    spare.clear();
}
FrameExport::Status FrameExport::status() {
    std::lock_guard g(lock);
    return {grabbed, written, skipped, stalls, queue.size(), width, height, path, error, running, writing};
}
} // namespace hyprune::render
