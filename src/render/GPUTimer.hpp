#pragma once
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <array>
#include <cstdint>
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
// GPU time between two points of the compositor's frame from timestamp queries
// (glQueryCounterEXT), which never nest with an elapsed query (GPUTimer, or the
// compositor's own): the plugin's own timers inside the span keep working. Ring-buffered;
// results are read frames later, never waited for. Diagnostics only.
class GPUSpan {
    using Counter = void (*)(GLuint, GLenum);
    using Read64 = void (*)(GLuint, GLenum, GLuint64 *);
    static constexpr GLenum timestamp = 0x8E28, disjointFlag = 0x8FBB;
    std::array<GLuint, 8> queries{}; // pairs
    std::array<bool, 4> pending{};
    Counter counter = nullptr;
    Read64 read64 = nullptr;
    std::size_t index = 0;
    bool checked = false, open = false;

  public:
    double milliseconds = -1; // the latest complete span
    double totalMs = 0;       // every complete span, and how many (means over a stretch)
    uint64_t count = 0;
    void begin() {
        if (!checked) {
            checked = true;
            GLint count = 0;
            glGetIntegerv(GL_NUM_EXTENSIONS, &count);
            for (int i = 0; i < count; ++i)
                if (std::strcmp(reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, i)), "GL_EXT_disjoint_timer_query") == 0) {
                    counter = reinterpret_cast<Counter>(eglGetProcAddress("glQueryCounterEXT"));
                    read64 = reinterpret_cast<Read64>(eglGetProcAddress("glGetQueryObjectui64vEXT"));
                }
            if (counter && read64)
                glGenQueries(queries.size(), queries.data());
        }
        if (!counter || !read64 || open)
            return;
        GLint disjoint = 0;
        glGetIntegerv(disjointFlag, &disjoint);
        if (disjoint)
            pending.fill(false);
        if (pending[index]) {
            GLuint ready = 0;
            glGetQueryObjectuiv(queries[2 * index + 1], GL_QUERY_RESULT_AVAILABLE, &ready);
            if (!ready)
                return; // the GPU is several frames behind: skip this frame's span
            GLuint64 a = 0, b = 0;
            read64(queries[2 * index], GL_QUERY_RESULT, &a);
            read64(queries[2 * index + 1], GL_QUERY_RESULT, &b);
            milliseconds = b > a ? (b - a) / 1.e6 : 0;
            totalMs += milliseconds, ++count;
            pending[index] = false;
        }
        counter(queries[2 * index], timestamp);
        open = true;
    }
    void end() {
        if (!open)
            return;
        counter(queries[2 * index + 1], timestamp);
        pending[index] = true;
        index = (index + 1) % pending.size();
        open = false;
    }
    void destroy() {
        if (counter && read64)
            glDeleteQueries(queries.size(), queries.data());
        queries.fill(0);
        pending.fill(false);
        counter = nullptr;
        read64 = nullptr;
        checked = open = false;
        milliseconds = -1;
    }
};
} // namespace hyprune::render
