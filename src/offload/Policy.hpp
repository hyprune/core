#pragma once
#include <algorithm>
#include <cmath>
#include <string>
namespace hyprune::offload {
struct Supply {
    bool known = false, ac = false, usb = false, discharging = false;
    double watts = 0;
};
struct Policy {
    std::string profile = "unknown", reason = "off";
    bool admitted = false;
    int fps = 0;
    double cap = 0;
};
inline Policy policy(const Supply &s, const std::string &mode, int fps, double cap) {
    Policy p;
    p.profile = !s.known ? "unknown" : !s.ac ? "battery" : s.usb ? "usb-c" : s.watts >= 150 ? "full-ac" : "limited-ac";
    const bool full = p.profile == "full-ac" && !s.discharging;
    p.cap = std::min(cap, full ? 80. : 50.);
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
