#include "Capture.hpp"
#include <algorithm>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <set>
namespace hyprune::platform {
// Runs on the event loop, never from IPassElement::draw. Read-only texture
// leases avoid snapshot FBOs and compositor recursion entirely for M0.
void Capture::refresh(PHLMONITOR monitor) {
    std::erase_if(entries, [&](Entry &e) {
        bool remove = !e.window || !e.window->m_isMapped || e.window->m_monitor != monitor;
        if (remove && e.window && e.window->m_isMapped) {
            auto ws = e.window->m_workspace;
            e.window->setSuspended(ws && !ws->isVisible());
        }
        return remove;
    });
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
        it->surface.pixelWidth = std::max(1, int(surface->m_current.size.x));
        it->surface.pixelHeight = std::max(1, int(surface->m_current.size.y));
        it->surface.height = it->surface.width * float(it->surface.pixelHeight) / it->surface.pixelWidth;
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
}
void Capture::release() {
    for (auto &e : entries)
        if (e.window && e.window->m_isMapped) {
            auto workspace = e.window->m_workspace;
            e.window->setSuspended(workspace && !workspace->isVisible());
        }
    entries.clear();
}
Capture::Entry *Capture::find(const std::string &id) {
    for (auto &e : entries)
        if (e.surface.id == id && e.window && e.window->m_isMapped)
            return &e;
    return nullptr;
}
} // namespace hyprune::platform
