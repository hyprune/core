// Shared clip library: retargeting a clip between rigs with the same skeleton (RFC-0010
// amendment). Synthetic legged rigs: a source with a plain rest pose walks an analytic,
// slip-free walk; a short (0.8x) and a tall (1.25x) target whose bones point differently at rest
// (and a thigh renamed through an alias) take it. Checks the bone mapping, the rest-pose transfer
// (world bone directions match the source's), the hips scaling and the foot-slip metric: a
// planted foot moves backwards at the clip's (scaled) speed, so it stays put on the ground.
#include "world/Motion.hpp"
#include "world/Retarget.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace hyprune;
using world::Character;
using world::Json;
namespace {
void check(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
}
using Quat = std::array<float, 4>;
Quat axisAngle(Vec3 a, float r) {
    a = normalized(a);
    float s = std::sin(r / 2);
    return {a.x * s, a.y * s, a.z * s, std::cos(r / 2)};
}
constexpr float L = .9f; // source leg length: hip to ankle, straight
struct Spec {
    std::string name, parent;
    Vec3 position; // world rest position at scale 1
};
// root at the feet, hips above, a spine, two straight legs (upleg at the hip, leg at the knee,
// foot at the ankle, toe ahead). Facing +Z, Y up.
const std::vector<Spec> skeleton = {
    {"Root", "", {0, 0, 0}},
    {"Hips", "Root", {0, L, 0}},
    {"Spine", "Hips", {0, L + .2f, 0}},
    {"LeftUpLeg", "Hips", {.1f, L, 0}},
    {"LeftLeg", "LeftUpLeg", {.1f, L / 2, 0}},
    {"LeftFoot", "LeftLeg", {.1f, 0, 0}},
    {"LeftToeBase", "LeftFoot", {.1f, 0, .12f}},
    {"RightUpLeg", "Hips", {-.1f, L, 0}},
    {"RightLeg", "RightUpLeg", {-.1f, L / 2, 0}},
    {"RightFoot", "RightLeg", {-.1f, 0, 0}},
    {"RightToeBase", "RightFoot", {-.1f, 0, .12f}},
};
// A rig at `scale` whose joints' world rest rotations are `twist` (a different one per joint when
// twisted: local axes differ, positions don't). prefix names the bones (mixamorig: or none).
Character rig(float scale, bool twisted, const std::string &prefix, const std::map<std::string, std::string> &rename = {}) {
    Character c;
    std::vector<Quat> world(skeleton.size());
    for (size_t i = 0; i < skeleton.size(); ++i) {
        world[i] = twisted ? axisAngle({std::sin(i * 1.3f), std::cos(i * .7f), .4f}, .3f + .25f * float(i)) : Quat{0, 0, 0, 1};
        auto &s = skeleton[i];
        Character::Joint j;
        j.name = rename.contains(s.name) ? rename.at(s.name) : prefix + s.name;
        j.parent = -1;
        Vec3 parentPos{};
        Quat parentRot{0, 0, 0, 1};
        for (size_t p = 0; p < i; ++p)
            if (skeleton[p].name == s.parent) {
                j.parent = int(p);
                parentPos = skeleton[p].position * scale;
                parentRot = world[p];
            }
        auto inv = conjugate(parentRot);
        j.translation = rotate(inv, s.position * scale - parentPos);
        j.rotation = quaternionProduct(inv, world[i]);
        c.joints.push_back(j);
    }
    return c;
}
// The source walk: each leg straight in stance, rotating about X so its ankle moves backwards at
// `speed` under the hips (in place: the ground moves), the hips bobbing to keep that ankle on the
// floor; in swing the leg swings forward with a bent knee. Keys at 60 fps over one cycle.
constexpr float speed = 1.2f, period = 1.0f;
Character::Clip walk(const Character &source) {
    Character::Clip clip;
    clip.name = "walk";
    clip.duration = period;
    clip.speed = speed;
    float reach = speed * period / 4; // stance covers -reach..+reach of ankle travel
    auto legAngle = [&](float phase, float &knee) {
        // phase 0..1; stance 0..0.5 (ankle z from +reach to -reach), swing 0.5..1.
        if (phase < .5f) {
            knee = 0;
            float z = reach - speed * phase * period; // ankle z relative to the hips
            return std::asin(-z / L);                 // rotation about +X: ankle z = -L sin(angle)
        }
        float u = (phase - .5f) / .5f;
        float a0 = std::asin(reach / L), a1 = -a0; // from behind (+a0) to ahead (-a0)
        knee = .9f * std::sin(u * 3.14159265f);
        return a0 + (a1 - a0) * (.5f - .5f * std::cos(u * 3.14159265f));
    };
    int hips = source.role("hips"), lu = source.role("leftupleg"), ll = source.role("leftleg");
    int ru = source.role("rightupleg"), rl = source.role("rightleg");
    Character::Channel th{hips, 0}, lr{lu, 1}, lk{ll, 1}, rr{ru, 1}, rk{rl, 1};
    for (int k = 0; k <= 60; ++k) {
        float t = period * k / 60.f, kl, kr;
        float al = legAngle(std::fmod(t / period, 1.f), kl), ar = legAngle(std::fmod(t / period + .5f, 1.f), kr);
        float stance = (std::fmod(t / period, 1.f) < .5f) ? al : ar;
        for (auto *c : {&th, &lr, &lk, &rr, &rk})
            c->times.push_back(t);
        Vec3 hipsPos = source.joints[hips].translation;
        hipsPos.y = L * std::cos(stance); // the planted ankle stays on the floor (y 0)
        th.values.insert(th.values.end(), {hipsPos.x, hipsPos.y, hipsPos.z});
        auto put = [](Character::Channel &c, Quat q) { c.values.insert(c.values.end(), q.begin(), q.end()); };
        put(lr, axisAngle({1, 0, 0}, al)), put(rr, axisAngle({1, 0, 0}, ar));
        put(lk, axisAngle({1, 0, 0}, kl)), put(rk, axisAngle({1, 0, 0}, kr));
    }
    clip.channels = {th, lr, lk, rr, rk};
    return clip;
}
std::vector<Vec3> positions(const Character &c, const Character::Clip &clip, float t) {
    std::vector<Mat4> m;
    c.pose(&clip, t, true, m); // inverse binds are identity: these are joint world matrices
    std::vector<Vec3> p;
    for (auto &x : m)
        p.push_back({x.v[12], x.v[13], x.v[14]});
    return p;
}
float angleBetween(Vec3 a, Vec3 b) {
    float d = std::clamp(dot(normalized(a), normalized(b)), -1.f, 1.f);
    return std::acos(d) * 57.2958f;
}
} // namespace
int main() {
    try {
        auto source = rig(1, false, "mixamorig:");
        auto clip = walk(source);
        // The source itself doesn't slip (the test's metric is sound).
        for (float scale : {.8f, 1.25f}) {
            std::map<std::string, std::string> aliases{{"LeftUpLeg", "thigh_l"}};
            auto target = rig(scale, true, "", {{"LeftUpLeg", "thigh_l"}});
            world::RetargetInfo info;
            auto moved = world::retargetClip(target, source, clip, aliases, &info);
            check(info.matched == int(skeleton.size()) && info.unmatched == 0 && info.missing.empty(),
                  "every bone maps by canonical name or alias: matched " + std::to_string(info.matched));
            check(std::abs(info.scale - scale) < 1e-3f, "hip height ratio " + std::to_string(info.scale));
            check(std::abs(moved.speed - speed * scale) < 1e-3f, "clip speed scales with the legs");
            // Rest-pose transfer: every bone's world direction matches the source's at each time.
            float worst = 0;
            for (int k = 0; k < 30; ++k) {
                float t = period * k / 30.f;
                auto ps = positions(source, clip, t), pt = positions(target, moved, t);
                for (size_t i = 0; i < skeleton.size(); ++i) {
                    int p = source.joints[i].parent;
                    if (p < 0 || skeleton[i].name == "Hips")
                        continue;
                    worst = std::max(worst, angleBetween(ps[i] - ps[p], pt[i] - pt[p]));
                }
            }
            check(worst < .5f, "bone directions differ by " + std::to_string(worst) + " deg");
            // Foot slip: while an ankle is planted (lowest), it moves backwards at the clip speed
            // (in place, the floor moves): |v + speed| stays within 3 % of the speed, at floor height.
            int ankle[2] = {target.role("leftfoot"), target.role("rightfoot")};
            float slip = 0, lift = 0;
            float dt = period / 240;
            for (int k = 0; k < 240; ++k) {
                float t = k * dt;
                auto a = positions(target, moved, t), b = positions(target, moved, t + dt);
                int planted = a[ankle[0]].y < a[ankle[1]].y ? ankle[0] : ankle[1];
                if (std::abs(a[ankle[0]].y - a[ankle[1]].y) < .01f * scale)
                    continue; // both down at the hand-over
                float v = (b[planted].z - a[planted].z) / dt;
                slip = std::max(slip, std::abs(v + moved.speed) / moved.speed);
                lift = std::max(lift, std::abs(a[planted].y));
            }
            check(slip < .03f, "foot slip " + std::to_string(slip * 100) + " % at scale " + std::to_string(scale));
            check(lift < .005f * scale, "planted foot off the floor by " + std::to_string(lift));
            // Core's own measures (npc-check, motion-pack) agree: the planted feet give the speed.
            float measured = world::plantedSpeed(target, moved), coreSlip = world::footSlip(target, moved);
            check(std::abs(measured - moved.speed) < .03f * moved.speed, "planted speed " + std::to_string(measured));
            check(coreSlip >= 0 && coreSlip < .03f, "core foot slip " + std::to_string(coreSlip));
            std::cout << "scale " << scale << ": matched " << info.matched << ", worst bone " << worst << " deg, slip "
                      << slip * 100 << " %\n";
        }
        // A target lacking a bone: the clip still binds; the missing key is reported.
        auto partial = rig(1, true, "");
        partial.joints[skeleton.size() - 1].name = "accessory";
        world::RetargetInfo info;
        world::retargetClip(partial, source, clip, {}, &info);
        check(info.unmatched == 1, "an unmatched target bone stays at rest");
        // Seat fit: a coat vertex skinned to a thigh hangs far below it. The character's own sit
        // measures it (the plain measure); a pack's (retargeted) sit sits at most a fifth of the
        // hip height below the hip joint.
        auto coated = rig(1, false, "");
        Character::Primitive prim;
        for (float y : {-.1f, -.85f}) { // thigh-local: on the thigh, and a coat hem
            Character::Vertex v;
            v.position = {0, y, 0};
            v.joints[0] = float(coated.role("leftupleg"));
            v.weights[0] = 1;
            prim.vertices.push_back(v);
        }
        coated.primitives.push_back(prim);
        Character::Clip sit;
        sit.name = "sit";
        sit.duration = 1;
        coated.clips.push_back(sit);
        auto own = coated.measureSeat();
        check(own.valid && std::abs(own.seatHeight - (L - .85f)) < 1e-3f, "own sit seat " + std::to_string(own.seatHeight));
        coated.clips.back().retargeted = true;
        auto packed = coated.measureSeat();
        check(std::abs(packed.seatHeight - .8f * L) < 1e-3f, "retargeted sit seat " + std::to_string(packed.seatHeight));
        // Catalog motion before a character loads: its own clips and the packs' on its skeleton.
        auto pack = std::make_shared<world::MotionPack>();
        pack->id = "moves", pack->skeleton = "test";
        pack->clips = {{"walk-proud", "walk", "proud"}, {"walk", "walk", ""}, {"dance-spin", "dance", "spin"}, {"run-x", "run", "x"}};
        auto other = std::make_shared<world::MotionPack>(*pack);
        other->id = "elsewhere", other->skeleton = "other", other->clips = {{"idle-calm", "idle", "calm"}};
        auto rigOnly = rig(1, false, "");
        rigOnly.clips = {clip};
        auto motion = world::characterMotionPreview(rigOnly, {{"skeleton", {{"id", "test"}}}}, {pack, other});
        check(motion["skeleton"] == "test" && motion["styles"]["walk"] == Json::array({"proud"}) && motion["styles"]["run"] == Json::array({"x"}) &&
                  motion["styles"]["idle"].empty() && motion["dances"] == Json::array({"spin"}) && motion["packs"] == Json::array({"moves"}),
              "catalog motion preview " + motion.dump());
        check(world::characterMotionPreview(rigOnly, Json::object(), {pack})["packs"].empty(), "no skeleton, no packs");
        std::cout << "retarget: mapping, aliases, rest-pose transfer, hip scaling, foot slip, seat fit, motion preview\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
