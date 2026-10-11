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
    bool localAxes = false, snap = false;
    // Gizmo 2.0: pivot at the base (bottom centre) instead of the centre; the
    // universal mode joins the wheel cycle only when enabled (HYPRUNE_GIZMO_UNIVERSAL):
    // IPC's editor submode has no "universal" value, so it reports "move".
    bool pivotBase = false, universal = false;
    std::string gizmoMode() const; // move, rotate, scale or universal
    // Undo/redo of transforms (tool 4, Ctrl+Z / Ctrl+Shift+Z). before/after are the
    // overlay records (objects) or window poses; null = no record.
    struct Edit {
        std::string kind, worldId, id;
        Json before, after;
    };
    static constexpr size_t undoLimit = 64;
    void recordEdit(const std::string &kind, const std::string &worldId, const std::string &id, Json before, Json after);
    std::optional<Edit> undo();
    std::optional<Edit> redo();
    bool canUndo() const { return undone < history.size(); }
    bool canRedo() const { return undone > 0; }
    // Declarative slot dispatch: adapters resolve actions, never physical buttons.
    // Future trusted extensions can register definitions and action handlers at
    // this boundary without introducing another mouse/key routing path.
    Json definitions() const;
    Json slots() const;
    std::string slotAction(const std::string &) const;
    void removePoint();
    void toggleClosed();
    std::vector<Handle> handles(Vec3 center, const std::array<float, 4> &rotation) const;
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
    std::vector<Edit> history; // oldest first
    size_t undone = 0;         // edits at the end of history that are undone
};
} // namespace hyprune::interaction
