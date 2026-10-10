// Capture watermark: placement, size variants, ink choice, compositing, config.
// `watermark-tests --apply in.png out.png` previews the mark on any PNG (no GL).
#include "interaction/Config.hpp"
#include "render/BrandMasks.hpp"
#include "render/PNG.hpp"
#include "render/Watermark.hpp"
#include <cmath>
#include <iostream>
#include <png.h>
using namespace hyprune;
using namespace hyprune::render;
using Json = nlohmann::json;
namespace {
void check(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F fn, const std::string &why) {
    bool failed = false;
    try {
        fn();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, "accepted: " + why);
}
std::vector<uint8_t> solid(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    std::vector<uint8_t> v(size_t(w) * h * 4);
    for (size_t i = 0; i < v.size(); i += 4) v[i] = r, v[i + 1] = g, v[i + 2] = b, v[i + 3] = a;
    return v;
}
void fill(std::vector<uint8_t> &v, int w, int x0, int y0, int x1, int y1, uint8_t value) {
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            for (int c = 0; c < 3; c++) v[(size_t(y) * w + x) * 4 + c] = value;
}
double sum(const std::vector<float> &v) {
    double s = 0;
    for (auto c : v) s += c;
    return s;
}
// Ink area of a master in source units^2, straight from the embedded PNG.
double masterArea(bool small) {
    auto &src = small ? brand::LockupSmallMono : brand::LockupMono;
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    check(png_image_begin_read_from_memory(&image, src.png, src.size), "mask header");
    image.format = PNG_FORMAT_GRAY;
    std::vector<uint8_t> px(PNG_IMAGE_SIZE(image));
    check(png_image_finish_read(&image, nullptr, px.data(), 0, nullptr), "mask decode");
    double s = 0;
    for (auto p : px) s += p / 255.0;
    return s / (src.scale * src.scale);
}
int apply(const char *in, const char *out) {
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    check(png_image_begin_read_from_file(&image, in), "read PNG");
    image.format = PNG_FORMAT_RGBA;
    std::vector<uint8_t> rgba(PNG_IMAGE_SIZE(image));
    check(png_image_finish_read(&image, nullptr, rgba.data(), 0, nullptr), "decode PNG");
    auto r = applyWatermark(rgba, image.width, image.height);
    writePNG(std::filesystem::absolute(out), image.width, image.height, rgba);
    std::cout << watermarkJson(true, r).dump() << "\n";
    return 0;
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 4 && std::string(argv[1]) == "--apply")
            return apply(argv[2], argv[3]);

        // Config: core-local Lua key, default off, boolean only.
        auto config = interaction::defaultConfig();
        check(config["capture"]["watermark"] == false, "watermark defaults off");
        check(interaction::configPatch(config, {{"capture", {{"watermark", true}}}})["capture"]["watermark"] == true,
              "watermark enables");
        rejects([&] { interaction::configPatch(config, {{"capture", {{"watermark", "yes"}}}}); }, "string watermark");
        rejects([&] { interaction::configPatch(config, {{"capture", {{"watermark", 1}}}}); }, "numeric watermark");
        rejects([&] { interaction::configPatch(config, {{"capture", {{"logo", true}}}}); }, "unknown capture key");

