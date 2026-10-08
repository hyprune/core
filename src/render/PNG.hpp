#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>
namespace hyprune::render {
void writePNG(const std::filesystem::path &, int width, int height, const std::vector<uint8_t> &rgba);
}
