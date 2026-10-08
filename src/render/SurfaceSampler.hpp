#pragma once
#include <GLES3/gl3.h>
namespace hyprune::render {
// Client buffers provide level zero only. Override their native texture
// filtering without changing texture state owned by the compositor.
inline GLuint createSurfaceSampler() {
    GLuint sampler;
    glGenSamplers(1, &sampler);
    glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return sampler;
}
} // namespace hyprune::render
