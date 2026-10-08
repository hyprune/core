#pragma once
#include "Packet.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
// clang-format off
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
// clang-format on
#include <chrono>
#include <cstring>
#include <fstream>
#include <json.hpp>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
namespace hyprune::offload {
using Json = nlohmann::json;
inline double now() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline void check(bool ok, const char *what) {
    if (!ok)
        throw std::runtime_error(std::string(what) + " EGL=" + std::to_string(eglGetError()));
}
template <class T> T proc(const char *name) {
    auto p = reinterpret_cast<T>(eglGetProcAddress(name));
    check(p, name);
    return p;
}
inline void send(int fd, const Json &body, const std::vector<int> &fds = {}) {
    check(sendPacket(fd, body, fds), "worker backpressure");
}
inline bool receive(int fd, Json &body, std::vector<int> &fds, bool = false) {
    auto p = receivePacket(fd);
    if (!p)
        return false;
    body = std::move(p->body);
    fds = std::move(p->fds);
    p->fds.clear();
    return true;
}
struct Context {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    explicit Context(const std::string &node) {
        auto query = proc<PFNEGLQUERYDEVICESEXTPROC>("eglQueryDevicesEXT");
        auto string = proc<PFNEGLQUERYDEVICESTRINGEXTPROC>("eglQueryDeviceStringEXT");
        EGLDeviceEXT devices[16];
        EGLint count = 0;
        check(query(16, devices, &count), "devices");
        for (int i = 0; i < count; i++) {
            const char *path = string(devices[i], EGL_DRM_RENDER_NODE_FILE_EXT);
            if (path && node == path) {
                auto candidate = proc<PFNEGLGETPLATFORMDISPLAYEXTPROC>("eglGetPlatformDisplayEXT")(
                    EGL_PLATFORM_DEVICE_EXT, devices[i], nullptr);
                if (eglInitialize(candidate, nullptr, nullptr)) {
                    display = candidate;
                    break;
                }
            }
        }
        check(display != EGL_NO_DISPLAY, "requested render node missing");
        check(eglInitialize(display, nullptr, nullptr), "initialize");
        check(eglBindAPI(EGL_OPENGL_ES_API), "bind GLES");
        EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
        EGLConfig config;
        check(eglChooseConfig(display, attrs, &config, 1, &count) && count, "config");
        EGLint ca[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE}, sa[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, ca);
        surface = eglCreatePbufferSurface(display, config, sa);
        check(eglMakeCurrent(display, surface, surface, context), "make current");
    }
    ~Context() {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(display, surface);
        eglDestroyContext(display, context);
        eglTerminate(display);
    }
};
inline GLuint texture(int w, int h) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return t;
}
inline GLuint framebuffer(GLuint t) {
    GLuint f;
    glGenFramebuffers(1, &f);
    glBindFramebuffer(GL_FRAMEBUFFER, f);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "framebuffer");
    return f;
}
inline EGLImageKHR import(EGLDisplay d, int fd, int w, int h, int stride, int offset, uint32_t format,
                          uint64_t modifier) {
    EGLint a[] = {EGL_WIDTH,
                  w,
                  EGL_HEIGHT,
                  h,
                  EGL_LINUX_DRM_FOURCC_EXT,
                  EGLint(format),
                  EGL_DMA_BUF_PLANE0_FD_EXT,
                  fd,
                  EGL_DMA_BUF_PLANE0_OFFSET_EXT,
                  offset,
                  EGL_DMA_BUF_PLANE0_PITCH_EXT,
                  stride,
                  EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
                  EGLint(modifier),
                  EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,
                  EGLint(modifier >> 32),
                  EGL_NONE};
    auto image =
        proc<PFNEGLCREATEIMAGEKHRPROC>("eglCreateImageKHR")(d, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, a);
    check(image != EGL_NO_IMAGE_KHR, "dmabuf EGLImage import");
    return image;
}
inline GLuint imageTexture(EGLImageKHR image) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    proc<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>("glEGLImageTargetTexture2DOES")(GL_TEXTURE_2D, image);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    check(glGetError() == GL_NO_ERROR, "image texture");
    return t;
}
inline int fence(EGLDisplay d) {
    auto s = proc<PFNEGLCREATESYNCKHRPROC>("eglCreateSyncKHR")(d, EGL_SYNC_NATIVE_FENCE_ANDROID, nullptr);
    check(s != EGL_NO_SYNC_KHR, "native fence");
    glFlush();
    int fd = proc<PFNEGLDUPNATIVEFENCEFDANDROIDPROC>("eglDupNativeFenceFDANDROID")(d, s);
    proc<PFNEGLDESTROYSYNCKHRPROC>("eglDestroySyncKHR")(d, s);
    check(fd >= 0, "native fence fd");
    return fd;
}
struct Timestamps {
    GLuint q[4]{};
    void init() { glGenQueries(4, q); }
    void stamp(int i) { proc<PFNGLQUERYCOUNTEREXTPROC>("glQueryCounterEXT")(q[i], GL_TIMESTAMP_EXT); }
    double between(int a, int b) {
        GLint disjoint = 0;
        glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
        check(!disjoint, "disjoint GPU clock");
        GLuint64 x = 0, y = 0;
        auto get = proc<PFNGLGETQUERYOBJECTUI64VEXTPROC>("glGetQueryObjectui64vEXT");
        get(q[a], GL_QUERY_RESULT_EXT, &x);
        get(q[b], GL_QUERY_RESULT_EXT, &y);
        return double(y - x) / 1e6;
    }
    void destroy() { glDeleteQueries(4, q); }
};
} // namespace hyprune::offload
