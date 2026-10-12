#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <utility>

namespace hyprune::interaction {
// Input work deferred out of the compositor's input handlers, oldest first. Each operation
// has its own idle callback, but the world tick also runs every queued operation before it
// steps: a tick scheduled before the callback used to run first, so an input recorded for
// frame N took effect in frame N+1 under load and replays drifted a frame from the recording.
class InputQueue {
  public:
    uint64_t push(std::function<void()> operation) {
        queue_.emplace_back(++last_, std::move(operation));
        return last_;
    }
    // Runs the operations queued up to `upTo`, oldest first; one already run is skipped.
    // Operations queued while these run wait for their own turn.
    size_t run(uint64_t upTo) {
        size_t ran = 0;
        while (!queue_.empty() && queue_.front().first <= upTo) {
            auto operation = std::move(queue_.front().second);
            queue_.pop_front();
            operation();
            ++ran;
        }
        return ran;
    }
    uint64_t last() const { return last_; }
    bool empty() const { return queue_.empty(); }
    void clear() { queue_.clear(); }

  private:
    std::deque<std::pair<uint64_t, std::function<void()>>> queue_;
    uint64_t last_ = 0;
};
} // namespace hyprune::interaction
