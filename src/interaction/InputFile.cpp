#include "InputFile.hpp"
#include <algorithm>
#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
namespace hyprune::interaction {
Json readJSON(const std::filesystem::path &p, size_t budget) {
    int fd = open(p.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        throw std::runtime_error("cannot open " + p.string());
    struct stat st{};
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != getuid() || st.st_size < 0 ||
        size_t(st.st_size) > budget) {
        close(fd);
        throw std::runtime_error("unsafe/oversize JSON file");
    }
    std::string text(st.st_size, '\0');
    size_t n = 0;
    while (n < text.size()) {
        auto r = ::read(fd, text.data() + n, text.size() - n);
        if (r <= 0) {
            close(fd);
            throw std::runtime_error("JSON read failed");
        }
        n += r;
    }
    close(fd);
    return Json::parse(text);
}
void atomicJSON(const std::filesystem::path &p, const Json &value, size_t budget) {
    auto text = value.dump(2) + "\n";
    if (text.size() > budget)
        throw std::runtime_error("JSON persistence budget");
    std::filesystem::create_directories(p.parent_path());
    auto tmp = p.string() + ".tmp-XXXXXX";
    std::vector<char> name(tmp.begin(), tmp.end());
    name.push_back(0);
    int fd = mkostemp(name.data(), O_CLOEXEC);
    if (fd < 0)
        throw std::runtime_error("cannot stage JSON");
    size_t n = 0;
    while (n < text.size()) {
        auto r = write(fd, text.data() + n, text.size() - n);
        if (r <= 0) {
            close(fd);
            unlink(name.data());
            throw std::runtime_error("JSON write failed");
        }
        n += r;
    }
    if (fsync(fd)) {
        close(fd);
        unlink(name.data());
        throw std::runtime_error("JSON sync failed");
    }
    close(fd);
    if (rename(name.data(), p.c_str())) {
        unlink(name.data());
        throw std::runtime_error("JSON commit failed");
    }
    int dir = open(p.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir >= 0) {
        fsync(dir);
        close(dir);
    }
}
InputFile::InputFile(std::string name, Json defaults, std::function<Json(const Json &)> validator)
    : current(std::move(defaults)), validate(std::move(validator)) {
    if (!validate)
        validate = [](const Json &j) {
            auto next = validateInput(j);
            if (!inputConflicts(next).empty())
                throw std::runtime_error("conflicting Hyprune actions in one mode");
            return next;
        };
    std::string environment = "HYPRUNE_" + name + "_FILE"; // HYPRUNE_INPUT_FILE, _GRAPHICS_FILE, _PLAYER_FILE
    std::transform(environment.begin(), environment.end(), environment.begin(), [](unsigned char c) { return std::toupper(c); });
    if (auto p = getenv(environment.c_str()))
        path = p;
    else if (auto p = getenv("XDG_CONFIG_HOME"))
        path = std::filesystem::path(p) / ("hyprune/" + name + ".json");
    else if (auto p = getenv("HOME"))
        path = std::filesystem::path(p) / (".config/hyprune/" + name + ".json");
    if (path.empty() || !path.is_absolute())
        throw std::runtime_error("input file path must be absolute");
    std::filesystem::create_directories(path.parent_path());
    if (!std::filesystem::exists(path))
        atomicJSON(path, current);
    watch = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (watch < 0 ||
        inotify_add_watch(watch, path.parent_path().c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE) < 0)
        throw std::runtime_error("cannot watch input config");
    try {
        if (auto j = read())
            current = *j;
    } catch (const std::exception &e) {
        error = e.what();
    }
}
InputFile::~InputFile() {
    if (watch >= 0)
        close(watch);
}
std::optional<Json> InputFile::read() {
    auto j = validate(readJSON(path));
    auto encoded = j.dump();
    error.clear();
    if (encoded == bytes)
        return {};
    bytes = encoded;
    return j;
}
std::optional<Json> InputFile::poll() {
    alignas(inotify_event) char b[8192];
    bool changed = false;
    ssize_t n;
    while ((n = ::read(watch, b, sizeof b)) > 0)
        for (size_t i = 0; i < size_t(n);) {
            auto e = reinterpret_cast<inotify_event *>(b + i);
            if (e->len && path.filename() == e->name)
                changed = true;
            i += sizeof(inotify_event) + e->len;
        }
    if (!changed)
        return {};
    try {
        auto j = read();
        if (j)
            current = *j;
        return j;
    } catch (const std::exception &e) {
        error = e.what();
        return {};
    }
}
void InputFile::save(const Json &j) {
    auto next = validate(j);
    if (next == current && error.empty())
        return;
    atomicJSON(path, next);
    current = next;
    bytes = next.dump();
    error.clear();
}
} // namespace hyprune::interaction
