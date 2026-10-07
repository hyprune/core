#pragma once
#include "surfaces/Surface.hpp"
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <vector>
namespace hyprune::platform {
class Capture {
  public:
    struct Entry {
        PHLWINDOWREF window;
        SP<Render::ITexture> texture;
        surfaces::Surface surface;
    };
    std::vector<Entry> entries;
    void refresh(PHLMONITOR monitor);
    void release();
    Entry *find(const std::string &id);

  private:
    unsigned sequence_ = 0;
};
} // namespace hyprune::platform
