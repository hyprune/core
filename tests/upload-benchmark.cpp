// Cold/warm world upload stalls: one budgeted continuePrepare(2 ms, 2 MiB) step per
// simulated 16 ms compositor tick, as core's deferred tick does. Reports the
// longest single job and step (the stall a frame can see) and total CPU time.
// Run with fresh XDG_CACHE_HOME and MESA_SHADER_CACHE_DISABLE=1 for cold shaders.
#include "render/ProgramCache.hpp"
#include "render/Renderer.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <chrono>
#include <iostream>
#include <thread>
using namespace hyprune;
int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "usage: upload-benchmark WORLD [tick_ms]\n";
        return 2;
    }
    EGLDisplay display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    eglInitialize(display, nullptr, nullptr);
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE}, count = 0;
    EGLConfig config;
    eglChooseConfig(display, attrs, &config, 1, &count);
    EGLint ctx[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2, EGL_NONE};
    auto context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx);
    EGLint size[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    auto surface = eglCreatePbufferSurface(display, config, size);
    eglMakeCurrent(display, surface, surface, context);
    double tick = argc > 2 ? std::stod(argv[2]) : 16.0;
    auto world = world::load(argv[1], {render::Renderer::compressedFormats()});
    render::Renderer renderer;
    renderer.warmShaders = false;
    auto started = std::chrono::steady_clock::now();
    renderer.beginPrepare(world);
    int ticks = 0;
    while (!renderer.continuePrepare(2, 2 * 1024 * 1024)) {
        ++ticks;
        std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(tick));
    }
    double wall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    auto upload = renderer.timings()["upload"];
    upload["ticks"] = ticks;
    upload["wallMs"] = wall;
    upload["shaderCache"] = {{"hits", render::ProgramCache::hits}, {"misses", render::ProgramCache::misses}};
    upload["device"] = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    std::cout << upload.dump() << "\n";
    renderer.destroy();
    return 0;
}
