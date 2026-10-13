#pragma once
#include "Common.hpp"
#include "Pacing.hpp"
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
        double received = 0, copying = 0; // when the frame packet arrived, when its copy was issued
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
    FrameBudget budget_;
    // Round trip of the latest promoted frames (ms after the pose was sent): packet, copy, front.
    std::deque<std::array<double, 3>> trips_;
    // Frame ticks (diagnostics): time, promoted sequence, sent sequence, slot phases, budget.
    std::deque<std::array<double, 6>> ticks_;
    std::string lastSent_; // the last pose request without its identity: unchanged means no new frame
    size_t workerAssets_ = 0; // assets the worker can draw (its "assets" messages)
    ExternalCopy externalCopy_;

  public:
    std::string error;
    Json capabilities, timings;
    bool connected() const { return fd_ >= 0; }
    // Stale (the in-process renderer stands in) when a requested frame is more than maxAgeMs
    // late; a front that nothing has replaced because nothing changed stays valid. Recordings
    // in lockstep accept any wait, the front being exactly the next frame in sequence.
    double inFlightMs() const {
        double age = 0;
        for (auto &b : slots_)
            if (b.phase)
                age = std::max(age, now() - b.request.at("sent").get<double>());
        return age;
    }
    bool available(double maxAgeMs = 50) const { return fd_ >= 0 && front_ >= 0 && inFlightMs() <= maxAgeMs; }
    GLuint fbo(double maxAgeMs = 50) const { return available(maxAgeMs) ? targets_[front_].fbo : 0; }
    uint64_t frontSequence() const { return front_ >= 0 ? frontSequence_ : 0; }
    // Age of the newest complete frame (ms), or -1.
    double frontAgeMs() const { return front_ >= 0 ? now() - frontReply_.at("sent").get<double>() : -1; }
    Json diagnostics() const {
        Json phases = Json::array();
        for (auto &b : slots_)
            phases.push_back(b.phase);
        return {{"front", front_}, {"frontSequence", frontSequence_}, {"sequence", sequence_}, {"phases", phases},
                {"frontAgeMs", front_ >= 0 ? Json(now() - frontReply_.at("sent").get<double>()) : Json(nullptr)},
                {"trips", Json(std::vector<std::array<double, 3>>(trips_.begin(), trips_.end()))},
                {"ticks", Json(std::vector<std::array<double, 6>>(ticks_.begin(), ticks_.end()))}};
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
    // Live, twice per compositor frame tick: Send::Promote at its start (the frame boundary)
    // makes the next finished frame in sequence the front (at most one per frame, so the
    // cadence stays even); Send::Frame once the camera and scene are final sends, with a slot
    // free, budget for this frame (share: frames since the last tick x fps /
    // refresh) and a request that differs from the last one, this tick's pose goes out: one
    // fresh frame per compositor frame while anything shown changes, at the policy's rate
    // on average. Two poses may be out: a worker round trip is close to a frame (7 ms render,
    // 1.5 ms transfer, 14 ms median and 21 ms p90 to the compositor on the work host), and
    // with one the rate halves whenever a frame misses its tick. The front then shows the
    // pose of one tick before (as before this change), two after a late frame.
    // Send::Paced sends at most fps poses a second whenever a slot is free. Recordings use
    // Send::Lockstep from the frame tick (the next frame in sequence becomes the front, waited
    // for if needed, then this tick's pose goes out: every presented frame shows the next
    // pose, two ticks behind) and Send::None from the in-between pump (receive and import
    // only; the front stays).
    enum class Send { Paced, Promote, Frame, Lockstep, None };
    void tick(const sim::Camera &camera, const Json &graphics, const Json &profile, int fps,
              const std::function<Json()> &extra = {}, const std::function<void()> &posted = {}, Send send = Send::Paced,
              double share = 1) {
        bool lockstep = send == Send::Lockstep, frame = send == Send::Frame;
        if (frame)
            budget_.accrue(share);
        if (fd_ < 0)
            return;
        render::GLState guard;
        try {
            double deadline = now() + 100;
            int promoted = 0;
            uint64_t promotedSeq = 0, sentSeq = 0;
            for (;;) {
            // Copied frames become the front, oldest first (Lockstep: one per frame tick).
            int limit = send == Send::None || frame ? 0 : lockstep || send == Send::Promote ? 1 - promoted : 2;
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
                    {
                        double sent = b.request.at("sent").get<double>();
                        trips_.push_back({b.received - sent, b.copying - sent, now() - sent});
                        while (trips_.size() > 240)
                            trips_.pop_front();
                    }
                    glDeleteSync(b.copied);
                    b.copied = nullptr;
                    if (b.reply.at("sequence").get<uint64_t>() > frontSequence_) {
                        if (front_ >= 0)
                            targets_[front_].busy = false;
                        front_ = b.target;
                        frontSequence_ = b.reply.at("sequence");
                        promotedSeq = frontSequence_;
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
                    b.received = now();
                    b.acquire = packet->take();
                    b.phase = 2;
                } else {
                    check(packet->fds.empty(), "unexpected descriptors");
                    if (type == "caps")
                        capabilities = p;
                    else if (type == "assets")
                        workerAssets_ = p.at("ready").get<size_t>(); // a new count poses again (below)
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
                    b.copying = now();
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
            int inFlight = 0;
            for (auto &b : slots_)
                inFlight += b.phase != 0;
            if (ready_ && (send == Send::Paced || send == Send::Frame || lockstep) &&
                (lockstep || (frame ? inFlight < 2 && budget_.ready() : now() >= nextSend_))) {
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
                        // The worker's drawable asset count is part of what a frame shows: an
                        // asset that finished loading after the last pose redraws a still scene.
                        b.request["assetsReady"] = workerAssets_;
                        if (frame) {
                            auto same = b.request;
                            for (auto key : {"slot", "sequence", "sent"})
                                same.erase(key);
                            auto signature = same.dump();
                            if (signature == lastSent_) {
                                --sequence_; // nothing changed: the front stays, no frame is drawn
                                b.request = {};
                                break;
                            }
                            lastSent_ = std::move(signature);
                            budget_.spend();
                        }
                        if (sendPacket(fd_, b.request)) {
                            b.phase = 1;
                            sentSeq = sequence_;
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
            if (frame || send == Send::Promote) {
                ticks_.push_back({now(), double(promotedSeq), double(sentSeq), double(slots_[0].phase), double(slots_[1].phase), budget_.credit});
                while (ticks_.size() > 400)
                    ticks_.pop_front();
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
        lastSent_.clear();
        workerAssets_ = 0;
        budget_ = {};
        front_ = -1;
        frontSequence_ = sequence_ = 0;
        nextSend_ = 0;
    }
};
} // namespace hyprune::offload
