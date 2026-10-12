// hyprune-object-check: core's catalog view of RFC-0011 object directories and libraries,
// the same rules hypruned applies when it scans the user folder and library roots.
//   hyprune-object-check <object-dir>...   one catalog entry per directory (JSON lines)
//   hyprune-object-check --library <dir>   the library record and its entries
#include "world/Catalog.hpp"
#include <cstdio>
#include <iostream>
int main(int argc, char **argv) {
    using namespace hyprune::world;
    if (argc < 2) {
        std::fprintf(stderr, "usage: hyprune-object-check <object-dir>... | --library <dir>\n");
        return 2;
    }
    bool failed = false;
    if (std::string(argv[1]) == "--library" && argc == 3) {
        std::filesystem::path dir = std::filesystem::absolute(argv[2]);
        CatalogRoots roots;
        roots.userLibraries = dir.parent_path();
        ScanCache cache;
        auto snap = scanCatalog(roots, cache);
        for (auto &l : snap.libraries)
            if (l.directory == dir) {
                std::cout << l.json().dump() << "\n";
                failed = l.state != "ready";
            }
        for (auto &[ref, e] : snap.refs)
            if (e.directory.parent_path().parent_path() == dir) {
                std::cout << e.json().dump() << "\n";
                failed = failed || e.state != "ready";
            }
        return failed ? 1 : 0;
    }
    for (int i = 1; i < argc; ++i) {
        std::filesystem::path dir = std::filesystem::absolute(argv[i]);
        auto e = scanObject(dir, "user/" + dir.filename().string(), "user");
        auto j = e.json();
        j["digest"] = e.digest;
        std::cout << j.dump() << "\n";
        failed = failed || e.state != "ready";
    }
    return failed ? 1 : 0;
}
