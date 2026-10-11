#pragma once
#include <ctime>
#include <filesystem>
#include <optional>
#include <string>
namespace hyprune::quests {
// Private per-user files of the quest engine (hypruned only). Everything is
// 0600 in 0700 directories, written atomically; nothing leaves the machine.
std::filesystem::path stateHome();  // $XDG_STATE_HOME/hyprune or ~/.local/state/hyprune
std::filesystem::path configHome(); // $XDG_CONFIG_HOME/hyprune or ~/.config/hyprune
// Missing file: nullopt. A file that is not a regular file owned by the user,
// or larger than `limit`, throws.
std::optional<std::string> readPrivate(const std::filesystem::path &, size_t limit);
void writeAtomic(const std::filesystem::path &, const std::string &bytes);
void appendPrivate(const std::filesystem::path &, const std::string &line);
// Local wall time, "YYYY-MM-DDTHH:MM:SS".
std::string localTime(std::time_t);
// Parses "YYYY-MM-DDTHH:MM" or "YYYY-MM-DDTHH:MM:SS" as local time.
std::optional<std::time_t> parseLocalTime(const std::string &);
} // namespace hyprune::quests
