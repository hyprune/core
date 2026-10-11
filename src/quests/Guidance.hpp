#pragma once
#include <filesystem>
#include <json.hpp>
#include <string>
namespace hyprune::quests {
using Json = nlohmann::json;
// Guidance settings (IPC 0.21 guidance.get/set): markers, quest notifications and
// focus sessions. Flat dotted keys; the file holds overrides only.
class Guidance {
  public:
    static Json defaults();
    static Json ranges();  // every numeric key: [min, max]
    static Json options(); // every enum key: allowed values
    // Validates a patch against the current overrides; null removes an override.
    // Throws std::invalid_argument naming the key.
    static Json patched(const Json &overrides, const Json &patch);
    std::filesystem::path path;
    Json overrides = Json::object();
    std::string error;
    explicit Guidance(std::filesystem::path);
    void load();
    Json config() const;
    Json snapshot() const;
    void set(const Json &patch); // validates, saves, then applies
    template <class T> T get(const std::string &key) const { return config().at(key).get<T>(); }
};
} // namespace hyprune::quests
