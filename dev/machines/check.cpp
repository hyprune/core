// machines-check PACKAGE [BINDINGS]: loads a world package like core, then the
// machines prototype's bindings (default PACKAGE/machines.bindings.json) through the
// runtime, ticks it once without data and prints its diagnostics. Exit 1 with the
// reason when the package, a manifest, a slot, a drive or a binding is invalid.
#include "interaction/InputFile.hpp"
#include "world/Machines.hpp"
#include "world/World.hpp"
#include <iostream>
int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "usage: machines-check PACKAGE [BINDINGS]\n";
        return 2;
    }
    try {
        std::filesystem::path package = argv[1];
        auto w = hyprune::world::load(package);
        auto spec = hyprune::interaction::readJSON(argc == 3 ? std::filesystem::path(argv[2]) : package / "machines.bindings.json", 1 << 20);
        hyprune::world::Machines m;
        m.load(spec, *w, package);
        m.tick(.3, hyprune::world::Json::array(), 0, false);
        std::cout << m.diagnostics().dump(1) << '\n';
    } catch (const std::exception &e) {
        std::cerr << "machines-check: " << e.what() << '\n';
        return 1;
    }
}
