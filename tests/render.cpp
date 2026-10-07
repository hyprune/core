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
        // Compare transformed UV1 against explicitly transformed UV0 for every material slot.
        auto materialWorld = std::make_shared<world::World>();
        world::Image pattern{2, 2, {255,20,10,255, 30,220,40,255, 20,40,240,255, 220,210,30,255}};
        materialWorld->images.push_back(pattern);
        materialWorld->samplers.push_back({10497,10497,9728,9728});
        world::Mesh mesh;
        float coords[6][2]={{0,0},{1,0},{1,1},{0,0},{1,1},{0,1}};
        for (auto &xy : coords) {
            world::Vertex v;
            v.position={-2+xy[0]*4,.2f+xy[1]*3,-2};v.normal={0,0,1};
            v.uv[0]=xy[0];v.uv[1]=xy[1];v.uv1[0]=xy[0]*2;v.uv1[1]=xy[1]*2;
            mesh.vertices.push_back(v);
        }
        materialWorld->meshes.push_back(mesh);
        auto pixels = [&] {
            renderer.prepare(materialWorld);
            renderer.draw(fbo,320,180,camera,{},false);
            std::vector<unsigned char> data(320*180*4);
            glReadPixels(0,0,320,180,GL_RGBA,GL_UNSIGNED_BYTE,data.data());
            return data;
        };
        for(int slot=0;slot<5;slot++) {
            materialWorld->meshes[0]=mesh;
            world::Material m;m.metallic=.4f;m.roughness=.6f;m.emissive={.2f,.2f,.2f};
            auto &t=m.slots[slot];t.texture=0;t.texCoord=1;t.offset[0]=.17f;t.offset[1]=.23f;
            t.scale[0]=1.7f;t.scale[1]=.6f;t.rotation=.4f;
            materialWorld->materials={m};auto transformed=pixels();
            for(auto &v:materialWorld->meshes[0].vertices) {
                float x=v.uv1[0]*t.scale[0],y=v.uv1[1]*t.scale[1];
                v.uv[0]=std::cos(t.rotation)*x-std::sin(t.rotation)*y+t.offset[0];
                v.uv[1]=std::sin(t.rotation)*x+std::cos(t.rotation)*y+t.offset[1];
            }
            materialWorld->materials[0].slots[slot]=world::TextureSlot{0};auto baked=pixels();
            size_t different=0;
            for(size_t i=0;i<baked.size();i++)different+=std::abs(int(baked[i])-int(transformed[i]))>2;
            check(different<500,"KHR_texture_transform disagrees with baked UVs");
        }
        materialWorld->meshes[0]=mesh;
        world::Material m;m.unlit=true;m.slots[0].texture=0;m.slots[0].texCoord=1;
        materialWorld->materials={m};auto repeat=pixels();
        materialWorld->samplers[0].wrapS=33648;auto mirror=pixels();
        materialWorld->samplers[0].wrapS=33071;auto clamp=pixels();
        check(repeat!=mirror && mirror!=clamp && repeat!=clamp,"sampler wrap modes not honored");
        check(glGetError()==GL_NO_ERROR,"material GL error");
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