        // Size: ~13% of the width, 160 px floor, small lockup below 240 px, 1024 px cap.
        struct Size { int w, h, lockup; bool small; };
        for (auto [w, h, lockup, small] : {Size{1920, 1080, 250, false}, Size{1280, 720, 166, true},
                                          Size{1600, 900, 208, true}, Size{1838, 1034, 239, true},
                                          Size{1846, 1038, 240, false}, Size{3840, 2160, 499, false},
                                          Size{800, 600, 160, true}, Size{12000, 6000, 1024, false}}) {
            auto p = planWatermark(w, h);
            auto tag = std::to_string(w) + "x" + std::to_string(h);
            check(p.fits, tag + " fits");
            check(p.lockupWidth == lockup, tag + " width " + std::to_string(p.lockupWidth));
            check(p.small == small, tag + " variant");
            // Bottom-right, clear space >= 32 source units (and < 1 px more) beyond the ink.
            check(p.clear == 32 * lockup / 840.f, tag + " clear space scale");
            check(w - p.inkRight >= p.clear && w - p.inkRight < p.clear + 1, tag + " right clear space");
            check(h - p.inkBottom >= p.clear && h - p.inkBottom < p.clear + 1, tag + " bottom clear space");
            check(p.inkLeft > w / 2.f && p.inkTop > h / 2.f, tag + " bottom-right quadrant");
            check(p.rect[0] >= 0 && p.rect[1] >= 0 && p.rect[0] + p.rect[2] <= w && p.rect[1] + p.rect[3] <= h,
                  tag + " rect inside");
            check(p.region[0] + p.region[2] == w && p.region[1] + p.region[3] == h, tag + " region reaches corner");
            check(std::abs(p.inkLeft - p.clear - p.region[0]) < 1, tag + " region includes left clear space");
            // Ink spans 764.5 x 185 of the 840-unit viewBox.
            check(std::abs((p.inkRight - p.inkLeft) - 764.5f * p.scale) < 1.5f, tag + " ink width");
            check(std::abs((p.inkBottom - p.inkTop) - 185 * p.scale) < 1.5f, tag + " ink height");
        }
        // Too small: skip entirely rather than shrink below 160 px or crowd the frame.
        for (auto [w, h] : {std::pair{320, 180}, {390, 300}, {400, 180}, {0, 0}, {120, 2000}}) {
            auto p = planWatermark(w, h);
            check(!p.fits && !p.reason.empty(), std::to_string(w) + "x" + std::to_string(h) + " skipped");
        }
        check(planWatermark(400, 200).fits, "400x200 just fits");
        {
            auto tiny = solid(320, 180, 10, 10, 10);
            auto copy = tiny;
            auto r = applyWatermark(tiny, 320, 180);
            check(!r.applied && tiny == copy, "skip leaves pixels untouched");
            check(watermarkJson(true, r)["applied"] == false && watermarkJson(true, r).contains("reason"),
                  "skip recorded");
        }

        // Ink: higher contrast of the composited ink against the region mean wins.
        auto ink = [](uint8_t r, uint8_t g, uint8_t b) {
            auto img = solid(1920, 1080, r, g, b);
            return chooseInk(img, 1920, 1080, planWatermark(1920, 1080).region, .55f);
        };
        check(ink(0, 0, 0).ink == Ink::White, "black gets white");
        check(ink(40, 45, 60).ink == Ink::White, "night blue gets white");
        check(ink(115, 115, 115).ink == Ink::White, "dark grey gets white");
        check(ink(140, 140, 140).ink == Ink::Navy, "light grey gets navy");
        check(ink(226, 214, 190).ink == Ink::Navy, "pale stone gets navy");
        check(ink(255, 255, 255).ink == Ink::Navy, "white gets navy");
        auto stone = ink(226, 214, 190);
        check(stone.contrastNavy > 2.5 && stone.contrastWhite < 1.5, "pale stone defeats white");
        // Crossover sits at mid grey (Y ~0.21) for any opacity in the brand range.
        for (float a : {.5f, .55f, .6f}) {
            check(chooseInkForLuminance(.19, a).ink == Ink::White, "Y .19 white");
            check(chooseInkForLuminance(.23, a).ink == Ink::Navy, "Y .23 navy");
        }
        {
            // Mixed: the choice follows the corner region, not the whole frame.
            auto p = planWatermark(1920, 1080);
            auto dark = solid(1920, 1080, 15, 15, 20);
            fill(dark, 1920, p.region[0], p.region[1], 1920, 1080, 230);
            check(chooseInk(dark, 1920, 1080, p.region, .55f).ink == Ink::Navy, "bright corner on dark frame");
            auto bright = solid(1920, 1080, 230, 230, 230);
            fill(bright, 1920, p.region[0], p.region[1], 1920, 1080, 15);
            check(chooseInk(bright, 1920, 1080, p.region, .55f).ink == Ink::White, "dark corner on bright frame");
            // Half black / half white: mean Y = .5, navy reads better.
            auto half = solid(1920, 1080, 0, 0, 0);
            fill(half, 1920, p.region[0], p.region[1], p.region[0] + p.region[2] / 2, 1080, 255);
            auto c = chooseInk(half, 1920, 1080, p.region, .55f);
            check(std::abs(c.luminance - .5) < .01 && c.ink == Ink::Navy, "split region");
        }

