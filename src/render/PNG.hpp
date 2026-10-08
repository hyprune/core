#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>
namespace hyprune::render {
struct PNG {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
};
PNG readAtlasPNG(const std::filesystem::path &);
void writePNG(const std::filesystem::path &, int width, int height, const std::vector<uint8_t> &rgba);
} // namespace hyprune::render
