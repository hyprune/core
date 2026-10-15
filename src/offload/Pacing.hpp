#pragma once
// Live pacing of the offload renderer: one fresh worker frame per compositor frame while
// anything shown changes, at the power policy's rate on average.
#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>
namespace hyprune::offload {
// Even pacing (graphics.frame_pacing "even"): a rate-capped worker gets a new pose every
// n-th compositor frame, n the smallest refresh divisor whose rate stays within the cap
// (the worker enforces it too: above it, it delays frames off the grid): 45 FPS on 98 Hz
// runs at 32.7 (every third refresh), 60 on 98 Hz at 49, 45 on 60 Hz at 30. An uneven 45 on
// 60 Hz shows frames for 1, 1, 1 and 2 refreshes while each carries the motion of its own
// send interval: judder at any rate.
inline int evenDivisor(double refreshHz, double fps) {
    int n = 1;
    while (n < 8 && refreshHz / n > fps * 1.02)
        ++n;
    return n;
}
// What the worker sustains: each frame's service time (from when it could start, its pose
// sent and the previous frame done, to its copy issued here), p90 over the recent frames.
// Even pacing needs a divisor the worker can keep: a 60 FPS cap on 98 Hz asks for a frame
// every second refresh (20.4 ms), but a worker taking 25 ms fills only some of those slots
// and frames land 2 or 3 refreshes apart. The divisor is the larger of the cap's and this
// one, which rises at once and falls once the frames fit a whole refresh sooner with 5 %
// to spare, at most every 2 s. Service time does not depend on the divisor (an idle worker
// starts each frame when its pose arrives), so the choice does not feed back on itself.
struct Throughput {
    std::deque<double> service;
    double lastDone = -1, changed = -1e9;
    int ticks = 1;
    void frame(double sentMs, double doneMs) {
        if (lastDone >= 0 && doneMs > lastDone)
            service.push_back(doneMs - std::max(sentMs, lastDone));
        lastDone = std::max(lastDone, doneMs);
        while (service.size() > 60)
            service.pop_front();
    }
    double p90Ms() const {
        if (service.size() < 8)
            return 0;
        std::vector<double> v(service.begin(), service.end());
        std::sort(v.begin(), v.end());
        return v[size_t(.9 * double(v.size() - 1))];
    }
    int divisor(double refreshMs) {
        auto p90 = p90Ms();
        if (refreshMs <= 0 || p90 <= 0)
            return ticks;
        int need = std::clamp(int(std::ceil((p90 + 1) / refreshMs)), 1, 8);
        if (need > ticks || (p90 + 1 < (ticks - 1) * refreshMs * .95 && lastDone - changed >= 2000)) {
            if (need != ticks)
                changed = lastDone;
            ticks = need;
        }
        return ticks;
    }
    void reset() { *this = {}; }
};
// Constant latency: each frame becomes the front a whole number of compositor frames after
// its pose was sent, enough for the worker's recent round trips (p90 plus a margin), so a
// frame that happens to finish early is not shown early. Hysteresis: shorter only once the
// trips fit a whole frame earlier with room to spare.
struct Latency {
    int ticks = 1;
    int update(double refreshMs, double readyP90Ms) {
        if (refreshMs <= 0)
            return ticks;
        int need = std::max(1, int(std::ceil((readyP90Ms + 2) / refreshMs)));
        if (need > ticks || readyP90Ms + 2 < (ticks - 1.25) * refreshMs)
            ticks = need;
        return ticks;
    }
};
// The plugin's per-frame budget: each compositor frame tick adds frames x fps / refresh
// (a 45 FPS policy on a 60 Hz output: 0.75 a frame, so 3 frames in 4 get a new pose,
// evenly); a pose spends one. Fractions carry over, but an idle stretch banks at most one
// frame beyond the current one.
struct FrameBudget {
    double credit = 1;
    void accrue(double share) { credit = std::min(credit, 1.) + std::min(share, 1.); }
    bool ready() const { return credit >= .999; }
    void spend() { credit = std::max(0., credit - 1); }
};
// The worker's cap is an average rate, not a spacing: a request is rendered at once while
// credit (accruing at fps, at most `burst` frames) lasts, so requests on the compositor's
// frame grid are not pushed off it, and the average never exceeds fps.
struct RateCap {
    double credit = 1, last = -1, burst = 2;
    bool ready(double nowMs, double fps) {
        if (last >= 0)
            credit = std::min(burst, credit + std::max(0., nowMs - last) * fps / 1000);
        last = nowMs;
        return credit >= .999;
    }
    void spend() { credit = std::max(0., credit - 1); }
};
} // namespace hyprune::offload
