#pragma once
// Optional brand watermark for user captures (U / photo mode stills).
// CPU only: runs on the capture writer thread, never in the render pass and
// never on the live HUD or world rendering. See assets/brand/BRAND.md.
#include <array>
#include <cstdint>
#include <json.hpp>
#include <string>
#include <vector>
namespace hyprune::render {
struct WatermarkOptions {
    float widthFraction = .13f; // lockup (840-unit viewBox) width relative to the capture width
    int minWidth = 160;         // brand minimum for the small Bold lockup
    int smallBelow = 240;       // full Bold lockup from 240 px wide
    int maxWidth = 1024;        // masters are rasterised so this is still a downscale
    float opacity = .55f;       // group opacity, brand range 50-60%
    float maxWidthShare = .4f;  // mark + clear space may take at most this share of the width
    float maxHeightShare = .25f; // ... and of the height; otherwise the capture is too small
};
inline constexpr float ClearSpaceUnits = 32; // brand clear space beyond painted ink

struct WatermarkPlan {
    bool fits = false;
    std::string reason;   // why it doesn't fit
    bool small = false;   // small lockup (160-239 px) vs full lockup
    int lockupWidth = 0;  // drawn viewBox width in px
    float scale = 0;      // px per source unit
    float clear = 0;      // clear space in px
    float inkLeft = 0, inkTop = 0, inkRight = 0, inkBottom = 0; // painted ink, image px, top-left origin
    std::array<int, 4> rect{};   // x, y, w, h: pixels the mark may touch
    std::array<int, 4> region{}; // x, y, w, h: mark + clear space, sampled for the ink choice
};
// Rows are top-down (row 0 = top), as Renderer::snapshot and writePNG use.
WatermarkPlan planWatermark(int width, int height, const WatermarkOptions & = {});

enum class Ink { White, Navy };
inline constexpr std::array<uint8_t, 3> WhiteInk{0xff, 0xff, 0xff}, NavyInk{0x0f, 0x1a, 0x27};
struct InkChoice {
    Ink ink = Ink::White;
    double luminance = 0;       // mean relative luminance (linear sRGB) of the region
    double contrastWhite = 0;   // WCAG contrast of white composited at `opacity` vs the region mean
    double contrastNavy = 0;
};
InkChoice chooseInk(const std::vector<uint8_t> &rgba, int width, int height, std::array<int, 4> region,
                    float opacity);
// Contrast-based choice for a uniform region of mean relative luminance `y`.
InkChoice chooseInkForLuminance(double y, float opacity);

// Area-filtered coverage (0..1) of the planned mark, rect.w * rect.h, row-major top-down.
std::vector<float> watermarkCoverage(const WatermarkPlan &);
// Group-opacity "over": every pixel blends once with coverage * opacity, so
// overlapping strokes never darken. Alpha becomes a + m * (1 - a).
void compositeInk(std::vector<uint8_t> &rgba, int width, int height, std::array<int, 4> rect,
                  const std::vector<float> &coverage, std::array<uint8_t, 3> ink, float opacity);

struct WatermarkResult {
    bool applied = false;
    std::string reason;
    WatermarkPlan plan;
    InkChoice ink;
    float opacity = 0;
};
WatermarkResult applyWatermark(std::vector<uint8_t> &rgba, int width, int height, const WatermarkOptions & = {});
// Capture JSON record: {enabled, applied, ...}. Not part of any IPC contract.
nlohmann::json watermarkJson(bool enabled, const WatermarkResult &);
} // namespace hyprune::render
