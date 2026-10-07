#pragma once
#include "surfaces/Layout.hpp"
#include "surfaces/Surface.hpp"
#include "world/World.hpp"
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <memory>
#include <vector>
namespace hyprune::platform {
class Capture {
  public:
    struct Entry {
        PHLWINDOWREF window;
        SP<Render::ITexture> texture;
        surfaces::Surface surface;
        surfaces::Instance instance;
        bool visible = true;
        std::shared_ptr<uint64_t> commits = std::make_shared<uint64_t>(0);
        CHyprSignalListener commitListener;
    };
    std::vector<Entry> entries;
    struct WorkspaceFrame {
        unsigned fbo = 0, texture = 0;
        int workspaceId = 0;
        surfaces::Surface surface;
    };
    std::vector<WorkspaceFrame> workspaces;
    void refresh(PHLMONITOR monitor, const world::World *world = nullptr, surfaces::Layout *layout = nullptr,
                 const sim::Camera *camera = nullptr);
    std::vector<surfaces::Surface> visible() const;
    WorkspaceFrame *workspace(const std::string &);
    void release();
    Entry *find(const std::string &id);

  private:
    unsigned sequence_ = 0, program_ = 0, vao_ = 0;
    void compose(PHLMONITOR, const world::World *, surfaces::Layout &);
};
} // namespace hyprune::platform
