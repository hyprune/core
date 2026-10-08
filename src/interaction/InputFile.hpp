#pragma once
#include "Input.hpp"
#include <functional>
namespace hyprune::interaction {
// Independent inotify watch on the parent directory survives atomic editor saves.
class InputFile {
  public:
    std::filesystem::path path;
    std::string error;
    InputFile(std::string name = "input", Json defaults = inputDefaults(),
              std::function<Json(const Json &)> validate = {});
    ~InputFile();
    Json current = inputDefaults();
    std::optional<Json> poll();
    void save(const Json &);

  private:
    std::function<Json(const Json &)> validate;
    int watch = -1;
    std::string bytes;
    std::optional<Json> read();
};
void atomicJSON(const std::filesystem::path &, const Json &, size_t budget = 262144);
Json readJSON(const std::filesystem::path &, size_t budget = 262144);
} // namespace hyprune::interaction
