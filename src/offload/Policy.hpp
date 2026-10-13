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
inline bool safeSample(double watts, double temperature, double ageMs, double cap) {
    return std::isfinite(watts) && watts >= 0 && std::isfinite(temperature) && temperature >= 0 && watts <= cap &&
           temperature <= 75 && ageMs >= 0 && ageMs <= 1500;
}
} // namespace hyprune::offload
