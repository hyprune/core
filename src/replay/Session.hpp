#pragma once
#include "interaction/Input.hpp"
#include "ipc/Text.hpp"
#include "world/World.hpp"
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <openssl/sha.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
namespace hyprune::replay {
using Json = world::Json;
inline uint64_t clockNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
inline Json pose(const sim::Camera &c) {
    return {{"position", {c.position.x, c.position.y, c.position.z}},
            {"yaw", c.yaw},
            {"pitch", c.pitch}, {"roll", c.roll}, {"lensShift", {c.shiftX,c.shiftY}},
            {"fov", c.fov},
            {"verticalSpeed", c.verticalSpeed},
            {"grounded", c.grounded},
            {"eyeHeight", c.eyeHeight}};
}
inline sim::Camera camera(const Json &p) {
    sim::Camera c;
    auto v = p.at("position");
    c.position = {v.at(0), v.at(1), v.at(2)};
    c.yaw = p.at("yaw");
    c.pitch = p.at("pitch");
    c.fov = p.at("fov");
    c.roll = p.value("roll",0.f);
    auto shift = p.value("lensShift",Json::array({0,0})); c.shiftX=shift[0]; c.shiftY=shift[1];
    c.verticalSpeed = p.at("verticalSpeed");
    c.grounded = p.at("grounded");
    c.eyeHeight = p.at("eyeHeight");
    return c;
}
// Format 2 keeps files small: floats rounded to `step` (1e-6 m or rad for poses, 1e-3 ms for
// timing) print in a few digits instead of seventeen.
inline void compact(Json &j, double step) {
    if (j.is_number_float())
        j = std::round(j.get<double>() / step) * step;
    else if (j.is_structured())
        for (auto &v : j)
            compact(v, step);
}
inline bool finite(const Json &j) {
    if (j.is_number_float() && !std::isfinite(j.get<double>()))
        return false;
    if (j.is_structured())
        for (auto &v : j)
            if (!finite(v))
                return false;
    return true;
}
inline std::string worldDigest(const world::World &world) {
    std::string bytes = world.manifest.dump();
    bytes.append(reinterpret_cast<const char *>(world.collision.data()), world.collision.size() * sizeof(Triangle));
    bytes.append(reinterpret_cast<const char *>(world.visualTriangles.data()),
                 world.visualTriangles.size() * sizeof(Triangle));
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size(), hash);
    std::ostringstream out;
    for (auto b : hash)
        out << std::hex << std::setw(2) << std::setfill('0') << unsigned(b);
    return out.str();
}
inline void validateState(const Json &state) {
    auto c = camera(state.at("camera"));
    if (!finite(state) || state.at("camera").at("position").size() != 3 || length(c.position) > 1e6 ||
        std::abs(c.yaw) > 1e6 || std::abs(c.pitch) > 1.571f || std::abs(c.roll) > 3.142f || std::abs(c.shiftX)>2 || std::abs(c.shiftY)>2 || c.fov < 3 || c.fov > 175 || c.eyeHeight < .5 ||
        c.eyeHeight > 2 || std::abs(c.verticalSpeed) > 10000)
        throw std::runtime_error("recorded camera out of range");
    // Tools 1-5 and 9 (the debug tool, with its shown layer), the universal position mode.
    // Recordings from before (tools 1-4, no debugLayer) read the same.
    int tool = state.at("tool");
    if (tool < 1 || (tool > 5 && tool != 9) || state.at("windowMode") < 0 || state.at("windowMode") > 1 ||
        state.at("pathMode") < 0 || state.at("pathMode") > 5 || state.at("positionMode") < 0 ||
        state.at("positionMode") > 3 ||
        (state.contains("debugLayer") && (!state["debugLayer"].is_number_integer() || state["debugLayer"] < 0 ||
                                          state["debugLayer"] > 63)))
        throw std::runtime_error("recorded tool out of range");
    std::string mode = state.at("mode"), move = state.at("movement");
    if ((mode != "world" && mode != "menu" && mode != "focus" && mode != "interactive" && mode != "cursor") ||
        (move != "walk" && move != "fly"))
        throw std::runtime_error("recorded mode invalid");
    state.at("noclip").get<bool>();
    state.at("scope").get<bool>();
}
inline void validateAssertions(const Json &checks) {
    if (!checks.is_object() || checks.dump().size() > 32768)
        throw std::runtime_error("invalid assertions");
    for (auto &[key, value] : checks.items()) {
        if (key == "recorded") {
            if (!value.is_boolean())
                throw std::runtime_error("recorded must be boolean");
        } else if (key == "tolerance") {
            if (!value.is_number() || !finite(value) || value < 0 || value > 100)
                throw std::runtime_error("invalid tolerance");
        } else if (key == "marks") {
            if (!value.is_object() || value.size() > 128)
                throw std::runtime_error("invalid mark assertions");
            for (auto &[name, expected] : value.items()) {
                if (name.empty() || name.size() > 64 || !expected.is_object())
                    throw std::runtime_error("invalid assertion mark");
                for (auto &[field, v] : expected.items()) {
                    if (field == "camera") {
                        if (!v.is_object())
                            throw std::runtime_error("invalid camera assertion");
                        for (auto &[component, n] : v.items()) {
                            if (component == "position") {
                                if (!n.is_array() || n.size() != 3)
                                    throw std::runtime_error("invalid position assertion");
                                for (auto &c : n)
                                    if (!c.is_number() || !finite(c))
                                        throw std::runtime_error("invalid position assertion");
                            } else if (component == "yaw" || component == "pitch" || component == "fov") {
                                if (!n.is_number() || !finite(n))
                                    throw std::runtime_error("invalid camera assertion");
                            } else
                                throw std::runtime_error("unknown camera assertion");
                        }
                    } else if (field == "tool") {
                        if (!v.is_number_integer() || v < 1 || v > 4)
                            throw std::runtime_error("invalid tool assertion");
                    } else if (field == "mode" || field == "subtype" || field == "movement") {
                        if (!v.is_string() || v.get<std::string>().size() > 64)
                            throw std::runtime_error("invalid state assertion");
                    } else if (field == "scope" || field == "noclip") {
                        if (!v.is_boolean())
                            throw std::runtime_error("invalid boolean assertion");
                    } else if (field == "tolerance") {
                        if (!v.is_number() || !finite(v) || v < 0 || v > 100)
                            throw std::runtime_error("invalid tolerance");
                    } else
                        throw std::runtime_error("unknown mark assertion: " + field);
                }
            }
        } else
            throw std::runtime_error("unknown assertion: " + key);
    }
}
// A label slug for file names: lowercase letters and digits, '-' between words, at most 40.
inline std::string labelSlug(const std::string &label) {
    std::string slug;
    for (unsigned char c : label) {
        if (std::isalnum(c))
            slug += char(std::tolower(c));
        else if (!slug.empty() && slug.back() != '-')
            slug += '-';
        if (slug.size() >= 40)
            break;
    }
    while (!slug.empty() && slug.back() == '-')
        slug.pop_back();
    return slug;
}
// Labels a finished recording: rec-<time>.jsonl becomes rec-<time>-<slug>.jsonl, with a 0600
// sidecar rec-<time>-<slug>.json holding the label and `extra` (what the labelling client
// adds, e.g. its version). Returns the new path.
inline std::string labelRecording(const std::string &path, const std::string &label, const Json &extra) {
    std::filesystem::path file(path);
    if (file.extension() != ".jsonl" || !std::filesystem::is_regular_file(file))
        throw std::runtime_error("no finished recording to label");
    auto stem = file.stem().string();
    auto slug = labelSlug(label);
    auto target = file;
    if (!slug.empty() && !stem.ends_with("-" + slug)) {
        target = file.parent_path() / (stem + "-" + slug + ".jsonl");
        if (std::filesystem::exists(target))
            throw std::runtime_error("a recording with that label exists");
        std::filesystem::rename(file, target);
        auto oldSide = file;
        oldSide.replace_extension(".json");
        if (std::filesystem::exists(oldSide))
            std::filesystem::remove(oldSide);
    }
    auto side = target;
    side.replace_extension(".json");
    Json meta = extra.is_object() ? extra : Json::object();
    meta["label"] = label;
    meta["recording"] = target.filename().string();
    int fd = open(side.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        throw std::runtime_error("cannot write recording label");
    auto bytes = meta.dump(1) + "\n";
    bool ok = write(fd, bytes.data(), bytes.size()) == ssize_t(bytes.size());
    close(fd);
    if (!ok)
        throw std::runtime_error("cannot write recording label");
    return target.string();
}
class Session {
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::string> queue_;
    std::thread writer_;
    bool closing_ = false;
    size_t queued_ = 0, total_ = 0;
    std::string writeError_;
    uint64_t began_ = 0;
    void emit(Json j) {
        if (!j.contains("ns"))
            j["ns"] = clockNs() - began_;
        auto bytes = j.dump() + "\n";
        std::lock_guard lock(mutex_);
        if (!writeError_.empty())
            return;
        if (queued_ + bytes.size() > 4 * 1024 * 1024 || total_ + bytes.size() > SizeLimit) {
            writeError_ = "recording size/queue limit";
            return;
        }
        queued_ += bytes.size();
        total_ += bytes.size();
        queue_.push_back(std::move(bytes));
        changed_.notify_one();
    }

  public:
    static constexpr double Step = 1. / 60.;
    // Version 2 (IPC 0.29) records live: each frame keeps the dt the tick really used and its
    // timing, and replay feeds that dt sequence back. Version 1 files ran (and replay) on fixed
    // 1/60 s steps. Bounds: 128 MiB, 30 minutes of live frames (v1: 64 MiB, 36,000 frames).
    static constexpr size_t SizeLimit = 128ull * 1024 * 1024;
    static constexpr uint64_t FrameLimitV1 = 36000, FrameLimitV2 = 432000;
    static constexpr uint64_t DurationLimitNs = 30ull * 60 * 1000000000;
    bool recording = false, playing = false, includeAppKeys = false;
    int version = 2;
    uint64_t frame = 0;
    std::vector<double> dts, tickNs; // version 2 playback: each frame's dt (s) and tick time (ns)
    std::string path, mode = "input";
    Json header, frames = Json::array(), views = Json::array(), failures = Json::array(), marks = Json::array(),
                 assertions = Json::object();
    std::map<uint64_t, std::vector<Json>> events;
    double smoothingMs = 0;
    ~Session() { stopRecording(); }
    std::string error() {
        std::lock_guard lock(mutex_);
        return writeError_;
    }
    // Playback's input clock: v1 the fixed step count, v2 the recorded tick time.
    uint64_t logicalMillis() const {
        if (version >= 2 && !tickNs.empty())
            return began_ / 1000000 + uint64_t(tickNs.at(std::min<size_t>(frame, tickNs.size() - 1)) / 1e6);
        return began_ / 1000000 + frame * 1000 / 60;
    }
    // A playback frame's dt: the recorded one (v2) or the fixed step (v1).
    double frameDt() const {
        if (version >= 2 && !dts.empty())
            return dts.at(std::min<size_t>(frame, dts.size() - 1));
        return Step;
    }
    // ns since the recording started for a steady clock time (0 before it).
    uint64_t since(std::chrono::steady_clock::time_point t) const {
        auto ns = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count());
        return ns > began_ ? ns - began_ : 0;
    }
    uint64_t elapsedNs() const { return recording ? clockNs() - began_ : 0; }
    uint64_t startedNs() const { return began_; }
    Json status() {
        Json bounded = Json::array();
        for (auto &failure : failures)
            bounded.push_back(ipc::boundedText(failure.get<std::string>(), ipc::FailureLimit));
        return {{"recording", recording},
                {"playing", playing},
                {"path", path.empty() ? Json(nullptr) : Json(ipc::boundedText(path, ipc::PathLimit))},
                {"frame", frame},
                {"mode", mode},
                {"failures", bounded},
                {"error", error().empty() ? Json(nullptr) : Json(ipc::boundedText(error(), ipc::RecordingErrorLimit))}};
    }
    void startRecording(Json initial, bool appKeys) {
        if (recording || playing)
            throw std::runtime_error("record/replay already active");
        stopRecording();
        if (initial.dump().size() > 250000)
            throw std::runtime_error("recording header too large");
        const char *xdg = getenv("XDG_STATE_HOME"), *home = getenv("HOME");
        if ((!xdg || !*xdg) && (!home || !*home))
            throw std::runtime_error("state directory unavailable");
        auto root = (xdg && *xdg ? std::filesystem::path(xdg) : std::filesystem::path(home) / ".local/state") /
                    "hyprune/recordings";
        std::filesystem::create_directories(root);
        // rec-<local time>.jsonl; a label given on stop renames it (rec-<time>-<slug>.jsonl).
        auto now = std::time(nullptr);
        std::tm local{};
        localtime_r(&now, &local);
        char stamp[32];
        std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &local);
        int fd = -1;
        for (int n = 1; n < 100 && fd < 0; ++n) {
            path = (root / (std::string("rec-") + stamp + (n > 1 ? "-" + std::to_string(n) : "") + ".jsonl")).string();
            fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
            if (fd < 0 && errno != EEXIST)
                break;
        }
        if (fd < 0)
            throw std::runtime_error("cannot create recording");
        frame = 0;
        version = 2;
        dts.clear();
        tickNs.clear();
        mode = "input"; events.clear(); frames = views = marks = Json::array();
        began_ = clockNs();
        queued_ = total_ = 0;
        closing_ = false;
        writeError_.clear();
        failures = Json::array();
        includeAppKeys = appKeys;
        header = std::move(initial);
        header.update({{"kind", "header"},
                       {"format", "hyprune.session"},
                       {"version", 2},
                       {"timing", "live"},
                       {"includeAppKeys", appKeys}});
        writer_ = std::thread([this, fd] {
            for (;;) {
                std::string bytes;
                {
                    std::unique_lock lock(mutex_);
                    changed_.wait(lock, [&] { return closing_ || !queue_.empty(); });
                    if (queue_.empty())
                        break;
                    bytes = std::move(queue_.front());
                    queue_.pop_front();
                    queued_ -= bytes.size();
                }
                size_t n = 0;
                while (n < bytes.size()) {
                    auto count = write(fd, bytes.data() + n, bytes.size() - n);
                    if (count < 0 && errno == EINTR)
                        continue;
                    if (count <= 0) {
                        std::lock_guard lock(mutex_);
                        writeError_ = "recording write failed";
                        break;
                    }
                    n += count;
                }
            }
            if (fsync(fd)) {
                std::lock_guard lock(mutex_);
                writeError_ = "recording sync failed";
            }
            close(fd);
        });
        recording = true;
        emit(header);
    }
    void stopRecording() {
        if (recording) {
            emit({{"kind", "end"}, {"frames", frame}});
            recording = false;
        }
        if (writer_.joinable()) {
            {
                std::lock_guard lock(mutex_);
                closing_ = true;
                changed_.notify_one();
            }
            writer_.join();
        }
    }
    void event(Json data, bool appKey = false) {
        if (!recording)
            return;
        if (appKey && !includeAppKeys)
            data = {{"type", "key"}, {"redacted", true}};
        emit({{"kind", "input"}, {"frame", frame}, {"event", data}});
    }
    // Tests and tools: a frame on the fixed 1/60 s grid.
    void recordFrame(const Json &state) { recordFrame(state, Step, frame * 16666667ull, Json::object()); }
    // A live frame (one tick): its tick time, the dt it moved by, its state and timing.
    void recordFrame(Json state, double dt, uint64_t tickStartNs, Json timing) {
        if (recording) {
            compact(state, 1e-6);
            compact(timing, 1e-3);
            emit({{"kind", "frame"}, {"frame", frame}, {"tickNs", tickStartNs}, {"dt", dt}, {"state", state}, {"timing", timing}});
            ++frame;
        }
    }
    // A completed 3D draw: the camera it showed and its timing (draw: CPU ms, ageMs: from the
    // camera sample to the draw; worker frames name the offload front they showed).
    void view(Json camera, uint64_t renderedFrame, Json timing) {
        if (recording) {
            compact(camera, 1e-6);
            compact(timing, 1e-3);
            Json j{{"kind", "view"}, {"frame", frame ? frame - 1 : 0}, {"renderedFrame", renderedFrame}, {"camera", camera}};
            j.update(timing);
            emit(std::move(j));
        }
    }
    // Periodic statistics (every ~5 s): written into the recording and kept for status.
    void stats(const Json &s) {
        if (recording) {
            Json j = s;
            j["kind"] = "stats";
            emit(std::move(j));
        }
    }
    void mark(const std::string &name, const Json &state) {
        if (!recording || frame == 0 || name.empty() || name.size() > 64)
            throw std::runtime_error("mark requires a recording frame and 1..64 character name");
        emit({{"kind", "mark"}, {"name", name}, {"frame", frame - 1}, {"state", state}});
    }
    void load(const std::string &file, const std::string &kind, double smooth, const Json &checks) {
        if (recording || playing)
            throw std::runtime_error("record/replay already active");
        if (kind != "input" && kind != "camera")
            throw std::runtime_error("unknown replay mode");
        if (!std::isfinite(smooth) || smooth < 0 || smooth > 1000)
            throw std::runtime_error("smoothingMs must be 0..1000");
        validateAssertions(checks);
        int fd = open(file.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        struct stat st{};
        if (fd < 0)
            throw std::runtime_error("recording unavailable");
        if (fstat(fd, &st) || !S_ISREG(st.st_mode) || size_t(st.st_size) > SizeLimit || st.st_uid != getuid()) {
            close(fd);
            throw std::runtime_error("recording must be an owned regular file <=128 MiB");
        }
        std::string bytes(st.st_size, '\0');
        size_t readBytes = 0;
        while (readBytes < bytes.size()) {
            auto n = read(fd, bytes.data() + readBytes, bytes.size() - readBytes);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0) {
                close(fd);
                throw std::runtime_error("recording read failed");
            }
            readBytes += n;
        }
        close(fd);
        header = nullptr;
        frames = Json::array();
        dts.clear();
        tickNs.clear();
        version = 1;
        views = Json::array();
        marks = Json::array();
        events.clear();
        failures = Json::array();
        bool ended = false;
        std::istringstream lines(bytes);
        std::string line;
        uint64_t previousNs = 0;
        while (std::getline(lines, line)) {
            if (line.empty() || line.size() > 262144)
                throw std::runtime_error("recording line limit");
            auto j = Json::parse(line);
            if (!finite(j) || !j.is_object())
                throw std::runtime_error("nonfinite recording");
            auto ns = j.at("ns").get<uint64_t>();
            if (ns < previousNs)
                throw std::runtime_error("unordered recording time");
            previousNs = ns;
            std::string type = j.at("kind");
            if (header.is_null()) {
                if (type != "header" || j.at("format") != "hyprune.session" ||
                    !((j.at("version") == 1 && j.at("timestepNs") == 16666667) ||
                      (j.at("version") == 2 && j.at("timing") == "live")))
                    throw std::runtime_error("unsupported recording header");
                header = j;
                version = j.at("version");
                continue;
            }
            if (ended)
                throw std::runtime_error("data after recording end");
            if (type == "end") {
                ended = true;
                if (j.at("frames") != frames.size())
                    throw std::runtime_error("frame count mismatch");
                continue;
            }
            if (type == "stats")
                continue; // statistics: read by the analysis tools, not by playback
            auto index = j.at("frame").get<uint64_t>();
            if (index > (version >= 2 ? FrameLimitV2 : FrameLimitV1))
                throw std::runtime_error("recording duration limit");
            if (type == "frame") {
                if (index != frames.size())
                    throw std::runtime_error("unordered frames");
                validateState(j.at("state"));
                frames.push_back(j.at("state"));
                if (version >= 2) {
                    double dt = j.at("dt");
                    if (!(dt >= 0 && dt <= 1))
                        throw std::runtime_error("recorded dt out of range");
                    dts.push_back(dt);
                    tickNs.push_back(j.at("tickNs").get<double>());
                }
            } else if (type == "input") {
                auto e = j.at("event");
                auto t = e.at("type").get<std::string>();
                if (t != "key" && t != "button" && t != "axis" && t != "motion")
                    throw std::runtime_error("unknown input event");
                if (!e.value("redacted", false)) {
                    if (t == "motion") {
                        for (auto key : {"dx", "dy"})
                            if (!e.at(key).is_number() || std::abs(e.at(key).get<double>()) > 1e6)
                                throw std::runtime_error("invalid motion");
                    } else {
                        interaction::parseBinding(e.at("key"));
                        e.at("down").get<bool>();
                        if (e.at("mods").get<unsigned>() > 255 || std::abs(e.at("value").get<float>()) > 1e6)
                            throw std::runtime_error("invalid input");
                    }
                }
                if (events[index].size() >= 4096)
                    throw std::runtime_error("events per frame limit");
                events[index].push_back(std::move(e));
            } else if (type == "view") {
                camera(version >= 2 ? j.at("camera") : j.at("state").at("camera"));
                views.push_back(j);
            } else if (type == "mark") {
                if (j.at("name").get<std::string>().size() > 64 || marks.size() > 1024)
                    throw std::runtime_error("mark limit");
                marks.push_back(j);
            } else
                throw std::runtime_error("unknown recording entry");
        }
        if (!ended || frames.empty())
            throw std::runtime_error("incomplete recording");
        if (checks.contains("marks"))
            for (auto &[name, _] : checks.at("marks").items()) {
                if (std::none_of(marks.begin(), marks.end(), [&](const auto &m) { return m.at("name") == name; }))
                    throw std::runtime_error("assertion mark missing: " + name);
            }
        path = file;
        mode = kind;
        smoothingMs = smooth;
        assertions = checks;
        frame = 0;
        began_ = clockNs();
        writeError_.clear();
    }
    Json cameraFrame() const {
        auto state = frames.at(std::min<size_t>(frame, frames.size() - 1));
        int radius = std::lround(smoothingMs * 60 / 1000.);
        if (!radius)
            return state;
        auto central = state.at("camera");
        double weights = 0;
        std::array<double, 9> sum{};
        int lo = std::max(0, int(frame) - radius), hi = std::min(int(frames.size()) - 1, int(frame) + radius);
        for (int i = lo; i <= hi; ++i) {
            auto &p = frames.at(i).at("camera");
            double weight = radius + 1 - std::abs(i - int(frame));
            weights += weight;
            for (int k = 0; k < 3; ++k)
                sum[k] += weight * p.at("position").at(k).get<double>();
            sum[3] += weight * (central.at("yaw").get<double>() +
                                std::remainder(p.at("yaw").get<double>() - central.at("yaw").get<double>(),
                                               2 * 3.141592653589793));
            sum[4] += weight * p.at("pitch").get<double>();
            sum[5] += weight * p.at("fov").get<double>();
            sum[6] += weight * (central.value("roll",0.) + std::remainder(p.value("roll",0.)-central.value("roll",0.),2*3.141592653589793));
            auto shift=p.value("lensShift",Json::array({0,0}));
            sum[7] += weight * shift[0].get<double>(); sum[8] += weight * shift[1].get<double>();
        }
        for (int k = 0; k < 3; ++k)
            state["camera"]["position"][k] = sum[k] / weights;
        state["camera"]["yaw"] = sum[3] / weights;
        state["camera"]["pitch"] = sum[4] / weights;
        state["camera"]["fov"] = sum[5] / weights;
        state["camera"]["roll"] = sum[6]/weights; state["camera"]["lensShift"]={sum[7]/weights,sum[8]/weights};
        return state;
    }
    void compare(const Json &actual, const Json &expected, const std::string &where, double tolerance) {
        for (auto &[key, value] : expected.items()) {
            if (key == "tolerance")
                continue;
            Json a = actual.value(key, Json(nullptr));
            if (value.is_object()) {
                compare(a.is_object() ? a : Json::object(), value, where + "." + key, tolerance);
                continue;
            }
            bool same = a == value;
            if (value.is_number() && a.is_number())
                same = std::abs(a.get<double>() - value.get<double>()) <= tolerance;
            if (value.is_array() && a.is_array() && value.size() == a.size()) {
                same = true;
                for (size_t i = 0; i < value.size(); ++i)
                    if (!value[i].is_number() || !a[i].is_number() ||
                        std::abs(a[i].get<double>() - value[i].get<double>()) > tolerance)
                        same = false;
            }
            if (!same && failures.size() < 128)
                failures.push_back(where + "." + key + " mismatch at frame " + std::to_string(frame));
        }
    }
    bool finishFrame(const Json &actual) {
        if (!playing)
            return false;
        double tolerance = assertions.value("tolerance", .001);
        if (!std::isfinite(tolerance) || tolerance < 0 || tolerance > 100)
            throw std::runtime_error("invalid assertion tolerance");
        if (assertions.value("recorded", false))
            compare(actual, frames.at(frame), "recorded", tolerance);
        if (assertions.contains("marks"))
            for (auto &m : marks)
                if (m.at("frame") == frame && assertions["marks"].contains(m.at("name"))) {
                    auto expected = assertions["marks"].at(m.at("name"));
                    compare(actual, expected, m.at("name"), expected.value("tolerance", tolerance));
                }
        return ++frame >= frames.size();
    }
};
} // namespace hyprune::replay
