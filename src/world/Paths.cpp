#include "Paths.hpp"
#include "Format.hpp"
#include <cmath>
#include <format>
#include <map>
#include <set>
#include <stdexcept>
namespace hyprune::world {
namespace {
Vec3 vec(const Json &j) { return {j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>()}; }
bool unit(const Json &q) {
    if (!q.is_array() || q.size() != 4)
        return false;
    float x = q[0], y = q[1], z = q[2], w = q[3];
    return std::isfinite(x + y + z + w) && std::abs(x * x + y * y + z * z + w * w - 1) < .002f;
}
const std::set<std::string> stopClips{"look_around", "wave", "nod", "idle_fidget"};
} // namespace
std::string pathUse(const Json &path) { return path.value("use", std::string("generic")); }
sim::Route routeOf(const Json &path) {
    std::vector<Vec3> points;
    std::vector<sim::Segment> types;
    for (auto &p : path.value("points", Json::array())) {
        points.push_back(vec(p.at("position")));
        types.push_back(sim::segmentType(p.value("type", std::string("line"))));
    }
    return sim::Route::build(points, types, path.value("closed", false));
}
std::vector<float> pointDistances(const Json &path) { return routeOf(path).points; }
std::vector<std::string> pathProblems(const Json &path, size_t pointLimit) {
    std::vector<std::string> out;
    auto fail = [&](std::string why) {
        if (out.size() < 8)
            out.push_back(std::move(why));
    };
    if (!path.is_object()) {
        fail("a path is an object");
        return out;
    }
    auto use = pathUse(path);
    if (use != "route" && use != "track" && use != "generic")
        fail("use must be route, track or generic");
    if (path.contains("name") && !(path["name"].is_string() && !path["name"].get<std::string>().empty() &&
                                   path["name"].get<std::string>().size() <= 64))
        fail("name: 1-64 characters");
    if (path.contains("speed") && !(path["speed"].is_number() && path["speed"] >= .1 && path["speed"] <= 30))
        fail("speed: 0.1-30 m/s");
    auto points = path.value("points", Json::array());
    if (points.size() < 2 || points.size() > pointLimit)
        fail(std::format("points: 2-{}", pointLimit));
    bool closed = path.value("closed", false);
    for (size_t i = 0; i < points.size(); ++i) {
        auto &p = points[i];
        if (!p.is_object() || !p.contains("position") || !p["position"].is_array() || p["position"].size() != 3) {
            fail(std::format("points[{}]: position [x, y, z]", i));
            continue;
        }
        for (auto &c : p["position"])
            if (!c.is_number() || std::abs(c.get<double>()) > 100000)
                fail(std::format("points[{}]: position out of range", i));
        auto type = p.value("type", std::string("line"));
        if (type != "line" && type != "curve" && type != "arc")
            fail(std::format("points[{}]: type must be line, curve or arc", i));
        // An arc's horizontal chord is at least 0.1 m (the first point of an open path has no segment).
        if (type == "arc" && (i > 0 || closed) && points.size() >= 2) {
            Vec3 a = vec(points[(i + points.size() - 1) % points.size()].at("position")), b = vec(p["position"]);
            if (std::hypot(b.x - a.x, b.z - a.z) < .1f)
                fail(std::format("points[{}]: an arc's horizontal chord is under 0.1 m", i));
        }
    }
    auto stops = path.value("stops", Json::array());
    if (stops.size() > 32)
        fail("stops: at most 32");
    if (!stops.empty() && use == "generic")
        fail("stops: generic paths have none");
    std::set<int> at;
    for (size_t i = 0; i < stops.size(); ++i) {
        auto &s = stops[i];
        auto label = std::format("stops[{}]", i);
        if (!s.is_object() || !s.contains("point") || !s["point"].is_number_integer()) {
            fail(label + ": point index required");
            continue;
        }
        int point = s["point"];
        if (point < 0 || point >= int(points.size()))
            fail(label + ": point out of range");
        if (!at.insert(point).second)
            fail(label + ": one stop per point");
        if (s.contains("seconds") && !(s["seconds"].is_number() && s["seconds"] >= .5 && s["seconds"] <= 600))
            fail(label + ": seconds 0.5-600");
        int kinds = int(s.contains("clip")) + int(s.contains("face")) + int(s.contains("seat"));
        if (kinds > 1)
            fail(label + ": at most one of clip, face and seat");
        if (use == "track" && kinds)
            fail(label + ": track stops have only point and seconds");
        if (s.contains("clip") && !(s["clip"].is_string() && stopClips.contains(s["clip"].get<std::string>())))
            fail(label + ": clip must be look_around, wave, nod or idle_fidget");
        if (s.contains("face")) {
            auto &f = s["face"];
            bool ok = f.is_object() && f.size() == 1 &&
                      ((f.contains("position") && f["position"].is_array() && f["position"].size() == 3) ||
                       (f.contains("anchor") && f["anchor"].is_string()) || (f.contains("object") && f["object"].is_string()));
            if (!ok)
                fail(label + ": face is {position}, {anchor} or {object}");
        }
        if (s.contains("seat") && !(s["seat"].is_string() && !s["seat"].get<std::string>().empty() && s["seat"].get<std::string>().size() <= 192))
            fail(label + ": seat is a seat reference");
        for (auto &[k, v] : s.items())
            if (k != "point" && k != "seconds" && k != "clip" && k != "face" && k != "seat")
                fail(label + ": unknown field " + k);
    }
    return out;
}
std::vector<std::string> interactionProblems(const World &w, const Json &list) {
    std::vector<std::string> out;
    auto fail = [&](std::string why) {
        if (out.size() < 8)
            out.push_back(std::move(why));
    };
    if (!list.is_array() || list.empty() || list.size() > 8) {
        fail("interactions: 1-8");
        return out;
    }
    std::set<std::string> ids;
    auto has = [](const Json &records, const char *key, const std::string &id) {
        for (auto &r : records)
            if (r.value(key, std::string()) == id)
                return true;
        return false;
    };
    for (auto &i : list) {
        auto id = i.value("id", std::string());
        if (!ids.insert(id).second)
            fail("interactions: duplicate id " + id);
        auto &a = i.at("action");
        auto type = a.value("type", std::string());
        if (type == "travel" || type == "quest")
            fail("interactions " + id + ": " + type + " is reserved in world format 0.9");
        else if (type == "guide") {
            bool route = false;
            for (auto &p : w.paths)
                route = route || (p["id"] == a.value("path", std::string()) && pathUse(p) == "route");
            if (!route)
                fail("interactions " + id + ": guide needs a route path, not " + a.value("path", std::string()));
        } else if (type == "waypoint") {
            auto &t = a.at("target");
            if (t.contains("zoneId") && !has(w.manifest.value("zones", Json::array()), "id", t["zoneId"]))
                fail("interactions " + id + ": unknown zone " + t["zoneId"].get<std::string>());
            if (t.contains("anchor") && !has(w.anchors, "id", t["anchor"]))
                fail("interactions " + id + ": unknown anchor " + t["anchor"].get<std::string>());
            if (t.contains("object") && !has(w.manifest.value("objects", Json::array()), "id", t["object"]))
                fail("interactions " + id + ": unknown object " + t["object"].get<std::string>());
        } else if (type != "say" && type != "panel")
            fail("interactions " + id + ": unknown action " + type);
    }
    return out;
}
void loadPaths(World &w, const std::string &formatVersion) {
    const auto &j = w.manifest;
    bool v09 = format::atLeast(formatVersion, "0.9");
    for (auto key : {"paths", "seats", "keepClear"})
        if (j.contains(key) && !v09)
            throw std::runtime_error(std::string(key) + " need world format 0.9");
    auto require = [](bool ok, const std::string &why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    // keepClear: boxes objects may not block.
    std::set<std::string> ids;
    auto keep = j.value("keepClear", Json::array());
    require(keep.size() <= keepClearLimit, "keepClear: at most 64");
    for (auto &k : keep) {
        require(ids.insert(k.at("id").get<std::string>()).second, "keepClear: duplicate id " + k["id"].get<std::string>());
        auto lo = vec(k.at("min")), hi = vec(k.at("max"));
        require(lo.x < hi.x && lo.y < hi.y && lo.z < hi.z, "keepClear " + k["id"].get<std::string>() + ": min < max on every axis");
    }
    w.keepClear = keep;
    // World seats.
    ids.clear();
    auto seats = j.value("seats", Json::array());
    require(seats.size() <= seatLimit, "seats: at most 64");
    for (auto &s : seats) {
        auto id = s.at("id").get<std::string>();
        require(ids.insert(id).second, "seats: duplicate id " + id);
        require(unit(s.at("pose").at("rotation")), "seats " + id + ": rotation must be normalized");
        float h = s.at("height");
        require(h >= .2f && h <= 1, "seats " + id + ": height 0.2-1 m");
        if (s.contains("approach"))
            require(s["approach"] >= .3 && s["approach"] <= 2, "seats " + id + ": approach 0.3-2 m");
    }
    w.seats = seats;
    // Paths.
    ids.clear();
    auto paths = j.value("paths", Json::array());
    require(paths.size() <= authoredPathLimit, "paths: at most 64");
    size_t points = 0;
    for (auto &p : paths) {
        auto id = p.at("id").get<std::string>();
        require(ids.insert(id).second, "paths: duplicate id " + id);
        auto problems = pathProblems(p);
        require(problems.empty(), "paths " + id + ": " + (problems.empty() ? "" : problems[0]));
        points += p["points"].size();
    }
    require(points <= authoredPointLimit, "paths: at most 1,024 points in total");
    w.paths = paths;
    // References: objects (by id, with their asset's traits), seats, anchors.
    std::map<std::string, const ObjectAsset *> assets;
    for (auto &a : w.objectAssets)
        assets[a.id] = &a;
    static const Json none = Json::array();
    const Json &objectList = j.contains("objects") ? j["objects"] : none;
    std::map<std::string, const Json *> objects;
    for (auto &o : objectList)
        objects[o.at("id").get<std::string>()] = &o;
    auto assetOf = [&](const Json &o) -> const ObjectAsset * {
        auto it = assets.find(o.at("asset").get<std::string>());
        return it == assets.end() ? nullptr : it->second;
    };
    auto traitOf = [&](const ObjectAsset *a, const char *trait) { return a && a->traits.is_object() && a->traits.contains(trait); };
    auto seatRef = [&](const std::string &ref, const std::string &where) {
        for (auto &s : w.seats)
            if (s["id"] == ref)
                return;
        auto slash = ref.find('/');
        auto object = ref.substr(0, slash);
        auto it = objects.find(object);
        require(it != objects.end(), where + ": unknown seat " + ref);
        auto *a = assetOf(*it->second);
        require(traitOf(a, "seat"), where + ": " + object + " is not a seat");
        if (slash != std::string::npos) {
            bool found = false;
            for (auto &anchor : a->traits["seat"].value("anchors", Json::array()))
                found = found || anchor["id"] == ref.substr(slash + 1);
            require(found, where + ": " + object + " has no seat anchor " + ref.substr(slash + 1));
        }
    };
    auto anchorKnown = [&](const std::string &id) {
        for (auto &a : w.anchors)
            if (a.value("id", std::string()) == id)
                return true;
        return false;
    };
    for (auto &p : w.paths)
        for (auto &s : p.value("stops", Json::array())) {
            auto where = "paths " + p["id"].get<std::string>() + " stop at point " + std::to_string(s["point"].get<int>());
            if (s.contains("seat"))
                seatRef(s["seat"], where);
            if (s.contains("face") && s["face"].contains("anchor"))
                require(anchorKnown(s["face"]["anchor"]), where + ": unknown anchor " + s["face"]["anchor"].get<std::string>());
            if (s.contains("face") && s["face"].contains("object"))
                require(objects.contains(s["face"]["object"]), where + ": unknown object " + s["face"]["object"].get<std::string>());
        }
    size_t characters = 0;
    std::map<std::string, size_t> perPath;
    for (auto &o : objectList) {
        auto id = o.at("id").get<std::string>();
        auto *a = assetOf(o);
        if (!a)
            continue; // loadObjects reports unknown assets
        bool character = traitOf(a, "character");
        auto label = "objects " + id;
        for (auto key : {"path", "reverse", "seat", "accent", "interactions", "locked"})
            require(!o.contains(key) || v09, label + ": " + key + " needs world format 0.9");
        if (o.contains("path")) {
            require(character, label + ": only characters bind to routes in world format 0.9");
            require(!o.contains("pose"), label + ": a path-bound instance has no pose");
            const Json *path = nullptr;
            for (auto &p : w.paths)
                if (p["id"] == o["path"])
                    path = &p;
            require(path, label + ": unknown path " + o["path"].get<std::string>());
            require(pathUse(*path) == "route", label + ": path " + o["path"].get<std::string>() + " is not a route");
            require(++perPath[o["path"]] <= charactersPerPath, "paths " + o["path"].get<std::string>() + ": at most 8 characters");
        } else
            require(o.contains("pose"), label + ": pose required (unbound instances stand there)");
        require(!o.contains("reverse") || o.contains("path"), label + ": reverse needs a path");
        for (auto key : {"accent", "interactions", "seat"})
            require(!o.contains(key) || character, label + ": " + key + " is for characters");
        require(!o.contains("seat") || !o.contains("path"), label + ": a route-bound character sits at seat stops, not an instance seat");
        if (o.contains("seat"))
            seatRef(o["seat"], label);
        require(!o.contains("locked") || traitOf(a, "movable"), label + ": locked is for movable objects");
        if (character) {
            require(++characters <= characterInstanceLimit, "objects: at most 16 character instances");
            require(o.value("scale", 1.f) == 1.f, label + ": characters are not scaled");
        }
        if (o.contains("interactions")) {
            auto problems = interactionProblems(w, o["interactions"]);
            require(problems.empty(), label + ": " + (problems.empty() ? "" : problems[0]));
        }
    }
}
} // namespace hyprune::world
