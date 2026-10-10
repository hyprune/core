#pragma once
#include "surfaces/Layout.hpp"
#include "surfaces/Surface.hpp"
#include "world/World.hpp"
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
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
        bool visible = true, queued = false;
        std::string parentId;
        bool parentKeyboard = false;
        std::shared_ptr<uint64_t> commits = std::make_shared<uint64_t>(0);
        CHyprSignalListener commitListener;
    };
    std::vector<Entry> entries;
    std::vector<std::pair<PHLWINDOWREF, Vector2D>> configureRequests;
    struct Child {
        WP<CWLSurfaceResource> resource;
        SP<Render::ITexture> texture;
        surfaces::Surface surface;
        std::string parentId;
        Vector2D offset;
        bool visible = true;
        std::shared_ptr<uint64_t> commits = std::make_shared<uint64_t>(0);
        CHyprSignalListener commitListener;
    };
    std::vector<Child> children;
    struct Target {
        SP<CWLSurfaceResource> resource;
        Vector2D local;
        std::string parentId;
    };
    std::optional<Target> target(const std::string &, float u, float v);
    surfaces::Surface *plane(const std::string &);
    float spawnDistance = 5, windowScale = .002;
    bool spawnNew = false;
    int worldWorkspace = 20;
    void attach();
    struct WorkspaceFrame {
        unsigned fbo = 0, texture = 0;
        int workspaceId = 0;
        surfaces::Surface surface;
        uint64_t key = 0; // inputs of the last composition; unchanged = no redraw
        uint64_t composed = 0; // redraw count (nested diagnostics)
        int width = 0, height = 0; // composite texture size (monitor pixels with windows)
    };
    std::vector<WorkspaceFrame> workspaces;
    void refresh(PHLMONITOR monitor, const world::World *world = nullptr, surfaces::Layout *layout = nullptr,
                 const sim::Camera *camera = nullptr);
    std::vector<surfaces::Surface> visible() const;
    WorkspaceFrame *workspace(const std::string &);
    void release();
    Entry *find(const std::string &id);

  private:
    std::vector<PHLWINDOWREF> knownWindows_;
    struct AdoptedWindow {
        PHLWINDOWREF window;
        std::string id, worldId;
    };
    std::vector<AdoptedWindow> adoptedWindows_;
    // Background/bottom layer-shell surfaces (wallpaper) on the world output, in the
    // compositor's own order: drawn behind every wall's windows, like the real desktop.
    struct Backdrop {
        PHLLSREF layer;
        std::shared_ptr<uint64_t> commits = std::make_shared<uint64_t>(0);
        CHyprSignalListener commitListener;
    };
    std::vector<Backdrop> backdrops_;
    void trackBackdrops(const PHLMONITOR &);

  public:
    nlohmann::json backdropDiagnostics() const; // nested checks only
  private:
    unsigned sequence_ = 0, program_ = 0, vao_ = 0, sampler_ = 0, placeholder_ = 0;
    void compose(PHLMONITOR, const world::World *, surfaces::Layout &);
};
} // namespace hyprune::platform
