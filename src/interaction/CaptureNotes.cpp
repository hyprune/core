#include "CaptureNotes.hpp"
#include "InputFile.hpp"
#include <chrono>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <unistd.h>

namespace hyprune::interaction {
namespace {
// Count code points of strict UTF-8 (no overlongs, surrogates or > U+10FFFF); -1 if invalid
// or if a control character (C0, DEL, C1) appears.
long codePoints(const std::string &s) {
    long count = 0;
    for (size_t i = 0; i < s.size(); ++count) {
        unsigned char c = s[i];
        uint32_t cp;
        size_t n;
        if (c < 0x80)
            cp = c, n = 1;
        else if ((c & 0xe0) == 0xc0)
            cp = c & 0x1f, n = 2;
        else if ((c & 0xf0) == 0xe0)
            cp = c & 0x0f, n = 3;
        else if ((c & 0xf8) == 0xf0)
            cp = c & 0x07, n = 4;
        else
            return -1;
        if (i + n > s.size())
            return -1;
        for (size_t k = 1; k < n; ++k) {
            unsigned char d = s[i + k];
            if ((d & 0xc0) != 0x80)
                return -1;
            cp = cp << 6 | (d & 0x3f);
        }
        static constexpr uint32_t minimum[] = {0, 0, 0x80, 0x800, 0x10000};
        if (cp < minimum[n] || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
            return -1;
        if (cp < 0x20 || (cp >= 0x7f && cp < 0xa0))
            return -1;
        i += n;
    }
    return count;
}
// One write(2) per line on an O_APPEND descriptor: concurrent readers never see half a line.
void appendLine(const std::filesystem::path &path, const std::string &line) {
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd < 0)
        throw std::runtime_error("cannot open " + path.filename().string());
    auto r = write(fd, line.data(), line.size());
    close(fd);
    if (r != ssize_t(line.size()))
        throw std::runtime_error("cannot append " + path.filename().string());
}
} // namespace

bool validCaptureId(const std::string &id) {
    static const std::regex pattern("capture-[0-9]{1,19}");
    return std::regex_match(id, pattern);
}

std::string captureNote(const nlohmann::json &value) {
    if (!value.is_string())
        throw std::invalid_argument("note must be a string");
    auto note = value.get<std::string>();
    auto n = codePoints(note);
    if (n < 0)
        throw std::invalid_argument("note must be UTF-8 text without control characters");
    if (size_t(n) > CaptureNoteLimit)
        throw std::invalid_argument("note is longer than 1000 characters");
    return note;
}

void annotateCapture(const std::filesystem::path &dir, const std::string &id, const std::string &note,
                     int64_t noteTimeMs) {
    if (!validCaptureId(id))
        throw std::invalid_argument("invalid capture ID");
    auto path = dir / (id + ".json");
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error(id + ".json is missing");
    auto value = nlohmann::json::parse(in);
    value["note"] = note;
    value["noteTimeMs"] = noteTimeMs;
    atomicJSON(path, value, 4 * 1048576 + 65536);
    nlohmann::json line = {{"timestampMs", value.value("timestampMs", int64_t(0))},
                           {"noteTimeMs", noteTimeMs},
                           {"capture", id + ".json"},
                           {"screenshot", id + ".png"},
                           {"note", note}};
    appendLine(dir / "index.jsonl", line.dump() + "\n");
    if (!note.empty()) {
        std::time_t seconds = noteTimeMs / 1000;
        std::tm local{};
        localtime_r(&seconds, &local);
        char when[32];
        std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &local);
        appendLine(dir / "notes.md", std::string("- ") + when + " · " + id + ".png — " + note + "\n");
    }
}

std::optional<SerialJobs::Done> SerialJobs::poll() {
    std::optional<Done> done;
    if (running_.valid() && running_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        done = Done{};
        try {
            done->result = running_.get();
        } catch (const std::exception &e) {
            done->error = e.what();
        }
    }
    if (!running_.valid() && !pending_.empty()) {
        running_ = std::async(std::launch::async, std::move(pending_.front()));
        pending_.pop_front();
    }
    return done;
}

void SerialJobs::drain() {
    while (size()) {
        if (running_.valid())
            running_.wait();
        poll();
    }
}
} // namespace hyprune::interaction
