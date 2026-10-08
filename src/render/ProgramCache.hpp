#pragma once
#include "ShaderBuild.hpp"
#include <GLES3/gl3.h>
#include <openssl/sha.h>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <sstream>
#include <iomanip>
namespace hyprune::render {
// Called only during deferred preparation, never from a visible render pass.
struct ProgramCache {
    static inline thread_local unsigned hits = 0, misses = 0, rejected = 0;
    static inline thread_local double compileMs = 0;
    static std::string digest(const std::string &s) {
        unsigned char bytes[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char *>(s.data()), s.size(), bytes);
        std::ostringstream out;
        for (auto b : bytes) out << std::hex << std::setw(2) << std::setfill('0') << unsigned(b);
        return out.str();
    }
    static std::filesystem::path path(const char *vertex, const char *fragment) {
        const char *base = getenv("XDG_CACHE_HOME"), *home = getenv("HOME");
        if ((!base || !*base) && (!home || !*home)) return {};
        std::string key = "hyprune-program-v1/" HYPRUNE_SHADER_BUILD;
        for (auto name : {GL_VENDOR, GL_RENDERER, GL_VERSION, GL_SHADING_LANGUAGE_VERSION}) {
            auto p = glGetString(name);
            if (!p) return {};
            key += '\n'; key += reinterpret_cast<const char *>(p);
        }
        key += '\n'; key += vertex; key += '\n'; key += fragment;
        return (base && *base ? std::filesystem::path(base) : std::filesystem::path(home)/".cache") /
               "hyprune/programs" / (digest(key) + ".bin");
    }
    static GLuint load(const std::filesystem::path &path) {
        GLint formats = 0; glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formats);
        if (path.empty() || formats <= 0) { ++misses; return 0; }
        int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) { ++misses; return 0; }
        struct Header { uint32_t magic, format, size; unsigned char digest[SHA256_DIGEST_LENGTH]; } h{};
        bool valid = read(fd, &h, sizeof h) == sizeof h && h.magic == 0x48595031 && h.size > 0 && h.size <= 16*1024*1024;
        std::string bytes(valid ? h.size : 0, '\0');
        size_t n = 0;
        while (valid && n < bytes.size()) { auto r = read(fd, bytes.data()+n, bytes.size()-n); if (r <= 0) valid = false; else n += r; }
        char extra; valid &= read(fd, &extra, 1) == 0; close(fd);
        unsigned char hash[SHA256_DIGEST_LENGTH]; SHA256(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size(), hash);
        valid &= !memcmp(hash, h.digest, sizeof hash);
        GLuint p = 0;
        if (valid) {
            p = glCreateProgram(); glProgramBinary(p, h.format, bytes.data(), bytes.size());
            GLint linked = 0; glGetProgramiv(p, GL_LINK_STATUS, &linked);
            GLenum error = glGetError(); valid = linked && error == GL_NO_ERROR;
        }
        if (valid) { ++hits; return p; }
        if (p) glDeleteProgram(p);
        ++rejected; ++misses; return 0;
    }
    static void save(GLuint p, const std::filesystem::path &path) {
        if (path.empty()) return;
        GLint size = 0; glGetProgramiv(p, GL_PROGRAM_BINARY_LENGTH, &size);
        if (size <= 0 || size > 16*1024*1024) return;
        std::string bytes(size, '\0'); GLenum format = 0; GLsizei written = 0;
        glGetProgramBinary(p, size, &written, &format, bytes.data());
        if (written <= 0 || glGetError() != GL_NO_ERROR) return;
        bytes.resize(written);
        struct Header { uint32_t magic, format, size; unsigned char digest[SHA256_DIGEST_LENGTH]; } h{0x48595031,format,uint32_t(written),{}};
        SHA256(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size(), h.digest);
        std::error_code ec; std::filesystem::create_directories(path.parent_path(), ec); if (ec) return;
        auto temp = path.string()+".XXXXXX";
        int fd = mkostemp(temp.data(), O_CLOEXEC); if (fd < 0) return;
        auto writeAll = [&](const void *data, size_t n) {
            auto b = static_cast<const char *>(data);
            while (n) { auto count = write(fd,b,n); if (count <= 0) return false; b += count; n -= count; } return true;
        };
        bool ok = writeAll(&h,sizeof h) && writeAll(bytes.data(),bytes.size()); close(fd);
        if (ok) std::filesystem::rename(temp,path,ec);
        std::filesystem::remove(temp,ec);
    }
};
}
