#include "interaction/InputQueue.hpp"
#include "replay/Session.hpp"
#include <cassert>
#include <deque>
#include <functional>
#include <iostream>
using namespace hyprune;
using Json = world::Json;
int main() {
    char root[] = "/tmp/hyprune-replay-unit-XXXXXX";
    assert(mkdtemp(root));
    setenv("XDG_STATE_HOME", root, 1);
    replay::Session session;
    sim::Camera cam; cam.roll=.3f; cam.shiftX=.2f; cam.shiftY=-.1f; cam.fov=15;
    Json initial = {
        {"camera", replay::pose(cam)}, {"mode", "world"},   {"tool", 1},       {"windowMode", 0}, {"pathMode", 0},
        {"positionMode", 0},           {"movement", "fly"}, {"noclip", false}, {"scope", false}};
    session.startRecording({{"initial", initial}, {"seed", 1}}, false);
    session.event({{"type", "key"}, {"key", "private-letter"}, {"mods", 0}, {"down", true}, {"value", 0}}, true);
    for (int i = 0; i < 4; ++i) {
        cam.position.x = i;
        initial["camera"] = replay::pose(cam);
        session.recordFrame(initial);
    }
    session.mark("end", initial);
    session.stopRecording();
    auto file = session.path;
    struct stat st{};
    assert(!stat(file.c_str(), &st));
    assert((st.st_mode & 0777) == 0600);
    std::ifstream stream(file);
    std::string bytes{std::istreambuf_iterator<char>(stream), {}};
    assert(bytes.find("private-letter") == std::string::npos);
    session.load(file, "input", 0, {{"recorded", true}, {"marks", {{"end", {{"tool", 1}}}}}});
    assert(session.events[0][0].at("redacted") == true);
    session.playing = true;
    for (int i = 0; i < 4; ++i)
        session.finishFrame(session.frames.at(i));
    assert(session.failures.empty());
    session.playing = false;
    session.load(file, "camera", 100, Json::object());
    session.frame = 1;
    auto smooth = session.cameraFrame();
    assert(std::abs(smooth["camera"]["roll"].get<float>()-.3f)<1e-6f);
    assert(std::abs(smooth["camera"]["lensShift"][0].get<float>()-.2f)<1e-6f);
    assert(replay::camera(smooth["camera"]).fov==15);
    assert(smooth["camera"]["position"][0] > 1 && smooth["camera"]["position"][0] < 2);
    session.playing = true;
    session.finishFrame({{"tool", 2}});
    session.playing = false;
    session.load(file, "input", 0, {{"marks", {{"end", {{"tool", 2}}}}}});
    session.playing = true;
    for (int i = 0; i < 4; ++i)
        session.finishFrame(session.frames.at(i));
    assert(!session.failures.empty());
    session.playing = false;
    bool failed = false;
    try {
        session.load(file, "input", 0, {{"marks", {{"missing", Json::object()}}}});
    } catch (...) {
        failed = true;
    }
    assert(failed);
    std::ofstream broken(file, std::ios::trunc);
    broken << bytes.substr(0, bytes.find("\n"));
    broken.close();
    failed = false;
    try {
        session.load(file, "input", 0, Json::object());
    } catch (...) {
        failed = true;
    }
    assert(failed);
    session.startRecording({{"initial", initial}}, true);
    session.event({{"type", "key"}, {"key", "a"}, {"mods", 0}, {"down", true}, {"value", 0}}, true);
    session.recordFrame(initial);
    session.stopRecording();
    session.load(session.path, "input", 0, Json::object());
    assert(session.events[0][0]["key"] == "a");
    // An input recorded for frame N takes effect in frame N, as a replay applies it. Live, the
    // compositor queues the frame's tick before the input's own idle callback; unless the tick
    // runs queued input first, the press steps a frame late and the replay drifts a frame.
    for (bool tickRunsQueuedInput : {true, false}) {
        interaction::InputQueue queue;
        std::deque<std::function<void()>> idle; // the event loop's doLater callbacks, in order
        bool held = false;
        float position = 0;
        std::vector<float> live, replayed;
        session.startRecording({{"initial", initial}}, true);
        auto tick = [&] {
            if (tickRunsQueuedInput)
                queue.run(queue.last());
            position += held ? 1.f : 0.f;
            live.push_back(position);
            session.recordFrame(initial);
        };
        auto key = [&](bool down) { // recorded now, applied through the deferred queue
            session.event({{"type", "key"}, {"key", "w"}, {"mods", 0}, {"down", down}, {"value", 0}}, false);
            auto upTo = queue.push([&held, down] { held = down; });
            idle.push_back([&queue, upTo] { queue.run(upTo); });
        };
        for (int frame = 0; frame < 6; ++frame) {
            idle.push_back(tick); // presentation feedback queues this frame's tick
            if (frame == 1)
                key(true);
            if (frame == 3)
                key(false);
            while (!idle.empty()) {
                auto next = std::move(idle.front());
                idle.pop_front();
                next();
            }
        }
        session.stopRecording();
        session.load(session.path, "input", 0, Json::object());
        held = false;
        position = 0;
        for (size_t frame = 0; frame < session.frames.size(); ++frame) {
            for (auto &e : session.events[frame])
                held = e.at("down");
            position += held ? 1.f : 0.f;
            replayed.push_back(position);
        }
        assert((live == replayed) == tickRunsQueuedInput);
        if (tickRunsQueuedInput)
            assert((live == std::vector<float>{0, 1, 2, 2, 2, 2}));
    }
    std::filesystem::remove_all(root);
    std::cout << "PASS recording redaction/opt-in, atomic ownership, fixed frames, smoothing, assertions, truncation, "
                 "input in its recorded frame\n";
}
