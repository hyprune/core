#include "Catalog.hpp"
#include "Format.hpp"
#include "Schema.hpp"
#include <cgltf.h>
#include <cstring>
#include <functional>
#include <fcntl.h>
#include <openssl/sha.h>
#include <regex>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
extern "C" {
// The single-file zstd decoder vendored with Basis Universal (third_party/basisu/zstd).
typedef struct ZSTD_DCtx_s ZSTD_DStream;
typedef struct {
    const void *src;
    size_t size, pos;
} ZSTD_inBuffer;
typedef struct {
    void *dst;
    size_t size, pos;
} ZSTD_outBuffer;
ZSTD_DStream *ZSTD_createDStream(void);
size_t ZSTD_freeDStream(ZSTD_DStream *);
size_t ZSTD_initDStream(ZSTD_DStream *);
size_t ZSTD_decompressStream(ZSTD_DStream *, ZSTD_outBuffer *, ZSTD_inBuffer *);
unsigned ZSTD_isError(size_t);
const char *ZSTD_getErrorName(size_t);
}
namespace hyprune::world {
namespace fs = std::filesystem;
namespace {
using objects::File;
using objects::Limits;
constexpr size_t ManifestBytes = 1048576;
const char *Slug = "[a-z0-9]([a-z0-9-]{0,62}[a-z0-9])?", *ObjSlug = "[a-z][a-z0-9-]*", *LibId = "[a-z][a-z0-9-]*(\\.[a-z][a-z0-9-]*)+";
const char *SemVer = "(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?(\\+[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?";
// Content problems (unsafe, invalid or over a limit), as opposed to I/O faults.
struct Failure : std::invalid_argument {
    using std::invalid_argument::invalid_argument;
};
void require(bool ok, const std::string &why) {
    if (!ok)
        throw Failure(why);
}
// RFC-0004 path safety for one directory-relative path (no empty, '.', '..' parts,
// no absolute paths, no \ : % ? # or control characters).
bool safeRelative(const std::string &path) {
    if (path.empty() || path.front() == '/')
        return false;
    for (unsigned char c : path)
        if (c < 0x20 || c == '\\' || c == ':' || c == '%' || c == '?' || c == '#')
            return false;
    size_t start = 0;
    while (true) {
        auto end = path.find('/', start);
        auto part = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part.empty() || part == "." || part == "..")
            return false;
        if (end == std::string::npos)
            return true;
        start = end + 1;
    }
}
bool executable(const std::string &path, mode_t mode) {
    static const std::regex ext("\\.(js|mjs|cjs|py|sh|lua|so|wasm|html|exe)$", std::regex::icase);
    return std::regex_search(path, ext) || (mode & 0111);
}
struct Inventory {
    std::vector<File> files;
    std::string signature; // paths, sizes and mtimes
};
// The SDK's inventory(): a plain directory tree, sorted, no links, devices or executable
// content, within the limits.
Inventory inventory(const fs::path &root, size_t maxFiles, uint64_t maxFile, uint64_t maxTotal, const std::string &label) {
    struct stat st{};
    require(lstat(root.c_str(), &st) == 0 && S_ISDIR(st.st_mode), label + " root must be a plain directory");
    Inventory out;
    uint64_t total = 0;
    std::function<void(const fs::path &, const std::string &, int)> walk = [&](const fs::path &dir, const std::string &prefix,
                                                                             int depth) {
        require(depth <= 64, "directory depth exceeds 64");
        std::vector<std::string> names;
        for (auto &e : fs::directory_iterator(dir))
            names.push_back(e.path().filename().string());
        std::sort(names.begin(), names.end());
        for (auto &name : names) {
            auto full = dir / name;
            auto path = prefix.empty() ? name : prefix + "/" + name;
            struct stat s{};
            require(lstat(full.c_str(), &s) == 0, "unreadable: " + path);
            require(!S_ISLNK(s.st_mode) && (S_ISDIR(s.st_mode) || S_ISREG(s.st_mode)), "links/devices are forbidden: " + path);
            if (S_ISDIR(s.st_mode)) {
                walk(full, path, depth + 1);
                continue;
            }
            require(safeRelative(path), "unsafe path: " + path);
            total += s.st_size;
            require(uint64_t(s.st_size) <= maxFile && total <= maxTotal && out.files.size() < maxFiles,
                    label + " directory exceeds " + std::to_string(maxFiles) + " files, " + std::to_string(maxTotal >> 20) +
                        " MiB or " + std::to_string(maxFile >> 20) + " MiB per file: " + path);
            require(!executable(path, s.st_mode), "Executable " + std::string(label == "Object" ? "object" : "library") + " content rejected: " + path);
            out.files.push_back({path, uint64_t(s.st_size)});
            out.signature += path + '\0' + std::to_string(s.st_size) + '\0' + std::to_string(s.st_mtim.tv_sec) + '.' +
                             std::to_string(s.st_mtim.tv_nsec) + '\n';
        }
    };
    walk(root, "", 0);
    return out;
}
// A no-follow read of a regular file of at most `limit` bytes.
std::string readFile(const fs::path &root, const std::string &path, uint64_t limit) {
    require(safeRelative(path), "unsafe path: " + path);
    auto full = root / path;
    int fd = open(full.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    require(fd >= 0, "cannot read " + path);
    struct stat st{};
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || uint64_t(st.st_size) > limit) {
        close(fd);
        throw Failure("asset must be a regular file <= " + std::to_string(limit >> 20) + " MiB: " + path);
    }
    std::string bytes(st.st_size, '\0');
    size_t done = 0;
    while (done < bytes.size()) {
        auto n = read(fd, bytes.data() + done, bytes.size() - done);
        if (n <= 0)
            break;
        done += n;
    }
    close(fd);
    require(done == bytes.size(), "short read: " + path);
    return bytes;
}
std::string hex(const unsigned char *bytes, size_t n) {
    static const char *digits = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < n; ++i)
        out += digits[bytes[i] >> 4], out += digits[bytes[i] & 15];
    return out;
}
// The SDK's packageDigest(): sha256 over path \0 size \0 bytes of every file in order.
std::string digestOf(const fs::path &root, const std::vector<File> &files) {
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    for (auto &f : files) {
        auto head = f.path + '\0' + std::to_string(f.size) + '\0';
        SHA256_Update(&ctx, head.data(), head.size());
        auto bytes = readFile(root, f.path, Limits::fileBytes);
        SHA256_Update(&ctx, bytes.data(), bytes.size());
    }
    unsigned char out[SHA256_DIGEST_LENGTH];
    SHA256_Final(out, &ctx);
    return hex(out, sizeof out);
}
Json parseJson(const std::string &bytes, const std::string &what) {
    try {
        return Json::parse(bytes);
    } catch (const std::exception &) {
        throw Failure(what + ": invalid JSON");
    }
}
struct Scene {
    Json gltf;
    std::string bin; // GLB binary chunk
};
Scene gltfDocument(const std::string &bytes, const std::string &path) {
    if (path.ends_with(".gltf"))
        return {parseJson(bytes, path), {}};
    auto u32 = [&](size_t at) {
        uint32_t v = 0;
        std::memcpy(&v, bytes.data() + at, 4);
        return v;
    };
    require(bytes.size() >= 20 && u32(0) == 0x46546c67 && u32(4) == 2 && u32(8) == bytes.size() && u32(16) == 0x4e4f534a,
            "invalid GLB header/JSON chunk");
    auto length = u32(12);
    require(length <= bytes.size() - 20, "invalid GLB chunk length");
    Scene s{parseJson(bytes.substr(20, length), path), {}};
    size_t at = 20 + length;
    if (at + 8 <= bytes.size() && u32(at + 4) == 0x004e4942)
        s.bin = bytes.substr(at + 8, std::min<size_t>(u32(at), bytes.size() - at - 8));
    return s;
}
std::string resourcePath(const std::string &scene, const std::string &uri) {
    require(safeRelative(uri), "unsafe glTF URI: " + uri);
    auto slash = scene.rfind('/');
    return slash == std::string::npos ? uri : scene.substr(0, slash) + "/" + uri;
}
std::vector<std::string> firstErrors(std::vector<std::string> errors) {
    if (errors.size() > 8)
        errors.resize(8);
    for (auto &e : errors)
        if (e.size() > 512)
            e = e.substr(0, 509) + "...";
    return errors;
}
std::string libraryKey(const std::string &id, const std::string &version) { return id + "@" + version; }
// SemVer precedence (prerelease < release; build ignored).
bool semverLess(const std::string &a, const std::string &b) {
    auto split = [](const std::string &v) {
        auto core = v.substr(0, v.find('+'));
        auto dash = core.find('-');
        std::string pre = dash == std::string::npos ? "" : core.substr(dash + 1);
        core = core.substr(0, dash);
        std::array<long, 3> n{};
        sscanf(core.c_str(), "%ld.%ld.%ld", &n[0], &n[1], &n[2]);
        return std::pair{n, pre};
    };
    auto [na, pa] = split(a);
    auto [nb, pb] = split(b);
    if (na != nb)
        return na < nb;
    if (pa.empty() != pb.empty())
        return !pa.empty();
    return pa < pb;
}
} // namespace
bool versionLess(const std::string &a, const std::string &b) { return semverLess(a, b); }
const std::map<std::string, std::string> &objectMethods() {
    static const std::map<std::string, std::string> methods{
        {"objects.catalog", "state.read"},         {"objects.pocket", "objects.control"},
        {"objects.place", "objects.control"},      {"objects.transform", "objects.control"},
        {"objects.remove", "objects.control"},     {"objects.hide", "objects.control"},
        {"objects.reset", "objects.control"},      {"objects.configure", "objects.control"},
        {"libraries.list", "state.read"},          {"libraries.install", "objects.control"},
        {"libraries.remove", "objects.control"},
        // IPC 0.23 (RFC-0011 P2): characters, paths and interactions.
        {"npcs.spawn", "objects.control"},         {"npcs.bind", "objects.control"},
        {"npcs.disable", "objects.control"},       {"npcs.remove", "objects.control"},
        {"paths.list", "state.read"},              {"paths.set", "objects.control"},
        {"paths.remove", "objects.control"},       {"paths.draw", "objects.control"},
        {"paths.finish", "objects.control"},       {"interaction.choose", "world.control"},
        // IPC 0.25 (RFC-0010 companions amendment).
        {"companion.set", "objects.control"},      {"companion.clear", "objects.control"},
        {"companion.mode", "objects.control"}};
    return methods;
}
std::optional<ObjectRef> parseRef(const std::string &ref) {
    static const std::regex package(std::string("^package/(") + Slug + ")$"),
        library(std::string("^library/(") + LibId + ")@(" + SemVer + ")/(" + ObjSlug + ")$"), user(std::string("^user/(") + ObjSlug + ")$"),
        folder(std::string("^world/(") + ObjSlug + ")$");
    std::smatch m;
    if (ref.size() > 256)
        return std::nullopt;
    if (std::regex_match(ref, m, package))
        return ObjectRef{"package", "", "", m[1]};
    if (std::regex_match(ref, m, user))
        return ObjectRef{"user", "", "", m[1]};
    if (std::regex_match(ref, m, folder)) // a world folder's copy (RFC-0014, IPC 0.27)
        return ObjectRef{"world", "", "", m[1]};
    if (std::regex_match(ref, m, library)) {
        // groups: 1 id, 2 (repeat), 3 version, then the version's subgroups, last the object
        return ObjectRef{"library", m[1], m[3], m[m.size() - 1]};
    }
    return std::nullopt;
}
Json CatalogEntry::json() const {
    return {{"ref", ref},
            {"source", source},
            {"id", id},
            {"version", version},
            {"formatVersion", formatVersion},
            {"name", name},
            {"description", description},
            {"category", category},
            {"tags", tags},
            {"traits", traits},
            {"surfaces", surfaces},
            {"scale", scale},
            {"bounds", bounds},
            {"triangles", triangles},
            {"gpuBytes", gpuBytes},
            {"collisionTriangles", collisionTriangles},
            {"thumbnail", thumbnail},
            {"license", license},
            {"library", library},
            {"versions", versions},
            {"state", state},
            {"placeable", placeable},
            {"reason", reason.empty() ? Json(nullptr) : Json(reason)},
            {"errors", firstErrors(errors)}};
}
Json LibraryRecord::json() const {
    return {{"id", id},         {"version", version}, {"name", name},     {"formatVersion", formatVersion},
            {"objects", objects}, {"root", root},     {"state", state},   {"errors", firstErrors(errors)},
            {"digest", digest.size() == 64 ? Json(digest) : Json(nullptr)}};
}
CatalogRoots CatalogRoots::fromEnvironment() {
    CatalogRoots r;
    fs::path data = getenv("XDG_DATA_HOME") && getenv("XDG_DATA_HOME")[0] == '/' ? fs::path(getenv("XDG_DATA_HOME"))
                                                                                 : fs::path(getenv("HOME") ? getenv("HOME") : "/") / ".local/share";
    r.userObjects = getenv("HYPRUNE_OBJECT_DIR") && getenv("HYPRUNE_OBJECT_DIR")[0] == '/' ? fs::path(getenv("HYPRUNE_OBJECT_DIR"))
                                                                                         : data / "hyprune/objects";
    r.userLibraries = data / "hyprune/libraries";
    if (auto list = getenv("HYPRUNE_LIBRARY_DIR")) {
        std::string text = list;
        size_t start = 0;
        while (start <= text.size()) {
            auto end = text.find(':', start);
            auto part = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!part.empty() && part[0] == '/' && fs::path(part) != r.userLibraries)
                r.systemLibraries.push_back(part);
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
    }
    return r;
}
std::optional<ImageHeader> imageHeader(const unsigned char *b, size_t n) {
    auto be32 = [&](size_t at) { return uint32_t(b[at]) << 24 | uint32_t(b[at + 1]) << 16 | uint32_t(b[at + 2]) << 8 | b[at + 3]; };
    auto le32 = [&](size_t at) { return uint32_t(b[at + 3]) << 24 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 1]) << 8 | b[at]; };
    if (n >= 24 && be32(0) == 0x89504e47 && be32(4) == 0x0d0a1a0a && std::memcmp(b + 12, "IHDR", 4) == 0)
        return ImageHeader{"png", int(be32(16)), int(be32(20))};
    static const unsigned char ktx2[12]{0xab, 0x4b, 0x54, 0x58, 0x20, 0x32, 0x30, 0xbb, 0x0d, 0x0a, 0x1a, 0x0a};
    if (n >= 28 && std::memcmp(b, ktx2, 12) == 0)
        return ImageHeader{"ktx2", int(le32(20)), int(std::max<uint32_t>(1, le32(24)))};
    if (n >= 4 && b[0] == 0xff && b[1] == 0xd8)
        for (size_t i = 2; i + 9 < n;) {
            if (b[i] != 0xff)
                return std::nullopt;
            unsigned marker = b[i + 1];
            if (marker == 0xff) {
                ++i;
                continue;
            }
            if (marker == 0xd8 || marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) {
                i += 2;
                continue;
            }
            if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc)
                return ImageHeader{"jpeg", int(b[i + 7]) << 8 | b[i + 8], int(b[i + 5]) << 8 | b[i + 6]};
            i += 2 + (size_t(b[i + 2]) << 8 | b[i + 3]);
        }
    return std::nullopt;
}
void decidePlaceable(CatalogEntry &e) {
    e.placeable = false;
    e.reason.clear();
    if (e.state != "ready") {
        e.reason = "invalid: " + (e.errors.empty() ? std::string("the object failed validation") : e.errors[0]);
        return;
    }
    auto newer = [&] {
        return e.formatVersion.is_string() && std::find(objects::formats().begin(), objects::formats().end(),
                                                        e.formatVersion.get<std::string>()) == objects::formats().end();
    };
    // Characters (RFC-0011 P2): spawned on a route or a standing spot (npcs.spawn), not placed.
    if (e.has("character")) {
        if (newer())
            e.reason = "format: object format " + e.formatVersion.get<std::string>() + " is newer than this core";
        else
            e.placeable = true;
        return;
    }
    if (e.has("mover")) {
        e.reason = "mover: tracks and items need world format 0.10 (RFC-0011 P3)";
        return;
    }
    for (auto &t : e.traits)
        if (t != "decorative" && t != "collidable" && t != "movable" && t != "seat" && t != "machine" && t != "mount") {
            e.reason = t + ": not placeable in this core";
            return;
        }
    if (!e.movable()) {
        e.reason = "not movable: only objects with the movable trait can be placed";
        return;
    }
    if (e.formatVersion.is_string() &&
        std::find(objects::formats().begin(), objects::formats().end(), e.formatVersion.get<std::string>()) == objects::formats().end()) {
        e.reason = "format: object format " + e.formatVersion.get<std::string>() + " is newer than this core";
        return;
    }
    e.placeable = true;
}
CatalogEntry scanObject(const fs::path &dir, const std::string &ref, const std::string &source, ScanCache *cache) {
    CatalogEntry e;
    e.ref = ref;
    e.source = source;
    e.directory = dir;
    e.id = dir.filename().string();
    e.name = e.id.substr(0, 64);
    if (auto r = parseRef(ref); r && r->source == "library")
        e.library = r->libraryKey();
    std::vector<std::string> errors, warnings;
    try {
        auto inv = inventory(dir, objects::HardCaps::files, objects::HardCaps::fileBytes, objects::HardCaps::directoryBytes, "Object");
        if (cache) {
            std::lock_guard guard(cache->lock);
            auto it = cache->entries.find(dir.string() + "\n" + ref);
            if (it != cache->entries.end() && it->second.first == inv.signature)
                return it->second.second;
        }
        auto manifest = parseJson(readFile(dir, "object.json", objects::HardCaps::fileBytes), "object.json");
        require(manifest.dump().size() <= ManifestBytes, "object.json exceeds 1 MiB");
        e.manifest = manifest;
        if (manifest.is_object()) {
            auto text = [&](const char *key, Json &into) {
                if (manifest.contains(key) && manifest[key].is_string())
                    into = manifest[key];
            };
            text("version", e.version);
            text("formatVersion", e.formatVersion);
            text("description", e.description);
            text("license", e.license);
            if (manifest.contains("name") && manifest["name"].is_string() && !manifest["name"].get<std::string>().empty())
                e.name = manifest["name"].get<std::string>().substr(0, 64);
            if (manifest.contains("category") && manifest["category"].is_string())
                e.category = manifest["category"];
            if (manifest.contains("tags") && manifest["tags"].is_array())
                for (auto &t : manifest["tags"])
                    if (t.is_string() && e.tags.size() < 8)
                        e.tags.push_back(t);
            if (manifest.contains("traits") && manifest["traits"].is_object())
                for (auto &[name, value] : manifest["traits"].items())
                    e.traits.push_back(name);
            if (manifest.contains("bounds") && manifest["bounds"].is_object())
                e.bounds = manifest["bounds"];
            if (manifest.contains("traits") && manifest["traits"].contains("movable") && manifest["traits"]["movable"].is_object()) {
                auto &m = manifest["traits"]["movable"];
                e.surfaces = m.value("surfaces", Json::array({"floor"}));
                e.scale = m.value("scale", Json::array({1, 1}));
            }
        }
        // Scene: the glTF (or GLB JSON chunk) and its resources, all files of the directory.
        std::optional<Scene> scene;
        auto has = [&](const std::string &path) {
            return std::any_of(inv.files.begin(), inv.files.end(), [&](auto &f) { return f.path == path; });
        };
        std::string scenePath = manifest.is_object() && manifest.contains("scene") && manifest["scene"].is_string() ? manifest["scene"].get<std::string>() : "";
        if (!scenePath.empty() && safeRelative(scenePath) && has(scenePath)) {
            try {
                auto bytes = readFile(dir, scenePath, objects::HardCaps::fileBytes);
                scene = gltfDocument(bytes, scenePath);
                std::vector<std::string> missing;
                for (auto key : {"buffers", "images"})
                    if (scene->gltf.contains(key) && scene->gltf[key].is_array())
                        for (auto &r : scene->gltf[key])
                            if (r.is_object() && r.contains("uri") && r["uri"].is_string()) {
                                auto p = resourcePath(scenePath, r["uri"]);
                                if (!has(p))
                                    missing.push_back(p);
                            }
                require(missing.empty(), "missing glTF resource " + [&] {
                    std::string s;
                    for (auto &m : missing)
                        s += (s.empty() ? "" : ", ") + m;
                    return s;
                }());
                // Structural glTF validation (accessor, view and buffer ranges) with cgltf.
                cgltf_options options{};
                cgltf_data *raw = nullptr;
                require(cgltf_parse(&options, bytes.data(), bytes.size(), &raw) == cgltf_result_success, "glTF parse failed");
                std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, cgltf_free);
                auto full = (dir / scenePath).string();
                require(cgltf_load_buffers(&options, raw, full.c_str()) == cgltf_result_success, "glTF buffers failed to load");
                require(cgltf_validate(raw) == cgltf_result_success, "glTF validation failed");
            } catch (const std::exception &x) {
                errors.push_back(std::string("scene: ") + x.what());
                scene.reset();
            }
        }
        // Textures: the largest dimension and the SDK's GPU estimate (PNG/JPEG 4 B/px,
        // KTX2 1 B/px, x4/3 for mips, plus every buffer's byteLength).
        objects::Options options;
        uint64_t gpu = 0;
        int maxTexture = 0;
        if (scene && errors.empty()) {
            auto &g = scene->gltf;
            for (auto &b : g.value("buffers", Json::array()))
                if (b.is_object() && b.contains("byteLength") && b["byteLength"].is_number())
                    gpu += b["byteLength"].get<uint64_t>();
            auto images = g.value("images", Json::array());
            for (size_t i = 0; i < images.size(); ++i)
                try {
                    auto &image = images[i];
                    std::string bytes;
                    if (image.contains("uri"))
                        bytes = readFile(dir, resourcePath(scenePath, image["uri"]), objects::HardCaps::fileBytes);
                    else {
                        auto views = g.value("bufferViews", Json::array());
                        size_t v = image.value("bufferView", size_t(-1));
                        require(v < views.size(), "no image data");
                        auto &view = views[v];
                        auto buffers = g.value("buffers", Json::array());
                        size_t b = view.value("buffer", size_t(-1));
                        require(b < buffers.size(), "no image data");
                        std::string data = buffers[b].contains("uri") ? readFile(dir, resourcePath(scenePath, buffers[b]["uri"]), objects::HardCaps::fileBytes)
                                                                      : scene->bin;
                        size_t offset = view.value("byteOffset", 0), length = view.value("byteLength", 0);
                        require(offset + length <= data.size(), "no image data");
                        bytes = data.substr(offset, length);
                    }
                    auto header = imageHeader(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size());
                    require(header.has_value(), "not a PNG, JPEG or KTX2 image");
                    maxTexture = std::max({maxTexture, header->width, header->height});
                    gpu += (uint64_t(header->width) * header->height * (header->format == "ktx2" ? 1 : 4) * 4 + 2) / 3;
                } catch (const std::exception &x) {
                    errors.push_back("images/" + std::to_string(i) + ": " + x.what());
                }
            options.gltf = &g;
            options.measured.maxTextureSize = maxTexture;
            options.measured.gpuBytes = gpu;
            e.gpuBytes = gpu;
        }
        // Thumbnail: the PNG's IHDR size and its bytes.
        std::string thumbPath = manifest.is_object() && manifest.contains("thumbnail") && manifest["thumbnail"].is_string()
                                    ? manifest["thumbnail"].get<std::string>()
                                    : "";
        if (!thumbPath.empty() && safeRelative(thumbPath) && has(thumbPath)) {
            auto bytes = readFile(dir, thumbPath, objects::HardCaps::fileBytes);
            auto header = imageHeader(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size());
            if (!header || header->format != "png")
                errors.push_back("thumbnail: not a PNG image");
            else
                options.thumbnail = objects::Thumbnail{header->width, header->height, bytes.size()};
        }
        options.directory = dir.filename().string();
        options.files = &inv.files;
        // import.json (RFC-0014): checked with the object; it names the source file and when.
        Json importRecord;
        if (has("import.json")) {
            try {
                importRecord = parseJson(readFile(dir, "import.json", objects::HardCaps::fileBytes), "import.json");
                options.importRecord = &importRecord;
            } catch (const std::exception &x) {
                errors.push_back(std::string("import.json: ") + x.what());
            }
        }
        auto result = objects::checkObject(manifest, options);
        errors.insert(errors.begin(), result.errors.begin(), result.errors.end());
        warnings = result.warnings;
        if (result.triangles)
            e.triangles = *result.triangles;
        if (scene && manifest.contains("traits") && manifest["traits"].contains("collidable")) {
            auto proxy = manifest["traits"]["collidable"].value("node", "");
            auto &g = scene->gltf;
            auto nodes = g.value("nodes", Json::array());
            for (auto &n : nodes)
                if (n.value("name", "") == proxy && n.contains("mesh")) {
                    uint64_t count = 0;
                    auto meshes = g.value("meshes", Json::array());
                    if (n["mesh"].get<size_t>() < meshes.size())
                        for (auto &p : meshes[n["mesh"].get<size_t>()].value("primitives", Json::array()))
                            count += objects::primitiveTriangles(g, p);
                    e.collisionTriangles = count;
                    break;
                }
        } else if (scene)
            e.collisionTriangles = 0;
        if (errors.empty() && options.thumbnail)
            e.thumbnail = (dir / thumbPath).string();
        e.digest = digestOf(dir, inv.files);
        e.errors = errors;
        e.warnings.assign(warnings.begin(), warnings.begin() + std::min<size_t>(8, warnings.size()));
        // IPC 0.27 provenance: import.json's record, else the attribution of ".", "unknown" where unrecorded.
        auto known = [](const Json &v, size_t limit) {
            return v.is_string() && !v.get<std::string>().empty() ? Json(v.get<std::string>().substr(0, limit)) : Json("unknown");
        };
        Json attribution = Json::object();
        for (auto &a : manifest.value("attribution", Json::array()))
            if (a.is_object() && a.value("path", "") == ".")
                attribution = a;
        if (importRecord.is_object() && options.importRecord && result.errors.empty()) {
            auto &l = importRecord["license"];
            e.provenance = {{"license", known(l["spdx"], 512)}, {"author", known(l["author"], 256)}, {"source", known(l["source"], 1024)}};
            e.imported = {{"at", importRecord["importedAt"]}, {"sourceName", importRecord["source"]["name"]}};
        } else
            e.provenance = {{"license", known(attribution.value("license", manifest.value("license", Json())), 512)},
                            {"author", known(attribution.value("author", Json()), 256)},
                            {"source", known(attribution.value("source", Json()), 1024)}};
        // primitive.json (RFC-0014 amendment: primitives and materials): how a primitive was
        // generated, for the shell's size and material fields. A bad record is a warning only.
        if (has("primitive.json")) {
            try {
                auto record = parseJson(readFile(dir, "primitive.json", 65536), "primitive.json");
                validateObjectDocument(record, "primitive", "0.11");
                auto &m = record["material"];
                e.primitive = {{"shape", record["shape"]}, {"size", record["size"]},
                               {"material", {{"key", m["key"]}, {"name", m["name"]}, {"colour", m["colour"]}, {"textured", m["textured"]}}}};
            } catch (const std::exception &x) {
                if (e.warnings.size() < 8)
                    e.warnings.push_back(std::string("primitive.json: ") + x.what());
            }
        }
        e.state = errors.empty() ? "ready" : "invalid";
        decidePlaceable(e);
        if (cache) {
            std::lock_guard guard(cache->lock);
            cache->entries[dir.string() + "\n" + ref] = {inv.signature, e};
        }
        return e;
    } catch (const std::exception &x) {
        errors.insert(errors.begin(), x.what());
    }
    e.errors = errors;
    e.state = "invalid";
    decidePlaceable(e);
    return e;
}
namespace {
struct ScannedLibrary {
    LibraryRecord record;
    std::vector<CatalogEntry> entries; // catalog order
};
ScannedLibrary scanLibrary(const fs::path &dir, const std::string &root, ScanCache &cache) {
    ScannedLibrary out;
    auto &r = out.record;
    r.directory = dir;
    r.root = root;
    auto name = dir.filename().string();
    auto at = name.find('@');
    r.id = name.substr(0, at);
    r.version = at == std::string::npos ? "" : name.substr(at + 1);
    try {
        static const std::regex id(std::string("^") + LibId + "$"), version(std::string("^") + SemVer + "$");
        require(at != std::string::npos && std::regex_match(r.id, id) && std::regex_match(r.version, version),
                "directory must be named <id>@<version>");
        auto inv = inventory(dir, Limits::libraryFiles, Limits::fileBytes, Limits::libraryBytes, "Library");
        auto manifest = parseJson(readFile(dir, "library.json", Limits::fileBytes), "library.json");
        if (manifest.is_object()) {
            r.name = manifest.value("name", Json("")).is_string() ? manifest.value("name", std::string()).substr(0, 64) : "";
            if (manifest.contains("formatVersion") && manifest["formatVersion"].is_string())
                r.formatVersion = manifest["formatVersion"];
        }
        std::map<std::string, Json> objects;
        if (fs::is_directory(dir / "objects"))
            for (auto &d : fs::directory_iterator(dir / "objects")) {
                auto oid = d.path().filename().string();
                if (!d.is_directory() || d.is_symlink()) {
                    r.errors.push_back("objects/" + oid + ": not an object directory");
                    continue;
                }
                try {
                    objects[oid] = parseJson(readFile(d.path(), "object.json", Limits::fileBytes), "object.json");
                } catch (const std::exception &x) {
                    r.errors.push_back("objects/" + oid + ": " + x.what());
                }
            }
        auto check = objects::checkLibrary(manifest, &objects, &inv.files);
        r.errors.insert(r.errors.begin(), check.errors.begin(), check.errors.end());
        if (manifest.is_object() && (manifest.value("id", Json()) != r.id || manifest.value("version", Json()) != r.version))
            r.errors.push_back("library.json names " + manifest.value("id", Json("")).dump() + "@" +
                               manifest.value("version", Json("")).dump() + ", not its directory");
        r.digest = digestOf(dir, inv.files);
        if (r.errors.empty()) {
            r.objects = manifest["objects"].size();
            for (auto &oid : manifest["objects"]) {
                auto ref = "library/" + r.id + "@" + r.version + "/" + oid.get<std::string>();
                out.entries.push_back(scanObject(dir / "objects" / oid.get<std::string>(), ref, "library", &cache));
            }
        }
    } catch (const std::exception &x) {
        r.errors.insert(r.errors.begin(), x.what());
    }
    r.state = r.errors.empty() ? "ready" : "invalid";
    return out;
}
} // namespace
CatalogSnapshot scanCatalog(const CatalogRoots &roots, ScanCache &cache) {
    CatalogSnapshot snap;
    // Libraries: every root, user first; the same id@version in two roots must match.
    std::map<std::string, ScannedLibrary> byKey;
    std::vector<std::pair<fs::path, std::string>> rootList{{roots.userLibraries, "user"}};
    for (auto &s : roots.systemLibraries)
        rootList.push_back({s, "system"});
    for (auto &[root, kind] : rootList) {
        std::error_code ec;
        if (!fs::is_directory(root, ec))
            continue;
        std::vector<fs::path> dirs;
        for (auto &d : fs::directory_iterator(root, ec))
            if (d.is_directory() && !d.is_symlink() && !d.path().filename().string().starts_with("."))
                dirs.push_back(d.path());
        std::sort(dirs.begin(), dirs.end());
        for (auto &dir : dirs) {
            if (byKey.size() >= 256)
                break;
            auto lib = scanLibrary(dir, kind, cache);
            auto key = libraryKey(lib.record.id, lib.record.version);
            if (auto it = byKey.find(key); it != byKey.end()) {
                if (it->second.record.digest != lib.record.digest) {
                    it->second.record.state = "conflict";
                    it->second.record.errors.insert(it->second.record.errors.begin(),
                                                    "library.conflict: " + key + " differs in " + dir.string());
                    it->second.entries.clear();
                }
                continue;
            }
            byKey[key] = std::move(lib);
        }
    }
    std::map<std::string, std::vector<std::string>> versions; // id -> ready versions
    for (auto &[key, lib] : byKey) {
        snap.libraries.push_back(lib.record);
        if (lib.record.state != "ready")
            continue;
        versions[lib.record.id].push_back(lib.record.version);
        for (auto &e : lib.entries)
            snap.refs[e.ref] = e;
    }
    for (auto &[id, list] : versions) {
        std::sort(list.begin(), list.end(), semverLess);
        auto &highest = byKey[libraryKey(id, list.back())];
        for (auto e : highest.entries) {
            for (size_t i = 0; i + 1 < list.size(); ++i)
                if (snap.refs.contains("library/" + id + "@" + list[i] + "/" + e.id) && e.versions.size() < 16)
                    e.versions.push_back(list[i]);
            snap.entries.push_back(std::move(e));
        }
    }
    // The user folder: one entry per directory (at most 256).
    std::error_code ec;
    if (fs::is_directory(roots.userObjects, ec)) {
        std::vector<fs::path> dirs;
        for (auto &d : fs::directory_iterator(roots.userObjects, ec))
            if (d.is_directory() && !d.is_symlink() && !d.path().filename().string().starts_with("."))
                dirs.push_back(d.path());
        std::sort(dirs.begin(), dirs.end());
        static const std::regex slug(std::string("^") + ObjSlug + "$");
        for (auto &dir : dirs) {
            if (snap.entries.size() >= 1024 || std::count_if(snap.entries.begin(), snap.entries.end(),
                                                             [](auto &e) { return e.source == "user"; }) >= 256)
                break;
            auto name = dir.filename().string();
            if (!std::regex_match(name, slug) || name.size() > 64)
                continue; // not addressable as user/<id>
            auto e = scanObject(dir, "user/" + name, "user", &cache);
            snap.refs[e.ref] = e;
            snap.entries.push_back(std::move(e));
        }
    }
    if (snap.entries.size() > 1024)
        snap.entries.resize(1024);
    return snap;
}
std::vector<CatalogEntry> packageEntries(const World &w, const fs::path &package, bool devMovable) {
    std::vector<CatalogEntry> out;
    auto assets = w.manifest.value("objectAssets", Json::array());
    for (size_t k = 0; k < w.objectAssets.size() && k < assets.size(); ++k) {
        auto &a = w.objectAssets[k];
        CatalogEntry e;
        e.ref = "package/" + a.id;
        e.source = "package";
        e.id = a.id;
        e.name = a.id.substr(0, 64);
        e.directory = package;
        e.manifest = assets[k];
        if (assets[k].contains("animations") && !assets[k]["animations"].empty())
            e.traits.push_back("decorative");
        if (assets[k].contains("collision"))
            e.traits.push_back("collidable");
        // World 0.9: the asset's traits (inline, object directory or library object); a
        // directory or library asset is described by its object.json.
        if (a.traits.is_object())
            for (auto &[name, value] : a.traits.items())
                if (std::find(e.traits.begin(), e.traits.end(), name) == e.traits.end())
                    e.traits.push_back(name);
        if (a.manifest.is_object()) {
            e.manifest = a.manifest;
            e.directory = a.directory;
            e.name = a.manifest.value("name", a.id).substr(0, 64);
            e.category = a.manifest.value("category", e.category);
            e.version = a.manifest.value("version", Json(nullptr));
            e.formatVersion = a.manifest.value("formatVersion", Json(nullptr));
            e.license = a.manifest.value("license", Json(nullptr));
        } else if (a.traits.is_object() && !a.traits.empty())
            e.manifest["traits"] = a.traits;
        if (a.traits.is_object() && a.traits.contains("movable")) {
            e.surfaces = a.traits["movable"].value("surfaces", Json::array({"floor"}));
            e.scale = a.traits["movable"].value("scale", Json::array({1, 1}));
        }
        if (devMovable && !a.traits.contains("movable") && !a.character) {
            e.traits.push_back("movable");
            e.surfaces = Json::array({"any"});
            e.scale = Json::array({.25, 4});
        }
        std::sort(e.traits.begin(), e.traits.end());
        e.bounds = {{"min", {a.low.x, a.low.y, a.low.z}}, {"max", {a.high.x, a.high.y, a.high.z}}};
        uint64_t triangles = 0;
        for (auto &m : a.meshes)
            triangles += m.vertices.size() / 3;
        e.triangles = triangles;
        e.collisionTriangles = a.collision.size();
        // The SDK's estimate from the asset's glTF (buffers and image headers).
        try {
            auto scenePath = assets[k].value("scene", e.manifest.value("scene", std::string()));
            auto bytes = readFile(e.directory, scenePath, 128u << 20);
            auto scene = gltfDocument(bytes, scenePath);
            uint64_t gpu = 0;
            for (auto &b : scene.gltf.value("buffers", Json::array()))
                gpu += b.value("byteLength", uint64_t(0));
            for (auto &image : scene.gltf.value("images", Json::array()))
                if (image.contains("uri")) {
                    auto data = readFile(e.directory, resourcePath(scenePath, image["uri"]), 128u << 20);
                    if (auto h = imageHeader(reinterpret_cast<const unsigned char *>(data.data()), data.size()))
                        gpu += (uint64_t(h->width) * h->height * (h->format == "ktx2" ? 1 : 4) * 4 + 2) / 3;
                }
            e.gpuBytes = gpu;
        } catch (const std::exception &) {
        }
        decidePlaceable(e);
        out.push_back(std::move(e));
    }
    return out;
}
std::string sha256File(const fs::path &path) {
    int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        throw std::invalid_argument("cannot open archive");
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    std::vector<char> buffer(1 << 20);
    while (true) {
        auto n = read(fd, buffer.data(), buffer.size());
        if (n < 0) {
            close(fd);
            throw std::invalid_argument("cannot read archive");
        }
        if (n == 0)
            break;
        SHA256_Update(&ctx, buffer.data(), n);
    }
    close(fd);
    unsigned char out[SHA256_DIGEST_LENGTH];
    SHA256_Final(out, &ctx);
    return hex(out, sizeof out);
}
namespace {
// Streams a .tar.zst: zstd frames into ustar records. Only regular files and
// directories under one root directory; no links, devices, traversal or
// unsafe characters (RFC-0004 archive rules); <= 4000 files, 512 MiB expanded.
void extractArchive(const fs::path &archive, const fs::path &into, std::string &root) {
    int fd = open(archive.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        throw std::invalid_argument("cannot open archive");
    std::unique_ptr<ZSTD_DStream, size_t (*)(ZSTD_DStream *)> stream(ZSTD_createDStream(), ZSTD_freeDStream);
    ZSTD_initDStream(stream.get());
    std::vector<char> in(1 << 17), out(1 << 17);
    std::string pending; // decompressed bytes not yet consumed
    bool eof = false;
    ZSTD_inBuffer input{in.data(), 0, 0};
    auto fill = [&](size_t need) {
        while (pending.size() < need && !eof) {
            if (input.pos == input.size) {
                auto n = read(fd, in.data(), in.size());
                if (n < 0)
                    throw std::invalid_argument("archive read failed");
                if (n == 0) {
                    eof = true;
                    break;
                }
                input = {in.data(), size_t(n), 0};
            }
            ZSTD_outBuffer output{out.data(), out.size(), 0};
            auto r = ZSTD_decompressStream(stream.get(), &output, &input);
            if (ZSTD_isError(r))
                throw std::invalid_argument(std::string("archive is not zstd: ") + ZSTD_getErrorName(r));
            pending.append(out.data(), output.pos);
        }
        return pending.size() >= need;
    };
    struct Closer {
        int fd;
        ~Closer() { close(fd); }
    } closer{fd};
    uint64_t total = 0;
    size_t count = 0;
    auto octal = [](const char *p, size_t n) {
        uint64_t v = 0;
        for (size_t i = 0; i < n && p[i]; ++i) {
            if (p[i] == ' ')
                continue;
            if (p[i] < '0' || p[i] > '7')
                throw std::invalid_argument("invalid tar header");
            v = v * 8 + (p[i] - '0');
        }
        return v;
    };
    while (true) {
        if (!fill(512))
            throw std::invalid_argument("truncated archive");
        std::string header = pending.substr(0, 512);
        pending.erase(0, 512);
        if (std::all_of(header.begin(), header.end(), [](char c) { return c == 0; }))
            break; // end of archive
        unsigned sum = 0;
        for (size_t i = 0; i < 512; ++i)
            sum += (i >= 148 && i < 156) ? ' ' : (unsigned char)header[i];
        if (sum != octal(header.data() + 148, 8) || header.compare(257, 5, "ustar") != 0)
            throw std::invalid_argument("archive must be ustar");
        std::string name(header.data(), strnlen(header.data(), 100)), prefix(header.data() + 345, strnlen(header.data() + 345, 155));
        if (!prefix.empty())
            name = prefix + "/" + name;
        char type = header[156];
        uint64_t size = octal(header.data() + 124, 12);
        bool dir = type == '5';
        if (dir && name.ends_with("/"))
            name.pop_back();
        if (!(type == '0' || type == 0 || dir) || !safeRelative(name))
            throw std::invalid_argument("unsafe archive member: " + name);
        auto first = name.substr(0, name.find('/'));
        if (root.empty())
            root = first;
        if (first != root)
            throw std::invalid_argument("archive members must share one root directory");
        ++count;
        total += size;
        if (count > Limits::libraryFiles || total > Limits::libraryBytes || size > Limits::fileBytes)
            throw std::invalid_argument("archive exceeds the library limits (4000 files, 512 MiB, 32 MiB per file)");
        auto target = into / name;
        if (dir) {
            fs::create_directories(target);
            continue;
        }
        fs::create_directories(target.parent_path());
        int outFd = open(target.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
        if (outFd < 0)
            throw std::invalid_argument("duplicate or unwritable archive member: " + name);
        uint64_t left = size;
        while (left) {
            if (!fill(1) && pending.empty()) {
                close(outFd);
                throw std::invalid_argument("truncated archive member: " + name);
            }
            auto chunk = std::min<uint64_t>(left, pending.size());
            if (write(outFd, pending.data(), chunk) != ssize_t(chunk)) {
                close(outFd);
                throw std::runtime_error("cannot write library file");
            }
            pending.erase(0, chunk);
            left -= chunk;
        }
        close(outFd);
        auto pad = (512 - size % 512) % 512;
        if (!fill(pad))
            throw std::invalid_argument("truncated archive");
        pending.erase(0, pad);
    }
    if (root.empty())
        throw std::invalid_argument("empty archive");
}
} // namespace
InstallResult installLibrary(const fs::path &archive, const std::string &sha256, const CatalogRoots &roots) {
    struct stat st{};
    if (!archive.is_absolute() || lstat(archive.c_str(), &st) || !S_ISREG(st.st_mode) || !archive.string().ends_with(".tar.zst"))
        throw std::invalid_argument("archive must be an absolute path to a regular .tar.zst file");
    if (uint64_t(st.st_size) > Limits::libraryBytes)
        throw std::invalid_argument("archive exceeds 512 MiB");
    if (sha256File(archive) != sha256)
        throw std::invalid_argument("archive sha256 mismatch");
    fs::create_directories(roots.userLibraries);
    auto staging = roots.userLibraries / (".install-" + std::to_string(getpid()) + "-" + std::to_string(time(nullptr)));
    fs::remove_all(staging);
    fs::create_directories(staging);
    struct Cleanup {
        fs::path path;
        ~Cleanup() {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
    } cleanup{staging};
    std::string root;
    extractArchive(archive, staging, root);
    auto extracted = staging / root;
    if (!fs::is_regular_file(extracted / "library.json"))
        throw std::invalid_argument("archive has no " + root + "/library.json");
    auto manifest = parseJson(readFile(extracted, "library.json", Limits::fileBytes), "library.json");
    if (!manifest.is_object() || manifest.value("id", Json()) != root || !manifest.contains("version") ||
        !manifest["version"].is_string())
        throw std::invalid_argument("library.json id must equal the archive root " + root);
    std::string version = manifest["version"];
    // Validate exactly as an installed library (directory named id@version).
    auto named = staging / libraryKey(root, version);
    fs::rename(extracted, named);
    ScanCache cache;
    auto lib = scanLibrary(named, "user", cache);
    std::vector<std::string> problems = lib.record.errors;
    for (auto &e : lib.entries)
        for (auto &x : e.errors)
            problems.push_back("objects/" + e.id + ": " + x);
    if (!problems.empty()) {
        std::string text = "invalid library: " + problems[0];
        if (problems.size() > 1)
            text += " (+" + std::to_string(problems.size() - 1) + " more)";
        throw std::invalid_argument(text);
    }
    auto target = roots.userLibraries / libraryKey(root, version);
    if (fs::exists(target)) {
        ScanCache other;
        auto existing = scanLibrary(target, "user", other);
        if (existing.record.digest == lib.record.digest)
            return {root, version, true};
        throw std::domain_error("library.conflict: " + libraryKey(root, version) + " is installed with different content");
    }
    for (auto &s : roots.systemLibraries)
        if (fs::exists(s / libraryKey(root, version))) {
            ScanCache other;
            auto existing = scanLibrary(s / libraryKey(root, version), "system", other);
            if (existing.record.digest == lib.record.digest)
                return {root, version, true};
            throw std::domain_error("library.conflict: " + libraryKey(root, version) + " is installed with different content");
        }
    fs::rename(named, target);
    return {root, version, false};
}
void removeLibrary(const std::string &id, const std::string &version, const CatalogRoots &roots) {
    auto key = libraryKey(id, version);
    auto target = roots.userLibraries / key;
    struct stat st{};
    if (lstat(target.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        // Rename first so a scan never sees a half-removed library.
        auto doomed = roots.userLibraries / (".remove-" + key + "-" + std::to_string(getpid()));
        fs::rename(target, doomed);
        fs::remove_all(doomed);
        return;
    }
    for (auto &s : roots.systemLibraries)
        if (fs::exists(s / key))
            throw std::domain_error("system libraries are read-only");
    throw std::invalid_argument("unknown library " + key);
}
} // namespace hyprune::world
