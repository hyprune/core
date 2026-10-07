#include "Spatial.hpp"
#include <fstream>
#include <libxml/parser.h>
#include <openssl/sha.h>
#include <regex>
#include <set>
namespace hyprune::world {
namespace {
void check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
void nodes(xmlNode *node, size_t &count, int depth) {
    const std::set<std::string> elements{"svg",  "g",        "rect",    "path", "circle", "ellipse",
                                         "line", "polyline", "polygon", "text", "tspan"};
    const std::set<std::string> attributes{"viewBox",
                                           "width",
                                           "height",
                                           "x",
                                           "y",
                                           "x1",
                                           "y1",
                                           "x2",
                                           "y2",
                                           "rx",
                                           "ry",
                                           "cx",
                                           "cy",
                                           "r",
                                           "d",
                                           "points",
                                           "fill",
                                           "stroke",
                                           "stroke-width",
                                           "stroke-linecap",
                                           "stroke-linejoin",
                                           "opacity",
                                           "fill-opacity",
                                           "stroke-opacity",
                                           "font-family",
                                           "font-size",
                                           "font-weight",
                                           "text-anchor",
                                           "transform"};
    for (; node; node = node->next) {
        check(++count <= 4096 && depth <= 32, "map SVG structure limit");
        if (node->type == XML_TEXT_NODE || node->type == XML_COMMENT_NODE)
            continue;
        check(node->type == XML_ELEMENT_NODE && elements.contains(reinterpret_cast<const char *>(node->name)),
              "unsafe map SVG element");
        check(!node->ns || std::string(reinterpret_cast<const char *>(node->ns->href)) == "http://www.w3.org/2000/svg",
              "unsafe map SVG namespace");
        for (auto *a = node->properties; a; a = a->next) {
            check(!a->ns && attributes.contains(reinterpret_cast<const char *>(a->name)), "unsafe map SVG attribute");
            auto *v = xmlNodeListGetString(node->doc, a->children, 1);
            std::string value = v ? reinterpret_cast<const char *>(v) : "";
            xmlFree(v);
            check(value.size() <= 8192 && value.find_first_of("&<>") == std::string::npos &&
                      !std::regex_search(value, std::regex("url|href|://", std::regex::icase)),
                  "unsafe map SVG value");
        }
        nodes(node->children, count, depth + 1);
    }
}
Vec3 xyz(const Json &v) {
    return {v[0], v[1], v[2]};
}
} // namespace
void validateMapSvg(const std::string &svg) {
    check(!svg.empty() && svg.size() <= 65536, "map SVG byte limit");
    // Reject declarations before parsing: neither entities nor a DTD are permitted.
    check(svg.find("<!") == std::string::npos && svg.find("<?") == std::string::npos, "map SVG declarations forbidden");
    auto *doc = xmlReadMemory(svg.data(), svg.size(), "map.svg", nullptr,
                              XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    check(doc, "invalid map SVG");
    try {
        auto *root = xmlDocGetRootElement(doc);
        check(root && std::string(reinterpret_cast<const char *>(root->name)) == "svg", "map root must be SVG");
        size_t count = 0;
        nodes(root, count, 0);
    } catch (...) {
        xmlFreeDoc(doc);
        throw;
    }
    xmlFreeDoc(doc);
}
void loadSpatial(World &w, const std::filesystem::path &package) {
    w.places = Json::array();
    w.map = nullptr;
    w.walkingNodes = Json::array();
    w.walkingEdges = Json::array();
    const auto ext = w.manifest.value("extensions", Json::object());
    const bool lumenRoutes = ext.contains("com.hyprune.lumen-reach.routes");
    if (lumenRoutes || ext.contains("com.hyprune.switchyard.routes")) {
        auto graph = ext.at(lumenRoutes ? "com.hyprune.lumen-reach.routes" : "com.hyprune.switchyard.routes");
        if (lumenRoutes)
            check(graph.at("version") == 1, "unsupported Lumen Reach walking graph version");
        auto ns = graph.at("nodes");
        check(ns.is_array() && ns.size() <= 64, "map place limit");
        std::set<std::string> ids;
        for (auto &n : ns) {
            auto id = n.at("id").get<std::string>();
            check(id.size() <= 64 && std::regex_match(id, std::regex("[a-z][a-z0-9-]*")) && ids.insert(id).second,
                  "invalid map place ID");

            auto position = n.at("position");
            check(position.is_array() && position.size() == 3, "invalid map place");
            for (auto &v : position)
                check(v.is_number() && std::isfinite(v.get<double>()) && std::abs(v.get<double>()) <= 100000,
                      "invalid map place coordinate");
            position[1] = position[1].get<float>() + 1.7f;
            if (lumenRoutes) {
                check(!n.contains("waypoint") || n.at("waypoint").is_boolean(), "route waypoint must be boolean");
                std::string label = id;
                if (n.contains("label")) {
                    check(n.at("label").is_string(), "route label must be string");
                    label = n.at("label").get<std::string>();
                    size_t chars = 0;
                    for (unsigned char c : label)
                        if ((c & 0xc0) != 0x80)
                            ++chars;
                    check(chars > 0 && chars <= 256, "route label length must be 1..256 characters");
                }
                w.walkingNodes.push_back({{"id", id}, {"position", position}});
                if (n.value("waypoint", true))
                    w.places.push_back({{"id", id}, {"name", label}, {"position", position}});
            } else if (!id.starts_with("junction-"))
                w.places.push_back({{"id", n.at("id")}, {"name", n.at("id")}, {"position", position}});
        }
        if (lumenRoutes) {
            auto edges = graph.at("edges");
            check(edges.is_array() && edges.size() <= 256, "walking edge limit");
            std::set<std::pair<std::string, std::string>> pairs;
            for (auto &edge : edges) {
                auto a = edge.at("from").get<std::string>(), b = edge.at("to").get<std::string>();
                double width = edge.at("width");
                check(a != b && ids.contains(a) && ids.contains(b) && std::isfinite(width) && width >= .6 &&
                          width <= 100,
                      "invalid walking edge or capsule clearance");
                check(pairs.emplace(std::min(a, b), std::max(a, b)).second, "duplicate walking edge");
                w.walkingEdges.push_back({{"from", a}, {"to", b}, {"width", width}});
            }
        }
    } else
        for (auto &s : w.spawns)
            w.places.push_back(
                {{"id", s.id}, {"name", s.id}, {"position", {s.position.x, s.position.y + 1.7f, s.position.z}}});
    const bool legacy = ext.contains("com.hyprune.switchyard.map");
    if (!ext.contains("com.hyprune.map") && !legacy)
        return;
    const auto &m = ext.at(ext.contains("com.hyprune.map") ? "com.hyprune.map" : "com.hyprune.switchyard.map");
    check(m.at("version") == "0.1" && m.at("projection") == "xz" && m.at("north") == Json::array({0, 0, -1}),
          "unsupported map profile");
    auto b = m.at("bounds");
    for (auto &v : b)
        check(v.is_number() && std::isfinite(v.get<double>()), "invalid map bound");
    check(b.is_array() && b.size() == 4 && b[0] < b[2] && b[1] < b[3], "invalid map bounds");
    auto transform =
        m.value("worldToMap", legacy ? Json::array({19. / 900, 0, 0, -18. / 1000, .5, .2}) : Json(nullptr));
    check(transform.is_array() && transform.size() == 6, "map affine transform required");
    for (auto &v : transform)
        check(v.is_number() && std::isfinite(v.get<double>()), "invalid map affine");
    auto path = safePath(package, m.at("path"));
    check(std::filesystem::file_size(path) <= 65536, "map SVG byte limit");
    std::ifstream input(path);
    std::string svg((std::istreambuf_iterator<char>(input)), {});
    validateMapSvg(svg);
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(svg.data()), svg.size(), digest);
    std::string hash;
    const char *hex = "0123456789abcdef";
    for (auto byte : digest) {
        hash += hex[byte >> 4];
        hash += hex[byte & 15];
    }
    w.map = {{"svg", svg}, {"sha256", hash}, {"bounds", b}, {"worldToMap", transform}};
}
std::vector<Vec3> walkingRoute(const World &w, Vec3 from, Vec3 to) {
    if (w.walkingNodes.empty())
        return {};
    const size_t count = w.walkingNodes.size();
    auto position = [&](size_t i) { return xyz(w.walkingNodes[i].at("position")); };
    auto nearest = [&](Vec3 point) {
        size_t best = 0;
        for (size_t i = 1; i < count; i++)
            if (length(position(i) - point) < length(position(best) - point))
                best = i;
        return best;
    };
    size_t start = nearest(from), goal = nearest(to);
    // Only expand declared destinations. Explicit off-graph routes keep their old semantics.
    if (length(position(goal) - to) > .25f)
        return {};
    std::vector<float> distance(count, INFINITY);
    std::vector<size_t> previous(count, count);
    std::vector<bool> visited(count, false);
    distance[start] = 0;
    for (size_t n = 0; n < count; n++) {
        size_t current = count;
        for (size_t i = 0; i < count; i++)
            if (!visited[i] && (current == count || distance[i] < distance[current]))
                current = i;
        if (current == count || !std::isfinite(distance[current]))
            break;
        visited[current] = true;
        if (current == goal)
            break;
        for (auto &edge : w.walkingEdges) {
            std::string nextId;
            if (edge.at("from") == w.walkingNodes[current].at("id"))
                nextId = edge.at("to");
            else if (edge.at("to") == w.walkingNodes[current].at("id"))
                nextId = edge.at("from");
            else
                continue;
            for (size_t next = 0; next < count; next++)
                if (w.walkingNodes[next].at("id") == nextId) {
                    float candidate = distance[current] + length(position(next) - position(current));
                    if (candidate < distance[next]) {
                        distance[next] = candidate;
                        previous[next] = current;
                    }
                }
        }
    }
    check(std::isfinite(distance[goal]), "walking destination is disconnected");
    std::vector<Vec3> route;
    for (size_t i = goal;; i = previous[i]) {
        route.push_back(position(i));
        if (i == start)
            break;
    }
    std::reverse(route.begin(), route.end());
    return route;
}
Json projectMarkers(const World &w, const sim::Camera &camera, float aspect) {
    Json result = Json::array();
    auto view = camera.view();
    float f = 1 / std::tan(camera.fov * .01745329252f / 2);
    for (const auto &p : w.places) {
        auto pos = xyz(p.at("position"));
        auto eye = view.point(pos);
        float depth = -eye.z;
        if (depth < .05f || depth > 500)
            continue;
        float x = .5f + .5f * eye.x * f / (aspect * depth), y = .5f - .5f * eye.y * f / depth;
        if (x < 0 || x > 1 || y < 0 || y > 1)
            continue;
        auto ray = pos - camera.position;
        float distance = length(ray);
        auto direction = normalized(ray);
        bool occluded = false;
        for (const auto &t : w.collision) {
            auto a = t.b - t.a, b = t.c - t.a, pv = cross(direction, b);
            float det = dot(a, pv);
            if (std::abs(det) < 1e-7f)
                continue;
            auto offset = camera.position - t.a;
            float u = dot(offset, pv) / det;
            if (u < 0 || u > 1)
                continue;
            auto q = cross(offset, a);
            float v = dot(direction, q) / det;
            if (v < 0 || u + v > 1)
                continue;
            float d = dot(b, q) / det;
            if (d > 0 && d < distance - .05f) {
                occluded = true;
                break;
            }
        }
        result.push_back({{"id", p.at("id")},
                          {"label", p.at("name")},
                          {"x", x},
                          {"y", y},
                          {"distance", distance},
                          {"occluded", occluded}});
    }
    return result;
}
} // namespace hyprune::world
