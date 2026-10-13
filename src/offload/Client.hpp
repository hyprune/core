#pragma once
#include "Common.hpp"
#include "render/GLState.hpp"
#include "sim/Camera.hpp"
#include <drm_fourcc.h>
#include <functional>
#include <poll.h>
namespace hyprune::offload {
class Client {
    struct Slot {
        GLuint tex = 0, fbo = 0;
        bool external = false; // tex is GL_TEXTURE_EXTERNAL_OES, fbo stays 0
        EGLImageKHR image = nullptr;
        int phase = 0, acquire = -1, target = -1;
        GLsync copied = nullptr;
        Json request, reply;
    } slots_[2];
    struct Target {
        GLuint tex = 0, fbo = 0;
        bool busy = false;
    } targets_[3];
    int fd_ = -1, w_ = 0, h_ = 0, front_ = -1;
    uint64_t generation_ = 0, sequence_ = 0, frontSequence_ = 0;
    double started_ = 0, nextSend_ = 0;
    bool ready_ = false;
    Json frontReply_;
    ExternalCopy externalCopy_;

  public:
    std::string error;
    Json capabilities, timings;
    bool connected() const { return fd_ >= 0; }
    // A front older than maxAgeMs is stale (the in-process renderer stands in); recordings in
    // lockstep accept any age, the front being exactly the next frame in sequence.
    bool available(double maxAgeMs = 50) const { return fd_ >= 0 && front_ >= 0 && now() - frontReply_.at("sent").get<double>() <= maxAgeMs; }
    GLuint fbo(double maxAgeMs = 50) const { return available(maxAgeMs) ? targets_[front_].fbo : 0; }
    // Age of the newest complete frame (ms), or -1.
    double frontAgeMs() const { return front_ >= 0 ? now() - frontReply_.at("sent").get<double>() : -1; }
    Json diagnostics() const {
        Json phases = Json::array();
        for (auto &b : slots_)
            phases.push_back(b.phase);
        return {{"front", front_}, {"frontSequence", frontSequence_}, {"sequence", sequence_}, {"phases", phases},
                {"frontAgeMs", front_ >= 0 ? Json(now() - frontReply_.at("sent").get<double>()) : Json(nullptr)}};
    }
    sim::Camera camera() const {
        auto p = frontReply_.at("pose");
        sim::Camera c;
        c.position = {p[0], p[1], p[2]};
        c.yaw = p[3];
        c.pitch = p[4];
        c.fov = p[5];
        if(p.size()>=9) { c.roll=p[6];c.shiftX=p[7];c.shiftY=p[8]; }
        return c;
    }
    void attach(int fd, int w, int h, uint64_t generation) {
        stop();
        fd_ = fd;
        w_ = w;
        h_ = h;
        generation_ = generation;
        started_ = now();
        error.clear();
    }
    // extra: fields added to the next pose (animation clock, object state delta); posted: called
    // once that pose was written, so a delta is never lost to backpressure.
    // Send::Paced sends at most fps poses a second whenever a slot is free. Recordings use
    // Send::Lockstep from the frame tick (the next frame in sequence becomes the front, waited
    // for if needed, then this tick's pose goes out: every presented frame shows the next
    // pose, two ticks behind) and Send::None from the in-between pump (receive and import
    // only; the front stays).
    enum class Send { Paced, Lockstep, None };
    void tick(const sim::Camera &camera, const Json &graphics, const Json &profile, int fps,
              const std::function<Json()> &extra = {}, const std::function<void()> &posted = {}, Send send = Send::Paced) {
        bool lockstep = send == Send::Lockstep;
        if (fd_ < 0)
            return;
        render::GLState guard;
        try {
            double deadline = now() + 100;
            int promoted = 0;
            for (;;) {
            // Copied frames become the front, oldest first (Lockstep: one per frame tick).
            int limit = send == Send::None ? 0 : lockstep ? 1 - promoted : 2;
            for (int k = 0; k < limit; ++k) {
                Slot *oldest = nullptr;
                for (auto &b : slots_)
                    if (b.phase == 3 && (!oldest || b.reply.at("sequence").get<uint64_t>() < oldest->reply.at("sequence").get<uint64_t>()))
                        oldest = &b;
                if (!oldest)
                    break;
                auto &b = *oldest;
                {
                    auto status = glClientWaitSync(b.copied, 0, 0);
                    check(status != GL_WAIT_FAILED, "copy fence");
                    if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED)
                        break;
                    ++promoted;
                    glDeleteSync(b.copied);
                    b.copied = nullptr;
                    if (b.reply.at("sequence").get<uint64_t>() > frontSequence_) {
                        if (front_ >= 0)
                            targets_[front_].busy = false;
                        front_ = b.target;
                        frontSequence_ = b.reply.at("sequence");
                        frontReply_ = b.reply;
                        timings = b.reply;
                    } else
                        targets_[b.target].busy = false;
                    b.phase = 0;
                    b.target = -1;
                }
            }
            for (int n = 0; n < 8; ++n) {
                auto packet = receivePacket(fd_);
                if (!packet)
                    break;
                auto &p = packet->body;
                auto type = p.at("type");
                if (type == "buffer") {
                    int i = p.at("slot");
                    check(i >= 0 && i < 2 && !slots_[i].tex && packet->fds.size() == 1, "buffer slot");
                    check(p.at("width") == w_ && p.at("height") == h_ && p.at("format") == DRM_FORMAT_ABGR8888 &&
                              p.at("modifier") == 0,
                          "buffer geometry/format");
                    auto stride = p.at("stride").get<uint64_t>(), offset = p.at("offset").get<uint64_t>();
                    check(stride >= uint64_t(w_) * 4 && stride <= uint64_t(w_) * 4 + 65536 && offset <= 65536 &&
                              stride * h_ + offset <= 256 * 1024 * 1024,
                          "buffer bounds");
                    auto &b = slots_[i];
                    glActiveTexture(GL_TEXTURE0);
                    b.image =
                        import(eglGetCurrentDisplay(), packet->fds[0], w_, h_, stride, offset, DRM_FORMAT_ABGR8888, 0);
                    b.external = externalOnly(eglGetCurrentDisplay(), DRM_FORMAT_ABGR8888, DRM_FORMAT_MOD_LINEAR);
                    if (b.external)
                        b.tex = externalTexture(b.image);
                    else {
                        b.tex = imageTexture(b.image);
                        b.fbo = framebuffer(b.tex);
                    }
                } else if (type == "frame") {
                    int i = p.at("slot");
                    check(i >= 0 && i < 2 && packet->fds.size() == 1, "frame slot");
                    auto &b = slots_[i];
                    check(b.phase == 1 && p.at("sequence") == b.request.at("sequence") &&
                              p.at("generation") == generation_ && p.at("pose") == b.request.at("pose") &&
                              p.at("sent") == b.request.at("sent"),
                          "frame identity");
                    b.reply = p;
                    b.acquire = packet->take();
                    b.phase = 2;
                } else {
                    check(packet->fds.empty(), "unexpected descriptors");
                    if (type == "caps")
                        capabilities = p;
                    else if (type == "ready") {
                        check(slots_[0].tex && slots_[1].tex, "ready before buffers");
                        ready_ = true;
                    } else
                        throw std::runtime_error("unknown worker message");
                }
            }
            for (auto &b : slots_)
                if (b.phase == 2) {
                    pollfd fence{b.acquire, POLLIN, 0};
                    int n = poll(&fence, 1, 0);
                    check(n >= 0, "acquire poll");
                    if (!n)
                        continue;
                    check(fence.revents & POLLIN, "acquire failed");
                    int target = -1;
                    for (int i = 0; i < 3; ++i)
                        if (!targets_[i].busy) {
                            target = i;
                            break;
                        }
                    if (target < 0)
                        continue;
                    auto &out = targets_[target];
                    out.busy = true;
                    b.target = target;
                    close(b.acquire);
                    b.acquire = -1;
                    glActiveTexture(GL_TEXTURE0);
                    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
                    if (!out.tex) {
                        out.tex = texture(w_, h_);
                        out.fbo = framebuffer(out.tex);
                    }
                    glDisable(GL_SCISSOR_TEST);
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, out.fbo);
                    if (b.external)
                        externalCopy_.draw(b.tex, w_, h_);
                    else {
                        glBindFramebuffer(GL_READ_FRAMEBUFFER, b.fbo);
                        glBlitFramebuffer(0, 0, w_, h_, 0, 0, w_, h_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                    }
                    b.copied = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                    glFlush();
                    b.phase = 3;
                    check(glGetError() == GL_NO_ERROR, "offload import/copy");
                }
            int pending = 0;
            for (auto &b : slots_)
                pending += b.phase != 0;
            if (!lockstep || !ready_ || now() > deadline || ((promoted || !pending) && pending <= 1))
                break;
            // Wait for whichever comes next: a packet, an acquire fence, a copy.
            std::vector<pollfd> waits{{fd_, POLLIN, 0}};
            for (auto &b : slots_)
                if (b.phase == 2)
                    waits.push_back({b.acquire, POLLIN, 0});
                else if (b.phase == 3)
                    glClientWaitSync(b.copied, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000);
            poll(waits.data(), waits.size(), 1);
            }
            if (ready_ && send != Send::None && (lockstep || now() >= nextSend_)) {
                for (int i = 0; i < 2; ++i)
                    if (slots_[i].phase == 0) {
                        auto &b = slots_[i];
                        auto sent = now();
                        b.request = {{"type", "pose"},
                                     {"version", 2},
                                     {"slot", i},
                                     {"generation", generation_},
                                     {"sequence", ++sequence_},
                                     {"sent", sent},
                                     {"graphics", graphics},
                                     {"profile", profile},
                                     {"pose",
                                      {camera.position.x, camera.position.y, camera.position.z, camera.yaw,
                                       camera.pitch, camera.fov, camera.roll, camera.shiftX, camera.shiftY}}};
                        if (extra)
                            b.request.update(extra());
                        if (sendPacket(fd_, b.request)) {
                            b.phase = 1;
                            b.request.erase("objects"); // the reply carries objectsRevision instead
                            b.request.erase("machines"); // and machinesRevision
                            if (posted)
                                posted();
                            auto period = 1000. / std::clamp(fps, 1, 60);
                            nextSend_ = sent - nextSend_ > period ? sent + period : nextSend_ + period;
                        }
                        break;
                    }
            }
            check(ready_ || now() - started_ < 30000, "worker warmup deadline");
            for (auto &b : slots_)
                if (b.phase)
                    check(now() - b.request.at("sent").get<double>() < 500, "worker frame deadline");
        } catch (const std::exception &e) {
            error = e.what();
            stop();
        }
    }
    void stop() {
        render::GLState guard;
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
        for (auto &b : slots_) {
            if (b.acquire >= 0)
                close(b.acquire);
            if (b.copied)
                glDeleteSync(b.copied);
            glDeleteFramebuffers(1, &b.fbo);
            glDeleteTextures(1, &b.tex);
            if (b.image)
                proc<PFNEGLDESTROYIMAGEKHRPROC>("eglDestroyImageKHR")(eglGetCurrentDisplay(), b.image);
            b = {};
        }
        for (auto &t : targets_) {
            glDeleteFramebuffers(1, &t.fbo);
            glDeleteTextures(1, &t.tex);
            t = {};
        }
        ready_ = false;
        front_ = -1;
        frontSequence_ = sequence_ = 0;
        nextSend_ = 0;
    }
};
} // namespace hyprune::offload
