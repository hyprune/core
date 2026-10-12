#pragma once
// Ambient NPC agent (RFC-0010 runtime rules; RFC-0011 routes, stops, seats and interactions).
// Follows a route by arc length at the walk clip's measured speed, stops where the route has
// stops (a clip, a facing, a seat), sits at seat anchors, glances at a nearby player, steps
// aside instead of blocking, faces the player during an interaction and walks a guide path.
// Pure logic: no GL, no Hyprland. The runtime poses the character from `pose` and draws it at
// `model()`; the same numbers travel to the offload worker.
#include "Route.hpp"
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
};
// What the agent sees each tick.
struct NpcWorld {
    Vec3 player{};          // the player's eye
    bool present = false;   // exploring this world: glance and step aside apply
    bool glance = true;     // false while the player reads a window
    // Walkable root position (floor, capsule, playable): sidesteps and legs. Null: walkable.
    std::function<bool(Vec3)> walkable;
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
    enum class Mode { Idle, Walk, Stop, ToSeat, Align, Sit, Seated, Stand, FromSeat, Interact, Guide, Suspended };
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
    int guidePhase = 0;              // 0 joining, 1 out, 2 waving, 3 back along, 4 returning
    Vec3 guideFrom{};
    bool guideCancelled = false;
    static constexpr float turnRate = 3.1416f, alignSeconds = .25f, fadeSeconds = .25f;
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
        case Mode::FromSeat: return "walking";
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
    // Guide: leave the route, walk to the guide path's start and along it ahead of the player,
    // wave at the end, walk it back and return to where it left. The runtime checked the legs.
    void beginGuide(Route path) {
        if (path.empty())
            return;
        endInteract();
        guide = std::move(path);
        guideFrom = position;
        guideS = 0, guideIdle = 0, guideBest = 0, guidePhase = 0, guideCancelled = false;
        legs = {guide.at(0)};
        resume = mode == Mode::Guide ? resume : mode;
        mode = Mode::Guide;
    }
    void cancelGuide() { guideCancelled = true; }

    // ---- per tick ----
    void update(float dt, const NpcWorld &w) {
        if (dt <= 0)
            return;
        dt = std::min(dt, .1f);
        pose.fade = std::min(1.f, pose.fade + dt / fadeSeconds);
        pose.previousTime += dt * previousRate;
        pose.clipTime += dt * clipRate;
        if (!pose.loop)
            pose.clipTime = std::clamp(pose.clipTime, 0.f, duration(pose.clip));
        sinceFidget += dt;
        look(dt, w);
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
        }
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
    void walk(float dt, const NpcWorld &w) {
        if (route.empty()) {
            mode = Mode::Idle;
            return;
        }
        Vec3 travel = route.tangent(s) * float(direction);
        float desired = std::atan2(travel.x, travel.z);
        float diff = angleTo(heading, desired);
        if (std::abs(diff) > 1.f) { // a sharp corner: turn in place before walking on
            turn(desired, dt);
            idle();
            return;
        }
        heading += std::copysign(std::min(std::abs(diff), turnRate * dt), diff);
        auto side = sidestep(w, travel);
        yielding = !side;
        if (side)
            lateralTarget = *side;
        if (yielding) {
            idle();
            return;
        }
        // Slower round corners, and while still stepping out of the player's way.
        float v = pace() * (std::abs(diff) > .35f ? .6f : 1.f) * (std::abs(lateralTarget - lateral) > .3f ? .5f : 1.f);
        float step = v * dt;
        // The next stop on the way (exactly at it: leaving it).
        float l = route.length();
        int hit = -1;
        float hitAt = 1e30f;
        for (size_t i = 0; i < stops.size(); ++i) {
            float d = (stops[i].at - s) * float(direction);
            if (route.closed)
                d = std::fmod(std::fmod(d, l) + l, l);
            if (d > 1e-5f && d <= step + 1e-4f && d < hitAt)
                hit = int(i), hitAt = d;
        }
        Vec3 before = position;
        if (hit >= 0) {
            s = stops[hit].at;
            place();
            enterStop(hit, w);
        } else {
            float next = s + direction * step;
            if (!route.closed && (next < 0 || next > l)) {
                s = std::clamp(next, 0.f, l);
                direction = -direction; // open routes: to the end and back
            } else
                s = route.wrap(next);
            float lstep = 1.f * dt;
            lateral += std::clamp(lateralTarget - lateral, -lstep, lstep);
            place();
        }
        float moved = std::hypot(position.x - before.x, position.z - before.z) / dt;
        if (mode != Mode::Walk)
            return;
        if (moved < .05f)
            idle();
        else if (running())
            play("run", true, moved / std::max(.1f, runSpeed));
        else
            play("walk", true, moved / std::max(.1f, walkSpeed));
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
        mode = Mode::Stop;
        if (!st.clip.empty() && has(st.clip))
            play(st.clip, false);
        else
            idle();
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
            mode = Mode::Walk;
        }
    }
    // Walk the straight legs at a careful pace; true when the last one is reached.
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
                idle();
                return false;
            }
            heading += std::clamp(angleTo(heading, target), -turnRate * dt, turnRate * dt);
            float step = std::min(dist, v * dt);
            Vec3 dir = d / dist;
            float y0 = position.y;
            position = position + dir * step;
            position.y = y0 + (legs.front().y - y0) * std::min(1.f, step / std::max(dist, 1e-3f));
            play("walk", true, v / std::max(.1f, walkSpeed));
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
        case 0: // join the guide path
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
                idle();
                if (guideIdle > 60)
                    guideCancelled = true;
                return;
            }
            float step = pace() * dt;
            float l = guide.length();
            guideS = std::min(l, guideS + step);
            Vec3 t = guide.tangent(guideS);
            turn(std::atan2(t.x, t.z), dt, .05f);
            position = guide.at(guideS);
            if (guideS > guideBest + .5f)
                guideBest = guideS, guideIdle = 0;
            play("walk", true, pace() / std::max(.1f, walkSpeed));
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
            guideS = std::max(0.f, guideS - pace() * dt);
            Vec3 t = guide.tangent(guideS) * -1.f;
            turn(std::atan2(t.x, t.z), dt, .05f);
            position = guide.at(guideS);
            play("walk", true, pace() / std::max(.1f, walkSpeed));
            if (guideS <= 1e-3f) {
                guidePhase = 4;
                legs = {guideFrom};
            }
            return;
        }
        default: // back to where it left its route
            if (!walkLegs(dt, walkSpeed))
                return;
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
