#include "Capture.hpp"
#include "interaction/Hands.hpp"
#include "render/GLState.hpp"
#include "render/SurfaceSampler.hpp"
#include <GLES3/gl3.h>
#include <algorithm>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/Popup.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/xwayland/XSurface.hpp>
#include <set>
namespace hyprune::platform {
// Runs on the event loop, never from IPassElement::draw. Read-only texture
// leases avoid snapshot FBOs and compositor recursion entirely for M0.
void Capture::refresh(PHLMONITOR monitor, const world::World *world, surfaces::Layout *layout,
                      const sim::Camera *camera) {
    std::erase_if(entries, [&](Entry &e) {
        bool remove = !e.window || !e.window->m_isMapped || e.window->m_monitor != monitor;
        if (remove && e.window && e.window->m_isMapped) {
            auto ws = e.window->m_workspace;
            e.window->setSuspended(ws && !ws->isVisible());
        }
        return remove;
    });
    if (layout) {
        layout->instances = world::Json::array();
        layout->windowScale = windowScale;
    }
    std::erase_if(knownWindows_, [](auto &w) { return !w || !w->m_isMapped; });
    std::erase_if(adoptedWindows_, [](auto &w) { return !w.window || !w.window->m_isMapped; });
    std::set<std::string> refreshed;
    size_t leasedBytes = 0;
    for (auto &w : Desktop::windowState()->windows()) {
        if (!w->m_isMapped || w->isHidden() || w->m_monitor != monitor || !w->resource())
            continue;
        bool newlyMapped = std::none_of(knownWindows_.begin(), knownWindows_.end(), [&](auto &v) { return v == w; });
        // Baseline all existing windows, including windows without a texture yet.
        if (!spawnNew && newlyMapped)
            knownWindows_.push_back(w);
        auto remembered = std::find_if(adoptedWindows_.begin(), adoptedWindows_.end(),
                                       [&](auto &v) { return v.window == w && world && v.worldId == world->id; });
        auto existing = std::find_if(entries.begin(), entries.end(), [&](auto &e) { return e.window == w; });
        surfaces::Instance identity{"", w->m_class, w->m_title, w->m_workspace ? int(w->m_workspace->m_id) : 0};
        if (world && !world->areas.empty() && existing == entries.end() && !newlyMapped && layout &&
            !layout->binding(identity.workspaceId, world) && !layout->hasPlacement(identity, world) &&
            remembered == adoptedWindows_.end())
            continue;
        auto surface = w->resource();
        auto texture = surface->m_current.texture;
        if (!texture || !texture->m_texID || texture->m_type == Render::TEXTURE_EXTERNAL)
            continue;
        // Unsupported source geometry is unavailable instead of silently
        // routing a click using mismatched texture coordinates.
        if (surface->m_current.transform != WL_OUTPUT_TRANSFORM_NORMAL || surface->m_current.viewport.hasSource)
            continue;
        size_t bytes = size_t(texture->m_size.x) * size_t(texture->m_size.y) * 4;
        if (bytes > 256 * 1024 * 1024 - leasedBytes)
            continue;
        leasedBytes += bytes;
        auto it = std::find_if(entries.begin(), entries.end(), [&](auto &e) { return e.window == w; });
        if (it == entries.end()) {
            if (entries.size() >= 16)
                continue;
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
            if (camera && world &&
                ((spawnNew && newlyMapped) ||
                 (remembered != adoptedWindows_.end() && layout && !layout->freePoses.contains(e.surface.id)))) {
                e.surface.yaw = -camera->yaw;
                e.surface.center = camera->position + camera->forward() * spawnDistance;
                if (auto wall = interaction::raycast(camera->position, camera->forward(), world->collision))
                    e.surface.center =
                        camera->position +
                        camera->forward() * std::min(spawnDistance, std::max(.2f, wall->distance - .15f));
                for (auto &other : entries)
                    if (auto hit = surfaces::intersect(other.surface, camera->position, camera->forward());
                        hit && hit->distance < spawnDistance)
                        e.surface.center = camera->position + camera->forward() * std::max(.2f, hit->distance - .15f);
                if (!surfaces::pushOut(e.surface, world->collision)) {
                    auto id = e.surface.id;
                    e.surface = surfaces::carryPlane(*camera, e.surface.width, e.surface.height, world->collision);
                    e.surface.id = id;
                }
                surfaces::Instance instance{e.surface.id, w->m_class, w->m_title,
                                            w->m_workspace ? int(w->m_workspace->m_id) : 0};
                if (layout && !layout->hasPlacement(instance, world)) {
                    layout->spawn(instance, world, *camera, e.surface, float(monitor->m_size.x) / monitor->m_size.y);
                    if (remembered == adoptedWindows_.end())
                        adoptedWindows_.push_back({w, e.surface.id, world->id});
                }
            }
            e.commitListener = surface->m_events.commit.listen([count = e.commits] { ++*count; });
            entries.push_back(std::move(e));
            it = entries.end() - 1;
        }
        if (newlyMapped)
            knownWindows_.push_back(w);
        refreshed.insert(it->surface.id);
        it->texture = texture;
        it->surface.texture = texture->m_texID;
        it->surface.title = w->m_title;
        it->instance = {it->surface.id, w->m_class, w->m_title, w->m_workspace ? int(w->m_workspace->m_id) : 0};
        it->visible = true;
        it->surface.pixelWidth = std::max(1, int(surface->m_current.size.x));
        it->surface.pixelHeight = std::max(1, int(surface->m_current.size.y));
        it->surface.height = it->surface.width * float(it->surface.pixelHeight) / it->surface.pixelWidth;
        if (world && world->areas.empty() && !spawnNew && (!layout || !layout->freePoses.contains(it->surface.id))) {
            auto anchors = world->manifest.value("anchors", world::Json::array());
            size_t index = std::distance(entries.begin(), it);
            if (index < anchors.size() && anchors[index].at("screenType") == "com.hyprune.window") {
                auto &a = anchors[index];
                auto pos = a.at("pose").at("position"), q = a.at("pose").at("rotation");
                it->surface.center = {pos[0], pos[1], pos[2]};
                it->surface.yaw =
                    std::atan2(2 * (q[3].get<float>() * q[1].get<float>() + q[0].get<float>() * q[2].get<float>()),
                               1 - 2 * (q[1].get<float>() * q[1].get<float>() + q[2].get<float>() * q[2].get<float>()));
                float ratio = float(it->surface.pixelWidth) / it->surface.pixelHeight;
                it->surface.width = std::min(a.at("size")[0].get<float>(), a.at("size")[1].get<float>() * ratio);
                it->surface.height = it->surface.width / ratio;
            }
        }
        if (layout && world->areas.empty() && layout->freePoses.contains(it->surface.id)) {
            auto pixels = it->surface;
            it->surface = layout->freePoses.at(it->surface.id);
            it->surface.texture = pixels.texture;
            it->surface.pixelWidth = pixels.pixelWidth;
            it->surface.pixelHeight = pixels.pixelHeight;
            it->surface.logicalSize(pixels.pixelWidth, pixels.pixelHeight, windowScale);
            if (!surfaces::pushOut(it->surface, world->collision))
                it->visible = false;
        }
        if (layout && camera && world && !world->areas.empty()) {
            it->visible = false;
            if (auto placed =
                    layout->locate(it->instance, world, *camera, it->surface.pixelWidth, it->surface.pixelHeight)) {
                placed->texture = texture->m_texID;
                it->surface = *placed;
                it->visible = true;
            }
        }
        w->setSuspended(false);
        surface->frame(std::chrono::steady_clock::now());
    }
    std::erase_if(entries, [&](Entry &e) {
        if (refreshed.contains(e.surface.id))
            return false;
        if (e.window && e.window->m_isMapped) {
            auto ws = e.window->m_workspace;
            e.window->setSuspended(ws && !ws->isVisible());
        }
        return true;
    });
    attach();
    if (layout && world && !world->areas.empty())
        compose(monitor, world, *layout);
}
void Capture::release() {
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
            if (it->instance.workspaceId == wall->workspaceId && !it->visible) {
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
        auto parent = w->parent();
        if (!parent)
            parent = w->x11Parent();
        e.parentKeyboard =
            w->m_isX11 && (w->m_X11ShouldntFocus || (w->m_xwaylandSurface && w->m_xwaylandSurface->m_overrideRedirect));
        if (!parent && e.parentKeyboard)
            parent = Desktop::focusState()->window();
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
uniform vec4 rect;out vec2 uv;
void main(){vec2 p[6]=vec2[6](vec2(0,0),vec2(1,0),vec2(1,1),vec2(0,0),vec2(1,1),vec2(0,1));uv=p[gl_VertexID];gl_Position=vec4((rect.xy+uv*rect.zw)*2.-1.,0,1);}
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
    std::set<int> live;
    for (const auto &b : layout.bindings) {
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
            glGenTextures(1, &frame.texture);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, frame.texture);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1280, 720, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindFramebuffer(GL_FRAMEBUFFER, frame.fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, frame.texture, 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                throw std::runtime_error("workspace composite FBO unavailable");
            workspaces.push_back(frame);
            it = workspaces.end() - 1;
        }
        it->surface.id = "workspace-" + std::to_string(id);
        it->surface.title = area->at("theme").get<std::string>() + " / workspace " + std::to_string(id);
        it->surface.pixelWidth = 1280;
        it->surface.pixelHeight = 720;
        it->surface.texture = it->texture;
        surfaces::anchorPlane(it->surface, *anchor, 1280, 720);
        glBindFramebuffer(GL_FRAMEBUFFER, it->fbo);
        glViewport(0, 0, 1280, 720);
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
        // Compose approved root texture leases at the compositor's actual window rectangles.
        // Shell layers/world pass never enter this source; no compositor rendering is called.
        for (auto &entry : entries) {
            if (entry.instance.workspaceId != id || layout.pulled(entry.instance, world))
                continue;
            auto window = entry.window.lock();
            auto box = window->logicalBox();
            if (!box)
                continue;
            glUniform4f(glGetUniformLocation(program_, "rect"), (box->x - monitor->m_position.x) / monitor->m_size.x,
                        (box->y - monitor->m_position.y) / monitor->m_size.y, box->w / monitor->m_size.x,
                        box->h / monitor->m_size.y);
            glBindTexture(GL_TEXTURE_2D, entry.surface.texture);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            for (auto &child : children)
                if (child.parentId == entry.surface.id && child.resource) {
                    glUniform4f(glGetUniformLocation(program_, "rect"),
                                (box->x - monitor->m_position.x + child.offset.x) / monitor->m_size.x,
                                (box->y - monitor->m_position.y + child.offset.y) / monitor->m_size.y,
                                child.surface.pixelWidth / monitor->m_size.x,
                                child.surface.pixelHeight / monitor->m_size.y);
                    glBindTexture(GL_TEXTURE_2D, child.surface.texture);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                }
        }
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
