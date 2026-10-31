#pragma once
// Hyprland API differences between the supported releases (CMakeLists.txt:
// HYPRUNE_HYPRLAND_SUPPORTED), as compile-time switches. The plugin is ABI-locked to
// the headers it builds against, so nothing here guesses at run time. Include this
// instead of the Hyprland headers that moved: a header missing from the build's
// Hyprland would otherwise resolve to another release's copy under /usr/include.
#ifndef HYPRUNE_HYPRLAND_VERSION
#error "HYPRUNE_HYPRLAND_VERSION is set by CMake (major * 10000 + minor * 100 + patch)"
#endif
#define HYPRUNE_HYPRLAND_AT_LEAST(major, minor) (HYPRUNE_HYPRLAND_VERSION >= (major) * 10000 + (minor) * 100)

#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/layout/target/WindowTarget.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#if HYPRUNE_HYPRLAND_AT_LEAST(0, 57)
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowFullscreenPolicy.hpp>
#include <hyprland/src/workspace/HLWorkspace.hpp>
#include <hyprland/src/keybinds/Manager.hpp>
#else
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#endif
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>
#include <string>
#include <vector>

namespace hyprune::platform::hl {
// Hyprland 0.57 gives special and named workspaces no number; 0.56 gave them negative
// ids. Callers only need "not a positive number", as before.
inline constexpr int SPECIAL_WORKSPACE = -99;

// A keybind as core reads it (the toggle chord, native bind conflicts).
struct Bind {
    bool enabled = true, ignoreMods = false;
    std::string key; // the trigger key name; modifiers are in mods
    unsigned mods = 0;
    std::string handler, arg, description, submap;
};

#if HYPRUNE_HYPRLAND_AT_LEAST(0, 57)
// Workspaces (PHLWORKSPACE)
inline int workspaceId(const auto &w) {
    auto id = w->numberedID();
    return id ? int(*id) : SPECIAL_WORKSPACE;
}
inline bool special(const auto &w) { return w->type() == Workspace::eWorkspaceType::SPECIAL; }
inline bool visible(const auto &w) { return w->visible(); }
inline const std::string &workspaceName(const auto &w) { return w->displayName(); }
inline PHLWORKSPACE findWorkspace(int id) {
    return id > 0 ? State::workspaceState()->query().numbered(Workspace::SWorkspaceNumberedID(id)).run() : nullptr;
}
inline PHLWORKSPACE createWorkspace(int id, PHLMONITOR monitor) {
    return State::workspaceState()->createNumbered(Workspace::SWorkspaceNumberedID(id), monitor);
}
// A workspace core created announces itself once it is placed and shown.
inline void ready(const PHLWORKSPACE &w) { w->ready(); }
// Windows (PHLWINDOW or PHLWINDOWREF)
inline bool mapped(const auto &w) { return w->mapped(); }
inline bool floating(const auto &w) { return w->isFloating(); }
// window.openEarly, before the layout sees the window: the initial flag only.
inline void floatInitially(const auto &w) { w->windowTarget()->setFloatingInitial(true); }
inline auto target(const auto &w) { return w->windowTarget(); }
inline bool pinned(const auto &w) { return bool(w->m_state & Desktop::View::WINDOW_STATE_PINNED); }
inline bool allowedOverFullscreen(const auto &w) { return w->fullscreenPolicy().allowedOverFullscreen(); }
inline void setAllowedOverFullscreen(const auto &w, bool allowed) { w->fullscreenPolicy().setAllowedOverFullscreen(allowed); }
inline const std::string &title(const auto &w) { return w->metadata().title(); }
inline const std::string &appClass(const auto &w) { return w->metadata().appID(); }
// The xdg-toplevel or X11 transient-for parent.
inline PHLWINDOW parent(const auto &w) { return w->backend().parent(); }
inline bool x11(const auto &w) { return w->backend().isX11(); }
// An X11 menu or tooltip: override-redirect, or a window that must not take focus.
inline bool x11Menu(const auto &w) {
    if (!w->backend().isX11())
        return false;
    auto traits = w->backend().traits();
    return traits.overrideRedirect || traits.preventsFocus;
}
inline bool x11OverrideRedirect(const auto &w) { return w->backend().isX11() && w->backend().traits().overrideRedirect; }
inline pid_t x11Pid(const auto &w) { return w->backend().isX11() ? w->backend().pid() : 0; }
inline CBox x11Geometry(const auto &w) { return w->backend().geometry().box; }
// Layer surfaces and popups
inline bool layerMapped(const auto &l) { return l->mapped(); }
inline bool popupMapped(const auto &p) { return p->mapped(); }
inline auto popupHead(const auto &owner) { return owner->popupHead(); }
// Keybinds
inline auto &keybinds() { return Keybinds::mgr(); }
inline std::vector<Bind> binds() {
    std::vector<Bind> out;
    for (const auto &b : Keybinds::mgr()->registry().binds()) {
        auto names = b->keyNames();
        const auto &meta = b->metadata();
        out.push_back({b->enabled(), b->hasFlag(Keybinds::BIND_FLAG_IGNORE_MODS), names.empty() ? std::string{} : names.back(),
                       unsigned(b->modifierMask()), meta.handler, meta.argument, meta.description.value_or(""), meta.submap});
    }
    return out;
}
// Switch to a workspace by name or number, as `hyprctl dispatch workspace` does.
inline bool changeWorkspace(const std::string &workspace) { return Config::Actions::changeWorkspace(workspace).has_value(); }
inline bool moveToWorkspaceSilent(const PHLWINDOW &w, int workspace) {
    return Config::Actions::moveToWorkspace(std::to_string(workspace), true, w).has_value();
}
#else
inline int workspaceId(const auto &w) { return int(w->m_id); }
inline bool special(const auto &w) { return w->m_isSpecialWorkspace; }
inline bool visible(const auto &w) { return w->isVisible(); }
inline const std::string &workspaceName(const auto &w) { return w->m_name; }
inline PHLWORKSPACE findWorkspace(int id) { return State::workspaceState()->query().id(id).run(); }
inline PHLWORKSPACE createWorkspace(int id, PHLMONITOR monitor) { return State::workspaceState()->create(id, monitor->m_id); }
inline void ready(const PHLWORKSPACE &) {}
inline bool mapped(const auto &w) { return w->m_isMapped; }
inline bool floating(const auto &w) { return w->m_isFloating; }
inline void floatInitially(const auto &w) { w->m_isFloating = true; }
inline auto target(const auto &w) { return w->m_target; }
inline bool pinned(const auto &w) { return w->m_pinned; }
inline bool allowedOverFullscreen(const auto &w) { return w->m_allowedOverFullscreen; }
inline void setAllowedOverFullscreen(const auto &w, bool allowed) { w->m_allowedOverFullscreen = allowed; }
inline const std::string &title(const auto &w) { return w->m_title; }
inline const std::string &appClass(const auto &w) { return w->m_class; }
inline PHLWINDOW parent(const auto &w) {
    auto p = w->parent();
    return p ? p : w->x11Parent();
}
inline bool x11(const auto &w) { return w->m_isX11; }
inline bool x11Menu(const auto &w) {
    return w->m_isX11 && (w->m_X11ShouldntFocus || (w->m_xwaylandSurface && w->m_xwaylandSurface->m_overrideRedirect));
}
inline bool x11OverrideRedirect(const auto &w) { return w->m_xwaylandSurface && w->m_xwaylandSurface->m_overrideRedirect; }
inline pid_t x11Pid(const auto &w) { return w->m_xwaylandSurface ? w->m_xwaylandSurface->m_pid : 0; }
inline CBox x11Geometry(const auto &w) { return w->m_xwaylandSurface ? w->m_xwaylandSurface->m_geometry : CBox{}; }
inline bool layerMapped(const auto &l) { return l->m_mapped; }
inline bool popupMapped(const auto &p) { return p->m_mapped; }
inline auto popupHead(const auto &owner) { return owner->m_popupHead; }
inline auto &keybinds() { return g_pKeybindManager; }
inline std::vector<Bind> binds() {
    std::vector<Bind> out;
    for (const auto &b : g_pKeybindManager->m_keybinds)
        out.push_back({b->enabled, b->ignoreMods, b->key, b->modmask, b->handler, b->arg, b->description, b->submap.name});
    return out;
}
inline bool changeWorkspace(const std::string &workspace) { return g_pKeybindManager->m_dispatchers.at("workspace")(workspace).success; }
inline bool moveToWorkspaceSilent(const PHLWINDOW &w, int workspace) {
    return g_pKeybindManager->m_dispatchers.at("movetoworkspacesilent")(
               std::to_string(workspace) + ",address:" + std::format("0x{:x}", reinterpret_cast<uintptr_t>(w.get())))
        .success;
}
#endif

// Render pass. Our elements draw inside Hyprland's pass (rule 2: nothing in them may make
// the compositor render; GL state is saved and restored by the callers' guards).
#if HYPRUNE_HYPRLAND_AT_LEAST(0, 57)
using RenderData = Render::SRenderData;
// {stage, monitor, context}: the context is only borrowed during the emission, PRE and POST have none.
using RenderStageEvent = Event::SRenderStageEvent;
inline eRenderStage stage(const RenderStageEvent &e) { return e.stage; }
inline PHLMONITOR stageMonitor(const RenderStageEvent &e) { return e.monitor; }
inline void addPass(const RenderStageEvent &e, UP<IPassElement> &&element) {
    if (e.context)
        Render::IHyprRenderer::addPassElement(e.context->get(), std::move(element));
}
// Elements of ours still queued in the renderer's context (unload: none may outlive the .so).
inline void removePasses(const char *name) { g_pHyprRenderer->context().m_pass.removeAllOfType(name); }
// What a failed beginRender leaves behind: RENDER_PRE without RENDER_POST.
inline void emitLeakedRenderPre() { Event::bus()->m_events.render.stage.emit({RENDER_PRE, nullptr, std::nullopt}); }
class PassElement : public IPassElement {
  public:
    virtual void run(RenderData &data) = 0;
    std::vector<UP<IPassElement>> draw(Render::CRenderContext &ctx) override {
        run(ctx.m_data);
        return {};
    }
    bool needsLiveBlur(Render::CRenderContext &) override { return false; }
    bool needsPrecomputeBlur(Render::CRenderContext &) override { return false; }
    ePassElementType type() override { return EK_CUSTOM; }
    bool undiscardable(Render::CRenderContext &) override { return true; }
    bool disableSimplification(Render::CRenderContext &) override { return true; }
};
#else
using RenderData = std::remove_cvref_t<decltype(g_pHyprRenderer->m_renderData)>;
using RenderStageEvent = eRenderStage;
inline eRenderStage stage(const RenderStageEvent &e) { return e; }
inline PHLMONITOR stageMonitor(const RenderStageEvent &) { return g_pHyprRenderer->m_renderData.pMonitor.lock(); }
inline void addPass(const RenderStageEvent &, UP<IPassElement> &&element) { g_pHyprRenderer->addPassElement(std::move(element)); }
inline void removePasses(const char *name) { g_pHyprRenderer->m_renderPass.removeAllOfType(name); }
inline void emitLeakedRenderPre() { Event::bus()->m_events.render.stage.emit(RENDER_PRE); }
class PassElement : public IPassElement {
  public:
    virtual void run(RenderData &data) = 0;
    std::vector<UP<IPassElement>> draw() override {
        run(g_pHyprRenderer->m_renderData);
        return {};
    }
    bool needsLiveBlur() override { return false; }
    bool needsPrecomputeBlur() override { return false; }
    ePassElementType type() override { return EK_CUSTOM; }
    bool undiscardable() override { return true; }
    bool disableSimplification() override { return true; }
};
#endif
}
