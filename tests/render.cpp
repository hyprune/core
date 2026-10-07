#include "render/Renderer.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <iostream>
using namespace hyprune;
void check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
int main(int argc, char **argv) {
    if (argc != 2)
        return 1;
    EGLDisplay display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    try {
        check(eglInitialize(display, nullptr, nullptr), "EGL init");
        eglBindAPI(EGL_OPENGL_ES_API);
        EGLint attrs[] = {EGL_SURFACE_TYPE,
                          EGL_PBUFFER_BIT,
                          EGL_RENDERABLE_TYPE,
                          EGL_OPENGL_ES3_BIT,
                          EGL_RED_SIZE,
                          8,
                          EGL_GREEN_SIZE,
                          8,
                          EGL_BLUE_SIZE,
                          8,
                          EGL_NONE};
        EGLConfig config;
        EGLint count;
        check(eglChooseConfig(display, attrs, &config, 1, &count) && count, "EGL config");
        EGLint ctx[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        auto context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx);
        EGLint size[] = {EGL_WIDTH, 320, EGL_HEIGHT, 180, EGL_NONE};
        auto pbuffer = eglCreatePbufferSurface(display, config, size);
        check(eglMakeCurrent(display, pbuffer, pbuffer, context), "EGL context");
        GLuint texture, fbo;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 320, 180, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "test framebuffer");
        render::Renderer renderer;
        renderer.prepare(world::load(argv[1]));
        unsigned char gray[] = {128, 128, 128, 255};
        GLuint app;
        glGenTextures(1, &app);
        glBindTexture(GL_TEXTURE_2D, app);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, gray);
        surfaces::Surface surface;
        surface.center = {0, 1.7f, -2};
        surface.texture = app;
        surface.width = 4;
        surface.height = 3;
        sim::Camera camera;
        glViewport(3, 5, 100, 90);
        glEnable(GL_BLEND);
        glEnable(GL_SCISSOR_TEST);
        glScissor(2, 4, 17, 19);
        glDepthMask(GL_FALSE);
        glColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, app);
        renderer.draw(fbo, 320, 180, camera, std::span(&surface, 1), false);
        GLint viewport[4], bound, active;
        GLboolean mask[4], depth;
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
        glGetBooleanv(GL_COLOR_WRITEMASK, mask);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depth);
        check(viewport[0] == 3 && viewport[1] == 5 && viewport[2] == 100 && viewport[3] == 90, "viewport leaked");
        check(active == GL_TEXTURE1 && bound == int(app), "texture state leaked");
        check(glIsEnabled(GL_BLEND) && glIsEnabled(GL_SCISSOR_TEST), "enable state leaked");
        check(!mask[1] && !mask[3] && !depth, "write masks leaked");
        unsigned char pixel[4];
        glReadPixels(180, 90, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        check(std::abs(int(pixel[0]) - 128) <= 1, "app pixels were tone mapped or gamma decoded twice");
        check(glGetError() == GL_NO_ERROR, "GL error");
        renderer.destroy();
        glDeleteTextures(1, &app);
        glDeleteTextures(1, &texture);
        glDeleteFramebuffers(1, &fbo);
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(display, context);
        eglDestroySurface(display, pbuffer);
        eglTerminate(display);
        std::cout << "PASS: GLES3 MSAA/HDR draw, app color preservation, compositor GL state restoration\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        eglTerminate(display);
        return 1;
    }
}
