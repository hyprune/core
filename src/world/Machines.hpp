#pragma once
// Machines prototype (RFC-0013 draft "live objects").
// Data-channel values (data.publish sources, RFC-0003/0007) reach placed world
// objects in two declarative steps: a binding maps a source key to a machine's
// typed input, and the asset's drives map inputs to slot targets (state palettes,
// animation speed and value-driven poses, emission, labels, toggles, stacks,
// levels, items spawned onto tracks, one-shot effects). Machines and slots come
// from the assets' object.json (RFC-0011 manifest with the RFC-0013 `machine`
// trait). Every placed instance of a machine asset is bound: by default input
// `<id>` reads key `<id>` of source `com.hyprune.sources.<instance id>`; an optional
// user bindings file and the HYPRUNE_MACHINES_PROTOTYPE development file override
// that (Setup). No contract change: the world format and IPC are untouched.
//
// Runs in the tick only: tick() turns the latest source values into per-instance
// draw state (ObjectState.hpp) that the renderer reads. It never touches GL.
#include "ObjectState.hpp"
#include "World.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
namespace hyprune::world {
namespace machines {
// The RFC-0013 mapping language: direct, cases, thresholds, scale (clamp, round),
// format (labels); `becomes` and `counter` produce events (Mapping::event). The
// `average` modifier (draft) feeds a value form the running average of the value
// (Averager, kept by the caller) instead of the value itself.
struct Mapping {
    enum Kind { Direct, Cases, Thresholds, Scale, Format, Becomes, Counter } kind = Direct;
    struct Case {
        std::vector<Json> match; // equals (one) or in (several)
        bool below = true;       // thresholds: below / atLeast
        double bound = 0;
        Json to;
    };
    std::vector<Case> cases;
    std::optional<Json> otherwise; // "else"
    double from0 = 0, from1 = 1, to0 = 0, to1 = 1;
    bool clamp = true;
    std::string round;          // floor, ceil, nearest
    int decimals = -1;          // format
    bool compact = false;
    std::string unit;
    Json becomes, from;         // becomes (from optional: null)
    bool average = false;       // the `average` modifier
    double windowS = 60;        // its window, 1-3600 s
    std::string override;       // drives: an input whose value, while present, replaces the average
    static Mapping parse(const Json &);
    bool event() const { return kind == Becomes || kind == Counter; }
    // Value mappings: the output, or nothing when no case matches and there is no else.
    std::optional<Json> apply(const Json &value) const;
};
double number(const Json &value, double fallback = 0); // bool -> 0/1, numeric strings parsed
std::string text(const Json &value, int decimals = -1, bool compact = false);
// Event detection over successive values: counters fire per increase (the first
// value and any decrease fire nothing); becomes fires on a change into `becomes`.
struct EventGate {
    std::optional<double> counter;
    Json previous; // null until seen
    int feed(const Mapping &, const Json &value);
    void rebaseline() { counter.reset(), previous = nullptr; }
};
// The running average behind the `average` modifier: the time-weighted mean of a
// piecewise-constant value over the last window of fresh data. Fresh samples
// extend the value held since the previous one; pause() ends a span (stale or
// missing time is not counted). The mean is exact when the window holds one value.
struct Averager {
    struct Span {
        int64_t start, end;
        double value;
    };
    std::vector<Span> spans; // oldest first
    bool open = false;
    void feed(double value, int64_t nowMs, double windowS);
    void pause() { open = false; }
    void reset() { spans.clear(), open = false; }
    std::optional<double> mean(int64_t nowMs, double windowS) const;
    static constexpr size_t maxSpans = 1024;
};
// RFC-0011 path geometry: line, curve (cubic Hermite, start tangent (b - previous) / 2,
// end tangent b - a), arc (line plus 0.3·|b - a|·sin(πt) horizontally towards
// (b - a) × +Y); 24 samples per segment, followed by arc length.
struct Track {
    std::vector<Vec3> samples;
    std::vector<float> distance; // cumulative arc length per sample
    std::vector<float> pointAt;  // arc length at each authored point
    float length() const { return distance.empty() ? 0 : distance.back(); }
    static Track build(const std::vector<std::pair<Vec3, std::string>> &points, bool closed);
    Vec3 at(float s, Vec3 *tangent = nullptr) const;
    float nearest(Vec3 p) const; // arc length of the closest sample
};
} // namespace machines
class Machines {
  public:
    // Where machine definitions and bindings come from. dev: the HYPRUNE_MACHINES_PROTOTYPE
    // file for this world ({worldId, manifests (resolved under devRoot), instances,
    // staleMs, effectAsset}); its manifests make machines of package assets, and only
    // the instances it lists are bound for those. user: the user bindings file
    // ({instances: {<id>: {source, inputs, averageWindowS, worldId}}}). Every other
    // instance of an asset whose object.json has the machine trait gets the default
    // bindings. strict: problems throw std::runtime_error; otherwise the instance is
    // skipped and the problem is listed in errors().
    struct Setup {
        Json dev = nullptr;
        std::filesystem::path devRoot;
        Json user = nullptr;
        std::string worldId;
        bool strict = false;
    };
    static constexpr const char *defaultClient = "com.hyprune.sources";
    // (Re)builds machines for the world's current instance list. Instances whose
    // asset, manifest and bindings are unchanged keep their runtime state (levels,
    // averages, counters, items), matched by instance ID.
    void sync(const World &, const Setup &);
    // The development entry point: sync with only a bindings file, strict.
    void load(const Json &spec, const World &, const std::filesystem::path &root);
    bool active() const { return !instances_.empty(); }
    // sources: Control::sources ({sourceId, values, sequence, monotonicMs, ttlMs});
    // nowMs: the steady clock in the milliseconds of monotonicMs. still: reduced
    // motion (states, labels, counts and flashes; no spins, travel or bursts).
    void tick(double dt, const Json &sources, int64_t nowMs, bool still);
    std::vector<ObjectDrawState> states; // parallel to World::objectInstances
    std::vector<ObjectExtra> extras;     // stack copies, items, effect pieces (rebuilt each tick)
    std::vector<WorldLabel> labels;      // rebuilt each tick
    std::vector<size_t> instances() const;
    Json diagnostics() const; // shaped like RFC-0013's `live` state key, plus runtime counts
    const std::vector<std::string> &errors() const { return errors_; }
    // Instances bound since the previous sync with the default source (for a one-time hint).
    std::vector<std::pair<size_t, std::string>> fresh;
    static constexpr int maxEffects = 8, maxPieces = 48, maxBacklog = 16, maxInstances = 64;
    static constexpr size_t maxStackCopies = 1024; // world-wide (RFC-0013 caps)

