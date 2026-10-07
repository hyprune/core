#include "render/Renderer.hpp"
#ifdef HYPRUNE_BENCH_LEGACY
#include "Legacy.hpp"
#endif
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2ext.h>
#include <algorithm>
#include <fstream>
#include <iostream>
using namespace hyprune;
int main(int argc, char **argv) {
    try {
        if (argc < 3)
            throw std::runtime_error("usage: material-benchmark WORLD EGL_DEVICE_INDEX [WIDTH HEIGHT]");
        auto devices = reinterpret_cast<PFNEGLQUERYDEVICESEXTPROC>(eglGetProcAddress("eglQueryDevicesEXT"));
        auto platform =
            reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
        EGLDeviceEXT list[16];
        EGLint count = 0;
        if (!devices || !platform || !devices(16, list, &count) || std::stoi(argv[2]) >= count)
            throw std::runtime_error("EGL device unavailable");
        auto display = platform(EGL_PLATFORM_DEVICE_EXT, list[std::stoi(argv[2])], nullptr);
        if (!eglInitialize(display, nullptr, nullptr))
            throw std::runtime_error("EGL initialize");
        eglBindAPI(EGL_OPENGL_ES_API);
        EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
        EGLConfig config;
        eglChooseConfig(display, attrs, &config, 1, &count);
        EGLint contextAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        auto context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttrs);
        EGLint size[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        auto surface = eglCreatePbufferSurface(display, config, size);
        if (!eglMakeCurrent(display, surface, surface, context))
            throw std::runtime_error("EGL context");
        int width = argc > 3 ? std::stoi(argv[3]) : 3840, height = argc > 4 ? std::stoi(argv[4]) : 2160;
        GLuint tex, fbo;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        render::Renderer renderer;
        auto scene = world::load(argv[1]);
        renderer.prepare(scene);
#ifdef HYPRUNE_BENCH_LEGACY
        legacy::Renderer legacy;
        legacy.prepare(scene);
#endif
        sim::Camera camera;
        camera.position = {0, 2.4f, 3};
        camera.pitch = .03f;
        auto begin = reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(eglGetProcAddress("glBeginQueryEXT"));
        auto end = reinterpret_cast<PFNGLENDQUERYEXTPROC>(eglGetProcAddress("glEndQueryEXT"));
        auto result = reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(eglGetProcAddress("glGetQueryObjectui64vEXT"));
        if (!begin || !end || !result)
            throw std::runtime_error("GPU timer unavailable");
        GLuint query;
        glGenQueries(1, &query);
        std::vector<double> samples[2];
        std::vector<double> differences;
        double baseline = 0;
        for (int i = 0; i < 700; i++) {
            int detailed = i % 2;
            renderer.materialDetails = detailed;
            glFinish();
            begin(GL_TIME_ELAPSED_EXT, query);
#ifdef HYPRUNE_BENCH_LEGACY
            if (!detailed)
                legacy.draw(fbo, width, height, camera, {}, false);
            else
#endif
                renderer.draw(fbo, width, height, camera, {}, false);
            end(GL_TIME_ELAPSED_EXT);
            glFinish();
            GLuint64 ns = 0;
            result(query, GL_QUERY_RESULT_EXT, &ns);
            GLint disjoint = 0;
            glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
            if (disjoint)
                throw std::runtime_error("disjoint GPU timing");
            if (i >= 100) {
                samples[detailed].push_back(ns / 1e6);
                if (!detailed)
                    baseline = ns / 1e6;
                else
                    differences.push_back(ns / 1e6 - baseline);
            }
        }
        nlohmann::json report = {{"renderer", reinterpret_cast<const char *>(glGetString(GL_RENDERER))},
                                 {"resolution", {width, height}},
                                 {"samplesPerMode", 300},
                                 {"anisotropyMax", 8},
                                 {"msaa", 4}};
        for (int i = 0; i < 2; i++) {
            std::sort(samples[i].begin(), samples[i].end());
            report[i ? "materials" : "diffuse"] = {{"medianMs", samples[i][150]}, {"p95Ms", samples[i][285]}};
        }
        report["addedMedianMs"] =
            report["materials"]["medianMs"].get<double>() - report["diffuse"]["medianMs"].get<double>();
        std::sort(differences.begin(), differences.end());
        report["pairedAddedMedianMs"] = differences[150];
        report["pairedAddedP95Ms"] = differences[285];
#ifdef HYPRUNE_BENCH_LEGACY
        report["baseline"] = "M0 renderer 6aa79e2 (no samplers/transforms/PBR)";
#else
        report["baseline"] = "M1 diffuse, samplers/transforms enabled, normal/MR/specular disabled";
#endif
        std::cout << report.dump(2) << '\n';
        renderer.materialDetails = true;
        renderer.draw(fbo, width, height, camera, {}, false);
        std::vector<unsigned char> pixels(size_t(width) * height * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        if (const char *path = getenv("HYPRUNE_BENCH_IMAGE")) {
            std::ofstream out(path, std::ios::binary);
            out << "P6\n" << width << ' ' << height << "\n255\n";
            for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++)
                    out.write(reinterpret_cast<char *>(pixels.data() + (size_t(y) * width + x) * 4), 3);
        }
        if (glGetError() != GL_NO_ERROR)
            throw std::runtime_error("GL error during benchmark");
#ifdef HYPRUNE_BENCH_LEGACY
        legacy.destroy();
#endif
        renderer.destroy();
        glDeleteQueries(1, &query);
        glDeleteTextures(1, &tex);
        glDeleteFramebuffers(1, &fbo);
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(display, surface);
        eglDestroyContext(display, context);
        eglTerminate(display);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
