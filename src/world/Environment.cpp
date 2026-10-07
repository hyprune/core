#include "Environment.hpp"
#include "Mounts.hpp"
#include <numbers>
namespace hyprune::world {
namespace {
constexpr float pi = std::numbers::pi_v<float>;
Vec3 vec(const Json &j) {
    return {j[0], j[1], j[2]};
}
float linear(float x) {
    return x <= .04045f ? x / 12.92f : std::pow((x + .055f) / 1.055f, 2.4f);
}
Vec3 direction(int f, float x, float y) {
    switch (f) {
    case 0:
        return normalized({1, -y, -x});
    case 1:
        return normalized({-1, -y, x});
    case 2:
        return normalized({x, 1, y});
    case 3:
        return normalized({x, -1, -y});
    case 4:
        return normalized({x, -y, 1});
    default:
        return normalized({-x, -y, -1});
    }
}
struct Profile {
    Vec3 horizon, zenith, ground, sunDirection, sunColor;
    float sunEdge, sunIntensity, intensity;
    bool srgb;
};
Vec3 sample(const World &w, const Profile &p, Vec3 d) {
    if (w.environment[0].width) {
        float ax = std::abs(d.x), ay = std::abs(d.y), az = std::abs(d.z), u, v;
        int f;
        if (ax >= ay && ax >= az) {
            f = d.x > 0 ? 0 : 1;
            u = (d.x > 0 ? -d.z : d.z) / ax;
            v = -d.y / ax;
        } else if (ay >= az) {
            f = d.y > 0 ? 2 : 3;
            u = d.x / ay;
            v = (d.y > 0 ? d.z : -d.z) / ay;
        } else {
            f = d.z > 0 ? 4 : 5;
            u = (d.z > 0 ? d.x : -d.x) / az;
            v = -d.y / az;
        }
        const auto &im = w.environment[f];
        float x = (u * .5f + .5f) * im.width - .5f, y = (v * .5f + .5f) * im.height - .5f;
        auto pixel = [&](int ix, int iy) {
            ix = std::clamp(ix, 0, im.width - 1);
            iy = std::clamp(iy, 0, im.height - 1);
            size_t k = (size_t(iy) * im.width + ix) * 4;
            Vec3 c{im.rgba[k] / 255.f, im.rgba[k + 1] / 255.f, im.rgba[k + 2] / 255.f};
            if (p.srgb)
                c = {linear(c.x), linear(c.y), linear(c.z)};
            return c;
        };
        int ix = int(std::floor(x)), iy = int(std::floor(y));
        float fx = x - ix, fy = y - iy;
        return (pixel(ix, iy) * (1 - fx) * (1 - fy) + pixel(ix + 1, iy) * fx * (1 - fy) +
                pixel(ix, iy + 1) * (1 - fx) * fy + pixel(ix + 1, iy + 1) * fx * fy) *
               p.intensity;
    }
    float t = std::pow(std::abs(d.y), .6f);
    Vec3 c = p.horizon * (1 - t) + (d.y >= 0 ? p.zenith : p.ground) * t;
    float a = dot(d, p.sunDirection),
          disc = std::clamp((a - p.sunEdge) / std::max(.00001f, (1 - p.sunEdge) * .1f), 0.f, 1.f);
    return c + p.sunColor * (p.sunIntensity * disc);
}
float radical(unsigned x) {
    x = (x << 16) | (x >> 16);
    x = ((x & 0x55555555u) << 1) | ((x & 0xaaaaaaaau) >> 1);
    x = ((x & 0x33333333u) << 2) | ((x & 0xccccccccu) >> 2);
    x = ((x & 0x0f0f0f0fu) << 4) | ((x & 0xf0f0f0f0u) >> 4);
    x = ((x & 0x00ff00ffu) << 8) | ((x & 0xff00ff00u) >> 8);
    return x * 2.3283064365386963e-10f;
}
} // namespace
std::vector<CubeLevel> prefilterEnvironment(const World &w, int resolution) {
    auto p = w.atmosphere.is_object() ? w.atmosphere : defaultAtmosphere();
    auto sky = p.at("sky"), sun = sky.at("sun");
    Profile config{vec(sky["horizon"]),
                   vec(sky["zenith"]),
                   vec(sky["ground"]),
                   normalized(vec(sun["direction"])),
                   vec(sun["color"]),
                   std::cos(sun["angularRadius"].get<float>()),
                   sun["intensity"],
                   1,
                   true};
    if (p.contains("environment")) {
        config.intensity = p["environment"]["intensity"];
        config.srgb = p["environment"]["encoding"] == "srgb";
    }
    int size = 16;
    while (size * 2 <= resolution)
        size *= 2;
    int levels = int(std::log2(size)) + 1;
    std::vector<CubeLevel> result;
    for (int mip = 0; mip < levels; mip++) {
        CubeLevel level;
        level.size = std::max(1, size >> mip);
        float rough = float(mip) / (levels - 1), a = rough * rough;
        for (int face = 0; face < 6; face++) {
            auto &out = level.faces[face];
            out.reserve(level.size * level.size * 4);
            for (int y = 0; y < level.size; y++)
                for (int x = 0; x < level.size; x++) {
                    auto n = direction(face, 2 * (x + .5f) / level.size - 1, 2 * (y + .5f) / level.size - 1);
                    Vec3 c;
                    if (mip == 0)
                        c = sample(w, config, n);
                    else {
                        Vec3 up = std::abs(n.z) < .999f ? Vec3{0, 0, 1} : Vec3{1, 0, 0}, t = normalized(cross(up, n)),
                             b = cross(n, t);
                        float weight = 0;
                        for (unsigned i = 0; i < 64; i++) {
                            float phi = 2 * pi * (i / 64.f), xi = radical(i),
                                  ct = std::sqrt((1 - xi) / (1 + (a * a - 1) * xi)),
                                  st = std::sqrt(std::max(0.f, 1 - ct * ct));
                            auto h = t * (std::cos(phi) * st) + b * (std::sin(phi) * st) + n * ct,
                                 l = normalized(h * (2 * dot(n, h)) - n);
                            float nl = std::max(0.f, dot(n, l));
                            c += sample(w, config, l) * nl;
                            weight += nl;
                        }
                        c = c / std::max(.0001f, weight);
                    }
                    out.insert(out.end(), {c.x, c.y, c.z, 1});
                }
        }
        result.push_back(std::move(level));
    }
    return result;
}
} // namespace hyprune::world
