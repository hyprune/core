#pragma once
#include "sim/Camera.hpp"
#include "world/World.hpp"
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>
namespace hyprune::quests {
using Json = nlohmann::json;
// RFC-0012 markers on the plugin side: core resolves the marker list against the
// loaded world, runs the TAB-tap reveal, and lays out world-anchored icons, labels
// and edge arrows for each frame's camera under the clutter rules. The renderer
// draws the layout; nothing here touches GL or the compositor.
struct Marker {
    std::string id, kind, label, icon, occlusion = "through", areaId;
    int tier = 4;
    Json target, source;
    std::optional<Vec3> position;
    float rangeMin = 2, rangeMax = 1000, labelRange = 120;
    bool tracked = false;
    Json json() const;
};
// Target resolution (RFC-0012 table). `bindings`: the layout's effective
// workspace bindings ({id, worldId, areaId}).
std::optional<Vec3> resolveTarget(const world::World &, const Json &target, const Json &bindings);
std::string areaAt(const world::World &, Vec3);
// The eye is inside a reach target: area/zone bounds, else within `radius` (default 4 m).
bool reaches(const world::World &, const Json &target, std::optional<float> radius, Vec3 eye, const Json &bindings);
// Builds the list (<= 64, tier then id): waypoint, route destination, requested
// quest/focus/break markers, area homes, other bound workspaces, places and zones.
std::vector<Marker> buildMarkers(const world::World *, const Json &waypoint, const Json &route, const Json &bindings,
                                 const Json &requested);

struct Reveal {
    static constexpr int64_t FadeIn = 120, Stagger = 30, FadeOut = 600;
    bool active = false, toggle = false, reduced = false;
    int64_t started = 0, holdMs = 4000;
    std::string reason = "tap";
    // A tap while shown or fading restarts the hold; in toggle mode it hides.
    void start(int64_t now, int64_t hold, bool toggleMode, bool reducedMotion, const std::string &why);
    void end() { active = false; }
    float alpha(int64_t now, int tier) const;
    bool expired(int64_t now) const;
    bool fading(int64_t now) const;
    Json json() const;
};

struct ScreenRect {
    float x0, y0, x1, y1; // output pixels
    bool aimed = false;   // the aimed or used window: markers never draw over it
};
struct DrawItem {
    std::string id, kind, icon, label, distance;
    int tier = 4, stack = 1, chevron = 0; // chevron: +1 up, -1 down
    float x = 0, y = 0, alpha = 1, angle = 0; // output pixels; angle of an edge arrow (radians, screen space)
    bool arrow = false, occluded = false, small = false, dimmed = false, tracked = false;
};
struct MarkerLayout {
    std::vector<DrawItem> items;
    int inView = 0, arrows = 0, labels = 0, stacks = 0;
    Json json() const;
};
struct MarkerView {
    sim::Camera camera;
    int width = 1, height = 1;
    float scale = 1;                   // output pixels per logical pixel
    float reticleX = .5f, reticleY = .5f; // 0..1
    std::vector<ScreenRect> windows;
    int64_t now = 0;
};

class MarkerRuntime {
  public:
    struct Settings {
        int64_t revealMs = 4000;
        bool toggle = false, coreDraw = true;
        int maxRevealed = 12, labels = 5;
        std::string persistent = "edge";
    } settings;
    std::vector<Marker> list;
    Reveal reveal;
    bool calm = false, reducedMotion = false;
    std::string mode = "hidden"; // world, interactive (persistent only) or hidden
    std::string eyeArea;
    std::set<std::string> occluded;
    std::set<std::string> inside; // reach watch keys holding the eye
    int64_t lastInput = 0;
    uint64_t version = 0; // bumps when the list or settings change
    Json requested = Json::array(), watch = Json::array();

    void sync(const Json &); // quests.sync from hypruned
    struct Context {
        const world::World *world = nullptr;
        Json waypoint, route, bindings;
        sim::Camera camera;
        std::string mode; // world, interactive, hidden
        bool active = false, reducedMotion = false;
        int64_t now = 0;
    };
    // Per plugin tick (outside the render pass).
    void update(const Context &);
    void tap(int64_t now);
    void request(int64_t now, std::optional<int64_t> durationMs);
    void input(int64_t now) { lastInput = std::max(lastInput, now); }
    Json stateKey() const;
    // Revealed and persistent markers with `markerId`, for shells that draw them (core_draw off).
    Json projection(const sim::Camera &, float aspect, int64_t now) const;
    uint64_t frameHash(int64_t now) const;
    bool fading(int64_t now) const { return reveal.active && reveal.fading(now); }
    MarkerLayout layout(const MarkerView &) const;

  private:
    Json listInputs_ = nullptr;
    int64_t nextOcclusion_ = 0;
    std::string lastMode_;
    const world::World *world_ = nullptr;
};
} // namespace hyprune::quests
