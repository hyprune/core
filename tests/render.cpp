#include "render/GLState.hpp"
#include "render/Renderer.hpp"
#include "render/SurfaceSampler.hpp"
#include "world/Mounts.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <iostream>
using namespace hyprune;
void check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
void checkSurfaceSampler(GLuint target) {
    render::GLState state;
    GLuint source, vao;
    glGenTextures(1, &source);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source);
    unsigned char pixels[4 * 4 * 4];
    for (int i = 0; i < 16; ++i) {
        pixels[i * 4] = 80;
        pixels[i * 4 + 1] = 120;
        pixels[i * 4 + 2] = 200;
        pixels[i * 4 + 3] = 255;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    // Match an imported client buffer before Hyprland has set its filtering.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    auto shader = [](GLenum type, const char *text) {
        GLuint id = glCreateShader(type);
        glShaderSource(id, 1, &text, nullptr);
        glCompileShader(id);
        GLint ok;
        glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
        check(ok, "surface sampler test shader");
        return id;
    };
    GLuint vertex = shader(GL_VERTEX_SHADER, R"(#version 300 es
void main(){vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));gl_Position=vec4(p[gl_VertexID],0,1);})");
    GLuint fragment = shader(GL_FRAGMENT_SHADER, R"(#version 300 es
precision highp float;uniform sampler2D image;out vec4 color;void main(){color=texture(image,vec2(.5));})");
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    check(linked, "surface sampler test program");
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glBindFramebuffer(GL_FRAMEBUFFER, target);
    glViewport(0, 0, 320, 180);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "image"), 0);
    glBindSampler(0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    unsigned char pixel[4];
    glReadPixels(160, 90, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    check(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0, "missing-mipmap negative control");
    GLuint sampler = render::createSurfaceSampler(), foreign;
    glGenSamplers(1, &foreign);
    glBindSampler(0, foreign);
    glActiveTexture(GL_TEXTURE5);
    {
        render::GLState restored;
        glActiveTexture(GL_TEXTURE0);
        glBindSampler(0, sampler);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glReadPixels(160, 90, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        check(pixel[0] == 80 && pixel[1] == 120 && pixel[2] == 200, "level-zero client buffer sampled black");
    }
    GLint active, bound, filter;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_SAMPLER_BINDING, &bound);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &filter);
    check(active == GL_TEXTURE5 && bound == GLint(foreign), "workspace sampler state leaked");
    check(filter == GL_NEAREST_MIPMAP_LINEAR, "compositor-owned texture filtering changed");
    check(glGetError() == GL_NO_ERROR, "surface sampler GL error");
    glDeleteSamplers(1, &foreign);
    glDeleteSamplers(1, &sampler);
    glDeleteTextures(1, &source);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
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
        checkSurfaceSampler(fbo);
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
        GLuint savedCube, savedSampler;
        glGenTextures(1, &savedCube);
        glGenSamplers(1, &savedSampler);
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_CUBE_MAP, savedCube);
        glBindSampler(6, savedSampler);
        glCullFace(GL_FRONT);
        glFrontFace(GL_CW);
        glActiveTexture(GL_TEXTURE5);
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
        check(active == GL_TEXTURE5 && bound == int(app), "texture state leaked");
        check(glIsEnabled(GL_BLEND) && glIsEnabled(GL_SCISSOR_TEST), "enable state leaked");
        check(!mask[1] && !mask[3] && !depth, "write masks leaked");
        GLint cull, frontFaceState, cube, sampler;
        glGetIntegerv(GL_CULL_FACE_MODE, &cull);
        glGetIntegerv(GL_FRONT_FACE, &frontFaceState);
        glActiveTexture(GL_TEXTURE6);
        glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &cube);
        glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
        check(cull == GL_FRONT && frontFaceState == GL_CW && cube == int(savedCube) && sampler == int(savedSampler),
              "IBL/culling state leaked");
        glActiveTexture(GL_TEXTURE5);
        unsigned char pixel[4];
        glReadPixels(180, 90, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        check(std::abs(int(pixel[0]) - 128) <= 1, "app pixels were tone mapped or gamma decoded twice");
        check(glGetError() == GL_NO_ERROR, "GL error");
        // Compare transformed UV1 against explicitly transformed UV0 for every material slot.
        auto materialWorld = std::make_shared<world::World>();
        world::Image pattern{2, 2, {255, 20, 10, 255, 30, 220, 40, 255, 20, 40, 240, 255, 220, 210, 30, 255}};
        materialWorld->images.push_back(pattern);
        materialWorld->samplers.push_back({10497, 10497, 9728, 9728});
        world::Mesh mesh;
        float coords[6][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
        for (auto &xy : coords) {
            world::Vertex v;
            v.position = {-2 + xy[0] * 4, .2f + xy[1] * 3, -2};
            v.normal = {0, 0, 1};
            v.uv[0] = xy[0];
            v.uv[1] = xy[1];
            v.uv1[0] = xy[0] * 2;
            v.uv1[1] = xy[1] * 2;
            mesh.vertices.push_back(v);
        }
        materialWorld->meshes.push_back(mesh);
        auto pixels = [&] {
            renderer.prepare(materialWorld);
            renderer.draw(fbo, 320, 180, camera, {}, false);
            std::vector<unsigned char> data(320 * 180 * 4);
            glReadPixels(0, 0, 320, 180, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
            return data;
        };
        for (int slot = 0; slot < 5; slot++) {
            materialWorld->meshes[0] = mesh;
            world::Material m;
            m.metallic = .4f;
            m.roughness = .6f;
            m.emissive = {.2f, .2f, .2f};
            auto &t = m.slots[slot];
            t.texture = 0;
            t.texCoord = 1;
            t.offset[0] = .17f;
            t.offset[1] = .23f;
            t.scale[0] = 1.7f;
            t.scale[1] = .6f;
            t.rotation = .4f;
            materialWorld->materials = {m};
            auto transformed = pixels();
            for (auto &v : materialWorld->meshes[0].vertices) {
                float x = v.uv1[0] * t.scale[0], y = v.uv1[1] * t.scale[1];
                v.uv[0] = std::cos(t.rotation) * x - std::sin(t.rotation) * y + t.offset[0];
                v.uv[1] = std::sin(t.rotation) * x + std::cos(t.rotation) * y + t.offset[1];
            }
            materialWorld->materials[0].slots[slot] = world::TextureSlot{0};
            auto baked = pixels();
            size_t different = 0;
            for (size_t i = 0; i < baked.size(); i++)
                different += std::abs(int(baked[i]) - int(transformed[i])) > 2;
            check(different < 500, "KHR_texture_transform disagrees with baked UVs");
        }
        materialWorld->meshes[0] = mesh;
        world::Material m;
        m.unlit = true;
        m.slots[0].texture = 0;
        m.slots[0].texCoord = 1;
        materialWorld->materials = {m};
        auto repeat = pixels();
        materialWorld->samplers[0].wrapS = 33648;
        auto mirror = pixels();
        materialWorld->samplers[0].wrapS = 33071;
        auto clamp = pixels();
        check(repeat != mirror && mirror != clamp && repeat != clamp, "sampler wrap modes not honored");
        materialWorld->images.push_back({1, 1, {0, 0, 0, 255}});
        materialWorld->samplers.push_back({});
        auto &dark = materialWorld->materials[0];
        dark.unlit = false;
        dark.lightmap = 1;
        dark.slots[1].texture = 0;
        dark.metallic = .8f;
        dark.roughness = .15f;
        renderer.materialDetails = false;
        auto diffuse = pixels();
        renderer.materialDetails = true;
        auto shaded = pixels();
        check(diffuse == shaded, "specular illuminated black lightmap");
        dark.lightmap = -1;
        dark.unlit = true;
        for (int minFilter : {9728, 9729, 9984, 9985, 9986, 9987}) {
            materialWorld->samplers[0].minFilter = minFilter;
            for (int magFilter : {9728, 9729}) {
                materialWorld->samplers[0].magFilter = magFilter;
                auto filtered = pixels();
                check(filtered[4 * (90 * 320 + 180)] + filtered[4 * (90 * 320 + 180) + 1] +
                              filtered[4 * (90 * 320 + 180) + 2] >
                          150,
                      "incomplete texture/filter combination");
                check(glGetError() == GL_NO_ERROR, "glTF filter combination failed");
            }
        }
        // M2 reference fixtures use numerical comparisons, independent of art grading.
        materialWorld->atmosphere = world::defaultAtmosphere();
        materialWorld->atmosphere["fog"]["density"] = 0;
        materialWorld->atmosphere["bloom"]["strength"] = 0;
        materialWorld->images.clear();
        materialWorld->samplers.clear();
        world::Material background;
        background.unlit = true;
        background.color = {.1f, .2f, .3f};
        world::Material glass;
        glass.unlit = true;
        glass.color = {1, 0, 0};
        glass.alpha = .35f;
        glass.alphaMode = 2;
        world::Material glass2 = glass;
        glass2.color = {0, 1, 0};
        materialWorld->materials = {background, glass, glass2};
        auto front = mesh, back = mesh, far = mesh;
        front.material = 1;
        back.material = 0;
        far.material = 2;
        for (auto &v : front.vertices)
            v.position.z = -1;
        for (auto &v : back.vertices)
            v.position.z = -4;
        for (auto &v : far.vertices)
            v.position.z = -2;
        materialWorld->meshes = {front, back, far};
        auto blendA = pixels();
        materialWorld->meshes = {far, front, back};
        auto blendB = pixels();
        check(blendA == blendB, "transparent primitives not depth sorted");
        materialWorld->materials[1].alphaMode = 1;
        materialWorld->materials[1].alphaCutoff = .5f;
        auto masked = pixels();
        materialWorld->meshes = {far, back};
        auto without = pixels();
        check(masked == without, "MASK factor cutoff did not discard");
        materialWorld->meshes = {mesh};
        materialWorld->materials = {background};
        materialWorld->atmosphere["fog"]["density"] = .2;
        materialWorld->atmosphere["fog"]["excludeUnlit"] = false;
        auto fogA = pixels();
        camera.position.x += 100;
        for (auto &v : materialWorld->meshes[0].vertices)
            v.position.x += 100;
        auto fogB = pixels();
        check(fogA == fogB, "fog changes with world origin instead of view distance");
        materialWorld->atmosphere["fog"]["excludeUnlit"] = true;
        auto skyExcluded = pixels();
        materialWorld->atmosphere["fog"]["density"] = 0;
        auto noFog = pixels();
        check(skyExcluded == noFog && noFog != fogB, "unlit fog exclusion");
        camera.position.x -= 100;
        materialWorld->meshes = {mesh};
        materialWorld->materials[0].unlit = false;
        materialWorld->materials[0].metallic = 1;
        materialWorld->materials[0].roughness = .12;
        auto reflected = pixels();
        renderer.effects = false;
        auto analytic = pixels();
        renderer.effects = true;
        check(reflected != analytic, "prefiltered environment has no visible reflection");
        materialWorld->materials[0].unlit = true;
        materialWorld->materials[0].emissive = {8, 2, .5f};
        // Small HDR practical with dark surroundings exposes spatial glow beyond its rectangle.
        for (auto &v : materialWorld->meshes[0].vertices) {
            v.position.x *= .12f;
            v.position.y = 1.7f + (v.position.y - 1.7f) * .12f;
        }
        materialWorld->atmosphere["bloom"]["strength"] = .7;
        auto glowing = pixels();
        materialWorld->atmosphere["bloom"]["strength"] = 0;
        auto sharp = pixels();
        check(glowing != sharp, "HDR bloom has no spatial kernel");
        materialWorld->atmosphere["bloom"]["strength"] = .7;
        renderer.profile = materialWorld->atmosphere;
        renderer.draw(fbo, 320, 180, camera, std::span(&surface, 1), false);
        glReadPixels(180, 90, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        check(std::abs(int(pixel[0]) - 128) <= 1, "world bloom/exposure changed app color");
        check(glGetError() == GL_NO_ERROR, "material GL error");
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
