#include "Capture.hpp"
#include "render/GLState.hpp"
#include <GLES3/gl3.h>
#include <algorithm>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
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
    if (layout)
        layout->instances = world::Json::array();
    std::set<std::string> refreshed;
    size_t leasedBytes = 0;
    for (auto &w : Desktop::windowState()->windows()) {
        if (!w->m_isMapped || w->isHidden() || w->m_monitor != monitor || !w->resource())
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
            e.surface.id = "surface-" + std::to_string(++sequence_);
            size_t i = entries.size();
            e.surface.center = {float(int(i % 3) - 1) * 3.8f, 2.0f, -2.f - float(i / 3) * 3};
            if (i == 0)
                e.surface.center = {0, 2, -2};
            if (i == 1)
                e.surface.center = {-3.8f, 2, -2};
            if (i == 2)
                e.surface.center = {3.8f, 2, -2};
            e.commitListener = surface->m_events.commit.listen([count = e.commits] { ++*count; });
            entries.push_back(std::move(e));
            it = entries.end() - 1;
        }
        refreshed.insert(it->surface.id);
        it->texture = texture;
        it->surface.texture = texture->m_texID;
        it->surface.title = w->m_title;
        it->instance = {it->surface.id, w->m_class, w->m_title, w->m_workspace ? int(w->m_workspace->m_id) : 0};
        it->visible = true;
        it->surface.pixelWidth = std::max(1, int(surface->m_current.size.x));
        it->surface.pixelHeight = std::max(1, int(surface->m_current.size.y));
        it->surface.height = it->surface.width * float(it->surface.pixelHeight) / it->surface.pixelWidth;
        if (world && world->areas.empty()) {
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
    Render::GL::g_pHyprOpenGL->makeEGLCurrent();
    render::GLState state;
    for (auto &frame : workspaces) {
        glDeleteTextures(1, &frame.texture);
        glDeleteFramebuffers(1, &frame.fbo);
    }
    workspaces.clear();
    glDeleteProgram(program_);
    glDeleteVertexArrays(1, &vao_);
    program_ = vao_ = 0;
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
    return result;
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
        glBindSampler(0, 0);
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
