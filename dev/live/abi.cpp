#include <hyprland/src/version.h>
#include <iostream>
#include <string>
int main() {
    auto strip = [](std::string s) { return s.substr(0, s.find_last_of('.')); };
    // Same ABI formula as PluginAPI.hpp's __hyprland_api_get_client_hash().
    std::cout << "{\"commit\":\"" << GIT_COMMIT_HASH << "\",\"tag\":\"" << GIT_TAG
              << "\",\"abiHash\":\"" << GIT_COMMIT_HASH << "_aq_" << strip(AQUAMARINE_VERSION)
              << "_hu_" << strip(HYPRUTILS_VERSION) << "_hg_" << strip(HYPRGRAPHICS_VERSION)
              << "_hc_" << strip(HYPRCURSOR_VERSION) << "_hlg_" << strip(HYPRLANG_VERSION) << "\"}\n";
}
