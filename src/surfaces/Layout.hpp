#pragma once
#include "Surface.hpp"
#include "world/World.hpp"
#include <set>
namespace hyprune::surfaces {
using Json = world::Json;
struct Instance {
    std::string id, appClass, title;
    int workspaceId = 0;
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
    std::map<std::string, std::string> spawnAreas;
    std::set<std::string> unplaced;
    std::map<std::string, Json> content;
    bool vacant(const Surface &, const std::string &except = "") const;
    void setContent(const Instance &, const world::World *, const Json &);
    const Json *contentFor(const Instance &, const world::World *) const;
    bool spawn(const Instance &, const world::World *, const sim::Camera &, Surface, float aspect = 16.f / 9);
    void mount(const Instance &, const world::World *, Surface);
    void invalidateCollision() { recovered.clear(); }
    Json desktop = nullptr;
    Json state(const world::World *) const;
    const Json *binding(int workspace, const world::World *) const;
    const Json *anchor(const std::string &, const world::World *) const;
    void bind(int id, const std::string &worldId, const std::string &areaId, const world::World *);
    void restore(const Json &);
    void pull(const Instance &, const world::World *);
    std::string place(const Instance &, const std::string &anchorId, const world::World *);
    void release(const Instance &, const world::World *);
    void recall(std::span<const Instance>);
    bool pulled(const Instance &, const world::World *) const;
    bool hasPlacement(const Instance &instance, const world::World *world) const {
        return saved(instance, world) != nullptr;
    }
    std::optional<Surface> locate(const Instance &, const world::World *, const sim::Camera &, int pw, int ph);
    std::vector<std::pair<std::string, Json>> events;

  private:
    struct Recovered {
        const world::World *world;
        Json pose;
        float width, height;
        Surface surface;
    };
    std::map<std::string, Recovered> recovered;
    std::map<std::string, int> recalled;
    std::map<std::string, std::string> temporaryAnchors;
    Json rule(const Instance &) const;
    bool matches(const Json &, const Instance &) const;
    const Json *saved(const Instance &, const world::World *) const;
};
bool overlaps(const Surface &, std::span<const Triangle>, float margin = .04f);
bool pushOut(Surface &, std::span<const Triangle>);
Surface carryPlane(const sim::Camera &, float width, float height, std::span<const Triangle>, int index = 0);
void anchorPlane(Surface &, const Json &, int width, int height);
} // namespace hyprune::surfaces
