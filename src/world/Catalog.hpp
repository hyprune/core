#pragma once
// RFC-0011 P1 object catalog: world-package assets, installed libraries and the user
// folder, validated with the shared object rules (ObjectCheck), plus library archive
// install/remove. Scans and installs block: run them off the compositor thread.
#include "ObjectCheck.hpp"
#include "World.hpp"
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
namespace hyprune::world {
// IPC 0.22 object and library methods and the grant each needs.
const std::map<std::string, std::string> &objectMethods();
struct ObjectRef {
    std::string source;          // package, library, user
    std::string library, version; // library refs only
    std::string id;              // asset id (package) or object id
    std::string libraryKey() const { return library + "@" + version; }
};
std::optional<ObjectRef> parseRef(const std::string &);
struct CatalogEntry {
    std::string ref, source, id, name, category = "other", state = "ready", reason;
    Json version = nullptr, formatVersion = nullptr, description = nullptr, library = nullptr, thumbnail = nullptr,
         license = nullptr, surfaces = nullptr, scale = nullptr, bounds = nullptr, triangles = nullptr,
         gpuBytes = nullptr, collisionTriangles = nullptr;
    std::vector<std::string> tags, traits, versions, errors;
    bool placeable = false;
    // IPC 0.27 (RFC-0014): validation warnings (sizes over their recommendation), provenance
    // {license, author, source} and import.json's {at, sourceName} (null without one).
    std::vector<std::string> warnings;
    Json provenance = nullptr, imported = nullptr;
    // Not on the wire.
    std::filesystem::path directory; // object directory, or the world package root
    Json manifest = nullptr;         // object.json, or a world 0.8 objectAssets entry
    std::string digest;              // content digest (path, size, bytes of every file)
    bool movable() const { return std::find(traits.begin(), traits.end(), "movable") != traits.end(); }
    bool has(const std::string &trait) const { return std::find(traits.begin(), traits.end(), trait) != traits.end(); }
    Json json() const;
};
struct LibraryRecord {
    std::string id, version, name, root = "user", state = "ready", digest;
    Json formatVersion = nullptr;
    size_t objects = 0;
    std::vector<std::string> errors;
    std::filesystem::path directory;
    Json json() const;
};
struct CatalogRoots {
    std::filesystem::path userObjects, userLibraries;
    std::vector<std::filesystem::path> systemLibraries;
    // $XDG_DATA_HOME/hyprune/{objects,libraries} (HYPRUNE_OBJECT_DIR overrides the first),
    // then $HYPRUNE_LIBRARY_DIR (colon-separated, read-only).
    static CatalogRoots fromEnvironment();
};
struct CatalogSnapshot {
    std::vector<CatalogEntry> entries;        // library (highest versions) and user entries, catalog order
    std::map<std::string, CatalogEntry> refs; // every installed library version and user object, by ref
    std::vector<LibraryRecord> libraries;
};
// Results by directory, keyed by a stat signature (paths, sizes, mtimes): an unchanged
// directory is not read or validated again.
struct ScanCache {
    std::mutex lock;
    std::map<std::string, std::pair<std::string, CatalogEntry>> entries;
};
// One object directory; never throws (problems make an invalid entry with <= 8 errors).
CatalogEntry scanObject(const std::filesystem::path &dir, const std::string &ref, const std::string &source,
                        ScanCache *cache = nullptr);
CatalogSnapshot scanCatalog(const CatalogRoots &, ScanCache &);
// The loaded world's object assets (world 0.8 objectAssets). devMovable: the
// HYPRUNE_GIZMO_OBJECTS development switch treats them as movable (surfaces any).
std::vector<CatalogEntry> packageEntries(const World &, const std::filesystem::path &package, bool devMovable);
// Placeability in P1 (sets placeable and reason).
void decidePlaceable(CatalogEntry &);
// Library archives: sha256 checked before extraction, RFC-0004 archive rules, validated
// in a staging directory, then renamed into the user library root.
struct InstallResult {
    std::string id, version;
    bool already = false; // the same content was installed
};
InstallResult installLibrary(const std::filesystem::path &archive, const std::string &sha256, const CatalogRoots &);
// Removes a library from the user root (throws std::invalid_argument for unknown ones and
// std::domain_error for system roots).
void removeLibrary(const std::string &id, const std::string &version, const CatalogRoots &);
std::string sha256File(const std::filesystem::path &);
// SemVer precedence (a prerelease sorts before its release; build metadata ignored).
bool versionLess(const std::string &a, const std::string &b);
// The SDK's GPU estimate pieces, exposed for tests: image header dimensions.
struct ImageHeader {
    std::string format; // png, jpeg, ktx2
    int width = 0, height = 0;
};
std::optional<ImageHeader> imageHeader(const unsigned char *data, size_t size);
} // namespace hyprune::world
