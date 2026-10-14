#pragma once
#include "ipc/Protocol.hpp"
#include <algorithm>
#include <array>
#include <string_view>
namespace hyprune::world::format {
// World formats this core loads (RFC-0004). Gates compare numerically through
// the shared version helper; never with ad-hoc lists at call sites.
// 0.11 follows 0.9 (there is no world format 0.10; object formats have one).
inline constexpr std::array<std::string_view, 10> supported{"0.1", "0.2", "0.3", "0.4", "0.5", "0.6", "0.7", "0.8", "0.9", "0.11"};
inline bool known(std::string_view version) {
    return std::ranges::find(supported, version) != supported.end();
}
inline bool atLeast(std::string_view version, std::string_view minimum) {
    return known(version) && ipc::protocol::atLeast(version, minimum);
}
} // namespace hyprune::world::format
