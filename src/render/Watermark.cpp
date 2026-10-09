#include "Watermark.hpp"
#include "BrandMasks.hpp"
#include <algorithm>
#include <cmath>
#include <png.h>
#include <stdexcept>
namespace hyprune::render {
namespace {
struct Mask {
    int width = 0, height = 0;
    std::vector<uint8_t> coverage;
    float scale = 1, inkX = 0, inkY = 0;
};
Mask decode(const brand::MaskSource &source) {
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, source.png, source.size))
        throw std::runtime_error("watermark mask header");
    image.format = PNG_FORMAT_GRAY;
    Mask mask{int(image.width), int(image.height), std::vector<uint8_t>(PNG_IMAGE_SIZE(image)), source.scale,
              source.inkX, source.inkY};
    if (!png_image_finish_read(&image, nullptr, mask.coverage.data(), 0, nullptr)) {
        png_image_free(&image);
        throw std::runtime_error("watermark mask decode");
    }
    return mask;
}
// Decoded once, on first use (the capture writer thread); static init is thread-safe.
const Mask &mask(bool small) {
    static const Mask full = decode(brand::LockupMono), compact = decode(brand::LockupSmallMono);
    return small ? compact : full;
}
double linear(double c) { return c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4); }
double encode(double y) { return y <= .0031308 ? y * 12.92 : 1.055 * std::pow(y, 1 / 2.4) - .055; }
double luminance(double r, double g, double b) { return .2126 * linear(r) + .7152 * linear(g) + .0722 * linear(b); }
double contrast(double a, double b) { return (std::max(a, b) + .05) / (std::min(a, b) + .05); }
// Exact box-filter weights: output pixel i covers [(i - origin) * k, (i + 1 - origin) * k) in
// master pixels; master pixels outside [0, n) are empty.
struct Tap { int first = 0; std::vector<float> weights; };
std::vector<Tap> taps(int outputs, float origin, float k, int n) {
    std::vector<Tap> result(outputs);
    for (int i = 0; i < outputs; i++) {
        double a = std::max(0.0, (i - double(origin)) * k), b = std::min(double(n), (i + 1 - double(origin)) * k);
        if (b <= a) continue;
        int first = int(std::floor(a)), last = std::min(n - 1, int(std::ceil(b)) - 1);
        result[i].first = first;
        for (int j = first; j <= last; j++)
            result[i].weights.push_back(float((std::min(b, j + 1.0) - std::max(a, double(j))) / k));
    }
    return result;
}
} // namespace

WatermarkPlan planWatermark(int width, int height, const WatermarkOptions &o) {
    WatermarkPlan p;
    if (width <= 0 || height <= 0) {
        p.reason = "empty capture";
        return p;
    }
    p.lockupWidth = std::clamp(int(std::lround(o.widthFraction * width)), o.minWidth, o.maxWidth);
    p.small = p.lockupWidth < o.smallBelow;
    auto &m = mask(p.small);
    p.scale = p.lockupWidth / brand::ViewBoxWidth;
    p.clear = ClearSpaceUnits * p.scale;
    float inkW = m.width / m.scale * p.scale, inkH = m.height / m.scale * p.scale;
    if (inkW + 2 * p.clear > o.maxWidthShare * width || inkH + 2 * p.clear > o.maxHeightShare * height) {
        p.reason = "capture too small for the mark and its clear space";
        return p;
    }
    // Snap the right and bottom ink edges to the pixel grid; the clear space stays >= 32 units.
    p.inkRight = std::floor(width - p.clear);
    p.inkBottom = std::floor(height - p.clear);
    p.inkLeft = p.inkRight - inkW;
    p.inkTop = p.inkBottom - inkH;
    int x0 = int(std::floor(p.inkLeft)), y0 = int(std::floor(p.inkTop));
    p.rect = {x0, y0, int(p.inkRight) - x0, int(p.inkBottom) - y0};
    int rx = std::max(0, int(std::floor(p.inkLeft - p.clear))), ry = std::max(0, int(std::floor(p.inkTop - p.clear)));
    p.region = {rx, ry, width - rx, height - ry};
    p.fits = true;
    return p;
}

InkChoice chooseInkForLuminance(double y, float opacity) {
    // Compare what the viewer will actually see: each ink composited in sRGB at the
    // group opacity over a grey of the region's mean luminance, by WCAG contrast.
    double v = encode(std::clamp(y, 0.0, 1.0)), a = opacity;
    auto over = [&](std::array<uint8_t, 3> ink) {
        return luminance(a * ink[0] / 255 + (1 - a) * v, a * ink[1] / 255 + (1 - a) * v, a * ink[2] / 255 + (1 - a) * v);
    };
    InkChoice c;
    c.luminance = y;
    c.contrastWhite = contrast(over(WhiteInk), y);
    c.contrastNavy = contrast(over(NavyInk), y);
    c.ink = c.contrastNavy > c.contrastWhite ? Ink::Navy : Ink::White;
    return c;
}

