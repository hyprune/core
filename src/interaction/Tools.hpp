#pragma once
#include "Hands.hpp"
namespace hyprune::interaction {
using Json = world::Json;
struct Line {
    Vec3 a, b, color;
    float width = .018f;
};
struct Handle {
    int axis;
    bool uniform = false;
    Vec3 start, end;
    std::string shape;
};
struct Transform {
    Vec3 position{}, scale{1, 1, 1};
    std::array<float, 4> rotation{0, 0, 0, 1};
    Mat4 matrix(Vec3 origin = {}) const;
};
std::vector<Handle> gizmo(Vec3 center, const std::string &mode, float size = 1);
std::optional<int> pickHandle(std::span<const Handle>, Vec3 origin, Vec3 ray, float tolerance = .12f);
std::vector<Line> pathLines(const Json &path);
class Tools {
  public:
    std::optional<uint64_t> deterministicId;
    int slot = 1, windowMode = 0, pathMode = 0, positionMode = 0;
    std::string selected, kind, activePath;
    int activeHandle = -1;
    Json paths = Json::array(), objects = Json::array(), locks = Json::array();
    std::string submode() const;
    void wheel(int);
    void point(const std::string &world, const std::string &area, Vec3);
    void finishPath();
    bool locked(const std::string &world, const surfaces::Instance &) const;
    void toggleLock(const std::string &world, const surfaces::Instance &);
    void restore(const Json &);
    Json saved() const;

  private:
    mutable std::map<std::string, Json> liveLocks;
};
} // namespace hyprune::interaction
