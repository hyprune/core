#pragma once
#include "surfaces/Surface.hpp"
#include "world/World.hpp"
#include <GLES3/gl3.h>
namespace hyprune::render {
class Renderer {
  public:
    ~Renderer() = default; // Owner explicitly destroys with its EGL context current.
    void prepare(std::shared_ptr<const world::World>);
    void draw(GLuint target, int width, int height, const sim::Camera &, std::span<const surfaces::Surface>,
              bool typing);
    void destroy();
    world::Json profile;
    bool intelQuality = false, effects = true;
    float pointerX = .5f, pointerY = .5f;
    bool materialDetails = true; // Also permits like-for-like benchmark comparisons.
    uint64_t frames = 0;

  private:
    struct Batch {
        GLuint vao = 0, vbo = 0;
        GLsizei count = 0;
        unsigned material = 0;
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
    void init();
    void targets(int, int);
    void freeTargets();
    void freeWorld();
};
} // namespace hyprune::render
