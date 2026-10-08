#pragma once
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <array>
#include <cstring>
namespace hyprune::render {
// Ring-buffered EXT timer queries. Never wait, glFinish, or nest another query.
class GPUTimer {
    using Read64 = void (*)(GLuint, GLenum, GLuint64 *);
    static constexpr GLenum elapsed = 0x88BF, disjointFlag = 0x8FBB;
    std::array<GLuint, 4> queries{};
    std::array<bool, 4> pending{};
    Read64 read64 = nullptr;
    std::size_t index = 0;
    bool checked = false, active = false;

  public:
    double milliseconds = 0;
    bool available = false;
    void begin() {
        if (!checked) {
            checked = true;
            GLint count = 0;
            glGetIntegerv(GL_NUM_EXTENSIONS, &count);
            for (int i = 0; i < count; ++i)
                if (std::strcmp(reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, i)),
                                "GL_EXT_disjoint_timer_query") == 0)
                    read64 = reinterpret_cast<Read64>(eglGetProcAddress("glGetQueryObjectui64vEXT"));
            if (read64)
                glGenQueries(queries.size(), queries.data());
        }
        if (!read64)
            return;
        GLint enclosing = 0;
        glGetQueryiv(elapsed, GL_CURRENT_QUERY, &enclosing);
        if (enclosing)
            return;
        GLint disjoint = 0;
        glGetIntegerv(disjointFlag, &disjoint);
        if (disjoint) {
            pending.fill(false);
            available = false;
        }
        if (pending[index]) {
            GLuint ready = 0;
            glGetQueryObjectuiv(queries[index], GL_QUERY_RESULT_AVAILABLE, &ready);
            if (!ready)
                return;
            GLuint64 ns = 0;
            read64(queries[index], GL_QUERY_RESULT, &ns);
            milliseconds = ns / 1.e6;
            available = !disjoint;
        }
        glBeginQuery(elapsed, queries[index]);
        active = true;
    }
    void end() {
        if (!active)
            return;
        glEndQuery(elapsed);
        pending[index] = true;
        index = (index + 1) % queries.size();
        active = false;
    }
    void destroy() {
        if (read64)
            glDeleteQueries(queries.size(), queries.data());
        queries.fill(0);
        pending.fill(false);
        read64 = nullptr;
        checked = active = available = false;
    }
};
} // namespace hyprune::render
