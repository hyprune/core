#include "Playable.hpp"
#include <limits>
#include <stdexcept>
namespace hyprune::world {
namespace {
using Json = nlohmann::json;
using Cell = PlayableBounds::Cell;
Vec3 vec(const Json &v) {
    return {v[0], v[1], v[2]};
}
bool inside(const Cell &cell, Vec3 p, float allowance) {
    for (auto &plane : cell)
        if (dot(plane.normal, p) > plane.offset + allowance + 1e-5f)
            return false;
    return true;
}
Vec3 project(const Cell &cell, Vec3 p, float allowance) {
    for (int i = 0; i < 96; i++) {
        float worst = 0;
        for (auto &plane : cell) {
            float d = dot(plane.normal, p) - plane.offset - allowance;
            if (d > 0) {
                p = p - plane.normal * d;
                worst = std::max(worst, d);
            }
        }
        if (worst < 1e-5f)
            break;
    }
    return p;
}
std::vector<Cell> declaration(const Json &j) {
    std::vector<Cell> cells;
    for (auto &v : j.value("volumes", Json::array())) {
        Cell cell;
        if (v.at("type") == "box") {
            auto a = vec(v.at("min")), b = vec(v.at("max"));
            if (a.x >= b.x || a.y >= b.y || a.z >= b.z)
                throw std::runtime_error("playable box bounds inverted");
            cell = {{{1, 0, 0}, b.x},   {{-1, 0, 0}, -a.x}, {{0, 1, 0}, b.y},
                    {{0, -1, 0}, -a.y}, {{0, 0, 1}, b.z},   {{0, 0, -1}, -a.z}};
        } else {
            std::vector<Vec3> points;
            for (auto &p : v.at("points"))
                points.push_back(vec(p));
            bool solid = false;
            for (size_t i = 1; i < points.size(); i++)
                for (size_t k = i + 1; k < points.size(); k++)
                    for (size_t l = k + 1; l < points.size(); l++)
                        solid |= std::abs(dot(points[i] - points[0],
                                              cross(points[k] - points[0], points[l] - points[0]))) > 1e-5f;
            if (!solid)
                throw std::runtime_error("playable convex hull must have nonzero volume");
            for (size_t i = 0; i < points.size(); i++)
                for (size_t k = i + 1; k < points.size(); k++)
                    for (size_t l = k + 1; l < points.size(); l++) {
                        auto n = cross(points[k] - points[i], points[l] - points[i]);
                        if (length(n) < 1e-6f)
                            continue;
                        n = normalized(n);
                        float d = dot(n, points[i]);
                        bool positive = false, negative = false;
                        for (auto p : points) {
                            auto distance = dot(n, p) - d;
                            positive |= distance > 1e-4f;
                            negative |= distance < -1e-4f;
                        }
                        if (positive && negative)
                            continue;
                        if (positive) {
                            n = n * -1;
                            d = -d;
                        }
                        bool duplicate = false;
                        for (auto &p : cell)
                            duplicate |= dot(n, p.normal) > .999999f && std::abs(d - p.offset) < 1e-4f;
                        if (!duplicate)
                            cell.push_back({n, d});
                    }
        }
        cells.push_back(std::move(cell));
    }
    if (cells.empty())
        cells.emplace_back();
    if (j.contains("maxFlyHeight"))
        for (auto &cell : cells)
            cell.push_back({{0, 1, 0}, j.at("maxFlyHeight")});
    return cells;
}
// Clip the segment to the connected part of the union containing its start.
float travel(const std::vector<Cell> &cells, Vec3 start, Vec3 delta, float allowance) {
    std::vector<std::pair<float, float>> intervals;
    for (auto &cell : cells) {
        float lo = 0, hi = 1;
        bool valid = true;
        for (auto &p : cell) {
            float a = dot(p.normal, delta), b = p.offset + allowance - dot(p.normal, start);
            if (std::abs(a) < 1e-8f) {
                if (b < -1e-5f) {
                    valid = false;
                    break;
                }
            } else if (a > 0)
                hi = std::min(hi, b / a);
            else
                lo = std::max(lo, b / a);
        }
        if (valid && lo <= hi + 1e-6f)
            intervals.push_back({lo, hi});
    }
    std::sort(intervals.begin(), intervals.end());
    float end = 0;
    for (auto [lo, hi] : intervals) {
        if (lo > end + 1e-5f)
            break;
        end = std::max(end, hi);
    }
    return std::clamp(end, 0.f, 1.f);
}
} // namespace
PlayableBounds loadPlayable(const Json &manifest) {
    PlayableBounds out;
    auto outer = declaration(manifest.value("playable", Json::object()));
    std::vector<Cell> areas;
    for (auto &a : manifest.value("areas", Json::array()))
        if (a.contains("playable")) {
            auto cells = declaration(a["playable"]);
            areas.insert(areas.end(), cells.begin(), cells.end());
        }
    if (areas.size() > 64)
        throw std::runtime_error("too many area playable volumes");
    if (areas.empty())
        areas.emplace_back();
    for (auto &a : outer)
        for (auto &b : areas) {
            auto c = a;
            c.insert(c.end(), b.begin(), b.end());
            out.cells.push_back(std::move(c));
        }
    if (out.cells.size() == 1 && out.cells[0].empty())
        out.cells.clear();
    return out;
}
bool PlayableBounds::contains(Vec3 p, float allowance) const {
    if (cells.empty())
        return true;
    for (auto &c : cells)
        if (inside(c, p, allowance))
            return true;
    return false;
}
Vec3 PlayableBounds::constrain(Vec3 before, Vec3 desired, float allowance, float dt) const {
    if (cells.empty())
        return desired;
    // Recover gradually if fly was enabled outside (e.g. after noclip). No teleport.
    if (!contains(before, allowance)) {
        Vec3 nearest = before;
        float distance = std::numeric_limits<float>::max();
        for (auto &cell : cells) {
            auto p = project(cell, before, allowance);
            if (!inside(cell, p, allowance))
                continue;
            float d = length(p - before);
            if (d < distance) {
                nearest = p;
                distance = d;
            }
        }
        return before + (nearest - before) * std::min(1.f, 3.f * dt / std::max(distance, 1e-6f));
    }
    auto delta = desired - before;
    if (length(delta) < 1e-8f)
        return before;
    float t = travel(cells, before, delta, allowance);
    Vec3 limited = before + delta * t;
    // Project along a containing cell to retain tangential motion at a wall.
    float error = length(desired - limited);
    if (t < 1)
        for (auto &cell : cells)
            if (inside(cell, before, allowance)) {
                auto p = project(cell, desired, allowance);
                float e = length(desired - p);
                if (inside(cell, p, allowance) && e < error) {
                    limited = p;
                    error = e;
                }
            }
    // Brake in the last metre, measuring along the requested direction through
    // the union so shared room faces never slow movement.
    float size = length(delta);
    if (size > 1e-6f) {
        float distance = travel(cells, before, delta / size, allowance);
        float ease = std::clamp(distance, .08f, 1.f);
        limited = before + (limited - before) * ease;
    }
    return limited;
}
} // namespace hyprune::world
