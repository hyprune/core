#pragma once
// Frame export for demo recordings (nested development tooling, not a wire contract):
// every frame the compositor presents on the world's output is read back (a ring of
// pixel-pack buffers, mapped a few frames later so the GPU never stalls the frame) and a
// writer thread streams it, raw RGBX top row first, into a file or FIFO (an encoder
// reading at the output's refresh rate). Screen-capture clients request each frame after
// the previous one arrives and miss some; this exports exactly one image per presented
// frame, in order, never dropping one (a full queue makes the compositor wait instead).
#include <GLES3/gl3.h>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace hyprune::render {
class FrameExport {
  public:
    ~FrameExport() {
        stop();
        join();
    }
    // Opens path for writing on the writer thread (a FIFO blocks there until its reader
    // opens). Frames of another size than width x height are counted and skipped. The
    // queue holds at most maxQueueBytes of frames (at least four).
    // every: export every n-th presented frame (a lower frame rate; the others are presented only).
    void start(const std::string &path, int width, int height, size_t maxQueueBytes = size_t(1) << 31, int every = 1);
    // Stops grabbing (GL context current): the frames in flight are queued, and the writer
    // thread writes out the queue and closes in the background (status().draining).
    void stop();
    bool active() const { return running; }
    // Inside the compositor render pass, GL context current, state saved by the caller.
    void grab(GLuint fbo, int width, int height);
    struct Status {
        uint64_t grabbed = 0, written = 0, skipped = 0, stalls = 0;
        size_t queued = 0;
        int width = 0, height = 0;
        std::string path, error;
        bool active = false, draining = false;
    };
    Status status();

  private:
    struct Slot {
        GLuint pbo = 0;
        GLsync fence = nullptr;
    };
    std::array<Slot, 4> ring{};
    size_t next = 0, oldest = 0, inFlight = 0;
    int width = 0, height = 0;
    size_t maxQueue = 240;
    int every = 1;
    uint64_t presented = 0;
    bool running = false;
    std::atomic<bool> writing{false};
    void join();
    void collect(bool wait); // maps finished slots, oldest first
    void release();
    std::thread writer;
    std::mutex lock;
    std::condition_variable changed;
    std::deque<std::vector<uint8_t>> queue;
    std::vector<std::vector<uint8_t>> spare;
    bool closing = false;
    std::string path, error;
    std::atomic<uint64_t> grabbed{0}, written{0}, skipped{0}, stalls{0};
    void write();
};
} // namespace hyprune::render
