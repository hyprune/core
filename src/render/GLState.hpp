#pragma once
#include <GLES3/gl3.h>
#include <array>
namespace hyprune::render {
// Every state changed by Renderer or its uploads is restored, even on exceptions.
class GLState {
    GLint draw, read, program, vao, array, renderbuffer, active, unpack, pbo, pack, packPbo;
    GLint uniformBuffer, materialBuffer; // generic and indexed binding 0 (world materials)
    GLint64 materialStart, materialSize;
    GLint viewport[4], scissor[4], depthFunc, cullMode, frontFace, srcRGB, dstRGB, srcAlpha, dstAlpha, eqRGB, eqAlpha;
    GLfloat clearColor[4], clearDepth, depthRange[2], lineWidth;
    std::array<GLint, 6> pixelRows;
    static constexpr std::array<GLenum, 6> rowEnums{GL_PACK_ROW_LENGTH,   GL_PACK_SKIP_ROWS,   GL_PACK_SKIP_PIXELS,
                                                    GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_PIXELS};
    GLboolean colorMask[4], depthMask;
    struct Unit {
        GLint texture, cube, sampler;
    } units[7];
    static constexpr std::array<GLenum, 8> caps{GL_BLEND,
                                                GL_DEPTH_TEST,
                                                GL_SCISSOR_TEST,
                                                GL_STENCIL_TEST,
                                                GL_CULL_FACE,
                                                GL_RASTERIZER_DISCARD,
                                                GL_SAMPLE_ALPHA_TO_COVERAGE,
                                                GL_SAMPLE_COVERAGE};
    std::array<GLboolean, 8> enabled;

  public:
    GLState() {
        for (std::size_t i = 0; i < rowEnums.size(); ++i)
            glGetIntegerv(rowEnums[i], &pixelRows[i]);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array);
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &pbo);
        glGetIntegerv(GL_PACK_ALIGNMENT, &pack);
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packPbo);
        glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &uniformBuffer);
        glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 0, &materialBuffer);
        glGetInteger64i_v(GL_UNIFORM_BUFFER_START, 0, &materialStart);
        glGetInteger64i_v(GL_UNIFORM_BUFFER_SIZE, 0, &materialSize);
        for (int i = 0; i < 7; i++) {
            glActiveTexture(GL_TEXTURE0 + i);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &units[i].texture);
            glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &units[i].cube);
            glGetIntegerv(GL_SAMPLER_BINDING, &units[i].sampler);
        }
        glActiveTexture(active);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_SCISSOR_BOX, scissor);
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
        glGetIntegerv(GL_CULL_FACE_MODE, &cullMode);
        glGetIntegerv(GL_FRONT_FACE, &frontFace);
        glGetIntegerv(GL_BLEND_SRC_RGB, &srcRGB);
        glGetIntegerv(GL_BLEND_DST_RGB, &dstRGB);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcAlpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &dstAlpha);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &eqRGB);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &eqAlpha);
        glGetFloatv(GL_LINE_WIDTH, &lineWidth);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
        glGetFloatv(GL_DEPTH_CLEAR_VALUE, &clearDepth);
        glGetFloatv(GL_DEPTH_RANGE, depthRange);
        glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        for (std::size_t i = 0; i < caps.size(); i++)
            enabled[i] = glIsEnabled(caps[i]);
    }
    ~GLState() {
        for (std::size_t i = 0; i < rowEnums.size(); ++i)
            glPixelStorei(rowEnums[i], pixelRows[i]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
        glUseProgram(program);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, array);
        glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
        for (int i = 0; i < 7; i++) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, units[i].texture);
            glBindTexture(GL_TEXTURE_CUBE_MAP, units[i].cube);
            glBindSampler(i, units[i].sampler);
        }
        glActiveTexture(active);
        glPixelStorei(GL_UNPACK_ALIGNMENT, unpack);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
        glPixelStorei(GL_PACK_ALIGNMENT, pack);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, packPbo);
        if (materialBuffer && materialSize)
            glBindBufferRange(GL_UNIFORM_BUFFER, 0, materialBuffer, materialStart, materialSize);
        else
            glBindBufferBase(GL_UNIFORM_BUFFER, 0, materialBuffer);
        glBindBuffer(GL_UNIFORM_BUFFER, uniformBuffer);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
        glDepthFunc(depthFunc);
        glCullFace(cullMode);
        glFrontFace(frontFace);
        glDepthMask(depthMask);
        glDepthRangef(depthRange[0], depthRange[1]);
        glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
        glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
        glClearDepthf(clearDepth);
        glLineWidth(lineWidth);
        glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
        glBlendEquationSeparate(eqRGB, eqAlpha);
        for (std::size_t i = 0; i < caps.size(); i++)
            if (enabled[i])
                glEnable(caps[i]);
            else
                glDisable(caps[i]);
    }
};
} // namespace hyprune::render
