#include "Capture.hpp"
#include <hyprland/src/protocols/LayerShell.hpp>
#include "interaction/Hands.hpp"
#include "ipc/Text.hpp"
#include "render/GLState.hpp"
#include "render/SurfaceSampler.hpp"
#include "render/Watermark.hpp"
#include <GLES3/gl3.h>
#include <algorithm>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/Popup.hpp>
#include <hyprland/src/layout/target/WindowTarget.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/xwayland/XSurface.hpp>
#include <set>
namespace hyprune::platform {
namespace {
PHLWINDOW attachmentParent(const PHLWINDOW &w) {
    auto parent = w->parent();
    if (!parent)
        parent = w->x11Parent();
    bool menu =
        w->m_isX11 && (w->m_X11ShouldntFocus || (w->m_xwaylandSurface && w->m_xwaylandSurface->m_overrideRedirect));
    if (!parent && menu)
        parent = Desktop::focusState()->window();
    return parent == w ? nullptr : parent;
}
} // namespace
// Runs on the event loop, never from IPassElement::draw. Read-only texture
// leases avoid snapshot FBOs and compositor recursion entirely for M0.
void Capture::refresh(PHLMONITOR monitor, const world::World *world, surfaces::Layout *layout,
                      const sim::Camera *camera) {
    std::erase_if(entries, [&](Entry &e) {
        bool remove = !e.window || !e.window->m_isMapped ||
                      (e.window->m_monitor != monitor &&
                       !(layout && e.window->m_workspace && e.window->m_workspace->m_id != worldWorkspace &&
                         layout->binding(e.window->m_workspace->m_id, world)));
        if (!remove && e.parentId.empty() && e.window->m_workspace && e.window->m_workspace->m_id != worldWorkspace &&
            layout) {
            auto identity = e.instance;
            identity.workspaceId = e.window->m_workspace->m_id;
            remove = !layout->binding(identity.workspaceId, world) && !layout->hasPlacement(identity, world);
        }
        if (remove && layout) {
            layout->forget(e.surface.id);
            layout->occupiedPoses.erase(e.surface.id);
            layout->freePoses.erase(e.surface.id);
            layout->unplaced.erase(e.surface.id);
            layout->content.erase(e.surface.id);
            std::erase(layout->pocketOrder, e.surface.id);
        }
        if (remove && e.window && e.window->m_isMapped) {
            auto ws = e.window->m_workspace;
            e.window->setSuspended(ws && !ws->isVisible());
        }
        return remove;
    });
    if (layout) {
        layout->instances = world::Json::array();
        layout->windowScale = windowScale;
        layout->roomWorkspace = worldWorkspace;
    }
    std::erase_if(knownWindows_, [](auto &w) { return !w || !w->m_isMapped; });
    std::erase_if(adoptedWindows_, [](auto &w) { return !w.window || !w.window->m_isMapped; });
    std::set<std::string> refreshed;
    size_t leasedBytes = 0;
    skipped.clear();
    auto skip = [&](const PHLWINDOW &w, const char *why) { skipped.emplace_back(ipc::boundedText(w->m_title, ipc::TitleLimit), why); };
    std::vector<PHLWINDOWREF> presenting;
    // Shown in the world, so it must keep drawing although its workspace is on no output:
    // clear the xdg "suspended" state the compositor gives hidden workspaces (browsers
    // throttle or stop painting on it) and pace it with frame callbacks. This includes a
    // window the capture skips below: one without a buffer yet never gets one otherwise.
    auto present = [&](const PHLWINDOW &w) {
        w->setSuspended(false);
        w->resource()->frame(std::chrono::steady_clock::now());
        presenting.push_back(w);
    };
    for (auto &w : Desktop::windowState()->windows()) {
        if (!w->m_isMapped || w->isHidden() || !w->resource())
            continue;
        // A home workspace can live on another output (a window moved there from it); its
        // windows still belong on this world's wall.
        const bool elsewhere = w->m_monitor != monitor;
        if (elsewhere && !(layout && world && w->m_workspace && w->m_workspace->m_id != worldWorkspace &&
                           layout->binding(w->m_workspace->m_id, world)))
            continue;
        bool attached = bool(attachmentParent(w));
        bool newlyMapped = std::none_of(knownWindows_.begin(), knownWindows_.end(), [&](auto &v) { return v == w; });
        // Baseline all existing windows, including windows without a texture yet.
        if (!spawnNew && newlyMapped)
            knownWindows_.push_back(w);
        auto remembered = std::find_if(adoptedWindows_.begin(), adoptedWindows_.end(),
                                       [&](auto &v) { return v.window == w && world && v.worldId == world->id; });
        surfaces::Instance identity{"", ipc::boundedText(w->m_class, ipc::ClassLimit),
                                    ipc::boundedText(w->m_title, ipc::TitleLimit),
                                    w->m_workspace ? int(w->m_workspace->m_id) : 0};
        // Only the dedicated room is adopted implicitly. Authored workspace
        // walls/mounts remain available without changing their native layout.
        const bool inRoom = identity.workspaceId == worldWorkspace;
        if (!attached && !inRoom &&
            (!layout || (!layout->binding(identity.workspaceId, world) && !layout->hasPlacement(identity, world))))
            continue;
        present(w);
        auto surface = w->resource();
        auto texture = surface->m_current.texture;
        if (!texture || !texture->m_texID || texture->m_type == Render::TEXTURE_EXTERNAL) {
            skip(w, !texture || !texture->m_texID ? "no texture" : "external texture");
            continue;
        }
        // Unsupported source geometry is unavailable instead of silently
        // routing a click using mismatched texture coordinates.
        if (surface->m_current.transform != WL_OUTPUT_TRANSFORM_NORMAL) {
            skip(w, "buffer transform");
            continue;
        }
        // A wp_viewporter source crop (Chromium uses one around resizes, on oversized
        // buffers): show that region, as Hyprland does. Skipping such a client made it
        // vanish from the world on resize and come back on a later commit.
        std::array<float, 4> uvRect{0, 0, 1, 1};
        if (surface->m_current.viewport.hasSource) {
            auto buffer = surface->m_current.bufferSize;
            auto source = surface->m_current.viewport.source;
            if (buffer.x > 0 && buffer.y > 0 && source.w > 0 && source.h > 0)
                uvRect = {float(std::clamp(source.x / buffer.x, 0., 1.)), float(std::clamp(source.y / buffer.y, 0., 1.)),
                          float(std::clamp((source.x + source.w) / buffer.x, 0., 1.)),
                          float(std::clamp((source.y + source.h) / buffer.y, 0., 1.))};
            if (uvRect[2] - uvRect[0] < .01f || uvRect[3] - uvRect[1] < .01f)
                uvRect = {0, 0, 1, 1};
        }
        size_t bytes = size_t(texture->m_size.x) * size_t(texture->m_size.y) * 4;
        if (bytes > 256 * 1024 * 1024 - leasedBytes) {
            skip(w, "texture budget");
            continue;
        }
        leasedBytes += bytes;
        auto it = std::find_if(entries.begin(), entries.end(), [&](auto &e) { return e.window == w; });
        if (it == entries.end() && spawnNew && !attached && inRoom && !w->m_isFloating &&
            !Fullscreen::controller()->isFullscreen(w)) {
            // Arrived tiled during the session (moved or dragged in): adopted once floating, at
            // its own size, so the spawn search never sees a full-output tile. Clients already
            // in the room when the world starts are adopted as before (anchor or pocket) and
            // floated below.
            floatRequests.push_back(w);
            skip(w, "floating first");
            continue;
        }
        if (it == entries.end()) {
            if (entries.size() >= 16) {
                skip(w, "capture limit");
                continue;
            }
            Entry e;
            e.window = w;
            e.surface.id =
                remembered != adoptedWindows_.end() ? remembered->id : "surface-" + std::to_string(++sequence_);
            size_t i = entries.size();
            e.surface.logicalSize(int(surface->m_current.size.x), int(surface->m_current.size.y), windowScale);
            e.surface.center = {float(int(i % 3) - 1) * 3.8f, 2.0f, -2.f - float(i / 3) * 3};
            if (i == 0)
                e.surface.center = {0, 2, -2};
            if (i == 1)
                e.surface.center = {-3.8f, 2, -2};
            if (i == 2)
                e.surface.center = {3.8f, 2, -2};
            if (!attached && inRoom && camera && world && (!layout || !layout->freePoses.contains(e.surface.id))) {
                e.surface.yaw = -camera->yaw;
                e.surface.center = camera->position + camera->forward() * spawnDistance;
                if (auto wall = interaction::raycast(camera->position, camera->forward(), world->collision,
                                                     &world->collisionIndex))
                    e.surface.center = camera->position + camera->forward() * interaction::limitSpawnDistance(
                                                                                  spawnDistance, wall->distance);
                // Keep the full client size. Failed recovery is handled by the
                // bounded spawn search, which queues the client if no spot fits.
                surfaces::pushOut(e.surface, world->collision);
                surfaces::Instance instance{e.surface.id, identity.appClass, identity.title, identity.workspaceId};
                if (layout && !layout->hasPlacement(instance, world))
                    switch (surfaces::adoptRoomClient(*world, i, !spawnNew, layout->unplaced.contains(e.surface.id))) {
                    case surfaces::RoomAdoption::Anchor: // posed at its authored anchor below, visible
                        break;
                    case surfaces::RoomAdoption::Pocket: // waits for explicit placement
                        layout->unplaced.insert(e.surface.id);
                        break;
                    case surfaces::RoomAdoption::Spawn: // only when a free full-sized spot exists
                        layout->spawn(instance, world, *camera, e.surface,
                                      float(monitor->m_size.x) / monitor->m_size.y);
                        break;
                    }
            }
            if (world && remembered == adoptedWindows_.end())
                adoptedWindows_.push_back({w, e.surface.id, world->id});
            e.commitListener = surface->m_events.commit.listen([count = e.commits] { ++*count; });
            entries.push_back(std::move(e));
            it = entries.end() - 1;
        }
        if (newlyMapped)
            knownWindows_.push_back(w);
        refreshed.insert(it->surface.id);
        it->texture = texture;
        it->surface.texture = texture->m_texID;
        it->surface.title = identity.title;
        it->instance = {it->surface.id, identity.appClass, identity.title, identity.workspaceId,
                        w->m_isFloating && !worldFloated.contains(w.get())};
        it->visible = true;
        it->surface.pixelWidth = std::max(1, int(surface->m_current.size.x));
        it->surface.pixelHeight = std::max(1, int(surface->m_current.size.y));
        it->surface.height = it->surface.width * float(it->surface.pixelHeight) / it->surface.pixelWidth;
        // Authored room anchors of an area-less world. A pocketed client stays hidden and
        // keeps no anchor pose: state never reports a window at a dock where it isn't drawn.
        if (!attached && world && !spawnNew &&
            (!layout || (!layout->freePoses.contains(it->surface.id) && !layout->unplaced.contains(it->surface.id))))
            if (auto a = surfaces::roomAnchor(*world, std::distance(entries.begin(), it)))
                surfaces::roomAnchorPose(it->surface, *a);
        if (!attached && layout && world->areas.empty() && layout->freePoses.contains(it->surface.id)) {
            auto pixels = it->surface;
            it->surface = layout->freePoses.at(it->surface.id);
            it->surface.title = pixels.title; // the pose was stored at spawn, before the client titled itself
            it->surface.texture = pixels.texture;
            it->surface.pixelWidth = pixels.pixelWidth;
            it->surface.pixelHeight = pixels.pixelHeight;
            if (auto c = layout->contentFor(it->instance, world)) {
                it->surface.width = (*c)["worldSize"][0];
                it->surface.height = (*c)["worldSize"][1];
            } else
                it->surface.logicalSize(pixels.pixelWidth, pixels.pixelHeight, windowScale);
            if (!surfaces::pushOut(it->surface, world->collision))
                it->visible = false;
        }
        it->elsewhere = elsewhere;
        if (!attached && layout && camera && world && !world->areas.empty() && !elsewhere) {
            it->visible = false;
            if (auto placed =
                    layout->locate(it->instance, world, *camera, it->surface.pixelWidth, it->surface.pixelHeight)) {
                placed->texture = texture->m_texID;
                placed->title = identity.title;
                it->surface = *placed;
                it->visible = true;
            }
        }
        it->surface.uvRect = uvRect;
        it->queued = layout && layout->unplaced.contains(it->surface.id);
        if (it->queued)
            it->visible = false;
        if (!attached && layout)
            if (auto c = layout->contentFor(it->instance, world)) {
                Vector2D logical{(*c)["logicalSize"][0].get<double>(), (*c)["logicalSize"][1].get<double>()};
                if ((std::abs(logical.x - surface->m_current.size.x) > 1 ||
                     std::abs(logical.y - surface->m_current.size.y) > 1) &&
                    w->m_target &&
                    (std::abs(w->m_target->position().w - logical.x) > 1 ||
                     std::abs(w->m_target->position().h - logical.y) > 1))
                    configureRequests.push_back({w, logical});
            }
        if (elsewhere)
            it->visible = false;
        if (!attached && !elsewhere && !Fullscreen::controller()->isFullscreen(w)) {
            bool free = inRoom || (layout && layout->pulled(it->instance, world));
            if (free && !w->m_isFloating) {
                floatRequests.push_back(w);
                if (!layout || layout->carried != it->surface.id)
                    it->visible = false; // never posed at its tiled size: shown once floating
            } else if (!free && w->m_isFloating && worldFloated.contains(w.get()))
                tileRequests.push_back(w);
        }
        if (attached && layout) {
            layout->freePoses.erase(it->surface.id);
            layout->spawnAreas.erase(it->surface.id);
        }
    }
    for (auto &previous : presenting_)
        if (auto w = previous.lock(); w && w->m_isMapped &&
                                      std::find(presenting.begin(), presenting.end(), previous) == presenting.end()) {
            auto ws = w->m_workspace;
            w->setSuspended(ws && !ws->isVisible());
        }
    presenting_ = std::move(presenting);
    std::erase_if(entries, [&](Entry &e) {
        if (refreshed.contains(e.surface.id))
            return false;
        if (layout)
            layout->forget(e.surface.id);
        if (e.window && e.window->m_isMapped) {
            auto ws = e.window->m_workspace;
            e.window->setSuspended(ws && !ws->isVisible());
        }
        return true;
    });
    if (layout)
        std::erase_if(layout->unplaced, [&](const auto &id) { return !refreshed.contains(id); });
    attach();
    trackBackdrops(monitor);
    if (layout && world && !world->areas.empty())
        compose(monitor, world, *layout);
}
void Capture::release() {
    configureRequests.clear();
    for (auto &previous : presenting_)
        if (auto w = previous.lock(); w && w->m_isMapped) {
            auto workspace = w->m_workspace;
            w->setSuspended(workspace && !workspace->isVisible());
        }
    presenting_.clear();
    for (auto &e : entries)
        if (e.window && e.window->m_isMapped) {
            auto workspace = e.window->m_workspace;
            e.window->setSuspended(workspace && !workspace->isVisible());
        }
    entries.clear();
    knownWindows_.clear();
    children.clear();
    Render::GL::g_pHyprOpenGL->makeEGLCurrent();
    render::GLState state;
    for (auto &frame : workspaces) {
        glDeleteTextures(1, &frame.texture);
        glDeleteFramebuffers(1, &frame.fbo);
    }
    workspaces.clear();
    backdrops_.clear();
    glDeleteTextures(1, &placeholder_);
    placeholder_ = 0;
    glDeleteProgram(program_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteSamplers(1, &sampler_);
    program_ = vao_ = sampler_ = 0;
}
Capture::Entry *Capture::find(const std::string &id) {
    for (auto &e : entries)
        if (e.surface.id == id && e.window && e.window->m_isMapped)
            return &e;
    return nullptr;
}
std::vector<surfaces::Surface> Capture::visible() const {
    std::vector<surfaces::Surface> result;
    for (auto &e : entries)
        if (e.visible)
            result.push_back(e.surface);
    for (auto &f : workspaces)
        result.push_back(f.surface);
    for (auto &c : children)
        if (c.resource && c.visible)
            result.push_back(c.surface);
    return result;
}
surfaces::Surface *Capture::plane(const std::string &id) {
    if (auto e = find(id))
        return &e->surface;
    for (auto &c : children)
        if (c.surface.id == id && c.resource)
            return &c.surface;
    if (auto w = workspace(id))
        return &w->surface;
    return nullptr;
}
std::optional<Capture::Target> Capture::target(const std::string &id, float u, float v) {
    if (auto wall = workspace(id)) {
        PHLMONITOR mon;
        for (auto &e : entries)
            if (e.instance.workspaceId == wall->workspaceId) {
                mon = e.window->m_monitor.lock();
                break;
            }
        if (!mon)
            return {};
        Vector2D global{mon->m_position.x + u * mon->m_size.x, mon->m_position.y + v * mon->m_size.y};
        for (auto it = entries.rbegin(); it != entries.rend(); ++it)
            if (it->instance.workspaceId == wall->workspaceId && !it->visible && !it->queued) {
                auto box = it->window->surfaceLogicalBox();
                if (!box)
                    continue;
                for (auto child = children.rbegin(); child != children.rend(); ++child)
                    if (child->parentId == it->surface.id && child->resource) {
                        auto local = global - Vector2D{box->x, box->y} - child->offset;
                        if (local.x >= 0 && local.y >= 0 && local.x < child->surface.pixelWidth &&
                            local.y < child->surface.pixelHeight &&
                            child->resource->m_current.effectiveInputRegion().containsPoint(local))
                            return Target{child->resource.lock(), local, it->surface.id};
                    }
                if (global.x < box->x || global.y < box->y || global.x >= box->x + box->w ||
                    global.y >= box->y + box->h)
                    continue;
                auto local = global - Vector2D{box->x, box->y};
                auto [resource, sub] = it->window->resource()->at(local, true);
                if (resource)
                    return Target{resource, sub, it->parentKeyboard ? it->parentId : it->surface.id};
            }
        return {};
    }
    if (auto e = find(id)) {
        Vector2D local{u * e->surface.pixelWidth, v * e->surface.pixelHeight};
        auto [resource, subLocal] = e->window->resource()->at(local, true);
        if (resource)
            return Target{resource, subLocal, e->parentKeyboard ? e->parentId : id};
    }
    for (auto &c : children)
        if (c.surface.id == id && c.resource) {
            Vector2D local{u * c.surface.pixelWidth, v * c.surface.pixelHeight};
            // at() on a subsurface still includes its parent-relative offset.
            // This plane already represents that exact leaf, so its input region
            // and seat coordinates must stay local to the leaf resource.
            if (c.resource->m_current.effectiveInputRegion().containsPoint(local))
                return Target{c.resource.lock(), local, c.parentId};
        }
    return {};
}
void Capture::attach() {
    auto previous = std::move(children);
    children.clear();
    auto add = [&](Entry &owner, SP<CWLSurfaceResource> r, Vector2D offset) {
        if (!r || !r->m_current.texture || !r->m_current.texture->m_texID ||
            r->m_current.texture->m_type == Render::TEXTURE_EXTERNAL ||
            r->m_current.transform != WL_OUTPUT_TRANSFORM_NORMAL || r->m_current.viewport.hasSource ||
            children.size() >= 128)
            return;
        Child child;
        child.resource = r;
        child.texture = r->m_current.texture;
        child.parentId = owner.surface.id;
        child.visible = owner.visible;
        child.offset = offset;
        auto old = std::find_if(previous.begin(), previous.end(), [&](auto &c) { return c.resource == r; });
        child.surface.id = old == previous.end() ? "child-" + std::to_string(++sequence_) : old->surface.id;
        if (old != previous.end()) {
            child.commits = old->commits;
            child.commitListener = std::move(old->commitListener);
        } else
            child.commitListener = r->m_events.commit.listen([count = child.commits] { ++*count; });
        auto &s = child.surface;
        auto &parent = owner.surface;
        s.rotation = parent.rotation;
        s.yaw = parent.yaw;
        s.texture = child.texture->m_texID;
        s.pixelWidth = std::max(1, int(r->m_current.size.x));
        s.pixelHeight = std::max(1, int(r->m_current.size.y));
        s.width = parent.width * s.pixelWidth / parent.pixelWidth;
        s.height = parent.height * s.pixelHeight / parent.pixelHeight;
        s.center = parent.center +
                   parent.right() * ((offset.x + s.pixelWidth * .5f) / parent.pixelWidth - .5f) * parent.width +
                   parent.up() * (.5f - (offset.y + s.pixelHeight * .5f) / parent.pixelHeight) * parent.height +
                   parent.normal() * (.006f + children.size() * .0001f);
        children.push_back(std::move(child));
        r->frame(std::chrono::steady_clock::now());
    };
    for (auto &e : entries) {
        auto w = e.window.lock();
        if (!w)
            continue;
        e.parentId.clear();
        auto parent = attachmentParent(w);
        e.parentKeyboard =
            w->m_isX11 && (w->m_X11ShouldntFocus || (w->m_xwaylandSurface && w->m_xwaylandSurface->m_overrideRedirect));
        if (parent)
            e.visible = false;
        if (parent)
            for (auto &p : entries)
                if (p.window == parent) {
                    e.parentId = p.surface.id;
                    e.visible = p.visible;
                    e.surface.rotation = p.surface.rotation;
                    e.surface.yaw = p.surface.yaw;
                    auto box = w->surfaceLogicalBox(), pb = parent->surfaceLogicalBox();
                    if (box && pb) {
                        float x = (box->x - pb->x + box->w * .5f) / std::max(1., pb->w) - .5f;
                        float y = .5f - (box->y - pb->y + box->h * .5f) / std::max(1., pb->h);
                        e.surface.width = p.surface.width * box->w / pb->w;
                        e.surface.height = p.surface.height * box->h / pb->h;
                        e.surface.center = p.surface.center + p.surface.right() * (x * p.surface.width) +
                                           p.surface.up() * (y * p.surface.height) + p.surface.normal() * .02f;
                    }
                    break;
                }
        auto root = w->resource();
        root->breadthfirst(
            [&](SP<CWLSurfaceResource> r, const Vector2D &off, void *) {
                if (r != root)
                    add(e, r, off);
            },
            nullptr);
        if (w->m_popupHead)
            w->m_popupHead->breadthfirst(
                [&](SP<Desktop::View::CPopup> p, void *) {
                    if (!p->m_mapped || !p->resource())
                        return;
                    auto box = w->surfaceLogicalBox();
                    if (!box)
                        return;
                    auto off = p->coordsGlobal() - Vector2D{box->x, box->y};
                    p->resource()->breadthfirst(
                        [&](SP<CWLSurfaceResource> r, const Vector2D &sub, void *) { add(e, r, off + sub); }, nullptr);
                },
                nullptr);
    }
}
Capture::WorkspaceFrame *Capture::workspace(const std::string &id) {
    for (auto &f : workspaces)
        if (f.surface.id == id)
            return &f;
    return nullptr;
}
void Capture::trackBackdrops(const PHLMONITOR &monitor) {
    std::vector<Backdrop> next;
    for (int level : {0, 1}) // ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, _BOTTOM
        for (auto &ref : monitor->m_layerSurfaceLayers[level]) {
            auto layer = ref.lock();
            if (!layer || !layer->m_mapped || !layer->m_layerSurface || !layer->m_layerSurface->m_surface)
                continue;
            auto old = std::find_if(backdrops_.begin(), backdrops_.end(), [&](auto &b) { return b.layer == layer; });
            if (old != backdrops_.end()) {
                next.push_back(std::move(*old));
                continue;
            }
            Backdrop b{.layer = layer};
            b.commitListener = layer->m_layerSurface->m_surface->m_events.commit.listen([count = b.commits] { ++*count; });
            next.push_back(std::move(b));
        }
    backdrops_ = std::move(next);
}
nlohmann::json Capture::backdropDiagnostics() const {
    auto out = nlohmann::json::array();
    for (auto &b : backdrops_) {
        auto layer = b.layer.lock();
        auto surface = layer && layer->m_layerSurface ? layer->m_layerSurface->m_surface.lock() : nullptr;
        auto box = layer ? layer->logicalBox() : std::nullopt;
        out.push_back({{"namespace", layer ? layer->m_namespace : ""},
                       {"commits", *b.commits},
                       {"texture", surface && surface->m_current.texture ? surface->m_current.texture->m_texID : 0},
                       {"box", box ? nlohmann::json{box->x, box->y, box->w, box->h} : nlohmann::json()}});
    }
    return out;
}
void Capture::compose(PHLMONITOR monitor, const world::World *world, surfaces::Layout &layout) {
    render::GLState state;
    if (!program_) {
        auto compile = [](GLenum type, const char *source) {
            GLuint sh = glCreateShader(type);
            glShaderSource(sh, 1, &source, nullptr);
            glCompileShader(sh);
            GLint ok;
            glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[1024];
                glGetShaderInfoLog(sh, sizeof log, nullptr, log);
                throw std::runtime_error(log);
            }
            return sh;
        };
        auto v = compile(GL_VERTEX_SHADER, R"(#version 300 es
precision highp float;
uniform vec4 rect;uniform vec4 crop;out vec2 uv;
void main(){vec2 p[6]=vec2[6](vec2(0,0),vec2(1,0),vec2(1,1),vec2(0,0),vec2(1,1),vec2(0,1));uv=crop.xy+p[gl_VertexID]*crop.zw;gl_Position=vec4((rect.xy+p[gl_VertexID]*rect.zw)*2.-1.,0,1);}
)");
        auto f = compile(GL_FRAGMENT_SHADER, R"(#version 300 es
precision highp float;
in vec2 uv;uniform sampler2D image;out vec4 color;void main(){color=texture(image,uv);}
)");
        program_ = glCreateProgram();
        glAttachShader(program_, v);
        glAttachShader(program_, f);
        glLinkProgram(program_);
        glDeleteShader(v);
        glDeleteShader(f);
        GLint ok;
        glGetProgramiv(program_, GL_LINK_STATUS, &ok);
        if (!ok)
            throw std::runtime_error("workspace composite link failed");
        glGenVertexArrays(1, &vao_);
        sampler_ = render::createSurfaceSampler();
    }
    if (!placeholder_) {
        auto pixels = render::brandPlaceholder(1280, 720);
        glGenTextures(1, &placeholder_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, placeholder_);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1280, 720, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    // Wallpaper layers the walls sample: texture, crop (viewporter source) and output rect.
    struct Backdrop {
        const Capture::Backdrop *source;
        uintptr_t identity; // the layer surface itself: stable across ticks
        SP<Render::ITexture> texture;
        CBox box;
        std::array<float, 4> crop{0, 0, 1, 1};
    };
    std::vector<Backdrop> backdrops;
    for (auto &b : backdrops_) {
        auto layer = b.layer.lock();
        auto surface = layer && layer->m_layerSurface ? layer->m_layerSurface->m_surface.lock() : nullptr;
        auto box = layer ? layer->logicalBox() : std::nullopt;
        if (!surface || !box || box->w <= 0 || box->h <= 0)
            continue;
        auto &current = surface->m_current;
        auto texture = current.texture;
        if (!texture || !texture->m_texID || texture->m_type == Render::TEXTURE_EXTERNAL ||
            current.transform != WL_OUTPUT_TRANSFORM_NORMAL)
            continue;
        Backdrop d{&b, reinterpret_cast<uintptr_t>(layer.get()), texture, *box};
        if (current.viewport.hasSource && current.bufferSize.x > 0 && current.bufferSize.y > 0) {
            // Source rectangle in surface coordinates; the buffer spans bufferSize / scale.
            Vector2D extent = current.bufferSize / std::max(1, current.scale);
            d.crop = std::array<float, 4>{float(current.viewport.source.x / extent.x), float(current.viewport.source.y / extent.y),
                                          float(current.viewport.source.w / extent.x), float(current.viewport.source.h / extent.y)};
        }
        backdrops.push_back(d);
    }
    for (auto &e : entries)
        e.wall.clear();
    // A window's rectangle is relative to the output its workspace is on.
    auto output = [&](const PHLWINDOW &w) {
        auto on = w ? w->m_monitor.lock() : nullptr;
        if (!on || on->m_size.x <= 0 || on->m_size.y <= 0)
            on = monitor;
        return std::pair{on->m_position, on->m_size};
    };
    std::set<int> live;
    for (const auto &b : layout.effectiveBindings()) {
        if (b.at("worldId") != world->id)
            continue;
        auto area = std::find_if(world->areas.begin(), world->areas.end(),
                                 [&](auto &a) { return a.at("id") == b.at("areaId"); });
        if (area == world->areas.end())
            continue;
        auto anchor = layout.anchor(area->at("primaryAnchor"), world);
        if (!anchor)
            continue;
        int id = b.at("id");
        live.insert(id);
        auto it = std::find_if(workspaces.begin(), workspaces.end(), [&](auto &f) { return f.workspaceId == id; });
        if (it == workspaces.end()) {
            WorkspaceFrame frame;
            frame.workspaceId = id;
            glGenFramebuffers(1, &frame.fbo);
            workspaces.push_back(frame);
            it = workspaces.end() - 1;
        }
        it->surface.id = "workspace-" + std::to_string(id);
        it->surface.title = area->at("theme").get<std::string>() + " / workspace " + std::to_string(id);
        it->surface.pixelWidth = 1280;
        it->surface.pixelHeight = 720;
        surfaces::anchorPlane(it->surface, *anchor, 1280, 720);
        // The workspace's windows in native stacking order: tiled, floating, pinned,
        // fullscreen; within a layer by the compositor's window list (a raise moves a
        // window to its end), so focus raises change the wall without a client commit.
        const auto &native = Desktop::windowState()->windows();
        auto layer = [](const PHLWINDOW &w) {
            return Fullscreen::controller()->isFullscreen(w) ? 3 : w->m_pinned ? 2 : w->m_isFloating ? 1 : 0;
        };
        std::vector<std::pair<std::pair<int, size_t>, Entry *>> stack;
        for (auto &entry : entries) {
            auto parent =
                entry.parentId.empty() ? entries.end() : std::find_if(entries.begin(), entries.end(), [&](auto &p) {
                    return p.surface.id == entry.parentId;
                });
            bool attached = parent != entries.end();
            if (!layout.composedOnWall(id, entry.instance, entry.elsewhere, world,
                                       attached ? &parent->instance : nullptr, attached && parent->elsewhere))
                continue;
            auto window = entry.window.lock();
            if (!window || !window->logicalBox())
                continue;
            size_t at = std::find(native.begin(), native.end(), window) - native.begin();
            stack.push_back({{layer(window), at}, &entry});
            // Not drawn on its own: its pose is its rectangle on the wall, never a placeholder.
            auto box = *window->logicalBox();
            auto &wall = it->surface;
            auto [origin, extent] = output(window);
            float u = float((box.x - origin.x) / extent.x), v = float((box.y - origin.y) / extent.y),
                  du = float(box.w / extent.x), dv = float(box.h / extent.y);
            entry.wall = wall.id;
            entry.surface.rotation = wall.rotation;
            entry.surface.yaw = wall.yaw;
            entry.surface.width = du * wall.width;
            entry.surface.height = dv * wall.height;
            entry.surface.center = wall.center + wall.right() * ((u + du * .5f - .5f) * wall.width) +
                                   wall.up() * ((.5f - v - dv * .5f) * wall.height);
        }
        std::stable_sort(stack.begin(), stack.end(), [](auto &a, auto &b) { return a.first < b.first; });
        // Recompose only when stacking, a window, popup, rectangle or pull changed: an idle
        // wall must not cost a clear and a draw per window on every tick.
        uint64_t key = 1469598103934665603ull;
        auto mix = [&](double v) { key = (key ^ std::hash<double>{}(v)) * 1099511628211ull; };
        mix(monitor->m_position.x), mix(monitor->m_position.y), mix(monitor->m_size.x), mix(monitor->m_size.y);
        // What the user sees on that workspace: the wallpaper behind its windows. With no
        // wallpaper surface, or no windows, the wall shows the Hyprune placeholder.
        bool placeholder = stack.empty() || backdrops.empty();
        mix(placeholder ? 1 : 2);
        // Windows are composed at the monitor's own pixel size, so a wall seen up close is
        // as sharp as the desktop (a fixed 1280x720 was magnified ~3x at 8 m on 4K). Mips
        // keep it clean across a court. A wall without windows (placeholder) stays small.
        int width = stack.empty() ? 1280 : std::clamp(int(monitor->m_pixelSize.x), 1, 4096),
            height = stack.empty() ? 720 : std::clamp(int(monitor->m_pixelSize.y), 1, 4096);
        mix(width), mix(height);
        if (it->width != width || it->height != height) {
            glDeleteTextures(1, &it->texture);
            glGenTextures(1, &it->texture);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, it->texture);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            glTexStorage2D(GL_TEXTURE_2D, 1 + int(std::floor(std::log2(std::max(width, height)))), GL_RGBA8, width, height);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindFramebuffer(GL_FRAMEBUFFER, it->fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, it->texture, 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                throw std::runtime_error("workspace composite FBO unavailable");
            it->width = width;
            it->height = height;
            it->key = 0;
        }
        it->surface.texture = it->texture;
        it->surface.mipmapped = true;
        if (!placeholder)
            for (auto &d : backdrops) {
                mix(double(d.identity)), mix(double(*d.source->commits)), mix(d.texture->m_texID);
                mix(d.box.x), mix(d.box.y), mix(d.box.w), mix(d.box.h);
                for (float c : d.crop) mix(c);
            }
        for (auto &[rank, item] : stack) {
            auto &entry = *item;
            auto box = entry.window.lock()->logicalBox();
            key = (key ^ std::hash<std::string>{}(entry.surface.id)) * 1099511628211ull;
            mix(rank.first);
            mix(double(*entry.commits)), mix(entry.surface.texture), mix(box->x), mix(box->y), mix(box->w), mix(box->h);
            for (auto &child : children)
                if (child.parentId == entry.surface.id && child.resource) {
                    mix(double(*child.commits)), mix(child.surface.texture), mix(child.offset.x), mix(child.offset.y);
                    mix(child.surface.pixelWidth), mix(child.surface.pixelHeight);
                }
        }
        if (key == it->key)
            continue;
        it->key = key;
        ++it->composed;
        glBindFramebuffer(GL_FRAMEBUFFER, it->fbo);
        glViewport(0, 0, it->width, it->height);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_RASTERIZER_DISCARD);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(.02, .03, .045, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(program_);
        glUniform1i(glGetUniformLocation(program_, "image"), 0);
        glBindVertexArray(vao_);
        glActiveTexture(GL_TEXTURE0);
        glBindSampler(0, sampler_);
        glEnable(GL_BLEND);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glUniform4f(glGetUniformLocation(program_, "crop"), 0, 0, 1, 1);
        if (placeholder) {
            glUniform4f(glGetUniformLocation(program_, "rect"), 0, 0, 1, 1);
            glBindTexture(GL_TEXTURE_2D, placeholder_);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        } else
            for (auto &d : backdrops) {
                glUniform4f(glGetUniformLocation(program_, "rect"), (d.box.x - monitor->m_position.x) / monitor->m_size.x,
                            (d.box.y - monitor->m_position.y) / monitor->m_size.y, d.box.w / monitor->m_size.x,
                            d.box.h / monitor->m_size.y);
                glUniform4f(glGetUniformLocation(program_, "crop"), d.crop[0], d.crop[1], d.crop[2], d.crop[3]);
                glBindTexture(GL_TEXTURE_2D, d.texture->m_texID);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }
        glUniform4f(glGetUniformLocation(program_, "crop"), 0, 0, 1, 1);
        // Compose approved root texture leases at the compositor's actual window rectangles.
        // Shell layers/world pass never enter this source; no compositor rendering is called.
        for (auto &[rank, item] : stack) {
            auto &entry = *item;
            auto box = entry.window.lock()->logicalBox();
            auto [origin, extent] = output(entry.window.lock());
            glUniform4f(glGetUniformLocation(program_, "rect"), (box->x - origin.x) / extent.x,
                        (box->y - origin.y) / extent.y, box->w / extent.x, box->h / extent.y);
            glBindTexture(GL_TEXTURE_2D, entry.surface.texture);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            for (auto &child : children)
                if (child.parentId == entry.surface.id && child.resource) {
                    glUniform4f(glGetUniformLocation(program_, "rect"), (box->x - origin.x + child.offset.x) / extent.x,
                                (box->y - origin.y + child.offset.y) / extent.y, child.surface.pixelWidth / extent.x,
                                child.surface.pixelHeight / extent.y);
                    glBindTexture(GL_TEXTURE_2D, child.surface.texture);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                }
        }
        glBindTexture(GL_TEXTURE_2D, it->texture);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    std::erase_if(workspaces, [&](auto &f) {
        if (live.contains(f.workspaceId))
            return false;
        glDeleteFramebuffers(1, &f.fbo);
        glDeleteTextures(1, &f.texture);
        return true;
    });
}
} // namespace hyprune::platform
