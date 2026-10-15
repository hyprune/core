#pragma once
// Data-driven behaviours (RFC-0014 amendment: data-driven behaviours, IPC 0.31 draft): any
// user placement can carry up to 8 behaviours, each reading one published value (a source
// key, by default key `value` of `com.hyprune.sources.<instance id>`, as a gauge does) mapped
// from min..max to 0..1, eased and smoothed, and driving one display property between A and B:
// scale per axis about an anchor face, move, rotate about the anchor, colour, glow, visible
// (a threshold) or the texture's offset or scroll. Display only: collision, picking and the
// placement rules keep the rest pose.
//
// Runs in the tick only: tick() turns the latest source values into per-instance draw state
// (ObjectState.hpp) that both renderers read. It never touches GL.
#include "ObjectState.hpp"
#include "World.hpp"
#include <array>
#include <cstdint>
#include <json.hpp>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace hyprune::world {
struct Behaviour {
    enum Property { Scale, Move, Rotate, Colour, Glow, Visible, Texture } property = Scale;
    enum Anchor { Bottom, Top, Left, Right, Front, Back, Centre } anchor = Bottom;
    enum Easing { Linear, EaseIn, EaseOut, EaseInOut, Step } easing = Linear;
    std::string key, source, input = "value"; // source empty: com.hyprune.sources.<instance id>
    double min = 0, max = 100, smoothing = .3;
    std::array<double, 3> from{}, to{};       // scale factors, metres, degrees, texture repeats, glow strength (from[0])
    Vec3 colourFrom{1, 1, 1}, colourTo{1, 1, 1}, glow{1, 1, 1}; // linear
    std::optional<double> above, below;       // visible: shown while above <= value < below
    bool scroll = false;                      // texture: from/to are speeds (repeats per second)
};
// The behaviours of one placement record (`behaviours`, JSON), checked: throws
// std::invalid_argument naming the first problem.
std::vector<Behaviour> parseBehaviours(const nlohmann::json &list);
inline constexpr size_t behaviourLimit = 8;
inline constexpr const char *behaviourClient = "com.hyprune.sources";

class Behaviours {
  public:
    enum class Condition { Fresh, Stale, Missing, Error };
    struct Bound {
        size_t instance = 0;
        std::string id;          // the placement id (the default source's last part)
        std::vector<Behaviour> list;
    };
    // The placements with behaviours: instance index, placement id and the parsed list. Smoothing
    // state carries over by placement id and behaviour key.
    void sync(const World &, std::vector<Bound> bound);
    // sources: Control::sources ({sourceId, values, monotonicMs, ...}); still: reduced motion or a
    // pinned clock (snap, no scrolling). staleMs: data older than this holds.
    void tick(double dt, const nlohmann::json &sources, int64_t nowMs, bool still, int64_t staleMs = 8000);
    // Folds this tick's results into the draw states (sized to the world's instances): display,
    // texture offset, and for colour, glow and visible every mesh's paint.
    void apply(const World &, std::vector<ObjectDrawState> &states) const;
    bool active() const { return !bound_.empty(); }
    // IPC 0.31 instance `behaviours`: [{key, property, condition, value, t}] (null without any).
    nlohmann::json summary(const std::string &placementId) const;
    // Bumps when a summary changes (values, conditions); not per frame of smoothing.
    uint64_t revision() const { return revision_; }
    static const char *propertyName(Behaviour::Property);

  private:
    struct Live {
        double target = 0, shown = 0, scrollU = 0, scrollV = 0;
        bool known = false;
        Condition condition = Condition::Missing;
        nlohmann::json raw = nullptr;
    };
    struct Instance {
        Bound bound;
        std::vector<Live> live;
        Vec3 low{}, high{};
        float scale = 1;
        unsigned meshes = 0;
        bool moving = false;
    };
    std::vector<Instance> bound_;
    std::map<std::string, Live> carried_; // id + "\n" + key -> state across syncs
    uint64_t revision_ = 0;
    std::string lastSummary_;
};
} // namespace hyprune::world
