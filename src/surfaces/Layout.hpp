#pragma once
#include "Surface.hpp"
#include "world/World.hpp"
#include <set>
namespace hyprune::surfaces {
using Json = world::Json;
struct Instance {
    std::string id, appClass, title;
    int workspaceId = 0;
    bool floating = false; // floated natively by the user's compositor, not by the world
};
// Pure authority: all identity rules and bindings are independent of compositor handles.
class Layout {
  public:
    // `bindings` are explicit user homes (durable). `defaults` are derived from the
    // loaded world (workspaceBase + workspaceSlot) and never persisted.
    Json bindings = Json::array(), defaults = Json::array(), placements = Json::array(), instances = Json::array();
    int roomWorkspace = 20, workspaceBase = 0;
    std::set<int> reservedWorkspaces;
    void deriveDefaults(const world::World *);
    Json effectiveBindings() const;
    std::vector<std::string> pocketOrder;
    void stash(const Instance &, Surface);
    float windowScale = .002f;
    std::string carried;
    std::map<std::string, Surface> freePoses, occupiedPoses;
    // Shrink factor a free window needed to fit after a client resize, per logical size.
    std::map<std::string, std::tuple<int, int, float>> fittedScale;
    std::map<std::string, std::string> spawnAreas;
    std::set<std::string> unplaced;
    std::map<std::string, Json> content;
    bool vacant(const Surface &, const std::string &except = "") const;
    void setContent(const Instance &, const world::World *, const Json &);
    const Json *contentFor(const Instance &, const world::World *) const;
    bool spawn(const Instance &, const world::World *, const sim::Camera &, Surface, float aspect = 16.f / 9);
    void mount(const Instance &, const world::World *, Surface);
    // Fit into an authored tool-mount frame (room clients too); RFC-0005 snap targets.
    void placeOnMount(const Instance &, const Json &mount, const world::World *);
    void invalidateCollision() {
        recovered.clear();
        fits.clear();
    }
    Json desktop = nullptr;
    Json state(const world::World *) const;
    const Json *binding(int workspace, const world::World *) const;
    const Json *anchor(const std::string &, const world::World *) const;
    void bind(int id, const std::string &worldId, const std::string &areaId, const world::World *);
    void restore(const Json &);
    void pull(const Instance &, const world::World *);
    std::string place(const Instance &, const std::string &anchorId, const world::World *);
    // Back to a workspace wall: the one it is dropped onto, else the home it left. Returns
    // that workspace, where the window belongs natively again (tiled).
    int release(const Instance &, const world::World *, int wallWorkspace = 0);
    void recall(std::span<const Instance>);
    bool pulled(const Instance &, const world::World *) const;
    // Composed on workspace `id`'s wall (its pose is its rectangle there) rather than drawn on
    // its own: on that workspace and not pulled into the world, unless its home lives on
    // another output (shown on its wall only). An attached child (an X11 menu, a transient
    // dialog) goes with its parent: a menu of a window pulled into the world stays beside it.
    bool composedOnWall(int id, const Instance &window, bool elsewhere, const world::World *w,
                        const Instance *parent = nullptr, bool parentElsewhere = false) const;
    bool hasPlacement(const Instance &instance, const world::World *world) const {
        return saved(instance, world) != nullptr;
    }
    std::optional<Surface> locate(const Instance &, const world::World *, const sim::Camera &, int pw, int ph);
    std::vector<std::pair<std::string, Json>> events;
    // Home workspace windows are tiled windows of that workspace, shown on its wall. One
    // the user moves into the world (pull/carry, place, mount, pocket) leaves for the room
    // workspace: homes maps it to the home workspace it left, until it returns.
    std::map<std::string, int> homes;
    // The home binding a window belongs to: its workspace's, or the one it left for the world.
    const Json *homeOf(const Instance &, const world::World *) const;
    // Still on its home workspace but moved into the world: to be moved to the room.
    bool leaving(const Instance &i) const {
        auto h = homes.find(i.id);
        return h != homes.end() && h->second == i.workspaceId;
    }
    // A closed or departed window gives up its wall, arrival anchor and holds.
    void forget(const std::string &surfaceId);

  private:
    struct Recovered {
        const world::World *world;
        Json pose;
        float width, height;
        Surface surface;
    };
    std::map<std::string, Recovered> recovered;
    // fitToMount per world, anchor and client size: a full collision test, too costly
    // to repeat for every mounted window on every tick.
    std::map<std::tuple<const world::World *, std::string, int, int>, std::optional<Surface>> fits;
    std::optional<Surface> mountFit(const Json &mount, int pw, int ph, const world::World &);
    std::map<std::string, int> recalled;
    std::map<std::string, std::string> temporaryAnchors;
    // Last holder of each tool anchor: the compositor's window order (a focus raise moves a
    // window last) or two windows matching one rule never swap clients between anchors.
    std::map<std::string, std::string> holders;
    std::map<std::string, Json> identities; // surface -> its bounded rule identity
    std::map<std::string, int> lastWorkspace;
    std::string holder(const std::string &anchorId, const std::string &except) const;
    bool taken(const std::string &anchorId, const std::string &except) const;
    void hold(const std::string &surfaceId, const std::string &anchorId);
    void enterWorld(const Instance &, const world::World *);
    Json rule(const Instance &) const;
    bool matches(const Json &, const Instance &) const;
    const Json *saved(const Instance &, const world::World *) const;
};
bool overlaps(const Surface &, std::span<const Triangle>, float margin = .04f);
// A spawned plane's lower edge may sit at most this far above the floor below it.
inline constexpr float MaxSpawnLift = 2.f;
// Floor (an upward-facing surface) under the plane within MaxSpawnLift of its lower
// edge, inside the world's playable volume. Worlds without collision always pass.
bool reachable(const Surface &, const world::World &);
bool pushOut(Surface &, std::span<const Triangle>);
Surface carryPlane(const sim::Camera &, float width, float height, std::span<const Triangle>, int index = 0);
void anchorPlane(Surface &, const Json &, int width, int height);
// Aspect-fit, centred and flush in a tool mount's frame without intersecting geometry.
std::optional<Surface> fitToMount(const Json &mount, int width, int height, std::span<const Triangle> collision);
// Area-less worlds author room-window poses as manifest anchors (screenType
// "com.hyprune.window"), taken in capture order. Null when entry `index` has none.
const Json *roomAnchor(const world::World &, size_t index);
// That anchor's window pose: its centre and yaw, aspect-fit to its size by the client's pixels.
void roomAnchorPose(Surface &, const Json &anchor);
// A room client adopted without a saved placement. A pocketed client stays pocketed. A
// client present at activation takes its authored room anchor, visible, or waits in the
// pocket (hidden, never posed at an anchor). A client opened later spawns.
enum class RoomAdoption { Anchor, Spawn, Pocket };
// Workspaces Hyprune owns: the room, every home binding (explicit ones and the loaded world's
// defaults) and configured world locations ({id,...}). Switching to or from one is a world
// transition, so it never plays the desktop's workspace animation.
std::set<int> worldWorkspaces(int room, const Json &bindings, const Json &locations);
RoomAdoption adoptRoomClient(const world::World &, size_t index, bool existing, bool pocketed);
} // namespace hyprune::surfaces
