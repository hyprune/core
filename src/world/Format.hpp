#pragma once
#include "ipc/Protocol.hpp"
#include <algorithm>
#include <array>
#include <string_view>
namespace hyprune::world::format {
// World formats this core loads (RFC-0004). Gates compare numerically through
// the shared version helper; never with ad-hoc lists at call sites.
inline constexpr std::array<std::string_view, 7> supported{"0.1", "0.2", "0.3", "0.4", "0.5", "0.6", "0.7"};
inline bool known(std::string_view version) {
    return std::ranges::find(supported, version) != supported.end();
}
inline bool atLeast(std::string_view version, std::string_view minimum) {
    return known(version) && ipc::protocol::atLeast(version, minimum);
}
} // namespace hyprune::world::format
