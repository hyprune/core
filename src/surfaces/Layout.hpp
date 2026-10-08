#pragma once
#include "Surface.hpp"
#include "world/World.hpp"
namespace hyprune::surfaces {
using Json = world::Json;
struct Instance {
    std::string id, appClass, title;
    int workspaceId = 0;
};
// Pure authority: all identity rules and bindings are independent of compositor handles.
class Layout {
  public:
    Json bindings = Json::array(), placements = Json::array(), instances = Json::array();
    float windowScale = .002f;
    std::string carried;
    std::map<std::string, Surface> freePoses;
    std::map<std::string, std::string> spawnAreas;
    void spawn(const Instance &, const world::World *, const sim::Camera &, Surface, float aspect = 16.f / 9);
    void mount(const Instance &, const world::World *, Surface);
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