  private:
    enum class Condition { Fresh, Stale, Missing, Error };
    struct Input {
        std::string id;
        enum Type { Enum, Int, Number, Bool, Text, Event } type = Enum;
        std::vector<std::string> values;
        double min = 0, max = 1e9;
        size_t maxLength = 64;
        Json fallback = nullptr; // default
    };
    struct Palette {
        std::string id;
        std::vector<unsigned> meshes;
        std::vector<std::pair<std::string, MeshPaint>> entries;
        int stale = -1;
        int find(const std::string &id) const;
    };
    struct LabelSlot {
        std::string id;
        Vec3 centre, right, up; // asset space; half extents
        size_t maxChars = 24;
        Vec3 colour{.55f, 1.f, .85f};
    };
    struct TrackSlot {
        std::string id;
        machines::Track track;
        float speed = .6f, spacing = .45f;
        size_t max = 24;
        bool dropOldest = false;
        std::vector<std::pair<float, float>> stops; // arc length, seconds (sorted)
        std::vector<unsigned> assets;
    };
    struct SpawnSlot {
        std::string id;
        int track = -1;
        float at = 0; // arc length where items enter
        unsigned asset = 0;
        float maxRate = 2;
    };
    struct StackSlot {
        std::string id;
        unsigned node = 0;
        std::vector<unsigned> meshes;
        std::vector<unsigned> painted; // meshes a states slot paints (copies take that paint)
        double rate = 0;               // copies per second towards a new count; 0: at once
        int grid[3]{1, 1, 1};
        Vec3 spacing{.3f, .3f, .3f};
        int order[3]{0, 2, 1}; // fill axes, fastest first ("xzy")
        size_t cap = 16;
    };
    // A level (draft): `fill` scales the node's subtree along the axis about the node's
    // origin (0 hides it), `slide` moves it by level x length; a fill's surface node
    // rides the top and rocks while the level sloshes (settle spring).
    struct LevelSlot {
        std::string id;
        unsigned node = 0;
        std::vector<unsigned> meshes, painted;
        int surface = -1;
        std::vector<unsigned> surfaceMeshes, surfacePainted;
        Vec3 origin, axis{0, 1, 0}, surfaceOrigin;
        float length = 1;
        bool fill = true, spring = false;
    };
    struct EffectSlot {
        std::string id;
        Vec3 position;
        enum Kind { Burst, Pulse, Flash, Ring } kind = Burst;
        Vec3 colour{1, .8f, .2f};
        float seconds = 1.5f;
    };
    struct Drive {
        int input = -1;
        enum Target { Animation, State, Emission, Label, Visible, Count, Spawn, Effect, Position, Level } target;
        int index = -1; // animation, slot or position index
        int override = -1; // average modifier: the overriding input
        machines::Mapping map;
    };
    // One machine asset: object.json resolved against the loaded asset.
    struct Definition {
        unsigned asset = 0;
        std::string id, primary;
        std::vector<Input> inputs;
        std::vector<Drive> drives;
        std::vector<Palette> palettes;
        std::vector<LabelSlot> labels;
        std::vector<TrackSlot> tracks;
        std::vector<SpawnSlot> spawns;
        std::vector<StackSlot> stacks;
        std::vector<LevelSlot> levels;
        std::vector<std::pair<std::string, std::vector<unsigned>>> toggles;
        std::vector<EffectSlot> effects;
        std::vector<std::pair<float, float>> positions; // per animation: range, or {-1,-1} clock-driven
        std::vector<bool> driven;                       // per animation: an animation: or position: drive
        bool dev = false;    // defined by the development file's manifests
        uint64_t digest = 0; // of the manifest, to keep instance state across a sync
    };
    struct Binding {
        std::string source, key;
        machines::Mapping map;
        bool bound = false;
    };
    struct Item {
        float s = 0, dwell = 0;
        size_t nextStop = 0;
        int state = 0;
    };
    struct Piece {
        size_t instance;
        Vec3 origin, velocity, axis;
        float age = 0, life = 1, spin = 0, size = 1;
        int kind = 0;
        uint32_t effect = 0;
        MeshPaint paint;
    };
    struct Instance {
        size_t object = 0; // World::objectInstances index
        std::string id;    // its instance ID
        const Definition *def = nullptr;
        std::string spec;  // the bindings it was built from (JSON), to keep state across a sync
        double averageWindowS = -1; // per-instance window for every average modifier (-1: the mapping's)
        std::vector<Binding> bindings;      // per input
        std::vector<machines::Averager> inputAverages; // per input (binding average modifier)
        std::vector<machines::Averager> driveAverages; // per drive
        std::vector<machines::EventGate> gates;      // per input (binding events)
        std::vector<machines::EventGate> driveGates; // per drive (becomes on a value input)
        std::vector<Json> raw, value;       // per input
        std::vector<Condition> condition;   // per input
        std::vector<int64_t> age;           // per input: ms since publication, -1 missing
        std::vector<int> fired;             // per input: events this evaluation
        std::vector<uint64_t> seenSequence; // per input
        // Target state
        std::vector<int> paletteEntry;      // per palette
        std::vector<float> emission;        // per palette
        std::vector<bool> paletteStale;
        std::vector<std::string> labelText; // per label
        std::vector<bool> labelStale;
        std::vector<bool> toggleHidden;
        std::vector<size_t> stackCount;     // per stack: the target count
        std::vector<double> stackShown;     // per stack: the count shown (moves at the slot's rate)
        std::vector<double> levelTarget, level, levelSpeed, tilt, tiltSpeed; // per level
        std::vector<double> speedTarget, speed, turns; // per animation
        std::vector<double> positionTarget, position;  // per animation (value-driven)
        std::vector<double> easeFrom, easeTo, easeTime; // per animation: the 300 ms ease in progress
        std::vector<std::vector<Item>> items;          // per track
        std::vector<int> backlog;                      // per spawn
        std::vector<double> rateClock;                 // per spawn
        std::vector<uint64_t> spawned, dropped;        // per spawn
        double sinceUpdate = 1e9;
        uint64_t signature = 0;
        bool anyStale = false;
        float look = 0; // 0 fresh .. 1 grey, eased
    };
    const World *world_ = nullptr;
    std::map<unsigned, Definition> definitions_; // by asset index
    std::map<std::string, std::pair<unsigned, Json>> items_; // item asset id -> asset, object.json
    int effectAsset_ = -1;
    int64_t staleMs_ = 8000; // stand-in for RFC-0013's data.publish staleMs
    std::vector<Instance> instances_;
    std::vector<Piece> pieces_;
    std::vector<std::string> errors_;
    int64_t now_ = 0; // the tick's clock (averages)
    size_t stackCopies_ = 0, stackCapped_ = 0;
    uint32_t seed_ = 0x9e3779b9u, nextEffect_ = 0;
    double coalesce_ = .25; // at most 4 visual updates per second per instance (RFC-0013)
    Definition define(unsigned asset, const Json &manifest) const;
    void bind(Instance &, const Json &spec, const std::string &objectId) const;
    void prepare(Instance &, size_t object) const;
    double window(const Instance &, const machines::Mapping &) const;
    void settle(Instance &, double dt, bool still, bool &moving);
    std::vector<unsigned> subtreeMeshes(unsigned asset, unsigned node) const;
    void evaluate(Instance &, const Json &sources, int64_t nowMs, bool still);
    void apply(Instance &, size_t drive, bool still);
    void fire(Instance &, const EffectSlot &, bool still);
    MeshPaint itemPaint(unsigned asset, const std::string &state) const;
};
} // namespace hyprune::world
