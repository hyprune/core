#pragma once
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
namespace hyprune::offload {
struct Supply {
    bool known = false, ac = false, usb = false, discharging = false;
    double watts = 0;
    // No power_supply of type Battery: mains power (desktops), never "unknown".
    bool mains = false;
};
struct Policy {
    std::string profile = "unknown", reason = "off", capSource = "profile";
    bool admitted = false;
    int fps = 0;
    double cap = 0;
};
// Default cap on mains power, as a fraction of the GPU's own enforced power limit.
inline constexpr double MainsCapFraction = .75;
// configured: graphics.offload_power_cap_w (nullopt = automatic). limitW: the
// offload GPU's enforced power limit, <= 0 if unknown.
inline Policy policy(const Supply &s, const std::string &mode, int fps, std::optional<double> configured,
                     double limitW = -1) {
    Policy p;
    p.profile = !s.known       ? "unknown"
                : !s.ac        ? "battery"
                : s.usb        ? "usb-c"
                : s.mains || s.watts >= 150 ? "full-ac"
                                            : "limited-ac";
    const bool full = p.profile == "full-ac" && !s.discharging;
    if (s.mains) {
        if (configured) {
            // 20 W floor, but never above the device's own limit (a GPU may enforce < 20 W:
            // clamp's range must stay ordered).
            p.cap = limitW > 0 ? std::clamp(*configured, std::min(20., limitW), limitW) : *configured;
            p.capSource = "configured";
        } else if (limitW > 0) {
            p.cap = std::round(limitW * MainsCapFraction);
            p.capSource = "device";
        } else {
            p.cap = 80;
            p.capSource = "fallback";
        }
    } else {
        // Battery present: the laptop ceilings; a configured cap can only lower them.
        const double ceiling = full ? 80. : 50.;
        p.cap = std::min(configured.value_or(ceiling), ceiling);
        p.capSource = configured ? "configured" : "profile";
    }
    p.fps = std::min(fps, full ? 60 : 45);
    if (mode == "off")
        return p;
    if (!s.known) {
        p.reason = "power supply unknown";
        return p;
    }
    if (s.ac && s.discharging) {
        p.reason = "battery discharging on AC";
        return p;
    }
    if (mode == "auto" && !full) {
        p.reason = "auto requires qualified full AC";
        return p;
    }
    p.admitted = true;
    p.reason = "admitted";
    return p;
}
// Independent software watchdog; no privileged hardware power-limit changes.
// Missing telemetry never means zero watts. A breach immediately withdraws the lease.
// Power over the cap throttles before it ends offload (the laptop may run on USB-C PD, so
// the limit stays hard): every sample over the cap (they come every 500 ms) lowers the
// worker's rate a step (25 % fewer frames, down to 30 FPS, target_fps's own floor; even
// pacing follows with a larger divisor), and only power still over the cap 2 s after the first step ends offload. Under
// 90 % of the cap for 3 s, one step back.
struct PowerThrottle {
    enum Result { Ok, Throttled, Exceeded };
    int level = 0;
    double overSince = -1, lastStep = -1e9, underSince = -1;
    static constexpr int MaxLevel = 4;
    Result sample(double watts, double cap, double nowMs) {
        if (watts > cap) {
            underSince = -1;
            if (overSince < 0)
                overSince = nowMs;
            if (level < MaxLevel && nowMs - lastStep >= 450)
                ++level, lastStep = nowMs;
            return nowMs - overSince >= 2000 ? Exceeded : Throttled;
        }
        overSince = -1;
        if (watts < cap * .9) {
            if (underSince < 0)
                underSince = nowMs;
            if (level > 0 && nowMs - underSince >= 3000)
                --level, underSince = nowMs;
        } else
            underSince = -1;
        return level ? Throttled : Ok;
    }
    int fps(int base) const {
        return level ? std::max(std::min(base, 30), int(std::lround(base * std::pow(.75, level)))) : base;
    }
};
// Temperature, a finite reading and a fresh sample stay hard limits.
inline bool safeReading(double watts, double temperature, double ageMs) {
    return std::isfinite(watts) && watts >= 0 && std::isfinite(temperature) && temperature >= 0 && temperature <= 75 &&
           ageMs >= 0 && ageMs <= 1500;
}
inline bool safeSample(double watts, double temperature, double ageMs, double cap) {
    return std::isfinite(watts) && watts >= 0 && std::isfinite(temperature) && temperature >= 0 && watts <= cap &&
           temperature <= 75 && ageMs >= 0 && ageMs <= 1500;
}
} // namespace hyprune::offload