InkChoice chooseInk(const std::vector<uint8_t> &rgba, int width, int height, std::array<int, 4> region,
                    float opacity) {
    static const auto table = [] {
        std::array<double, 256> t{};
        for (int i = 0; i < 256; i++) t[i] = linear(i / 255.0);
        return t;
    }();
    int x0 = std::clamp(region[0], 0, width), y0 = std::clamp(region[1], 0, height);
    int x1 = std::clamp(region[0] + region[2], x0, width), y1 = std::clamp(region[1] + region[3], y0, height);
    double sum = 0;
    size_t count = 0;
    for (int y = y0; y < y1; y++) {
        auto *row = rgba.data() + (size_t(y) * width + x0) * 4;
        for (int x = x0; x < x1; x++, row += 4)
            sum += .2126 * table[row[0]] + .7152 * table[row[1]] + .0722 * table[row[2]];
        count += x1 - x0;
    }
    return chooseInkForLuminance(count ? sum / count : 0, opacity);
}

std::vector<float> watermarkCoverage(const WatermarkPlan &p) {
    auto &m = mask(p.small);
    int w = p.rect[2], h = p.rect[3];
    std::vector<float> out(size_t(std::max(0, w)) * std::max(0, h), 0.f);
    if (!p.fits || w <= 0 || h <= 0) return out;
    float k = m.scale / p.scale; // master px per image px
    auto columns = taps(w, p.inkLeft - p.rect[0], k, m.width), rows = taps(h, p.inkTop - p.rect[1], k, m.height);
    std::vector<float> horizontal(size_t(m.height) * w);
    for (int j = 0; j < m.height; j++) {
        auto *src = m.coverage.data() + size_t(j) * m.width;
        for (int x = 0; x < w; x++) {
            float s = 0;
            for (size_t t = 0; t < columns[x].weights.size(); t++) s += columns[x].weights[t] * src[columns[x].first + t];
            horizontal[size_t(j) * w + x] = s;
        }
    }
    for (int y = 0; y < h; y++)
        for (size_t t = 0; t < rows[y].weights.size(); t++) {
            float wt = rows[y].weights[t] / 255.f;
            auto *src = horizontal.data() + size_t(rows[y].first + t) * w;
            for (int x = 0; x < w; x++) out[size_t(y) * w + x] += wt * src[x];
        }
    for (auto &c : out) c = std::clamp(c, 0.f, 1.f);
    return out;
}

void compositeInk(std::vector<uint8_t> &rgba, int width, int height, std::array<int, 4> rect,
                  const std::vector<float> &coverage, std::array<uint8_t, 3> ink, float opacity) {
    if (rgba.size() != size_t(width) * height * 4 || coverage.size() != size_t(rect[2]) * rect[3])
        throw std::runtime_error("watermark composite size mismatch");
    for (int y = 0; y < rect[3]; y++) {
        int py = rect[1] + y;
        if (py < 0 || py >= height) continue;
        for (int x = 0; x < rect[2]; x++) {
            int px = rect[0] + x;
            float m = coverage[size_t(y) * rect[2] + x] * opacity;
            if (px < 0 || px >= width || m <= 0) continue;
            auto *d = rgba.data() + (size_t(py) * width + px) * 4;
            for (int c = 0; c < 3; c++) d[c] = uint8_t(std::lround(ink[c] * m + d[c] * (1 - m)));
            d[3] = uint8_t(std::lround(255 * m + d[3] * (1 - m)));
        }
    }
}

WatermarkResult applyWatermark(std::vector<uint8_t> &rgba, int width, int height, const WatermarkOptions &o) {
    WatermarkResult r;
    r.opacity = o.opacity;
    if (width <= 0 || height <= 0 || rgba.size() != size_t(width) * height * 4) {
        r.reason = "invalid capture pixels";
        return r;
    }
    r.plan = planWatermark(width, height, o);
    if (!r.plan.fits) {
        r.reason = r.plan.reason;
        return r;
    }
    r.ink = chooseInk(rgba, width, height, r.plan.region, o.opacity);
    compositeInk(rgba, width, height, r.plan.rect, watermarkCoverage(r.plan),
                 r.ink.ink == Ink::Navy ? NavyInk : WhiteInk, o.opacity);
    r.applied = true;
    return r;
}

nlohmann::json watermarkJson(bool enabled, const WatermarkResult &r) {
    nlohmann::json j = {{"enabled", enabled}};
    if (!enabled) return j;
    j["applied"] = r.applied;
    if (!r.applied) {
        j["reason"] = r.reason;
        return j;
    }
    auto &p = r.plan;
    j["variant"] = p.small ? "lockup-bold-horizontal-small-mono" : "lockup-bold-horizontal-mono";
    j["lockupWidthPx"] = p.lockupWidth;
    j["ink"] = r.ink.ink == Ink::Navy ? "#0f1a27" : "#ffffff";
    j["opacity"] = r.opacity;
    j["rect"] = {p.rect[0], p.rect[1], p.rect[2], p.rect[3]};
    j["clearSpacePx"] = p.clear;
    j["regionLuminance"] = r.ink.luminance;
    j["contrast"] = {{"white", r.ink.contrastWhite}, {"navy", r.ink.contrastNavy}};
    return j;
}
} // namespace hyprune::render
