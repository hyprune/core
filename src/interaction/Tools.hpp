#pragma once
#include "Hands.hpp"
namespace hyprune::interaction {
using Json = world::Json;
struct Line {
    Vec3 a, b, color;
    float width = .018f;
    int segment = -1; // path lines: the index of the point that ends this segment
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
// Tool 3: the path point (or segment) under a ray, nearest first; points win over segments.
// Tolerances grow with distance (about a constant screen size); nothing beyond maxDistance
// (the aimed surface) is picked.
struct PathPick {
    std::string path;
    int point = -1, segment = -1; // segment: insert before this point index
    Vec3 at{};
    float distance = 0;
};
std::optional<PathPick> pickPath(const Json &paths, const std::string &worldId, Vec3 origin, Vec3 ray,
                                 float maxDistance);
class Tools {
  public:
    std::optional<uint64_t> deterministicId;
    int slot = 1, windowMode = 0, pathMode = 0, positionMode = 0;
    std::string selected, kind, activePath;
    int activeHandle = -1;
    Json paths = Json::array(), objects = Json::array(), locks = Json::array();
    // RFC-0011 overlay (editor.json version 2): user placements and overrides of
    // authored world objects, per world id. objects keeps scene-node transforms only.
    Json placements = Json::array(), overrides = Json::array();
    bool migrated = false; // restore() upgraded a version 1 file (instance records moved)
    static constexpr size_t placementsPerWorld = 128, placementLimit = 1024, overrideLimit = 1024;
    bool localAxes = false, snap = false;
    // Gizmo 2.0: pivot at the base (bottom centre) instead of the centre; the
    // universal mode joins the wheel cycle only when enabled (HYPRUNE_GIZMO_UNIVERSAL);
    // the submode reports "universal" (IPC 0.22; older peers see "move").
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
    // Point edits of any path in the overlay (tool 3). Each returns the path record before
    // the edit (null when the path did not exist) for the undo history.
    Json path(const std::string &id) const;           // null when absent
    void setPath(const std::string &id, Json record); // null removes
    void movePoint(const std::string &id, int index, Vec3 position);
    void insertPoint(const std::string &id, int index, Vec3 position);
    void removePointAt(const std::string &id, int index);
    std::vector<Handle> handles(Vec3 center, const std::array<float, 4> &rotation) const;
    std::string submode() const;
    void wheel(int);
    void point(const std::string &world, const std::string &area, Vec3);
    void finishPath();
    bool locked(const std::string &world, const surfaces::Instance &) const;
    void toggleLock(const std::string &world, const surfaces::Instance &);
    void restore(const Json &);
    Json saved() const;
    // Overlay records of one world (null when absent).
    Json placement(const std::string &world, const std::string &id) const;
    Json override(const std::string &world, const std::string &objectId) const;
    void setPlacement(const std::string &world, const std::string &id, Json record);  // null removes
    void setOverride(const std::string &world, const std::string &objectId, Json record); // null removes

  private:
    mutable std::map<std::string, Json> liveLocks;
    std::vector<Edit> history; // oldest first
    size_t undone = 0;         // edits at the end of history that are undone
};
} // namespace hyprune::interaction
