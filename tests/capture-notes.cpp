// Capture notes: validation, JSON + index + notes.md writes, ordering of rapid captures.
#include "interaction/CaptureNotes.hpp"
#include "interaction/InputFile.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>
using namespace hyprune;
using Json = nlohmann::json;
namespace {
void check(bool value, const std::string &why) {
    if (!value)
        throw std::runtime_error(why);
}
bool rejects(const Json &note) {
    try {
        interaction::captureNote(note);
        return false;
    } catch (const std::invalid_argument &) {
        return true;
    }
}
std::vector<std::string> lines(const std::filesystem::path &p) {
    std::ifstream in(p);
    std::vector<std::string> out;
    for (std::string l; std::getline(in, l);)
        out.push_back(l);
    return out;
}
} // namespace
int main() {
    try {
        using interaction::captureNote;
        // Validation.
        check(captureNote("") == "", "empty note allowed");
        check(captureNote("ship rail clips the stair") == "ship rail clips the stair", "plain note");
        auto utf8 = std::string("Brücke · 橋 · 🌉");
        check(captureNote(utf8) == utf8, "UTF-8 note kept byte for byte");
        std::string limit;
        for (int i = 0; i < 1000; i++)
            limit += "é"; // 1000 code points, 2000 bytes
        check(captureNote(limit) == limit, "1000 code points allowed");
        check(rejects(limit + "e"), "1001 code points rejected");
        check(rejects("line\nbreak") && rejects("tab\there") && rejects(std::string("nul\0x", 5)) &&
                  rejects("del\x7f"),
              "control characters rejected");
        check(rejects(std::string("\xc3")) && rejects(std::string("\xc0\xaf")) && rejects(std::string("\xed\xa0\x80")),
              "invalid UTF-8 (truncated, overlong, surrogate) rejected");
        check(rejects(Json(nullptr)) && rejects(Json(3)), "non-string rejected");
        check(interaction::validCaptureId("capture-1791547507663") && !interaction::validCaptureId("../x") &&
                  !interaction::validCaptureId("capture-1.json") && !interaction::validCaptureId("capture-"),
              "capture IDs are file stems only");

        auto dir = std::filesystem::temp_directory_path() / ("hyprune-notes-" + std::to_string(getpid()));
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        auto capture = [&](int64_t stamp) {
            auto id = "capture-" + std::to_string(stamp);
            interaction::atomicJSON(dir / (id + ".json"), {{"version", 1}, {"timestampMs", stamp}});
            return id;
        };
        // Note write: JSON gains note + noteTimeMs, index and notes.md gain one line.
        auto first = capture(1000);
        interaction::annotateCapture(dir, first, utf8, 1500);
        auto json = Json::parse(std::ifstream(dir / (first + ".json")));
        check(json["note"] == utf8 && json["noteTimeMs"] == 1500 && json["version"] == 1, "note in capture JSON");
        auto index = lines(dir / "index.jsonl");
        check(index.size() == 1 && Json::parse(index[0])["note"] == utf8 &&
                  Json::parse(index[0])["capture"] == first + ".json" &&
                  Json::parse(index[0])["screenshot"] == first + ".png" && Json::parse(index[0])["timestampMs"] == 1000,
              "index line");
        auto notes = lines(dir / "notes.md");
        check(notes.size() == 1 && notes[0].find(first + ".png") != std::string::npos &&
                  notes[0].find(utf8) != std::string::npos,
              "notes.md line");
        // Empty note (Enter): note "" in JSON and index, nothing in notes.md.
        auto second = capture(2000);
        interaction::annotateCapture(dir, second, "", 2100);
        check(Json::parse(std::ifstream(dir / (second + ".json")))["note"] == "", "empty note stored as \"\"");
        check(lines(dir / "index.jsonl").size() == 2 && lines(dir / "notes.md").size() == 1, "empty note not in notes.md");
        // Missing capture file is an error, not a new file.
        bool missing = false;
        try {
            interaction::annotateCapture(dir, "capture-42", "x", 1);
        } catch (const std::runtime_error &) {
            missing = true;
        }
        check(missing && !std::filesystem::exists(dir / "capture-42.json"), "annotating a missing capture fails");

        // Rapid U U U: capture writes (slow, uneven) and notes run strictly in submission order,
        // so each note finds its capture file and the index reads in capture order.
        interaction::SerialJobs jobs;
        std::vector<std::string> order;
        std::vector<std::string> ids;
        for (int i = 0; i < 3; i++) {
            auto id = "capture-" + std::to_string(3000 + i);
            ids.push_back(id);
            jobs.push([&, id, i] {
                std::this_thread::sleep_for(std::chrono::milliseconds(60 - 25 * i)); // later writes finish faster
                interaction::atomicJSON(dir / (id + ".json"), {{"version", 1}, {"timestampMs", 3000 + i}});
                order.push_back("write " + id);
                return id;
            });
            jobs.push([&, id, i] {
                interaction::annotateCapture(dir, id, "note " + std::to_string(i), 4000 + i);
                order.push_back("note " + id);
                return std::string();
            });
        }
        check(jobs.size() == 6, "six queued jobs");
        jobs.drain();
        check(order == std::vector<std::string>{"write " + ids[0], "note " + ids[0], "write " + ids[1], "note " + ids[1],
                                                "write " + ids[2], "note " + ids[2]},
              "rapid captures keep order");
        index = lines(dir / "index.jsonl");
        check(index.size() == 5 && Json::parse(index[2])["note"] == "note 0" && Json::parse(index[4])["note"] == "note 2",
              "index in capture order");
        // A failing job reports its error and the queue continues.
        jobs.push([]() -> std::string { throw std::runtime_error("disk full"); });
        jobs.push([] { return std::string("after"); });
        std::vector<interaction::SerialJobs::Done> done;
        while (jobs.size())
            if (auto d = jobs.poll())
                done.push_back(*d);
            else
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        check(done.size() == 2 && done[0].error == "disk full" && done[1].result == "after", "errors do not stall the queue");
        std::filesystem::remove_all(dir);
        std::cout << "capture notes: validation, JSON/index/notes.md, empty note, UTF-8, bound, rapid ordering\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
