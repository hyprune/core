#include "render/GLState.hpp"
#include "render/PNG.hpp"
#include "render/Renderer.hpp"
#include "render/SurfaceSampler.hpp"
#include "world/Mounts.hpp"
#include "world/Visibility.hpp"
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
        auto loaded = world::load(argv[1]);
        renderer.prepare(loaded);
        renderer.beginPrepare(loaded);
        check(!renderer.continuePrepare(.1, 256 * 1024), "world upload did not yield");
        renderer.cancelPrepare();
        renderer.cancelPrepare();
        renderer.beginPrepare(loaded);
        int uploadSteps = 0;
        while (!renderer.continuePrepare(.5, 256 * 1024)) {
            ++uploadSteps;
            check(uploadSteps < 100000, "world upload failed to complete");
        }
        check(uploadSteps > 1, "world upload was not incremental");
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
        glFinish();
        std::array<unsigned char, 4> behind{};
        renderer.draw(fbo, 320, 180, camera, {}, false);
        glFinish();
        glReadPixels(180, 90, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, behind.data());
        unsigned char transparent[] = {0, 0, 0, 0};
        glBindTexture(GL_TEXTURE_2D, app);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, transparent);
        renderer.draw(fbo, 320, 180, camera, std::span(&surface, 1), false);
        glFinish();
        glReadPixels(180, 90, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        check(std::abs(int(pixel[0]) - behind[0]) <= 1, "transparent client texel discarded world background");
        unsigned char halfAlpha[] = {64, 64, 64, 128};
        glBindTexture(GL_TEXTURE_2D, app);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, halfAlpha);
        renderer.draw(fbo, 320, 180, camera, std::span(&surface, 1), false);
        glFinish();
        glReadPixels(180, 90, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        check(std::abs(int(pixel[0]) - (64 + behind[0] * 127. / 255)) <= 2,
              "client premultiplied alpha composed incorrectly");
        glBindTexture(GL_TEXTURE_2D, app);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, gray);
        renderer.renderScale = .5f;
        renderer.draw(fbo, 320, 180, camera, std::span(&surface, 1), false);
        glFinish();
        glReadPixels(180, 90, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        check(std::abs(int(pixel[0]) - 128) <= 1, "scaled world changed native application colour");
        check(renderer.timings()["nativeSize"] == world::Json::array({320, 180}), "native window target size");
        // A high-frequency client texture must be identical at both world
        // scales. This detects accidentally upscaling the application pass too.
        std::vector<unsigned char> stripes(256 * 4);
        for (int x = 0; x < 256; ++x) {
            stripes[x * 4] = stripes[x * 4 + 1] = stripes[x * 4 + 2] = x % 2 ? 240 : 20;
            stripes[x * 4 + 3] = 255;
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, app);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, stripes.data());
        std::array<unsigned char, 32 * 4> nativeStrip{}, scaledStrip{};
        renderer.draw(fbo, 320, 180, camera, std::span(&surface, 1), false);
        glFinish();
        glReadPixels(125, 100, 32, 1, GL_RGBA, GL_UNSIGNED_BYTE, scaledStrip.data());
        renderer.renderScale = 1;
        renderer.draw(fbo, 320, 180, camera, std::span(&surface, 1), false);
        glFinish();
        glReadPixels(125, 100, 32, 1, GL_RGBA, GL_UNSIGNED_BYTE, nativeStrip.data());
        check(nativeStrip == scaledStrip, "world scale blurred native client stripes");
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, app);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, gray);
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
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, app);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, gray);
        auto foregroundClient = surface;
        foregroundClient.center.z = 0;
        renderer.draw(fbo, 320, 180, camera, std::span(&foregroundClient, 1), false);
        glFinish();
        glReadPixels(180, 90, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        check(std::abs(int(pixel[0]) - 128) <= 1, "world glass behind client covered it");
        foregroundClient.center.z = -1.5f;
        renderer.draw(fbo, 320, 180, camera, std::span(&foregroundClient, 1), false);
        glFinish();
        glReadPixels(180, 90, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        check(std::abs(int(pixel[0]) - 128) > 3 || std::abs(int(pixel[1]) - 128) > 3,
              "world glass in front of client did not blend over it");
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
        glFinish(); // Test-only synchronization; production fences never wait.
        renderer.waypoint = {{"position", {0, 1.7, -4}}, {"label", "ACTIVE WAYPOINT"}};
        renderer.route = {{{"position", {0, 1.7, -4}}}};
        renderer.draw(fbo, 320, 180, camera, {}, false);
        check(renderer.overlayDiagnostics["routeSegments"] == 1, "active route absent from core frame");
        check(renderer.overlayDiagnostics["labels"].size() == 2, "default overlays should only show active waypoint");
        glFinish();
        camera.yaw = .2f;
        renderer.draw(fbo, 320, 180, camera, {}, false);
        check(renderer.overlayDiagnostics["heading"] == camera.yaw, "overlay camera lags renderer");
        camera.yaw = 0;
        // Each checked draw must be fresh: two pending fences re-present the last frame.
        glFinish();
        renderer.toolPrompt = {{"position", {0, 1.7, -3}}, {"text", "W/S CLIMB"}};
        renderer.draw(fbo, 320, 180, camera, {}, false);
        check(renderer.overlayDiagnostics["labels"].size() == 3, "climbing prompt missing in world control");
        auto promptSurface = surface;
        promptSurface.height = .8f;
        glFinish();
        renderer.draw(fbo, 320, 180, camera, std::span(&promptSurface, 1), false);
        check(renderer.overlayDiagnostics["labels"].size() == 4, "aimed-window hint missing in world control");
        renderer.showInteractionHints = false;
        glFinish();
        renderer.draw(fbo, 320, 180, camera, std::span(&promptSurface, 1), false);
        check(renderer.overlayDiagnostics["labels"].size() == 2, "free cursor leaked interaction hints");
        check(renderer.overlayDiagnostics["routeSegments"] == 1, "free cursor removed waypoint route");
        renderer.showInteractionHints = true;
        renderer.toolPrompt = nullptr;
        renderer.waypoint = nullptr;
        renderer.route = world::Json::array();
        auto atlasPath = std::filesystem::temp_directory_path() / "hyprune-test-icon-atlas.png";
        std::vector<uint8_t> atlasBytes(32 * 32 * 4, 255);
        render::writePNG(atlasPath, 32, 32, atlasBytes);
        auto atlas = render::readAtlasPNG(atlasPath);
        check(atlas.width == 32 && atlas.rgba == atlasBytes, "bounded PNG atlas roundtrip");
        renderer.applyOverlayStyle(
            {{"waypointColor", {1, 0, 0}},
             {"iconAtlas",
              {{"path", atlasPath.string()}, {"cellSize", 32}, {"waypointIndex", 0}, {"promptIndex", 0}}}});
        glFinish();
        renderer.waypoint = {{"position", {0, 1.7, -4}}, {"label", "ICON"}};
        renderer.draw(fbo, 320, 180, camera, {}, false);
        glReadPixels(160, 90, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        check(pixel[0] > 240 && pixel[1] < 10, "themed core icon missing");
        renderer.applyOverlayStyle({{"iconAtlas", nullptr}});
        std::filesystem::remove(atlasPath);
        renderer.waypoint = nullptr;
        glFinish();
        renderer.pointerX = .3f;
        renderer.pointerY = .25f;
        renderer.draw(fbo, 320, 180, camera, {}, true);
        glReadPixels(96, 45, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        check(pixel[1] > pixel[0] && pixel[1] > pixel[2], "reticle native screen Y disagrees with pointer ray");
        glFinish();
        auto screenshot = renderer.snapshot();
        check(screenshot.width == 320 && screenshot.height == 180 && screenshot.rgba.size() == 320 * 180 * 4,
              "completed-frame snapshot dimensions");
        check(screenshot.rgba[(45 * 320 + 96) * 4 + 1] > screenshot.rgba[(45 * 320 + 96) * 4],
              "PNG row zero must remain logical top");
        glPixelStorei(GL_PACK_ROW_LENGTH, 400);
        glPixelStorei(GL_PACK_SKIP_ROWS, 2);
        glPixelStorei(GL_PACK_ALIGNMENT, 8);
        glFinish();
        auto guarded = renderer.snapshot();
        GLint packRow, packSkip, packAlignment;
        glGetIntegerv(GL_PACK_ROW_LENGTH, &packRow);
        glGetIntegerv(GL_PACK_SKIP_ROWS, &packSkip);
        glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment);
        check(packRow == 400 && packSkip == 2 && packAlignment == 8, "snapshot pixel pack state leaked");
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glPixelStorei(GL_PACK_SKIP_ROWS, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glFinish();
        renderer.debug = true;
        renderer.draw(fbo, 320, 180, camera, {}, false);
        glFinish();
        auto debugFrame = renderer.snapshot();
        check(debugFrame.rgba != screenshot.rgba, "core debug overlay absent");
        auto png = std::filesystem::temp_directory_path() / "hyprune-render-snapshot.png";
        render::writePNG(png, guarded.width, guarded.height, guarded.rgba);
        check(std::filesystem::file_size(png) > 100, "PNG encode failed");
        std::filesystem::remove(png);
        check(glGetError() == GL_NO_ERROR, "snapshot/debug GL error");
        GLuint landmarkTexture;
        glGenTextures(1, &landmarkTexture);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, landmarkTexture);
        const uint8_t magenta[4] = {255, 0, 200, 255};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, magenta);
        auto landmark = surface;
        landmark.center = camera.position + Vec3{0, 0, -1};
        landmark.width = landmark.height = .15f;
        landmark.texture = landmarkTexture;
        renderer.debug = false;
        renderer.applyOverlayStyle({{"iconAtlas", nullptr}});
        float centres[2];
        int direction = 0;
        for (float pitch : {-.2f, .2f}) {
            glFinish();
            camera.pitch = pitch;
            renderer.draw(fbo, 320, 180, camera, std::span(&landmark, 1), false);
            glFinish();
            auto image = renderer.snapshot();
            float sum = 0, count = 0;
            for (int y = 0; y < 180; ++y)
                for (int x = 0; x < 320; ++x) {
                    auto i = (y * 320 + x) * 4;
                    if (image.rgba[i] > 240 && image.rgba[i + 1] < 5 && image.rgba[i + 2] > 180) {
                        sum += y;
                        ++count;
                    }
                }
            check(count > 0, "native pitch test landmark absent");
            centres[direction++] = sum / count;
        }
        check(centres[0] < 90 && centres[1] > 90, "look-up render sign is inverted in native target coordinates");
        camera.pitch = 0;
        glDeleteTextures(1, &landmarkTexture);
        check(render::Renderer::intelTier("auto", "Mesa Intel(R) Graphics"), "auto Intel tier not selected");
        check(!render::Renderer::intelTier("high", "Intel") && !render::Renderer::intelTier("auto", "AMD"),
              "explicit/device quality mismatch");
        glFinish();
        auto lowSettings = render::graphicsDefaults();
        lowSettings["quality"] = "low";
        renderer.applyGraphics(lowSettings);
        renderer.draw(fbo, 320, 180, camera, {}, false);
        glFinish();
        check(renderer.timings()["msaaSamples"] <= 2, "live MSAA tier did not reallocate targets");
        // Dynamic world resolution must leave application pixels and capture
        // dimensions native, with depth and all compositor GL state intact.
        renderer.renderScale = .5f;
        renderer.draw(fbo, 320, 180, camera, {}, false);
        glFinish();
        check(renderer.timings()["internalSize"] == world::Json::array({160, 90}), "fixed internal resolution");
        auto half = renderer.snapshot();
        check(half.width == 320 && half.height == 180, "capture was downscaled");
        check(glGetError() == GL_NO_ERROR, "scaled depth resolve/upscale GL error");
        render::Resolution scale;
        scale.reset(0, 60, true, 3840 * 2160);
        auto initial = scale.scale;
        for (int i = 0; i < 20; ++i)
            scale.sample(30, .016);
        check(scale.scale == initial, "resolution hysteresis ignored");
        for (int i = 0; i < 30; ++i)
            scale.sample(30, .016);
        check(scale.scale < initial, "resolution did not reduce above GPU budget");
        for (int i = 0; i < 1000; ++i)
            scale.sample(100, .016);
        check(scale.scale == .375f, "resolution lower bound");
        scale.reset(0, 60, true, 3840 * 2160);
        for (int i = 0; i < 50; ++i)
            scale.sample(1, .1);
        check(scale.scale == .5f, "resolution increased before stable headroom");
        for (int i = 0; i < 15; ++i)
            scale.sample(1, .1);
        check(scale.scale > .5f, "resolution never recovers under sustained headroom");
        scale.reset(.75f, 60, true, 3840 * 2160);
        for (int i = 0; i < 1000; ++i)
            scale.sample(100, .016);
        check(scale.scale == .75f, "fixed render scale drifted");
        // The intermediate upscale must not perform the final output Y flip.
        // A world panel above the eye and a native surface behind/in front of it
        // pin both colour orientation and reconstructed-depth orientation.
        auto orientationWorld = std::make_shared<world::World>();
        world::Material magentaMaterial;
        magentaMaterial.unlit = true;
        magentaMaterial.color = {1, 0, 1};
        magentaMaterial.doubleSided = true;
        orientationWorld->materials = {magentaMaterial};
        world::Mesh panel;
        for (auto xy : {Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{1, 1, 0}, Vec3{0, 0, 0}, Vec3{1, 1, 0}, Vec3{0, 1, 0}}) {
            world::Vertex v{};
            v.position = {-.8f + xy.x * 1.6f, 2.3f + xy.y, 0};
            v.normal = {0, 0, 1};
            panel.vertices.push_back(v);
        }
        orientationWorld->meshes = {panel};
        renderer.prepare(orientationWorld);
        renderer.effects = false;
        camera = sim::Camera{};
        surface.center = {0, 2.8f, -.2f};
        surface.width = 1.4f;
        surface.height = .8f;
        for (auto [upscaler, scale] : {std::pair{"bilinear", 1.f}, {"bilinear", .5f}, {"fsr1", .5f}}) {
            renderer.upscaler = upscaler;
            renderer.renderScale = scale;
            glFinish();
            renderer.draw(fbo, 320, 180, camera, std::span(&surface, 1), false);
            glFinish();
            if (std::string_view(upscaler) == "fsr1")
                check(renderer.timings()["upscaler"]["active"] == "fsr1",
                      ("FSR 1 inactive: " + renderer.timings()["upscaler"]["error"].dump()).c_str());
            glReadPixels(150, 48, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            if (!(pixel[0] > 150 && pixel[1] < 20 && pixel[2] > 150)) std::cerr << upscaler << " pixel " << int(pixel[0]) << "," << int(pixel[1]) << "," << int(pixel[2]) << "\n";
            check(pixel[0] > 150 && pixel[1] < 20 && pixel[2] > 150, "scaled world colour/depth Y flip or occlusion");
            surface.center.z = .2f;
            renderer.draw(fbo, 320, 180, camera, std::span(&surface, 1), false);
            glFinish();
            glReadPixels(150, 48, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            check(std::abs(int(pixel[0]) - 128) <= 1, "native surface incorrectly hidden by scaled world depth");
            surface.center.z = -.2f;
            check(glGetError() == GL_NO_ERROR, "upscaler GL error");
        }
        renderer.upscaler = "bilinear";
        // A fully prepared worker frame replaces opaque colour only. Local
        // reconstruction must still occlude clients, and preserve native colour.
        GLuint remoteTex, remoteFbo;
        glGenTextures(1,&remoteTex);glBindTexture(GL_TEXTURE_2D,remoteTex);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,320,180,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glGenFramebuffers(1,&remoteFbo);glBindFramebuffer(GL_FRAMEBUFFER,remoteFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,remoteTex,0);
        renderer.renderScale=1;renderer.worldOnly=true;
        renderer.draw(remoteFbo,320,180,camera,{},false);glFinish();
        renderer.worldOnly=false;renderer.externalWorldFbo=remoteFbo;
        glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        surface.center.z=-.2f;
        renderer.draw(fbo,320,180,camera,std::span(&surface,1),false);glFinish();
        glReadPixels(150,48,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        check(pixel[0]>150 && pixel[1]<20 && pixel[2]>150,"offload world depth lost client occlusion");
        surface.center.z=.2f;
        renderer.draw(fbo,320,180,camera,std::span(&surface,1),false);glFinish();
        glReadPixels(150,48,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        check(std::abs(int(pixel[0])-128)<=1,"offload native client colour changed");
        check(glGetError()==GL_NO_ERROR,"offload native reconstruction GL error");
        renderer.externalWorldFbo=0;
        glDeleteFramebuffers(1,&remoteFbo);glDeleteTextures(1,&remoteTex);
        renderer.profile["outline"]={{"enabled",true},{"colour",{0,1,0}},{"widthPx",4},
                                      {"depthThreshold",.02},{"normalThreshold",.2}};
        renderer.profile["toneMapping"]="linear";
        renderer.showInteractionHints=false; renderer.pointerX=renderer.pointerY=-1;
        renderer.graphics["outline"]=true; renderer.graphics["outline_quality"]="smooth";
        camera.position.y=2.8f;
        auto stroke = [&] {
            renderer.draw(fbo,320,180,camera,{},false); glFinish();
            std::vector<unsigned char> row(320*4);
            glReadPixels(0,90,320,1,GL_RGBA,GL_UNSIGNED_BYTE,row.data());
            int count=0;
            for(int x=0;x<320;++x) if(row[x*4]<20 && row[x*4+1]>220 && row[x*4+2]<20) ++count;
            return count;
        };
        for(float scaling:{1.f,.5f}) for(float density:{1.f,2.f}) {
            renderer.renderScale=scaling; renderer.logicalScale=density;
            camera.position.z=4; int near=stroke(); camera.position.z=8; int far=stroke();
            check(near>=int(6*density) && near<=int(10*density),"ink logical width incorrect");
            check(std::abs(near-far)<=2,"ink width changed with distance");
        }
        // Fast mode remains one logical pixel even if the author requests four.
        for(int samples:{1,2,4}) for(float scale:{1.f,.5f}) {
            renderer.msaa=samples;renderer.renderScale=scale;renderer.logicalScale=1;
            renderer.graphics["outline_quality"]="fast";camera.position.z=4;
            int count=stroke();check(count<=4,"fast ink magnified by internal render scale");
            // Use integrated coverage to include an antialiased one-pixel line.
            std::vector<unsigned char> row(320*4);glReadPixels(0,90,320,1,GL_RGBA,GL_UNSIGNED_BYTE,row.data());
            renderer.graphics["outline"]=false;stroke();
            std::vector<unsigned char> background(320*4);glReadPixels(0,90,320,1,GL_RGBA,GL_UNSIGNED_BYTE,background.data());
            renderer.graphics["outline"]=true;
            float coverage=0;for(int x=0;x<320;++x)coverage+=std::max(0.f,float(row[x*4+1]-background[x*4+1])/std::max(1,255-background[x*4+1]));
            if(coverage<1.5f || coverage>2.6f)std::cerr<<"ink coverage="<<coverage<<" samples="<<samples<<" scale="<<scale<<'\n';
            check(coverage>=1.5f && coverage<=2.6f,"fast ink lost subpixel coverage");
        }
        renderer.graphics["outline_quality"]="smooth";
        renderer.graphics["outline"]=false;
        check(stroke()==0,"disabled ink still draws");
        renderer.graphics["outline"]=true; renderer.logicalScale=1; camera.position.z=4;
        surface.center={0,2.8f,.2f};
        renderer.draw(fbo,320,180,camera,std::span(&surface,1),false);glFinish();
        glReadPixels(160,90,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        check(std::abs(int(pixel[0])-128)<=1 && std::abs(int(pixel[1])-128)<=1,"ink altered live app pixels");
        check(glGetError()==GL_NO_ERROR,"ink GL state error");
        // Equal-depth/equal-colour seam: depth-only reconstruction would miss it.
        auto seams=std::make_shared<world::World>(*orientationWorld);
        auto left=panel,right=panel;
        for(auto &v:left.vertices)v.position.x=(v.position.x-.8f)*.5f;
        for(auto &v:right.vertices){v.position.x=(v.position.x+.8f)*.5f;v.normal={1,0,0};}
        seams->meshes={left,right};renderer.prepare(seams);
        renderer.profile["outline"]={{"enabled",true},{"colour",{0,1,0}},{"widthPx",2},{"depthThreshold",.02},{"normalThreshold",.2}};
        renderer.profile["toneMapping"]="linear";renderer.graphics["outline_quality"]="smooth";
        for(int samples:{1,2,4}) {
            renderer.msaa=samples;renderer.renderScale=.5;renderer.logicalScale=1;
            renderer.draw(fbo,320,180,camera,{},false);glFinish();
            glReadPixels(160,90,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
            check(pixel[1]>180,"ink lost coplanar normal discontinuity after resolve");
            glReadPixels(140,90,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
            check(pixel[0]>240 && pixel[1]<5 && pixel[2]>240,"flat normal region or unlit colour changed");
        }
        // Offload worker must include ink in its world-only colour transport.
        renderer.worldOnly=true;renderer.draw(fbo,320,180,camera,{},false);glFinish();renderer.worldOnly=false;
        glReadPixels(160,90,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        check(pixel[1]>180,"offload worker omitted ink");
        renderer.prepare(orientationWorld);
        // Visibility membership and fallback are independent of workspace homes.
        orientationWorld->nodeParents={-1,-1,1};
        auto hidden=panel;hidden.node=2;orientationWorld->meshes.push_back(hidden);
        orientationWorld->manifest["visibility"]={{"areas",world::Json::array({
            {{"id","front"},{"bounds",{{"min",{-2,0,2}},{"max",{2,5,6}}}},{"nodes",{0}},{"visibleFrom",world::Json::array()}},
            {{"id","rear"},{"bounds",{{"min",{-2,0,-6}},{"max",{2,5,-2}}}},{"nodes",{1}},{"visibleFrom",world::Json::array()}}
        })}};
        world::loadVisibility(*orientationWorld);renderer.prepare(orientationWorld);
        camera.position={0,2.8f,4};renderer.draw(fbo,320,180,camera,{},false);glFinish();
        check(renderer.timings()["visibility"]["culled"]==1,"hidden descendant submitted");
        renderer.graphics["visibility_culling"]=false;renderer.draw(fbo,320,180,camera,{},false);glFinish();
        check(renderer.timings()["visibility"]["submitted"]==2,"visibility override ignored");
        renderer.graphics["visibility_culling"]=true;camera.position.z=0;
        renderer.draw(fbo,320,180,camera,{},false);glFinish();
        // The camera sits in the panels' plane here: they are frustum-culled, not region-culled.
        check(renderer.timings()["visibility"]["culled"]==0 &&
              renderer.timings()["visibility"]["submitted"].get<int>()+renderer.timings()["visibility"]["frustumCulled"].get<int>()==2,
              "outside-region fallback hid scene");
        camera.position.z=4;renderer.transforms[2]=Mat4::identity();
        renderer.draw(fbo,320,180,camera,{},false);glFinish();
        check(renderer.timings()["visibility"]["submitted"]==2,"edited object wrongly culled");
        {
            // Frustum: a panel behind the camera is skipped; moving it in front by
            // an edited transform brings it back (transformed bounds).
            auto frustum = std::make_shared<world::World>();
            frustum->materials = orientationWorld->materials;
            auto front = panel, behind = panel;
            behind.node = 1;
            for (auto &v : behind.vertices) v.position.z = 10;
            frustum->meshes = {front, behind};
            renderer.transforms.clear();
            renderer.prepare(frustum);
            camera.position = {0, 2.8f, 4};
            renderer.draw(fbo, 320, 180, camera, {}, false);
            glFinish();
            auto v = renderer.timings()["visibility"];
            check(v["submitted"] == 1 && v["frustumCulled"] == 1, "batch behind camera not frustum-culled");
            Mat4 forward = Mat4::identity();
            forward.v[14] = -10;
            renderer.transforms[1] = forward;
            renderer.draw(fbo, 320, 180, camera, {}, false);
            glFinish();
            check(renderer.timings()["visibility"]["submitted"] == 2, "edited object culled by its untransformed bounds");
            renderer.transforms.clear();
        }
        {
            // One image as base colour and occlusion: one sRGB upload, data slots
            // read raw through a skip-decode sampler (or two copies without it).
            // Must match separate colour/data images exactly.
            auto shared = std::make_shared<world::World>();
            world::Image image{2, 2, std::vector<uint8_t>(16, 100)};
            for (int i = 3; i < 16; i += 4) image.rgba[i] = 255;
            world::Material material;
            material.slots[0].texture = material.slots[3].texture = 0;
            material.baseTexture = 0;
            material.doubleSided = true;
            shared->materials = {material};
            shared->images = {image};
            shared->samplers = {world::Sampler{}};
            world::Mesh quad = panel;
            for (auto &v : quad.vertices) { v.uv[0] = v.position.x > 0; v.uv[1] = v.position.y > 2.8f; }
            shared->meshes = {quad};
            auto separate = std::make_shared<world::World>(*shared);
            separate->images.push_back(image);
            separate->samplers.push_back(world::Sampler{});
            separate->materials[0].slots[3].texture = 1;
            renderer.graphics["ambient_occlusion"] = true;
            renderer.graphics["outline"] = false;
            renderer.renderScale = 1;
            camera.position = {0, 2.8f, 4};
            auto frame = [&](std::shared_ptr<world::World> w, bool decode) {
                renderer.srgbDecodeAllowed = decode;
                renderer.prepare(w);
                renderer.draw(fbo, 320, 180, camera, {}, false);
                glFinish();
                glReadPixels(160, 90, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
                return std::array{int(pixel[0]), int(pixel[1]), int(pixel[2]), int(renderer.timings()["upload"]["gpuBytes"])};
            };
            auto reference = frame(separate, true), skip = frame(shared, true), copies = frame(shared, false);
            for (int c = 0; c < 3; ++c)
                check(std::abs(reference[c] - skip[c]) <= 1 && std::abs(reference[c] - copies[c]) <= 1,
                      "shared colour/data image sampled with the wrong decode");
            check(skip[3] < copies[3] && copies[3] == reference[3], "shared image GPU bytes not single-counted");
            renderer.srgbDecodeAllowed = true;
            // Edited objects: the CPU normal matrix must shade a rotated node like
            // the same mesh rotated in the package (pre-transformed vertices).
            auto rotated = std::make_shared<world::World>(*shared);
            rotated->materials[0] = world::Material{};
            rotated->materials[0].doubleSided = true;
            rotated->images.clear();
            rotated->samplers.clear();
            float angle = .5f, c = std::cos(angle), s = std::sin(angle);
            Mat4 turn = Mat4::identity();
            turn.v[0] = c, turn.v[2] = -s, turn.v[8] = s, turn.v[10] = c;
            auto baked = std::make_shared<world::World>(*rotated);
            for (auto &v : baked->meshes[0].vertices) {
                v.position = turn.point(v.position);
                v.normal = {s, 0, c};
            }
            renderer.prepare(baked);
            renderer.draw(fbo, 320, 180, camera, {}, false);
            glFinish();
            std::array<unsigned char, 4> expected{};
            glReadPixels(160, 90, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, expected.data());
            renderer.prepare(rotated);
            renderer.transforms[rotated->meshes[0].node] = turn;
            renderer.draw(fbo, 320, 180, camera, {}, false);
            glFinish();
            glReadPixels(160, 90, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            renderer.transforms.clear();
            for (int ch = 0; ch < 3; ++ch)
                check(std::abs(int(pixel[ch]) - expected[ch]) <= 2, "edited object normal matrix shading");
            renderer.prepare(orientationWorld);
        }
        {
            // Material constants use indexed uniform buffer binding 0; restore it.
            GLuint buffer;
            glGenBuffers(1, &buffer);
            glBindBuffer(GL_UNIFORM_BUFFER, buffer);
            glBufferData(GL_UNIFORM_BUFFER, 1024, nullptr, GL_STATIC_DRAW);
            glBindBufferRange(GL_UNIFORM_BUFFER, 0, buffer, 256, 512);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
            renderer.draw(fbo, 320, 180, camera, {}, false);
            GLint bound = 0, generic = -1;
            GLint64 start = 0, size = 0;
            glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 0, &bound);
            glGetInteger64i_v(GL_UNIFORM_BUFFER_START, 0, &start);
            glGetInteger64i_v(GL_UNIFORM_BUFFER_SIZE, 0, &size);
            glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &generic);
            check(bound == int(buffer) && start == 256 && size == 512 && generic == 0, "uniform buffer state leaked");
            glBindBufferBase(GL_UNIFORM_BUFFER, 0, 0);
            glDeleteBuffers(1, &buffer);
        }
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
