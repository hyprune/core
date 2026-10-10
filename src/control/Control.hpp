#pragma once
#include "ipc/Server.hpp"
#include "world/World.hpp"
namespace hyprune::control {
// World authority, independent of Hyprland and GL. Invoked only by the owner event loop.
class Control {
  public:
    using Json = ipc::Json;
    Json location = {{"zones", Json::array()},
                     {"waypoint", nullptr},
                     {"route", Json::array()},
                     {"autodrive", false},
                     {"transition", nullptr}};
    Json leases = Json::array(), sources = Json::array();
    std::vector<std::pair<std::string, Json>> events;
    Json execute(const std::string &, const Json &, const std::string &actor, const std::string &client,
                 const world::World *, sim::Camera &, const Json &outputs);
    // With a walker, autodrive walks the route with the player's walker (step height,
    // gravity, slopes) instead of a straight capsule-checked line (fly).
    void tick(float dt, sim::Camera &, const world::World *, const sim::MotionStrategy *walker = nullptr,
              sim::Intent walkIntent = {});
    void reset(const world::World *);
    void beginLoad(const Json &to, const Json &output);
    void endLoad(bool success, bool cancelled = false);
    Json outputId = nullptr;
    void stop();
    void disconnect(const std::string &, sim::Camera &);
    void associate(const std::string &actor, bool verified);
    bool ownsLease(const std::string &actor, const std::string &lease) const;
    bool exclusive() const;
    void retainOutputs(const Json &);
    bool driving() const;
    bool photoHeld() const { return !photoOwner.empty(); }
    float photoScale = 1;
    void releasePhoto(sim::Camera &);
    void restorePhoto(sim::Camera &camera) const { if (photoHeld()) camera = photoPose; }
    bool uiVisible() const;
    float holdSeconds = 1;
    void openMenu();
    void hold(bool pressed); // Owner owns hold-to-open timing, never the shell.
    static std::map<std::string, std::string> methods();

  private:
    std::string photoOwner;
    sim::Camera photoPose, photoBefore;
    std::map<std::string, std::string> owners;
    std::set<std::string> associations;
    std::map<std::string, std::chrono::steady_clock::time_point> publications, expiry;
    Vec3 travelFrom{}, travelTo{};
    float elapsed = 0, duration = 0, speed = 3.5f, held = 0;
    bool holding = false, opened = false;
    size_t routeIndex = 0;
    float stalled = 0; // seconds of autodrive walking without horizontal progress
    uint64_t transitions = 0;
    Vec3 target(const Json &, const world::World *) const;
    void transition(const std::string &, float);
};
} // namespace hyprune::control
