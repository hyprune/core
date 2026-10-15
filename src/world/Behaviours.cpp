#include "Behaviours.hpp"
#include <algorithm>
#include <cmath>
#include <regex>
#include <set>
#include <stdexcept>
namespace hyprune::world {
namespace {
using Json = nlohmann::json;
[[noreturn]] void bad(const std::string &why) { throw std::invalid_argument(why); }
double number(const Json &j, const std::string &what, double lo, double hi) {
    if (!j.is_number() || !std::isfinite(j.get<double>()) || j.get<double>() < lo || j.get<double>() > hi)
        bad(what + " is a number from " + std::to_string(lo) + " to " + std::to_string(hi));
    return j.get<double>();
}
std::array<double, 3> triple(const Json &j, const std::string &what, double lo, double hi, size_t n = 3) {
    if (!j.is_array() || j.size() != n)
        bad(what + " is " + std::to_string(n) + " numbers");
    std::array<double, 3> out{};
    for (size_t i = 0; i < n; ++i)
        out[i] = number(j[i], what, lo, hi);
    return out;
}
float channel(int v) {
    float c = v / 255.f;
    return c <= .04045f ? c / 12.92f : std::pow((c + .055f) / 1.055f, 2.4f);
}
Vec3 colour(const Json &j, const std::string &what) {
    static const std::regex hex("^#[0-9a-f]{6}$");
    if (!j.is_string() || !std::regex_match(j.get<std::string>(), hex))
        bad(what + " is #rrggbb");
    auto s = j.get<std::string>();
    auto c = [&](int i) { return channel(std::stoi(s.substr(1 + 2 * i, 2), nullptr, 16)); };
    return {c(0), c(1), c(2)};
}
double ease(Behaviour::Easing e, double t) {
    switch (e) {
    case Behaviour::EaseIn: return t * t;
    case Behaviour::EaseOut: return 1 - (1 - t) * (1 - t);
    case Behaviour::EaseInOut: return t * t * (3 - 2 * t);
    case Behaviour::Step: return t >= .5 ? 1 : 0;
    default: return t;
    }
}
Mat4 translate(Vec3 v) {
    auto m = Mat4::identity();
    m.v[12] = v.x, m.v[13] = v.y, m.v[14] = v.z;
    return m;
}
Mat4 scaling(double x, double y, double z) {
    auto m = Mat4::identity();
    m.v[0] = float(x), m.v[5] = float(y), m.v[10] = float(z);
    return m;
}
Mat4 rotation(int axis, double degrees) {
    double a = degrees * M_PI / 180, c = std::cos(a), s = std::sin(a);
    auto m = Mat4::identity();
    int i = (axis + 1) % 3, j = (axis + 2) % 3; // x: (y, z), y: (z, x), z: (x, y)
    m.v[i * 4 + i] = float(c), m.v[i * 4 + j] = float(s);
    m.v[j * 4 + i] = float(-s), m.v[j * 4 + j] = float(c);
    return m;
}
Vec3 anchorPoint(Behaviour::Anchor a, Vec3 lo, Vec3 hi) {
    Vec3 c = (lo + hi) * .5f;
    switch (a) {
    case Behaviour::Bottom: return {c.x, lo.y, c.z};
    case Behaviour::Top: return {c.x, hi.y, c.z};
    case Behaviour::Left: return {lo.x, c.y, c.z};
    case Behaviour::Right: return {hi.x, c.y, c.z};
    case Behaviour::Front: return {c.x, c.y, hi.z};
    case Behaviour::Back: return {c.x, c.y, lo.z};
    default: return c;
    }
}
template <class T> T lerp(T a, T b, double t) { return a + (b - a) * float(t); }
const char *conditionName(Behaviours::Condition c) {
    return c == Behaviours::Condition::Fresh ? "fresh" : c == Behaviours::Condition::Stale ? "stale" : c == Behaviours::Condition::Error ? "error" : "missing";
}
} // namespace

std::vector<Behaviour> parseBehaviours(const Json &list) {
    if (!list.is_array())
        bad("behaviours is a list");
    if (list.size() > behaviourLimit)
        bad("at most " + std::to_string(behaviourLimit) + " behaviours");
    static const std::regex slug("^[a-z][a-z0-9-]{0,47}$"), sourceId("^[a-z][a-z0-9-]*(\\.[a-z0-9][a-z0-9-]*)+$"),
        inputKey("^[A-Za-z_][A-Za-z0-9_.-]{0,63}$");
    static const std::map<std::string, Behaviour::Property> properties{
        {"scale", Behaviour::Scale}, {"move", Behaviour::Move}, {"rotate", Behaviour::Rotate}, {"colour", Behaviour::Colour},
        {"glow", Behaviour::Glow}, {"visible", Behaviour::Visible}, {"texture", Behaviour::Texture}};
    static const std::map<std::string, Behaviour::Anchor> anchors{{"bottom", Behaviour::Bottom}, {"top", Behaviour::Top},
                                                                  {"left", Behaviour::Left},     {"right", Behaviour::Right},
                                                                  {"front", Behaviour::Front},   {"back", Behaviour::Back},
                                                                  {"centre", Behaviour::Centre}};
    static const std::map<std::string, Behaviour::Easing> easings{{"linear", Behaviour::Linear}, {"ease-in", Behaviour::EaseIn},
                                                                  {"ease-out", Behaviour::EaseOut}, {"ease-in-out", Behaviour::EaseInOut},
                                                                  {"step", Behaviour::Step}};
    std::vector<Behaviour> out;
    std::set<std::string> keys;
    for (auto &j : list) {
        if (!j.is_object())
            bad("a behaviour is an object");
        Behaviour b;
        auto known = [&](std::initializer_list<const char *> fields) {
            for (auto &[k, v] : j.items())
                if (std::none_of(fields.begin(), fields.end(), [&](auto f) { return k == f; }))
                    bad("behaviour field " + k + " is not known here");
        };
        if (!j.contains("key") || !j["key"].is_string() || !std::regex_match(j["key"].get<std::string>(), slug))
            bad("a behaviour has a key: a slug of 1 to 48 characters");
        b.key = j["key"];
        if (!keys.insert(b.key).second)
            bad("behaviour key " + b.key + " is used twice");
        auto what = "behaviour " + b.key + ": ";
        auto p = properties.find(j.value("property", ""));
        if (p == properties.end())
            bad(what + "property is scale, move, rotate, colour, glow, visible or texture");
        b.property = p->second;
        if (j.contains("input")) {
            auto &in = j["input"];
            if (!in.is_object())
                bad(what + "input is an object");
            for (auto &[k, v] : in.items())
                if (k != "source" && k != "key" && k != "min" && k != "max")
                    bad(what + "input." + k + " is not known");
            if (in.contains("source")) {
                if (!in["source"].is_string() || in["source"].get<std::string>().size() > 128 ||
                    !std::regex_match(in["source"].get<std::string>(), sourceId))
                    bad(what + "input.source is a source id like com.hyprune.sources.cpu");
                b.source = in["source"];
            }
            if (in.contains("key")) {
                if (!in["key"].is_string() || !std::regex_match(in["key"].get<std::string>(), inputKey))
                    bad(what + "input.key is a key name");
                b.input = in["key"];
            }
            if (in.contains("min"))
                b.min = number(in["min"], what + "input.min", -1e9, 1e9);
            if (in.contains("max"))
                b.max = number(in["max"], what + "input.max", -1e9, 1e9);
            if (b.min == b.max)
                bad(what + "input.min and input.max differ");
        }
        if (j.contains("easing")) {
            auto e = easings.find(j["easing"].is_string() ? j["easing"].get<std::string>() : "");
            if (e == easings.end())
                bad(what + "easing is linear, ease-in, ease-out, ease-in-out or step");
            b.easing = e->second;
        }
        if (j.contains("smoothing"))
            b.smoothing = number(j["smoothing"], what + "smoothing (seconds)", 0, 10);
        if (j.contains("anchor")) {
            auto a = anchors.find(j["anchor"].is_string() ? j["anchor"].get<std::string>() : "");
            if (a == anchors.end())
                bad(what + "anchor is bottom, top, left, right, front, back or centre");
            b.anchor = a->second;
        }
        auto need = [&](const char *field) {
            if (!j.contains(field))
                bad(what + field + " is needed for " + j["property"].get<std::string>());
        };
        switch (b.property) {
        case Behaviour::Scale:
            known({"key", "property", "input", "easing", "smoothing", "anchor", "from", "to"});
            need("from"), need("to");
            b.from = triple(j["from"], what + "from (scale per axis)", .01, 100), b.to = triple(j["to"], what + "to (scale per axis)", .01, 100);
            break;
        case Behaviour::Move:
            known({"key", "property", "input", "easing", "smoothing", "from", "to"});
            need("from"), need("to");
            b.from = triple(j["from"], what + "from (metres)", -100, 100), b.to = triple(j["to"], what + "to (metres)", -100, 100);
            break;
        case Behaviour::Rotate:
            known({"key", "property", "input", "easing", "smoothing", "anchor", "from", "to"});
            need("from"), need("to");
            b.from = triple(j["from"], what + "from (degrees)", -3600, 3600), b.to = triple(j["to"], what + "to (degrees)", -3600, 3600);
            break;
        case Behaviour::Colour:
            known({"key", "property", "input", "easing", "smoothing", "from", "to"});
            need("from"), need("to");
            b.colourFrom = colour(j["from"], what + "from"), b.colourTo = colour(j["to"], what + "to");
            break;
        case Behaviour::Glow:
            known({"key", "property", "input", "easing", "smoothing", "from", "to", "colour"});
            need("from"), need("to");
            b.from[0] = number(j["from"], what + "from (glow strength)", 0, 16), b.to[0] = number(j["to"], what + "to (glow strength)", 0, 16);
            if (j.contains("colour"))
                b.glow = colour(j["colour"], what + "colour");
            break;
        case Behaviour::Visible:
            known({"key", "property", "input", "above", "below"});
            if (j.contains("above"))
                b.above = number(j["above"], what + "above", -1e9, 1e9);
            if (j.contains("below"))
                b.below = number(j["below"], what + "below", -1e9, 1e9);
            if (!b.above && !b.below)
                bad(what + "visible needs above, below or both");
            break;
        case Behaviour::Texture:
            known({"key", "property", "input", "easing", "smoothing", "from", "to", "mode"});
            need("from"), need("to");
            b.from = triple(j["from"], what + "from (texture repeats)", -1000, 1000, 2), b.to = triple(j["to"], what + "to (texture repeats)", -1000, 1000, 2);
            if (j.contains("mode")) {
                if (j["mode"] != "offset" && j["mode"] != "scroll")
                    bad(what + "mode is offset or scroll");
                b.scroll = j["mode"] == "scroll";
            }
            break;
        }
        out.push_back(std::move(b));
    }
    return out;
}

const char *Behaviours::propertyName(Behaviour::Property p) {
    static const char *names[] = {"scale", "move", "rotate", "colour", "glow", "visible", "texture"};
    return names[p];
}

void Behaviours::sync(const World &w, std::vector<Bound> bound) {
    // Keep the live state of behaviours that stay (same placement, same key).
    for (auto &in : bound_)
        for (size_t i = 0; i < in.bound.list.size() && i < in.live.size(); ++i)
            carried_[in.bound.id + "\n" + in.bound.list[i].key] = in.live[i];
    bound_.clear();
    for (auto &b : bound) {
        if (b.instance >= w.objectInstances.size() || b.list.empty())
            continue;
        Instance in;
        const auto &o = w.objectInstances[b.instance];
        const auto &a = w.objectAssets[o.asset];
        in.low = a.low, in.high = a.high, in.scale = o.scale > 0 ? o.scale : 1, in.meshes = unsigned(a.meshes.size());
        for (auto &item : b.list) {
            auto it = carried_.find(b.id + "\n" + item.key);
            in.live.push_back(it != carried_.end() ? it->second : Live{});
        }
        in.bound = std::move(b);
        bound_.push_back(std::move(in));
    }
    // Forget state that no longer has a placement.
    std::set<std::string> keep;
    for (auto &in : bound_)
        for (auto &item : in.bound.list)
            keep.insert(in.bound.id + "\n" + item.key);
    std::erase_if(carried_, [&](auto &e) { return !keep.contains(e.first); });
    ++revision_;
}

void Behaviours::tick(double dt, const Json &sources, int64_t nowMs, bool still, int64_t staleMs) {
    for (auto &in : bound_) {
        in.moving = false;
        for (size_t i = 0; i < in.bound.list.size(); ++i) {
            const auto &b = in.bound.list[i];
            auto &l = in.live[i];
            auto sourceId = b.source.empty() ? std::string(behaviourClient) + "." + in.bound.id : b.source;
            const Json *entry = nullptr;
            for (auto &s : sources)
                if (s.value("sourceId", "") == sourceId)
                    entry = &s;
            const Json *raw = entry && (*entry)["values"].contains(b.input) ? &(*entry)["values"][b.input] : nullptr;
            std::optional<double> value;
            if (raw && raw->is_number())
                value = raw->get<double>();
            else if (raw && raw->is_boolean())
                value = raw->get<bool>() ? 1 : 0;
            else if (raw && raw->is_string()) {
                try {
                    size_t used = 0;
                    double v = std::stod(raw->get<std::string>(), &used);
                    if (used && std::isfinite(v))
                        value = v;
                } catch (...) {
                }
            }
            l.raw = raw ? *raw : Json(nullptr);
            if (!entry || !raw)
                l.condition = Condition::Missing;
            else if (!value || (*entry)["values"].value("_status", "ok") == "error")
                l.condition = Condition::Error;
            else
                l.condition = nowMs - (*entry).value("monotonicMs", nowMs) > staleMs ? Condition::Stale : Condition::Fresh;
            // Missing, stale or unreadable data holds the last value (A before any).
            if (value && l.condition != Condition::Error) {
                if (b.property == Behaviour::Visible)
                    l.target = (!b.above || *value >= *b.above) && (!b.below || *value < *b.below) ? 1 : 0;
                else
                    l.target = ease(b.easing, std::clamp((*value - b.min) / (b.max - b.min), 0., 1.));
                l.known = true;
            }
            double before = l.shown;
            if (b.property == Behaviour::Visible || still || b.smoothing <= 0 || !l.known)
                l.shown = l.target;
            else
                l.shown += (l.target - l.shown) * (1 - std::exp(-dt / b.smoothing));
            if (std::abs(l.target - l.shown) < 1e-4)
                l.shown = l.target;
            else
                in.moving = true;
            in.moving |= std::abs(l.shown - before) > 1e-6; // a jump still needs a frame
            if (b.property == Behaviour::Texture && b.scroll && !still) {
                double u = lerp(b.from[0], b.to[0], l.shown), v = lerp(b.from[1], b.to[1], l.shown);
                l.scrollU = std::fmod(l.scrollU + u * dt, 1.0), l.scrollV = std::fmod(l.scrollV + v * dt, 1.0);
                in.moving |= u != 0 || v != 0;
            }
        }
    }
    // A summary change (rounded values, conditions) is an IPC state change; smoothing frames are not.
    std::string text;
    for (auto &in : bound_)
        text += summary(in.bound.id).dump();
    if (text != lastSummary_)
        lastSummary_ = std::move(text), ++revision_;
}

void Behaviours::apply(const World &w, std::vector<ObjectDrawState> &states) const {
    for (auto &in : bound_) {
        size_t k = in.bound.instance;
        if (k >= w.objectInstances.size())
            continue;
        if (states.size() < w.objectInstances.size())
            states.resize(w.objectInstances.size());
        auto &s = states[k];
        // Move x rotate (about the anchor) x scale (about the anchor), in list order per kind.
        Mat4 moves = Mat4::identity(), turns = Mat4::identity(), scales = Mat4::identity();
        bool displayed = false, painted = false, hidden = false, glowing = false;
        Vec3 paint{1, 1, 1}, glowColour{1, 1, 1};
        float emission = 0, uvU = s.uvU, uvV = s.uvV;
        for (size_t i = 0; i < in.bound.list.size(); ++i) {
            const auto &b = in.bound.list[i];
            const auto &l = in.live[i];
            double t = l.shown;
            Vec3 a = anchorPoint(b.anchor, in.low, in.high);
            switch (b.property) {
            case Behaviour::Scale: {
                auto f = [&](int c) { return lerp(b.from[c], b.to[c], t); };
                scales = scales * translate(a) * scaling(f(0), f(1), f(2)) * translate(a * -1.f);
                displayed = true;
                break;
            }
            case Behaviour::Move: {
                Vec3 m{float(lerp(b.from[0], b.to[0], t)), float(lerp(b.from[1], b.to[1], t)), float(lerp(b.from[2], b.to[2], t))};
                moves = moves * translate(m * (1 / in.scale)); // metres in the object's axes
                displayed = true;
                break;
            }
            case Behaviour::Rotate: {
                auto r = rotation(1, lerp(b.from[1], b.to[1], t)) * rotation(0, lerp(b.from[0], b.to[0], t)) * rotation(2, lerp(b.from[2], b.to[2], t));
                turns = turns * translate(a) * r * translate(a * -1.f);
                displayed = true;
                break;
            }
            case Behaviour::Colour:
                painted = true, paint = lerp(b.colourFrom, b.colourTo, t);
                break;
            case Behaviour::Glow:
                glowing = true, glowColour = b.glow, emission = float(lerp(b.from[0], b.to[0], t));
                break;
            case Behaviour::Visible:
                hidden |= t < .5;
                break;
            case Behaviour::Texture:
                if (b.scroll)
                    uvU = float(l.scrollU), uvV = float(l.scrollV);
                else
                    uvU = float(lerp(b.from[0], b.to[0], t)), uvV = float(lerp(b.from[1], b.to[1], t));
                break;
            }
        }
        s.bound = true;
        s.moving = s.moving || in.moving;
        if (displayed) {
            s.displayed = true;
            s.display = moves * turns * scales;
        }
        s.uvU = uvU, s.uvV = uvV;
        if (painted || glowing || hidden) {
            if (s.meshes.size() < in.meshes)
                s.meshes.resize(in.meshes);
            for (auto &m : s.meshes) {
                // Glow paints the object in its glow colour unless a colour behaviour sets one.
                if (painted || glowing) {
                    m.painted = true;
                    m.colour = painted ? paint : glowColour;
                    m.emission = glowing ? emission : 0;
                }
                m.hidden = m.hidden || hidden;
            }
        }
    }
}

Json Behaviours::summary(const std::string &placementId) const {
    for (auto &in : bound_) {
        if (in.bound.id != placementId)
            continue;
        Json list = Json::array();
        for (size_t i = 0; i < in.bound.list.size(); ++i) {
            const auto &l = in.live[i];
            Json value = l.raw.is_string() && l.raw.get<std::string>().size() > 64 ? Json(l.raw.get<std::string>().substr(0, 64)) : l.raw;
            if (!value.is_null() && !value.is_number() && !value.is_string() && !value.is_boolean())
                value = nullptr;
            list.push_back({{"key", in.bound.list[i].key},
                            {"property", propertyName(in.bound.list[i].property)},
                            {"condition", conditionName(l.condition)},
                            {"value", value},
                            {"t", std::round(l.target * 100) / 100}});
        }
        return list;
    }
    return nullptr;
}
} // namespace hyprune::world
