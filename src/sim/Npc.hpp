#pragma once
// Ambient NPC agent (RFC-0010 runtime rules; RFC-0011 routes, stops, seats and interactions).
// Follows a route by arc length at the walk clip's measured speed, stops where the route has
// stops (a clip, a facing, a seat), sits at seat anchors, glances at a nearby player, steps
// aside instead of blocking, faces the player during an interaction and walks a guide path.
// Pure logic: no GL, no Hyprland. The runtime poses the character from `pose` and draws it at
// `model()`; the same numbers travel to the offload worker.
#include "Route.hpp"
#include "Steer.hpp"
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace hyprune::sim {
struct NpcSeat { // world space, resolved by the runtime
    std::string ref;
    Vec3 position{};  // where the sitter's root stands at the start of `sit`
    float yaw = 0;    // radians; facing (sin yaw, 0, cos yaw)
    float height = .45f, approach = .6f;
    Vec3 forward() const { return {std::sin(yaw), 0, std::cos(yaw)}; }
};
struct NpcStop {
    float at = 0;       // arc length of the stop's point
    int point = 0;
    float seconds = 3;
    std::string clip;   // look_around, wave, nod, idle_fidget ("" none)
    std::optional<Vec3> face;
    int seat = -1;      // NpcAgent::seats index
    // Where to stand for a stop that faces a screen: in front of it on its normal, outside its
    // approach zone (the runtime checked the legs there and back). None: on the route point.
    std::optional<Vec3> spot;
};
// A way round something on the route (a placed object, collision): from the route at arc
// length `enter` along `legs` (the last one is the route at `exit`, enter < exit, both
// unwrapped forward arc lengths). Walking the route backwards runs it in reverse.
struct NpcDetour {
    float enter = 0, exit = 0;
    std::vector<Vec3> legs;
};
// What the agent sees each tick.
struct NpcWorld {
    Vec3 player{};          // the player's eye
    bool present = false;   // exploring this world: glance and step aside apply
    bool glance = true;     // false while the player reads a window
    // Walkable root position (floor, capsule, playable): sidesteps and legs. Null: walkable.
    std::function<bool(Vec3)> walkable;
    // Every character as of the start of the tick (steering); null: none.
    const std::vector<Mover> *movers = nullptr;
};
// What posing needs; also the offload packet. Times are clip seconds.
struct NpcPose {
    std::string clip = "idle", previous;
    float clipTime = 0, previousTime = 0, fade = 1; // fade 0 -> 1 over 0.25 s from previous to clip
    bool loop = true, previousLoop = true;
    float lookYaw = 0, lookPitch = 0;              // radians, character space; + = left / up
};
struct NpcAgent {
    // ---- configuration ----
    Route route;                     // empty: a standing spot (home)
    std::vector<NpcStop> stops;
    std::vector<NpcDetour> detours;  // ordered by enter, not overlapping
    int id = -1, priority = 0;       // its Mover id and priority (sim/Steer.hpp)
    std::vector<NpcSeat> seats;
    int homeSeat = -1;               // a standing character's permanent seat
    std::map<std::string, float> clips; // name -> duration (seconds); presence = the character has it
    float walkSpeed = 1.3f, runSpeed = 0; // measured clip speeds (m/s)
    // The sit clip's fit (Character::measureSeat or object.json): seated, the root rises by
    // anchor height - seatHeight; the clip's own ankle sink is what that lift must at least
    // undo (feet clearance = lift - sink, kept within [-0.03, 0.45] m or the stop stays plain).
    bool seatKnown = false;
    float seatHeight = 0, seatSink = 0;
    // The travel the import step removed from `sit` (its extras.speed x duration): sitting
    // moves the root back by it from the anchor, so the hips land on the seat.
    float sitTravel = 0;
    float speed = 0;                 // route speed, m/s (0: walkSpeed)
    float rate = 1;                  // instance speed: scales pace and clip rate
    bool reverse = false;
    float phase = 0;
    float height = 1.75f, radius = .35f;
    float personalSpace = 1.2f, fidgetEvery = 45, fidgetRange = 4, glanceRange = 4;
    Vec3 home{};
    float homeHeading = 0;
    // Seat reservation (capacity is the runtime's): false refuses the anchor.
    std::function<bool(int)> reserveSeat;
    std::function<void(int)> releaseSeat;
    // ---- state ----
    enum class Mode { Idle, Walk, Stop, ToSeat, Align, Sit, Seated, Stand, FromSeat, Interact, Guide, Suspended, ToSpot, Detour };
    Mode mode = Mode::Idle;
    Vec3 position{};
    float heading = 0;               // radians; forward = (sin h, 0, cos h) (the character's +Z)
    float s = 0;                     // arc length on the route
    int direction = 1;
    float lateral = 0, lateralTarget = 0; // sidestep offset (m), + = right of travel
    bool yielding = false;
    NpcPose pose;
    float clipRate = 1, previousRate = 1;
    float seatOffset = 0, seatOffsetFrom = 0, seatOffsetTo = 0; // root height correction while seated
    float stopLeft = 0, sinceFidget = 0;
    int stop = -1, seat = -1;        // current stop and reserved seat
    std::optional<Vec3> faceTarget;
    std::vector<Vec3> legs;          // straight legs (seat approach and return, guide join)
    Vec3 alignFrom{};
    float alignHeading = 0, alignTime = 0;
    // Interaction and guide.
    Mode resume = Mode::Walk;        // mode an interaction returns to
    std::optional<Vec3> interactWith;
    Route guide;
    float guideS = 0, guideIdle = 0, guideBest = 0;
    int guidePhase = 0; // -1 standing up, 0 joining, 1 out, 2 waving, 3 back along, 4 returning
    Vec3 guideFrom{};
    int guideSeat = -1; // the seat a guide stood up from; it sits there again after
    bool guideCancelled = false;
    // Locomotion: the ground speed now (eased towards the wanted one), the gait cycle's phase
    // (0-1, shared by walk and run so a blend keeps the feet in step) and the velocity over the
    // last tick (what the others see).
    float speedNow = 0, gaitPhase = 0;
    Vec3 velocity{};
    // Steering: the motion chosen last tick, how long a mover has held it up, and a back-off
    // (walking its route the other way to let a higher-priority character through).
    float steerLateral = 0, steerFactor = 1, blockedFor = 0, backoffLeft = 0;
    int blockedBy = -1, backoffFrom = 0, detour = -1;
    static constexpr float turnRate = 3.1416f, alignSeconds = .25f, fadeSeconds = .25f, accel = 2.5f, decel = 4.f,
                           maxRunRate = 1.8f, minWalkRate = .3f;
    // Root lift for an anchor, if the character fits it.
    std::optional<float> seatLift(const NpcSeat &h) const {
        if (!seatKnown || !has("sit"))
            return std::nullopt;
        float lift = h.height - seatHeight, clearance = lift - seatSink;
        if (clearance < -.03f || clearance > .45f)
            return std::nullopt;
        return lift;
    }

    bool has(const std::string &clip) const { return clips.contains(clip); }
    float duration(const std::string &clip) const {
        auto it = clips.find(clip);
        return it == clips.end() ? 0 : it->second;
    }
    Vec3 forward() const { return {std::sin(heading), 0, std::cos(heading)}; }
    float pace() const { return (speed > 0 ? speed : walkSpeed) * std::max(.05f, rate); }
    bool running() const { return runSpeed > 0 && pace() > 1.25f * walkSpeed; }
    std::string activity() const {
        switch (mode) {
        case Mode::Walk: return yielding ? "stopped" : running() ? "running" : "walking";
        case Mode::Stop: return "stopped";
        case Mode::ToSeat:
        case Mode::FromSeat:
        case Mode::ToSpot:
        case Mode::Detour: return "walking";
        case Mode::Align:
        case Mode::Sit:
        case Mode::Seated:
        case Mode::Stand: return "sitting";
        case Mode::Interact: return "interacting";
        case Mode::Guide: return "guiding";
        case Mode::Suspended: return "suspended";
        default: return "idle";
        }
    }
    bool seated() const { return mode == Mode::Sit || mode == Mode::Seated || mode == Mode::Stand || mode == Mode::Align; }
    std::string seatRef() const { return seat >= 0 && seat < int(seats.size()) && seated() ? seats[seat].ref : ""; }

    void play(const std::string &name, bool loop = true, float rate = 1, float at = 0) {
        if (name == pose.clip && loop == pose.loop && loop) {
            clipRate = rate;
            return;
        }
        pose.previous = pose.clip, pose.previousTime = pose.clipTime, pose.previousLoop = pose.loop;
        previousRate = clipRate;
        pose.clip = name, pose.clipTime = at, pose.loop = loop, pose.fade = 0;
        clipRate = rate;
    }
    bool clipDone() const {
        if (pose.loop)
            return false;
        float d = duration(pose.clip);
        return clipRate >= 0 ? pose.clipTime >= d - 1e-3f : pose.clipTime <= 1e-3f;
    }
    void idle() { play("idle"); }

    void start() {
        lateral = lateralTarget = 0;
        speedNow = gaitPhase = 0, velocity = {};
        steerLateral = 0, steerFactor = 1, blockedFor = backoffLeft = 0, blockedBy = detour = -1;
        seatOffset = 0;
        legs.clear();
        stop = seat = -1;
        if (homeSeat >= 0 && homeSeat < int(seats.size()) && seatLift(seats[homeSeat]) && has("sit_idle")) {
            auto &h = seats[homeSeat];
            seat = homeSeat;
            heading = h.yaw;
            sitBack(1);
            seatOffset = seatOffsetTo = seatLift(h).value_or(0);
            mode = Mode::Seated;
            pose = {};
            pose.clip = has("sit_idle") ? "sit_idle" : "idle";
            return;
        }
        if (route.empty()) {
            position = home, heading = homeHeading, mode = Mode::Idle;
            pose = {};
            return;
        }
        direction = reverse ? -1 : 1;
        s = route.wrap(phase * route.length());
        if (int d = detourAt(s); d >= 0) // starting inside a blocked stretch: past it
            s = route.closed ? route.wrap(detours[d].exit) : std::min(detours[d].exit, route.length());
        position = route.at(s);
        Vec3 t = route.tangent(s) * float(direction);
        heading = std::atan2(t.x, t.z);
        mode = Mode::Walk;
        pose = {};
    }
    // A route that stopped being walkable: idle where it stands (RFC-0011).
    void suspend() {
        if (mode == Mode::Suspended)
            return;
        if (seat >= 0 && seat != homeSeat && releaseSeat)
            releaseSeat(seat);
        if (seat != homeSeat)
            seat = -1;
        seatOffset = 0;
        mode = Mode::Suspended;
        legs.clear();
        idle();
    }

    // The pose model: root at position (+ the seat offset), turned by heading.
    Mat4 model() const {
        Mat4 m = Mat4::identity();
        float c = std::cos(heading), sn = std::sin(heading);
        m.v[0] = c, m.v[2] = -sn, m.v[8] = sn, m.v[10] = c;
        m.v[12] = position.x, m.v[13] = position.y + seatOffset, m.v[14] = position.z;
        return m;
    }

    // ---- interactions ----
    bool canInteract() const {
        return mode == Mode::Walk || mode == Mode::Stop || mode == Mode::Idle || mode == Mode::Seated || mode == Mode::Suspended;
    }
    void beginInteract(Vec3 player) {
        if (mode != Mode::Interact)
            resume = mode;
        interactWith = player;
        if (resume == Mode::Seated)
            return; // a seated character turns its head, not its body
        mode = Mode::Interact;
        if (has("nod"))
            play("nod", false);
        else
            idle();
    }
    void endInteract() {
        interactWith.reset();
        if (mode == Mode::Interact)
            mode = resume == Mode::Interact ? Mode::Walk : resume;
        if (mode == Mode::Walk && route.empty())
            mode = Mode::Idle;
    }
    // Where a guide's join leg starts: a seated character first stands and steps off its seat
    // to the seat's approach point (the runtime checks the leg from there).
    Vec3 guideStart() const {
        if (mode == Mode::Seated && seat >= 0 && seat < int(seats.size()))
            return seats[seat].position + seats[seat].forward() * seats[seat].approach;
        return position;
    }
    // Guide: leave the route, walk to the guide path's start and along it ahead of the player,
    // wave at the end, walk it back and return to where it left. The runtime checked the legs.
    // From a seat: stand up (the stand clip), step off to the approach point, guide, then walk
    // back there and sit down again (the seat stays reserved).
    void beginGuide(Route path) {
        if (path.empty())
            return;
        endInteract();
        guide = std::move(path);
        guideS = 0, guideIdle = 0, guideBest = 0, guidePhase = 0, guideCancelled = false;
        guideSeat = -1;
        if (mode == Mode::Seated && seat >= 0 && seat < int(seats.size())) {
            guideSeat = seat;
            guideFrom = guideStart();
            guidePhase = -1;
            seatOffsetFrom = seatOffset;
            if (has("stand"))
                play("stand", false);
            else
                play("sit", false, -1, duration("sit")); // sit reversed
        } else {
            guideFrom = position;
            legs = {guide.at(0)};
        }
        resume = mode == Mode::Guide ? resume : mode;
        mode = Mode::Guide;
    }
    void cancelGuide() { guideCancelled = true; }

    // ---- per tick ----
    // Clip times, the crossfade and the glance; update() does this first. A driver that moves
    // the agent itself (the companion's follow mode) calls it alone.
    void advance(float dt, const NpcWorld &w) {
        pose.fade = std::min(1.f, pose.fade + dt / fadeSeconds);
        pose.previousTime += dt * previousRate;
        pose.clipTime += dt * clipRate;
        if (!pose.loop)
            pose.clipTime = std::clamp(pose.clipTime, 0.f, duration(pose.clip));
        sinceFidget += dt;
        look(dt, w);
    }
    void update(float dt, const NpcWorld &w) {
        if (dt <= 0)
            return;
        dt = std::min(dt, .1f);
        Vec3 before = position;
        step(dt, w);
        velocity = Vec3{position.x - before.x, 0, position.z - before.z} / dt;
    }
    // The fastest it goes: the run clip played at most maxRunRate (feet still in step).
    float topSpeed() const { return (runSpeed > walkSpeed ? runSpeed : walkSpeed) * maxRunRate; }
    // Locomotion at ground speed v: walk below the walk clip's speed (slower steps), a walk-run
    // blend in between and run above the run clip's speed (faster steps, up to maxRunRate).
    // The two clips share one cycle phase advanced by v over the blended stride, so the planted
    // foot moves with the ground whatever the speed: no sliding when speeding up or slowing down.
    void gait(float v, float dt) {
        float wv = std::max(.1f, walkSpeed);
        bool locomoting = pose.clip == "walk" || pose.clip == "run";
        bool crossfading = pose.fade < 1 && pose.previous != "walk" && pose.previous != "run";
        if (v < .05f) {
            idle();
            return;
        }
        bool blend = has("run") && runSpeed > wv + .05f;
        float t = blend ? std::clamp((v - wv) / (runSpeed - wv), 0.f, 1.f) : 0.f;
        float walkCycle = std::max(.1f, duration("walk")), runCycle = blend ? std::max(.1f, duration("run")) : walkCycle;
        if (!locomoting || crossfading) { // from idle, a stop or a seat: the usual crossfade
            if (!locomoting)
                gaitPhase = 0;
            play(t > .5f ? "run" : "walk", true, t > .5f ? v / runSpeed : std::max(minWalkRate, v / wv));
            return;
        }
        float stride = (1 - t) * wv * walkCycle + t * (blend ? runSpeed * runCycle : wv * walkCycle);
        if (pose.fade >= 1 && pose.clipTime > 0 && gaitPhase == 0) // joined from a plain clip: keep its phase
            gaitPhase = std::fmod(pose.clipTime / (pose.clip == "run" ? runCycle : walkCycle), 1.f);
        gaitPhase = std::fmod(gaitPhase + v * dt / std::max(.05f, stride), 1.f);
        if (t <= 0 || t >= 1) {
            auto clip = t >= 1 ? "run" : "walk";
            if (pose.clip != clip) // past the blend: settle on one clip, from the other in step
                pose.previous = pose.clip, pose.previousLoop = true, pose.fade = 0;
            pose.clip = clip, pose.loop = true;
            pose.fade = std::max(pose.fade, pose.previous == "walk" || pose.previous == "run" ? 1.f : pose.fade);
        } else {
            pose.previous = "walk", pose.clip = "run", pose.loop = pose.previousLoop = true;
            pose.fade = t;
        }
        pose.previousTime = gaitPhase * (pose.previous == "run" ? runCycle : walkCycle);
        pose.clipTime = gaitPhase * (pose.clip == "run" ? runCycle : walkCycle);
        clipRate = previousRate = 0; // the phase drives both
    }
    // Eases the ground speed towards a wanted one (no instant starts or stops).
    float ease(float wanted, float dt) {
        float d = wanted - speedNow;
        speedNow += std::clamp(d, -decel * dt, accel * dt);
        if (speedNow < .02f && wanted <= 0)
            speedNow = 0;
        return speedNow;
    }
    void step(float dt, const NpcWorld &w) {
        advance(dt, w);
        switch (mode) {
        case Mode::Idle: standIdle(dt, w); break;
        case Mode::Walk: walk(dt, w); break;
        case Mode::Stop: stopTick(dt, w); break;
        case Mode::ToSeat: toSeat(dt, w); break;
        case Mode::Align: align(dt); break;
        case Mode::Sit: sitTick(dt); break;
        case Mode::Seated: seatedTick(dt); break;
        case Mode::Stand: standTick(dt); break;
        case Mode::FromSeat: fromSeat(dt, w); break;
        case Mode::Interact: interactTick(dt); break;
        case Mode::Guide: guideTick(dt, w); break;
        case Mode::Suspended: idle(); break;
        case Mode::ToSpot: toSpot(dt); break;
        case Mode::Detour: detourTick(dt, w); break;
        }
        if (mode != Mode::Walk && mode != Mode::Detour && mode != Mode::Guide && mode != Mode::ToSpot &&
            mode != Mode::ToSeat && mode != Mode::FromSeat)
            speedNow = 0;
    }

    // Turns towards a heading at most `rate` rad/s; true when within tolerance.
    bool turnTowards(float target, float dt, float rate = turnRate, float tolerance = .05f) { return turn(target, dt, tolerance, rate); }

    // The detour a route position falls inside (enter < s < exit, wrapped), or -1.
    int detourAt(float at) const {
        float l = route.length();
        for (size_t i = 0; i < detours.size(); ++i) {
            float a = detours[i].enter, b = detours[i].exit;
            float u = route.closed && at < a ? at + l : at;
            if (u > a && u < b)
                return int(i);
        }
        return -1;
    }
  private:
    float eye() const { return height * .93f; }
    static float angleTo(float from, float to) { return std::remainder(to - from, 6.2831853f); }
    // Turn towards a heading at most turnRate; true when within tolerance.
    bool turn(float target, float dt, float tolerance = .05f, float rateLimit = turnRate) {
        float diff = angleTo(heading, target);
        heading += std::copysign(std::min(std::abs(diff), rateLimit * dt), diff);
        return std::abs(angleTo(heading, target)) <= tolerance;
    }
    // Head look-at: towards the player's eye within glanceRange, in front, clamped and eased.
    void look(float dt, const NpcWorld &w) {
        float yaw = 0, pitch = 0;
        Vec3 head = position + Vec3{0, eye() + seatOffset, 0};
        Vec3 to = (interactWith ? *interactWith : w.player) - head;
        float flat = std::hypot(to.x, to.z);
        bool watching = interactWith.has_value() || (w.present && w.glance && flat < glanceRange);
        if (watching && mode != Mode::Suspended && flat > .05f) {
            float rel = angleTo(heading, std::atan2(to.x, to.z));
            if (std::abs(rel) < 1.92f) { // 110 degrees: in front
                yaw = std::clamp(rel, -.785f, .785f);
                pitch = std::clamp(std::atan2(to.y, flat), -.35f, .35f);
            }
        }
        float k = 1 - std::exp(-dt / .12f); // ~0.35 s to settle
        pose.lookYaw += (yaw - pose.lookYaw) * k;
        pose.lookPitch += (pitch - pose.lookPitch) * k;
    }
    // Idle at a spot: the calm loop, now and then the fidget when the player is near.
    void idleTick(const NpcWorld &w) {
        if (pose.clip == "idle_fidget" && !clipDone())
            return;
        if (!pose.loop && !clipDone())
            return;
        Vec3 near = w.player - position;
        if (has("idle_fidget") && w.present && sinceFidget >= fidgetEvery && std::hypot(near.x, near.z) < fidgetRange) {
            play("idle_fidget", false);
            sinceFidget = 0;
            stopLeft = std::max(stopLeft, duration("idle_fidget"));
            return;
        }
        idle();
    }
    void standIdle(float, const NpcWorld &w) {
        if (faceTarget)
            faceTarget.reset();
        idleTick(w);
    }
    // Personal space: a player on the way ahead. The lateral offset to walk at (0: the route's
    // line), or nullopt to wait (no walkable side clears the player).
    std::optional<float> sidestep(const NpcWorld &w, Vec3 travel) const {
        if (!w.present || std::abs(w.player.y - eye() - position.y) > 1.5f)
            return 0.f;
        Vec3 right{travel.z, 0, -travel.x};
        Vec3 d = w.player - position;
        d.y = 0;
        float ahead = dot(d, travel), side = dot(d, right) + lateral; // side: from the line
        constexpr float clearance = .85f, widest = 1.f;
        if (ahead < -.3f || ahead > personalSpace + 1.f || std::abs(side) >= clearance)
            return 0.f;
        if (lateral != 0 && std::abs(side - lateral) >= clearance - .05f)
            return lateral; // already clear of the player: hold until they are behind
        // Keep the side already chosen (a player right on the line would flip it every tick).
        int away = lateralTarget != 0 ? (lateralTarget > 0 ? 1 : -1) : side >= 0 ? -1 : 1;
        for (int choice : {away, -away}) {
            float target = side + choice * clearance;
            if (std::abs(target) > widest)
                continue;
            bool ok = true;
            for (float a : {.4f, .9f, 1.5f})
                if (w.walkable && !w.walkable(route.at(s + direction * a) + right * target))
                    ok = false;
            if (ok)
                return target;
        }
        return std::nullopt;
    }
    struct SteerChoice {
        float lateral = 0, factor = 1;
        Conflict conflict; // of the chosen motion at its speed
        Conflict ahead;    // of going on at full speed at the chosen offset (who holds it up)
    };
    // The motion that keeps clear of the other characters: a lateral offset from the route's
    // line (walkable ahead) and a speed factor, scored over the next two seconds. `fixed`: the
    // player's sidestep already chose the offset; only the speed is free.
    SteerChoice steer(const NpcWorld &w, Vec3 travel, std::optional<float> fixed, float v0) {
        SteerChoice none{fixed.value_or(0.f), 1, {}};
        if (!w.movers || !anyNear(*w.movers, id, position))
            return none;
        std::vector<float> laterals;
        if (fixed)
            laterals = {*fixed};
        else
            for (float l : {steerLateral, 0.f, -.45f, .45f, -.9f, .9f})
                if (std::find(laterals.begin(), laterals.end(), l) == laterals.end())
                    laterals.push_back(l);
        Vec3 right{travel.z, 0, -travel.x};
        float l = route.length();
        auto arc = [&](float d) { return route.closed ? route.wrap(d) : std::clamp(d, 0.f, l); };
        bool backing = backoffLeft > 0;
        float best = 1e30f, kept = 1e30f;
        SteerChoice out = none;
        for (float lat : laterals) {
            if (lat != 0 && !fixed && w.walkable) { // room to the side, here and a little way ahead
                bool ok = true;
                for (float a : {0.f, .5f, 1.2f, 2.f})
                    if (!w.walkable(route.at(arc(s + direction * a)) + right * lat))
                        ok = false;
                if (!ok)
                    continue;
            }
            for (float f : {1.f, .5f, 0.f}) {
                auto own = [&](float time) {
                    float at = arc(s + direction * v0 * f * time);
                    Vec3 tt = route.tangent(at) * float(direction);
                    float side = lateral + std::clamp(lat - lateral, -time, time);
                    return route.at(at) + Vec3{tt.z, 0, -tt.x} * side;
                };
                auto c = predict(own, radius, *w.movers, id);
                bool lower = c.any() && (*w.movers)[c.mover].priority > priority;
                // Off the line costs a little (keep right: the negative side), slowing more
                // (less for the lower priority, who gives way); backing off, it wants to stand aside.
                float cost = conflictCost(c) + .6f * std::abs(lat) + (backing ? 0.f : (1 - f) * (lower ? .4f : 1.2f)) +
                             .3f * std::abs(lat - steerLateral) + (lat > 0 ? .08f : 0.f);
                if (backing) // stand aside and wait there once there is room; along the line, keep going
                    cost += std::abs(lat) < .4f ? 1.f : -.4f + f * .8f;
                if (cost < best)
                    best = cost, out = {lat, f, c};
                if (lat == steerLateral && f == steerFactor)
                    kept = cost;
            }
        }
        { // who would be in the way at full speed
            float lat = out.lateral;
            out.ahead = predict(
                [&](float time) {
                    float at = arc(s + direction * v0 * time);
                    Vec3 tt = route.tangent(at) * float(direction);
                    return route.at(at) + Vec3{tt.z, 0, -tt.x} * (lateral + std::clamp(lat - lateral, -time, time));
                },
                radius, *w.movers, id);
        }
        if (kept <= best + .25f && kept < 1e29f) { // hysteresis: keep last tick's choice unless clearly worse
            for (float lat : {steerLateral})
                if (std::find(laterals.begin(), laterals.end(), lat) != laterals.end()) {
                    auto own = [&](float time) {
                        float at = arc(s + direction * v0 * steerFactor * time);
                        Vec3 tt = route.tangent(at) * float(direction);
                        return route.at(at) + Vec3{tt.z, 0, -tt.x} * (lateral + std::clamp(lat - lateral, -time, time));
                    };
                    out = {lat, steerFactor, predict(own, radius, *w.movers, id), out.ahead};
                }
        }
        return out;
    }
    // Held up by a mover: after a while the lower priority (or, much later, either) walks its
    // route the other way until the other has passed; the delay differs per character.
    void yieldRule(const SteerChoice &c, const NpcWorld &w, float dt, Vec3 travel) {
        if (backoffLeft > 0) {
            backoffLeft -= dt;
            const Mover *b = nullptr;
            for (auto &m : *w.movers)
                if (m.id == blockedBy)
                    b = &m;
            // The original travel is the reverse of now: passed once it is behind on it.
            bool passed = !b || dot(b->position - position, travel * -1.f) < -(radius + b->radius + .2f) ||
                          flatDistance(b->position, position) > 5;
            if (passed || backoffLeft <= 0) {
                backoffLeft = 0, blockedFor = 0, blockedBy = -1;
                if (direction != backoffFrom)
                    turnBack();
            }
            return;
        }
        const Mover *b = c.ahead.any() ? &(*w.movers)[c.ahead.mover] : nullptr;
        bool stuck = c.factor == 0 && speedNow < .05f && b && flatSpeed(b->velocity) < .2f &&
                     flatDistance(b->position, position) < radius + b->radius + 2.5f && dot(b->position - position, travel) > 0;
        if (!stuck) {
            blockedFor = std::max(0.f, blockedFor - dt);
            return;
        }
        blockedFor += dt;
        blockedBy = b->id;
        float after = b->priority > priority ? 1.f : 3.f + .37f * float(((id % 8) + 8) % 8);
        if (blockedFor > after) {
            backoffFrom = direction;
            turnBack();
            backoffLeft = 10;
            steerFactor = 1;
        }
    }
    // Walk the route the other way: offsets from the line keep their side in the world.
    void turnBack() {
        direction = -direction;
        lateral = -lateral, lateralTarget = -lateralTarget, steerLateral = -steerLateral;
    }
    void walk(float dt, const NpcWorld &w) {
        if (route.empty()) {
            mode = Mode::Idle;
            return;
        }
        Vec3 travel = route.tangent(s) * float(direction);
        float desired = std::atan2(travel.x, travel.z);
        float diff = angleTo(heading, desired);
        if (std::abs(diff) > 1.f) { // a sharp corner or turning back: turn in place before walking on
            turn(desired, dt);
            speedNow = 0;
            idle();
            return;
        }
        heading += std::copysign(std::min(std::abs(diff), turnRate * dt), diff);
        // The player first (never walked through), then the other characters.
        auto side = sidestep(w, travel);
        bool playerAside = side && *side != 0;
        float base = pace() * (std::abs(diff) > .35f ? .6f : 1.f);
        float factor = 1;
        if (side) {
            auto choice = steer(w, travel, playerAside ? side : std::nullopt, base);
            if (!playerAside)
                side = choice.lateral;
            factor = choice.factor;
            steerLateral = choice.lateral, steerFactor = choice.factor;
            if (w.movers)
                yieldRule(choice, w, dt, travel);
        }
        yielding = !side || factor <= 0;
        if (side)
            lateralTarget = *side;
        // Slower round corners, and while still stepping out of the way.
        float v = ease(yielding ? 0.f : base * factor * (std::abs(lateralTarget - lateral) > .3f ? .5f : 1.f), dt);
        lateral += std::clamp(lateralTarget - lateral, -dt, dt); // 1 m/s sideways, standing too
        if (v <= 0) {
            place();
            idle();
            return;
        }
        float step = v * dt;
        // The next stop or detour on the way (exactly at it: leaving it).
        float l = route.length();
        int hit = -1, hitDetour = -1;
        float hitAt = 1e30f;
        auto ahead = [&](float at) {
            float d = (at - s) * float(direction);
            if (route.closed)
                d = std::fmod(std::fmod(d, l) + l, l);
            return d;
        };
        for (size_t i = 0; i < stops.size(); ++i)
            if (float d = ahead(stops[i].at); d > 1e-5f && d <= step + 1e-4f && d < hitAt)
                hit = int(i), hitAt = d;
        for (size_t i = 0; i < detours.size(); ++i) {
            float entry = direction > 0 ? detours[i].enter : detours[i].exit;
            if (float d = ahead(route.closed ? route.wrap(entry) : entry); d > 1e-5f && d <= step + 1e-4f && d < hitAt)
                hit = -1, hitDetour = int(i), hitAt = d;
        }
        Vec3 before = position;
        if (hitDetour >= 0) {
            enterDetour(hitDetour);
            return;
        }
        if (hit >= 0) {
            s = stops[hit].at;
            place();
            enterStop(hit, w);
        } else {
            float next = s + direction * step;
            if (!route.closed && (next < 0 || next > l)) {
                s = std::clamp(next, 0.f, l);
                turnBack(); // open routes: to the end and back
                if (backoffLeft > 0)
                    backoffFrom = -backoffFrom;
            } else
                s = route.wrap(next);
            place();
        }
        float moved = std::hypot(position.x - before.x, position.z - before.z) / dt;
        if (mode != Mode::Walk)
            return;
        gait(moved, dt);
    }
    // ---- detours ----
    void enterDetour(int index) {
        auto &d = detours[index];
        detour = index;
        float entry = direction > 0 ? d.enter : d.exit;
        s = route.closed ? route.wrap(entry) : entry;
        lateral = lateralTarget = 0;
        place();
        if (direction > 0)
            legs = d.legs;
        else { // the same way round, backwards: to the enter point
            legs.assign(d.legs.rbegin() + 1, d.legs.rend());
            legs.push_back(route.at(route.closed ? route.wrap(d.enter) : d.enter));
        }
        mode = Mode::Detour;
    }
    void detourTick(float dt, const NpcWorld &w) {
        // Others still count: it slows or waits for someone in the way.
        float factor = 1;
        if (w.movers && !legs.empty() && anyNear(*w.movers, id, position)) {
            Vec3 to = legs.front() - position;
            float dist = std::max(1e-3f, std::hypot(to.x, to.z));
            Vec3 dir{to.x / dist, 0, to.z / dist};
            float best = 1e30f;
            for (float f : {1.f, .5f, 0.f}) {
                auto c = predict([&](float time) { return position + dir * (pace() * f * time); }, radius, *w.movers, id);
                float cost = conflictCost(c) + (1 - f);
                if (cost < best)
                    best = cost, factor = f;
            }
        }
        if (!walkLegs(dt, pace() * factor))
            return;
        auto &d = detours[detour];
        float exit = direction > 0 ? d.exit : d.enter;
        s = route.closed ? route.wrap(exit) : std::clamp(exit, 0.f, route.length());
        detour = -1;
        place();
        mode = Mode::Walk;
    }
    void place() {
        Vec3 t = route.tangent(s) * float(direction);
        Vec3 right{t.z, 0, -t.x};
        position = route.at(s) + right * lateral;
    }
    void enterStop(int index, const NpcWorld &w) {
        auto &st = stops[index];
        stop = index;
        stopLeft = st.seconds;
        faceTarget = st.face;
        lateral = lateralTarget = 0;
        place();
        if (st.seat >= 0 && st.seat < int(seats.size()) && has("sit") && has("sit_idle")) {
            auto &h = seats[st.seat];
            auto lift = seatLift(h);
            if (lift && (!reserveSeat || reserveSeat(st.seat))) {
                seat = st.seat;
                seatOffsetTo = *lift;
                legs = {h.position + h.forward() * h.approach, h.position};
                mode = Mode::ToSeat;
                return;
            }
        }
        if (st.spot) { // facing a screen: in front of it first
            legs = {*st.spot};
            mode = Mode::ToSpot;
            return;
        }
        beginStop();
    }
    void beginStop() {
        auto &st = stops[stop];
        mode = Mode::Stop;
        if (!st.clip.empty() && has(st.clip))
            play(st.clip, false);
        else
            idle();
    }
    void toSpot(float dt) {
        if (walkLegs(dt, walkSpeed * .8f))
            beginStop();
    }
    void stopTick(float dt, const NpcWorld &w) {
        if (faceTarget) {
            Vec3 d = *faceTarget - position;
            if (std::hypot(d.x, d.z) > .05f)
                turn(std::atan2(d.x, d.z), dt, .05f, turnRate * .6f);
        }
        if (!pose.loop && !clipDone()) {
            stopLeft -= dt;
            return;
        }
        idleTick(w);
        stopLeft -= dt;
        if (stopLeft <= 0 && (pose.loop || clipDone())) {
            faceTarget.reset();
            if (stop >= 0 && stop < int(stops.size()) && stops[stop].spot) { // back to the route point
                legs = {route.at(s)};
                mode = Mode::FromSeat;
            } else
                mode = Mode::Walk;
        }
    }
    // Walk the straight legs at a careful pace (eased, braking for the last); true when the last
    // one is reached. v 0: wait where it is.
    bool walkLegs(float dt, float v, bool lastIsExact = true) {
        while (!legs.empty()) {
            Vec3 d = legs.front() - position;
            d.y = 0;
            float dist = std::hypot(d.x, d.z);
            if (dist < .03f || (!lastIsExact && legs.size() > 1 && dist < .15f)) {
                position.x = legs.front().x, position.z = legs.front().z;
                position.y = legs.front().y;
                legs.erase(legs.begin());
                continue;
            }
            float target = std::atan2(d.x, d.z);
            if (std::abs(angleTo(heading, target)) > .9f) {
                turn(target, dt);
                speedNow = 0;
                idle();
                return false;
            }
            heading += std::clamp(angleTo(heading, target), -turnRate * dt, turnRate * dt);
            float remaining = dist;
            for (size_t i = 1; i < legs.size(); ++i)
                remaining += std::hypot(legs[i].x - legs[i - 1].x, legs[i].z - legs[i - 1].z);
            float sp = ease(std::min(v, std::sqrt(2 * decel * .8f * remaining) + .05f) * (v > 0 ? 1.f : 0.f), dt);
            if (sp <= 0) {
                idle();
                return false;
            }
            float step = std::min(dist, sp * dt);
            Vec3 dir = d / dist;
            float y0 = position.y;
            position = position + dir * step;
            position.y = y0 + (legs.front().y - y0) * std::min(1.f, step / std::max(dist, 1e-3f));
            gait(sp, dt);
            return false;
        }
        return true;
    }
    void toSeat(float dt, const NpcWorld &) {
        if (!walkLegs(dt, walkSpeed * .8f))
            return;
        auto &h = seats[seat];
        alignFrom = position, alignHeading = heading, alignTime = 0;
        Vec3 d = h.position - position;
        if (std::hypot(d.x, d.z) > .15f) { // too far off: a plain stop instead
            if (releaseSeat)
                releaseSeat(seat);
            seat = -1;
            mode = Mode::Stop;
            idle();
            return;
        }
        mode = Mode::Align;
        idle();
    }
    void align(float dt) {
        auto &h = seats[seat];
        alignTime += dt;
        float u = std::min(1.f, alignTime / alignSeconds);
        // The approach ends facing away from the seat's front: turn round to its yaw.
        float target = h.yaw;
        heading = alignHeading + angleTo(alignHeading, target) * u;
        position = alignFrom + (h.position - alignFrom) * u;
        if (u >= 1) {
            mode = Mode::Sit;
            seatOffsetFrom = 0;
            play("sit", false);
        }
    }
    // The root while sitting: back from the anchor by the sit clip's travel, u of the way.
    void sitBack(float u) {
        auto &h = seats[seat];
        position = h.position - h.forward() * (sitTravel * std::clamp(u, 0.f, 1.f));
    }
    void sitTick(float) {
        float d = std::max(1e-3f, duration("sit"));
        float u = std::clamp(pose.clipTime / d, 0.f, 1.f);
        seatOffset = seatOffsetFrom + (seatOffsetTo - seatOffsetFrom) * u;
        sitBack(u);
        if (clipDone()) {
            seatOffset = seatOffsetTo;
            mode = Mode::Seated;
            play("sit_idle");
        }
    }
    void seatedTick(float dt) {
        if (homeSeat >= 0 && homeSeat == seat)
            return; // a permanent seat
        stopLeft -= dt;
        if (stopLeft <= 0) {
            mode = Mode::Stand;
            seatOffsetFrom = seatOffset;
            if (has("stand"))
                play("stand", false);
            else
                play("sit", false, -1, duration("sit")); // sit reversed
        }
    }
    void standTick(float) {
        float d = std::max(1e-3f, duration(pose.clip));
        float u = clipRate >= 0 ? pose.clipTime / d : 1 - pose.clipTime / d;
        seatOffset = seatOffsetFrom * (1 - std::clamp(u, 0.f, 1.f));
        sitBack(1 - u);
        if (!clipDone())
            return;
        seatOffset = 0;
        if (route.empty()) { // a standing character whose seat went away
            mode = Mode::Idle;
            idle();
            return;
        }
        auto &h = seats[seat];
        legs = {h.position + h.forward() * h.approach, route.at(s)};
        mode = Mode::FromSeat;
    }
    void fromSeat(float dt, const NpcWorld &) {
        if (!walkLegs(dt, walkSpeed * .8f))
            return;
        if (releaseSeat && seat >= 0)
            releaseSeat(seat);
        seat = -1;
        place();
        faceTarget.reset();
        mode = Mode::Walk;
    }
    void interactTick(float dt) {
        if (interactWith) {
            Vec3 d = *interactWith - position;
            if (std::hypot(d.x, d.z) > .05f)
                turn(std::atan2(d.x, d.z), dt, .05f, turnRate);
        }
        if (!pose.loop && !clipDone())
            return;
        idle();
    }
    void guideTick(float dt, const NpcWorld &w) {
        Vec3 toPlayer = w.player - position;
        float playerDistance = std::hypot(toPlayer.x, toPlayer.z);
        if (playerDistance > 30)
            guideCancelled = true;
        auto back = [&] {
            guidePhase = 3;
            pose.loop = true;
        };
        switch (guidePhase) {
        case -1: { // stand up from the seat (as standTick), then step off and join
            float d = std::max(1e-3f, duration(pose.clip));
            float u = clipRate >= 0 ? pose.clipTime / d : 1 - pose.clipTime / d;
            seatOffset = seatOffsetFrom * (1 - std::clamp(u, 0.f, 1.f));
            sitBack(1 - u);
            if (!clipDone())
                return;
            seatOffset = 0;
            // Cancelled while standing (the player left): step off and sit down again.
            legs = guideCancelled ? std::vector<Vec3>{guideFrom} : std::vector<Vec3>{guideFrom, guide.at(0)};
            guidePhase = guideCancelled ? 4 : 0;
            return;
        }
        case 0: // join the guide path; cancelled on the way (the player left): straight back
            if (guideCancelled) {
                guidePhase = 4;
                legs = {guideFrom};
                return;
            }
            if (walkLegs(dt, walkSpeed))
                guidePhase = 1;
            return;
        case 1: { // out: ahead of the player, waiting while they fall more than 6 m behind
            if (guideCancelled) {
                back();
                return;
            }
            guideIdle += dt;
            if (playerDistance > 6) {
                Vec3 d = w.player - position;
                turn(std::atan2(d.x, d.z), dt, .1f, turnRate * .5f);
                speedNow = 0;
                idle();
                if (guideIdle > 60)
                    guideCancelled = true;
                return;
            }
            float sp = ease(pace(), dt), step = sp * dt;
            float l = guide.length();
            guideS = std::min(l, guideS + step);
            Vec3 t = guide.tangent(guideS);
            turn(std::atan2(t.x, t.z), dt, .05f);
            position = guide.at(guideS);
            if (guideS > guideBest + .5f)
                guideBest = guideS, guideIdle = 0;
            gait(sp, dt);
            if (guideS >= l - 1e-3f) {
                guidePhase = 2;
                if (has("wave"))
                    play("wave", false);
                else
                    idle();
                Vec3 d = w.player - position;
                faceTarget = w.player;
                (void)d;
            }
            return;
        }
        case 2: // wave at the end, facing the player
            if (faceTarget) {
                Vec3 d = *faceTarget - position;
                turn(std::atan2(d.x, d.z), dt);
            }
            if (!pose.loop && !clipDone())
                return;
            faceTarget.reset();
            back();
            return;
        case 3: { // back along the guide path
            float sp = ease(pace(), dt);
            guideS = std::max(0.f, guideS - sp * dt);
            Vec3 t = guide.tangent(guideS) * -1.f;
            turn(std::atan2(t.x, t.z), dt, .05f);
            position = guide.at(guideS);
            gait(sp, dt);
            if (guideS <= 1e-3f) {
                guidePhase = 4;
                legs = {guideFrom};
            }
            return;
        }
        default: // back to where it left its route
            if (!walkLegs(dt, walkSpeed))
                return;
            if (guideSeat >= 0) { // back at the seat's approach point: sit down again
                seat = guideSeat;
                guideSeat = -1;
                legs = {seats[seat].position};
                mode = Mode::ToSeat;
                return;
            }
            mode = resume == Mode::Guide || resume == Mode::Interact ? (route.empty() ? Mode::Idle : Mode::Walk) : resume;
            if (mode == Mode::Stop)
                mode = Mode::Walk;
            if (!route.empty())
                place();
            idle();
        }
    }
};
} // namespace hyprune::sim
