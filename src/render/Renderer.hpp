#pragma once
#include "GPUTimer.hpp"
#include "interaction/Tools.hpp"
#include "surfaces/Surface.hpp"
#include "world/World.hpp"
#include <GLES3/gl3.h>
#include <chrono>
namespace hyprune::render {
class Renderer {
  public:
    ~Renderer() = default; // Owner explicitly destroys with its EGL context current.
    void prepare(std::shared_ptr<const world::World>);
    void draw(GLuint target, int width, int height, const sim::Camera &, std::span<const surfaces::Surface>,
              bool typing);
    void destroy();
    void drawHUD(GLuint target, int width, int height);
    void collisionChanged(); // Deferred owner tick after editor transforms.
    bool debug = false;
    double updateMs = 0, captureMs = 0, ipcMs = 0;
    struct Pixels {
        int width = 0, height = 0;
        sim::Camera camera;
        std::vector<uint8_t> rgba;
        double readbackMs = 0, ageMs = 0;
    };
    Pixels snapshot(); // Owner event loop after the frame; never a compositor snapshot.
    world::Json timings() const;
    struct Label {
        float x, y;
        std::string text;
        Vec3 color{1, 1, 1};
    };
    std::vector<Label> labels;
    std::string font = "monospace";
    world::Json profile;
    bool intelQuality = false, effects = true;
    float pointerX = .5f, pointerY = .5f;
    bool scopeVignette = false;
    std::vector<interaction::Line> lines;
    std::map<unsigned, Mat4> transforms;
    bool materialDetails = true; // Also permits like-for-like benchmark comparisons.
    uint64_t frames = 0;

  private:
    struct Batch {
        GLuint vao = 0, vbo = 0;
        GLsizei count = 0;
        unsigned material = 0, node = 0;
        Vec3 center;
    };
    std::shared_ptr<const world::World> world_;
    std::vector<Batch> batches_;
    std::vector<GLuint> textures_, colorTextures_, samplers_;
    GLuint scene_ = 0, tone_ = 0, quadVao_ = 0, quadVbo_ = 0, screenVao_ = 0, repeat_ = 0, clamp_ = 0;
    GLuint environment_ = 0, bloom_ = 0, sky_ = 0;
    int cubeLevels_ = 1;
    GLuint bloomFbo_[2]{}, bloomTexture_[2]{};
    GLuint msaa_ = 0, color_ = 0, depth_ = 0, resolve_ = 0, resolveTexture_ = 0;
    int width_ = 0, height_ = 0, samples_ = 4;
    GLuint collisionVao_ = 0, collisionVbo_ = 0;
    GLsizei collisionCount_ = 0;
    std::array<GPUTimer, 4> gpu_;
    std::array<double, 4> cpu_{};
    double frameMs_ = 0, renderMs_ = 0;
    std::chrono::steady_clock::time_point lastFrame_{};
    sim::Camera frameCamera_;
    bool frameTyping_ = false;
    GLuint textProgram_ = 0, textTexture_ = 0, textVao_ = 0, textBuffer_ = 0;
    std::string loadedFont_;
    struct Glyph {
        float width = 0, height = 0, left = 0, top = 0, advance = 0, u = 0, v = 0;
    };
    std::array<Glyph, 128> glyphs_{};
    void textAtlas();
    void debugHUD(int, int);
    void text(float x, float y, const std::string &, Vec3 color, int w, int h);
    void present(GLuint, int, int, bool);
    void debugGeometry(const sim::Camera &, std::span<const surfaces::Surface>);
    void init();
    void targets(int, int);
    void freeTargets();
    void freeWorld();
};
} // namespace hyprune::render
