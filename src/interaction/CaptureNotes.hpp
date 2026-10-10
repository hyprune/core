#pragma once
// Debug capture notes (IPC 0.17 capture.annotate) and the serial writer that keeps
// capture files and their notes in order on disk.
#include <json.hpp>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <optional>
#include <string>

namespace hyprune::interaction {
inline constexpr size_t CaptureNoteLimit = 1000; // Unicode code points

// Capture IDs are the capture file stem: "capture-" + millisecond timestamp.
bool validCaptureId(const std::string &id);
// A note as written: valid UTF-8, no control characters, at most CaptureNoteLimit
// code points; empty is allowed. Throws std::invalid_argument otherwise.
std::string captureNote(const nlohmann::json &value);
// Writes note + noteTimeMs into <dir>/<id>.json (latest note wins) and appends one
// line to <dir>/index.jsonl; a non-empty note is also appended to <dir>/notes.md.
void annotateCapture(const std::filesystem::path &dir, const std::string &id, const std::string &note,
                     int64_t noteTimeMs);

// One background job at a time, in submission order (capture writes, then their notes).
class SerialJobs {
  public:
    using Job = std::function<std::string()>;
    void push(Job job) { pending_.push_back(std::move(job)); }
    size_t size() const { return pending_.size() + (running_.valid() ? 1 : 0); }
    // Starts the next job when idle; returns a finished job's result or error.
    struct Done {
        std::string result, error;
    };
    std::optional<Done> poll();
    // Runs everything to completion (tests, unload).
    void drain();

  private:
    std::deque<Job> pending_;
    std::future<std::string> running_;
};
} // namespace hyprune::interaction
