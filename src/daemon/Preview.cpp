// hyprune-preview (RFC-0016 preview.render): renders the loaded world from a pose into a PNG,
// in its own process with its own headless GL context, so a preview never runs in the
// compositor's render pass and never moves the user's camera. The daemon writes the scene
// (the package or world folder, the user's placements, the pose) to a JSON file and runs:
//
//   hyprune-preview SCENE.json OUT.png
//
// Scene: {root, manifest, libraryRoots[], objects[{dir, manifest, position, rotation, scale}],
// camera {position, yaw, pitch, fov} (degrees), width, height}. Windows are never drawn: screens
// show their empty frames. Exit status 0 and a one-line JSON {width, height, renderMs} on stdout.
#include "render/PNG.hpp"
#include "render/Renderer.hpp"
#include "world/Objects.hpp"
#include "world/World.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
using namespace hyprune;
using Json = world::Json;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
// The render node: HYPRUNE_PREVIEW_RENDER_NODE, else the first non-NVIDIA node (the integrated GPU
// on a hybrid laptop, which keeps the dGPU asleep), else Mesa's surfaceless default.
EGLDisplay display() {
    auto platform = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    auto query = reinterpret_cast<PFNEGLQUERYDEVICESEXTPROC>(eglGetProcAddress("eglQueryDevicesEXT"));
    auto info = reinterpret_cast<PFNEGLQUERYDEVICESTRINGEXTPROC>(eglGetProcAddress("eglQueryDeviceStringEXT"));
    EGLDeviceEXT devices[16];
    EGLint count = 0;
    const char *wanted = getenv("HYPRUNE_PREVIEW_RENDER_NODE");
    if (platform && query && info && query(16, devices, &count))
        for (EGLint i = 0; i < count; ++i) {
            const char *node = info(devices[i], EGL_DRM_RENDER_NODE_FILE_EXT);
            if (!node)
                continue;
            std::string vendor;
            std::ifstream(std::filesystem::path("/sys/class/drm") / std::filesystem::path(node).filename() / "device/vendor") >> vendor;
            if (wanted ? std::strcmp(node, wanted) == 0 : vendor != "0x10de")
                return platform(EGL_PLATFORM_DEVICE_EXT, devices[i], nullptr);
        }
    return eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
}
Vec3 vec3(const Json &v) { return {v.at(0).get<float>(), v.at(1).get<float>(), v.at(2).get<float>()}; }
} // namespace
int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "usage: hyprune-preview SCENE.json OUT.png\n";
        return 2;
    }
    try {
        auto started = std::chrono::steady_clock::now();
        Json scene = Json::parse(std::ifstream(argv[1]));
        int width = std::clamp(scene.value("width", 1024), 64, 1024), height = std::clamp(scene.value("height", 576), 64, 1024);
        EGLDisplay dpy = display();
        check(eglInitialize(dpy, nullptr, nullptr), "EGL init");
        eglBindAPI(EGL_OPENGL_ES_API);
        EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
        EGLConfig configs[64], config = nullptr;
        EGLint count = 0;
        check(eglChooseConfig(dpy, attrs, configs, 64, &count) && count, "EGL config");
        for (EGLint i = 0; i < count && !config; ++i) {
            EGLint r = 0, g = 0, b = 0;
            eglGetConfigAttrib(dpy, configs[i], EGL_RED_SIZE, &r);
            eglGetConfigAttrib(dpy, configs[i], EGL_GREEN_SIZE, &g);
            eglGetConfigAttrib(dpy, configs[i], EGL_BLUE_SIZE, &b);
            if (r == 8 && g == 8 && b == 8)
                config = configs[i];
        }
        check(config, "EGL 8-bit config");
        EGLint contextAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        auto context = eglCreateContext(dpy, config, EGL_NO_CONTEXT, contextAttrs);
        EGLint size[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
        auto pbuffer = eglCreatePbufferSurface(dpy, config, size);
        check(context != EGL_NO_CONTEXT && eglMakeCurrent(dpy, pbuffer, pbuffer, context), "EGL context");

        world::LoadOptions options{render::Renderer::compressedFormats()};
        options.manifest = scene.value("manifest", "world.json");
        for (auto &root : scene.value("libraryRoots", Json::array()))
            options.libraryRoots.push_back(root.get<std::string>());
        auto loaded = world::load(scene.at("root").get<std::string>(), options);
        // The user's placements: each object directory decoded once, merged, then instanced.
        std::map<std::string, unsigned> assets;
        world::LoadOptions objectOptions{options.compressedFormats};
        objectOptions.motion = loaded->motionPacks;
        for (auto &o : scene.value("objects", Json::array())) {
            auto dir = o.at("dir").get<std::string>();
            if (!assets.contains(dir)) {
                auto scratch = world::loadObjectAsset(dir, o.at("manifest"), objectOptions);
                assets[dir] = world::mergeObjectAsset(*loaded, *scratch);
            }
            world::ObjectInstance inst;
            inst.id = o.value("id", "preview");
            inst.asset = assets[dir];
            inst.position = vec3(o.at("position"));
            inst.rotation = o.at("rotation").get<std::array<float, 4>>();
            inst.authoredScale = inst.scale = o.value("scale", 1.f);
            inst.model = world::instanceModel(inst.position, inst.rotation, inst.scale);
            auto &a = loaded->objectAssets[inst.asset];
            inst.center = inst.model.point(a.center);
            inst.radius = a.radius * inst.scale;
            loaded->objectInstances.push_back(inst);
        }
        render::Renderer renderer;
        renderer.pacing = false; // every draw renders (an offline owner)
        renderer.prepare(loaded);
        GLuint texture, fbo;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "preview framebuffer");
        auto &c = scene.at("camera");
        sim::Camera camera;
        camera.position = vec3(c.at("position"));
        camera.yaw = c.value("yaw", 0.f) * float(M_PI / 180);
        camera.pitch = std::clamp(c.value("pitch", 0.f), -89.f, 89.f) * float(M_PI / 180);
        camera.fov = std::clamp(c.value("fov", 70.f), 20.f, 120.f);
        // Two draws: the first settles anything that converges over frames (exposure, probes).
        renderer.draw(fbo, width, height, camera, {}, false);
        renderer.draw(fbo, width, height, camera, {}, false);
        glFinish();
        std::vector<uint8_t> rgba(size_t(width) * height * 4);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        // Row zero is the logical top (the renderer's target convention): no flip.
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        for (size_t i = 3; i < rgba.size(); i += 4)
            rgba[i] = 255;
        render::writePNG(argv[2], width, height, rgba);
        renderer.destroy();
        auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        std::cout << Json{{"width", width}, {"height", height}, {"renderMs", std::round(ms)}}.dump() << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "hyprune-preview: " << e.what() << '\n';
        return 1;
    }
}
