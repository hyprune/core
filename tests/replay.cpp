#include "replay/Session.hpp"
#include <cassert>
#include <iostream>
using namespace hyprune;
using Json = world::Json;
int main() {
    char root[] = "/tmp/hyprune-replay-unit-XXXXXX";
    assert(mkdtemp(root));
    setenv("XDG_STATE_HOME", root, 1);
    replay::Session session;
    sim::Camera cam;
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
    std::filesystem::remove_all(root);
    std::cout << "PASS recording redaction/opt-in, atomic ownership, fixed frames, smoothing, assertions, truncation\n";
}
