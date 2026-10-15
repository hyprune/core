#pragma once
// Live pacing of the offload renderer: one fresh worker frame per compositor frame while
// anything shown changes, at the power policy's rate on average.
#include <algorithm>
#include <cmath>
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
