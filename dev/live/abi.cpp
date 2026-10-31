#include <hyprland/src/version.h>
#include <iostream>
#include <sstream>
#include <string>
int main() {
    auto strip = [](std::string s) { return s.substr(0, s.find_last_of('.')); };
    // Same ABI formula as PluginAPI.hpp's __hyprland_api_get_client_hash().
    std::cout << "{\"commit\":\"" << GIT_COMMIT_HASH << "\",\"tag\":\"" << GIT_TAG
              << "\",\"abiHash\":\"" << GIT_COMMIT_HASH << "_aq_" << strip(AQUAMARINE_VERSION)
              << "_hu_" << strip(HYPRUTILS_VERSION) << "_hg_" << strip(HYPRGRAPHICS_VERSION)
              << "_hc_" << strip(HYPRCURSOR_VERSION) << "_hlg_" << strip(HYPRLANG_VERSION) << "\",\"supported\":[";
    // The Hyprland releases this core supports (CMakeLists.txt), for the launcher's advice.
    std::istringstream supported(HYPRUNE_HYPRLAND_SUPPORTED);
    std::string release, separator;
    while (supported >> release)
        std::cout << separator << '"' << release << '"', separator = ",";
    std::cout << "]}\n";
}
