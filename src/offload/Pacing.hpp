#pragma once
// Live pacing of the offload renderer: one fresh worker frame per compositor frame while
// anything shown changes, at the power policy's rate on average.
#include <algorithm>
namespace hyprune::offload {
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