        // Compositing math: d = ink * m + d * (1 - m), m = coverage * opacity; alpha a + m(1 - a).
        {
            auto img = solid(3, 1, 100, 100, 100, 255);
            compositeInk(img, 3, 1, {0, 0, 3, 1}, {0.f, .5f, 1.f}, WhiteInk, .55f);
            check(img[0] == 100 && img[3] == 255, "zero coverage untouched");
            check(img[4] == std::lround(255 * .275 + 100 * .725), "half coverage");
            check(img[8] == std::lround(255 * .55 + 100 * .45) && img[11] == 255, "full coverage");
            auto clear = solid(1, 1, 0, 0, 0, 0);
            compositeInk(clear, 1, 1, {0, 0, 1, 1}, {1.f}, NavyInk, .55f);
            check(clear[0] == std::lround(15 * .55) && clear[1] == std::lround(26 * .55) &&
                      clear[2] == std::lround(39 * .55) && clear[3] == std::lround(255 * .55),
                  "transparent destination");
            rejects([&] { compositeInk(img, 3, 1, {0, 0, 2, 1}, {1.f}, WhiteInk, .5f); }, "coverage size mismatch");
        }

        // Whole capture: group opacity, crisp interior, correct corner, area preserved across sizes.
        for (auto [w, h] : {std::pair{1920, 1080}, {1280, 720}, {3840, 2160}}) {
            auto tag = std::to_string(w) + "x" + std::to_string(h);
            auto img = solid(w, h, 0, 0, 0);
            auto r = applyWatermark(img, w, h);
            check(r.applied && r.ink.ink == Ink::White && r.opacity == .55f, tag + " applied white");
            int peak = 0, full = 0;
            bool outside = false;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    int v = img[(size_t(y) * w + x) * 4];
                    bool in = x >= r.plan.rect[0] && x < r.plan.rect[0] + r.plan.rect[2] && y >= r.plan.rect[1] &&
                              y < r.plan.rect[1] + r.plan.rect[3];
                    if (v && !in) outside = true;
                    peak = std::max(peak, v);
                    full += v == 140;
                }
            check(!outside, tag + " nothing outside the mark rect");
            check(peak == 140, tag + " group opacity caps at 55% (" + std::to_string(peak) + ")");
            check(full > r.plan.lockupWidth, tag + " solid stroke interiors (crisp)");
            check(img[0] == 0 && img[3] == 255, tag + " top-left untouched");
            auto cover = watermarkCoverage(r.plan);
            double area = sum(cover) / (r.plan.scale * r.plan.scale);
            double expected = masterArea(r.plan.small);
            check(std::abs(area / expected - 1) < .01, tag + " area-preserving filter " + std::to_string(area / expected));
            auto j = watermarkJson(true, r);
            check(j["ink"] == "#ffffff" && j["rect"].size() == 4 && j["opacity"] == .55f, tag + " JSON record");
        }
        {
            auto pale = solid(1920, 1080, 226, 214, 190);
            auto r = applyWatermark(pale, 1920, 1080);
            check(r.applied && r.ink.ink == Ink::Navy, "pale stone capture uses navy");
            check(watermarkJson(true, r)["ink"] == "#0f1a27", "navy recorded");
            int darkest = 255;
            for (size_t i = 0; i < pale.size(); i += 4) darkest = std::min<int>(darkest, pale[i]);
            check(darkest == std::lround(15 * .55 + 226 * .45), "navy at 55%");
        }
        check(watermarkJson(false, {}) == Json{{"enabled", false}}, "disabled record");
        // Wall placeholder: brand navy everywhere except a centred lockup in lettering mint.
        {
            int w = 1280, h = 720;
            auto px = brandPlaceholder(w, h);
            check(px.size() == size_t(w) * h * 4, "placeholder size");
            auto at = [&](int x, int y) { return px.data() + (size_t(y) * w + x) * 4; };
            for (auto [x, y] : {std::pair{0, 0}, {w - 1, 0}, {0, h - 1}, {w - 1, h - 1}, {w / 2, 8}})
                check(at(x, y)[0] == 0x0f && at(x, y)[1] == 0x1a && at(x, y)[2] == 0x27 && at(x, y)[3] == 255,
                      "placeholder background is brand navy");
            long sx = 0, sy = 0, n = 0;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                    if (at(x, y)[1] > 0x80) sx += x, sy += y, ++n;
            check(n > 5000, "placeholder draws the lockup");
            check(std::abs(sx / n - w / 2) < w / 20 && std::abs(sy / n - h / 2) < h / 10, "placeholder lockup centred");
            check(brandPlaceholder(64, 36).size() == size_t(64) * 36 * 4, "tiny placeholder stays navy-only");
        }
        std::cout << "watermark tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
