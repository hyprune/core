// Input callbacks enter here only through deferInput, never from a render pass.
void Runtime::toggleFreeCursor() {
    if (!active || loading) return;
    if (renderDepth) throw std::runtime_error("free cursor transition inside render pass");
    if (layerCursor) {
        auto saved = cursorReturn;
        for (auto held : freeNativeButtons) {
            g_pSeatManager->sendPointerButton(0, held, WL_POINTER_BUTTON_STATE_RELEASED);
            swallowedButtons.insert(held);
        }
        if (!freeNativeButtons.empty()) g_pSeatManager->sendPointerFrame();
        freeNativeButtons.clear();
        layerCursor = freeCursor = false;
        cancelGrab(); input.clear(); keys.clear(); smoothIntent = {}; lookX = lookY = 0;
        camera = saved.camera;
        climbing = saved.climbing;
        typing = false;
        Desktop::focusState()->rawMonitorFocus(monitor.lock());
        Pointer::pointerController()->warpTo(monitor->m_position + monitor->m_size * .5, true);
        returnToWorldWorkspace();
        if (!saved.wall.empty() && capture.workspace(saved.wall)) {
            focusWall(saved.wall);
        } else if (saved.use && capture.workspace(saved.relative)) {
            auto wall = capture.workspace(saved.relative);
            auto result = g_pKeybindManager->m_dispatchers.at("workspace")(std::to_string(wall->workspaceId));
            if (result.success) {
                if (!saved.window.empty() && capture.find(saved.window)) focus(saved.window, true);
                typing = true; relativeSurface = saved.relative;
                localX = saved.x; localY = saved.y; projectCursor();
            }
        } else if (!saved.window.empty() && capture.find(saved.window)) {
            focus(saved.window, true);
            if (saved.fullscreen) {
                auto e = capture.find(saved.window);
                Fullscreen::controller()->setFullscreenMode(e->window.lock(), Fullscreen::FSMODE_FULLSCREEN,
                                                            Fullscreen::FSMODE_FULLSCREEN);
            } else {
                relativeSurface = saved.relative;
                localX = saved.x; localY = saved.y;
                projectCursor();
            }
        } else {
            focused.clear(); relativeSurface.clear();
            g_pSeatManager->setPointerFocus(nullptr, {});
            g_pSeatManager->setKeyboardFocus(nullptr);
        }
        cursorReturn = {};
    } else {
        // Finish the gesture exactly like its release before handing pointer ownership back.
        if (gestureButton) {
            IPointer::SButtonEvent release{};
            release.button = gestureButton; release.state = WL_POINTER_BUTTON_STATE_RELEASED;
            button(release, false);
        }
        if (objectCarry || editor.activeHandle >= 0) saveEditor();
        cursorReturn = {};
        cursorReturn.camera = camera;
        cursorReturn.climbing = climbing;
        cursorReturn.x = localX; cursorReturn.y = localY;
        cursorReturn.relative = relativeSurface;
        cursorReturn.use = typing;
        if (fullscreen.phase && fullscreen.phase != 3) {
            cursorReturn.window = fullscreen.id;
            cursorReturn.fullscreen = true;
        } else if (!layout.desktop.is_null() || handover.phase == 1) {
            int ws = layout.desktop.is_null() ? handover.workspace : layout.desktop.get<int>();
            for (auto &wall : capture.workspaces)
                if (wall.workspaceId == ws) cursorReturn.wall = wall.surface.id;
            if (handover.animated) cursorReturn.camera = handover.before;
        } else if (typing) cursorReturn.window = focused;
        auto keyboard = g_pSeatManager->m_state.keyboardFocus;
        control.stop();
        leaveInteraction();
        // Complete focus exit before freezing. All compositor mutations remain after the frame.
        if (handover.phase == 1) {
            camera = handover.before; handover = {};
        }
        if (handover.phase || fullscreen.phase) transitionTick(10.f);
        camera = cursorReturn.camera;
        layerCursor = freeCursor = true;
        typing = false;
        objectCarry = false; editor.activeHandle = -1;
        zoomHeld = false; zoom = 1; camera.fov = baseFov;
        input.clear(); keys.clear(); smoothIntent = {}; lookX = lookY = 0;
        g_pSeatManager->setKeyboardFocus(keyboard.lock());
        Pointer::pointerController()->warpTo(monitor->m_position + monitor->m_size * .5, true);
    }
    cursor(); publish();
}
bool Runtime::freeWorldPointer(Vector2D pos) const {
    if (!freeNativeButtons.empty() || !active || !layerCursor || !monitor || control.uiVisible() || !layout.desktop.is_null() || fullscreen.phase == 2)
        return false;
    return pos.x >= monitor->m_position.x && pos.y >= monitor->m_position.y &&
           pos.x < monitor->m_position.x + monitor->m_size.x && pos.y < monitor->m_position.y + monitor->m_size.y &&
           !interactiveLayerAt(pos);
}
void Runtime::freePointerPosition(Vector2D pos) {
    pointerX = (pos.x - monitor->m_position.x) / monitor->m_size.x;
    pointerY = (pos.y - monitor->m_position.y) / monitor->m_size.y;
    auto hit = aim();
    if (!grabbed.empty())
        if (auto plane = capture.plane(grabbed))
            if (auto h = surfaces::intersect(*plane, pointerCamera().position, pointerRay(), false))
                hit = {{grabbed, *h}};
    if (hit) routePointer(hit->first, hit->second);
    else if (buttons.empty()) g_pSeatManager->setPointerFocus(nullptr, {});
}
void Runtime::freePointerButton(IPointer::SButtonEvent event) {
    if (event.state == WL_POINTER_BUTTON_STATE_RELEASED) { button(event, false); return; }
    freePointerPosition(Pointer::mgr()->position());
    auto hit = aim();
    if (!hit) return; // Empty world never exposes a hidden native window.
    if (auto target = capture.target(hit->first, hit->second.u, hit->second.v)) {
        if (buttons.empty()) {
            if (auto e = capture.find(target->parentId)) {
                Desktop::focusState()->fullWindowFocus(e->window.lock(), Desktop::FOCUS_REASON_CLICK);
                g_pSeatManager->setKeyboardFocus(e->window->resource());
            }
            grabbed = hit->first; grabResource = target->resource;
        }
        buttons.insert(event.button);
        routePointer(hit->first, hit->second, event.timeMs);
        g_pSeatManager->sendPointerButton(event.timeMs, event.button, event.state);
        g_pSeatManager->sendPointerFrame();
    }
}
