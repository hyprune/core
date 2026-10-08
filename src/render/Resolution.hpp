#pragma once
#include <algorithm>
#include <cmath>
namespace hyprune::render {
// GPU budget leaves headroom for the compositor. Slow reductions and slower
// increases prevent oscillation; invalid/disjoint timer samples never steer it.
struct Resolution {
    float scale = 1, requested = 0; // zero = auto
    double average = 0, elapsed = 0;
    int targetFPS = 60;
    void reset(float request, int fps, bool intel, int pixels) {
        requested = request;
        targetFPS = fps;
        scale = request > 0 ? request : intel && pixels > 2560 * 1440 ? .5f : 1.f;
        average = elapsed = 0;
    }
    bool sample(double gpuMs, double dt) {
        if (requested > 0 || gpuMs <= 0 || !std::isfinite(gpuMs))
            return false;
        average = average > 0 ? average * .9 + gpuMs * .1 : gpuMs;
        elapsed += std::clamp(dt, 0., .1);
        double budget = 600. / targetFPS;
        float next = scale;
        if (average > budget * 1.08 && elapsed >= .6)
            next = std::max(.375f, scale - .0625f);
        else if (average < budget * .60 && elapsed >= 6.)
            next = std::min(1.f, scale + .0625f);
        if (next == scale)
            return false;
        scale = next;
        elapsed = average = 0;
        return true;
    }
};
} // namespace hyprune::render
