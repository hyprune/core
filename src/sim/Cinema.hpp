#pragma once
// Demo camera driver (development tooling, not a wire contract): camera tracks and follow
// shots that core evaluates once per presented frame, so recordings move by the same
// amount every frame instead of jumping whenever a script gets a command through.
//
// Track: keyframes (t, eye, target or yaw/pitch, fov, ease). The eye moves along a
// centripetal Catmull-Rom curve, timed by arc length with a monotone cubic through the
// keys' times (velocity is continuous; "ease" puts a key at rest). A target moves the
// same way and the view looks at it; otherwise yaw (shortest arc), pitch and fov are
// monotone cubics in time.
// Follow: an eye offset in the subject's heading frame and a look-ahead target, both
// critically damped (half-life), the heading too, so a turning subject never steps.
// Director: one of them at a time, cross-fading from whatever drove the camera before.
#include "Math.hpp"
#include <functional>
#include <optional>
#include <string>
#include <vector>
namespace hyprune::sim::cinema {
struct Pose {
    Vec3 eye;
    float yaw = 0, pitch = 0, fov = 60;
};
enum class Ease { None, In, Out, InOut };
struct Key {
    double t = 0;
    Vec3 eye;
    std::optional<Vec3> target; // look at this point; otherwise yaw/pitch
    float yaw = 0, pitch = 0;
    std::optional<float> fov;   // missing: the previous key's (the first: the camera's)
    Ease ease = Ease::None;     // the segment to the next key: In leaves this key from rest, Out stops at the next
};
float wrap(float angle); // to (-pi, pi]
float smoothstep(float u);
Pose lookAt(Vec3 eye, Vec3 target, float fov);
// Shortest-arc yaw; u is used as given (callers ease it).
Pose blend(const Pose &a, const Pose &b, float u);

// Monotone piecewise cubic (Fritsch-Carlson) through (x_i, y_i), x increasing. rest[i]
// forces a zero slope at knot i. Constant outside the knots.
class Monotone {
  public:
    Monotone() = default;
    Monotone(std::vector<double> x, std::vector<double> y, std::vector<bool> rest = {});
    double at(double x) const;

  private:
    std::vector<double> x, y, m;
};

// Centripetal Catmull-Rom curve through points (end tangents from mirrored neighbours),
// addressed by arc length.
class Curve {
  public:
    explicit Curve(std::vector<Vec3> points);
    float length() const { return knots.empty() ? 0 : knots.back(); }
    float knot(size_t i) const { return knots[i]; } // arc length at point i
    Vec3 at(float s) const;

  private:
    static constexpr int Samples = 128;
    std::vector<Vec3> points;
    std::vector<float> knots;                    // cumulative length at each point
    std::vector<std::vector<float>> tables;      // per segment: cumulative length at Samples+1 params
    Vec3 segment(size_t i, float u) const;       // u in 0..1 between points i and i+1
};

class Track {
  public:
    Track(std::vector<Key> keys, float fov);
    Pose at(double t) const;
    double duration() const { return keys.empty() ? 0 : keys.back().t - keys.front().t; }
    double start() const { return keys.empty() ? 0 : keys.front().t; }

  private:
    std::vector<Key> keys;
    std::optional<Curve> eye, target;
    Monotone eyeTiming, targetTiming, yaw, pitch, fov;
};

// Exact critically damped spring towards a goal (half-life: time to halve the distance
// to a still goal).
struct Spring {
    float x = 0, v = 0;
    void step(float goal, float halfLife, float dt);
};
struct Spring3 {
    Vec3 x, v;
    void step(Vec3 goal, float halfLife, float dt);
};

struct Subject {
    Vec3 position;
    float heading = 0; // forward (sin h, 0, cos h)
};
struct FollowParams {
    float back = 3.4f, side = 1.2f, height = 1.9f; // eye: behind, to the subject's left (+), above its feet
    float ahead = 1.5f, lookHeight = 1.f;          // target: ahead of the subject, above its feet
    float halfLife = .35f, lookHalfLife = .2f, headingHalfLife = .45f;
    float snap = 12; // a subject that jumps further than this in one tick is a cut, not a swoop
    std::optional<float> fov;
};
class Follow {
  public:
    explicit Follow(FollowParams p) : p(p) {}
    // Eye and target for a subject at a heading, unsmoothed.
    std::pair<Vec3, Vec3> goal(Vec3 position, float heading) const;
    // clip(target, eye) -> eye pulled in front of anything solid between them.
    Pose step(const Subject &, float dt, float fov, const std::function<Vec3(Vec3, Vec3)> &clip = {});
    const FollowParams &params() const { return p; }

  private:
    FollowParams p;
    bool started = false;
    Vec3 last;
    Spring heading;
    Spring3 eye, target;
};

class Director {
  public:
    enum class Mode { Idle, Hold, Path, Follow };
    using Lookup = std::function<std::optional<Subject>(const std::string &)>;
    // current: the camera now (a blend starts from it, or from the driver in charge).
    void path(Track, double blendSeconds, const Pose &current);
    void follow(std::string subject, FollowParams, double blendSeconds, const Pose &current);
    void stop() { *this = {}; }
    std::optional<Pose> tick(double dt, const Lookup &, float fov, const std::function<Vec3(Vec3, Vec3)> &clip = {});

    Mode mode() const { return now.mode; }
    bool active() const { return now.mode != Mode::Idle; }
    // The camera still changes by itself (frames must keep coming).
    bool moving() const { return blending() || now.mode == Mode::Follow || (now.mode == Mode::Path && !done()); }
    bool done() const { return now.mode == Mode::Path && now.time >= now.track->duration(); }
    bool blending() const { return blendTime < blendLength; }
    double time() const { return now.time; }
    double duration() const { return now.track ? now.track->duration() : 0; }
    const std::string &subject() const { return now.subject; }
    bool lost() const { return now.lost; }
    std::optional<Pose> pose() const { return last; }

  private:
    struct Driver {
        Mode mode = Mode::Idle;
        std::optional<Track> track;
        double time = 0;
        std::string subject;
        std::optional<Follow> follow;
        Pose hold;
        bool lost = false;
        Pose evaluate(double dt, const Lookup &, float fov, const std::function<Vec3(Vec3, Vec3)> &clip);
    };
    Driver now, before;
    double blendTime = 0, blendLength = 0;
    std::optional<Pose> last;
    Pose handOver(double blendSeconds, const Pose &current); // returns the pose handed over
};
} // namespace hyprune::sim::cinema
