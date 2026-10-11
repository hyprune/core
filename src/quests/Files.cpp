#include "Files.hpp"
#include <cstdio>
#include <fcntl.h>
#include <random>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace hyprune::quests {
namespace {
std::filesystem::path base(const char *xdg, const char *fallback) {
    if (const char *v = getenv(xdg); v && *v == '/')
        return std::filesystem::path(v) / "hyprune";
    if (const char *home = getenv("HOME"))
        return std::filesystem::path(home) / fallback / "hyprune";
    throw std::runtime_error("no home directory");
}
void directories(const std::filesystem::path &dir) {
    std::filesystem::create_directories(dir);
    std::filesystem::permissions(dir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
}
void writeAll(int fd, const std::string &bytes) {
    size_t off = 0;
    while (off < bytes.size()) {
        auto n = write(fd, bytes.data() + off, bytes.size() - off);
        if (n <= 0)
            throw std::runtime_error("write failed");
        off += n;
    }
}
} // namespace
std::filesystem::path stateHome() {
    return base("XDG_STATE_HOME", ".local/state");
}
std::filesystem::path configHome() {
    return base("XDG_CONFIG_HOME", ".config");
}
std::optional<std::string> readPrivate(const std::filesystem::path &path, size_t limit) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        if (errno == ENOENT)
            return std::nullopt;
        throw std::runtime_error("cannot open " + path.filename().string());
    }
    struct stat st{};
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != getuid() || size_t(st.st_size) > limit) {
        close(fd);
        throw std::runtime_error("unsafe or oversized " + path.filename().string());
    }
    std::string data(st.st_size, '\0');
    size_t off = 0;
    while (off < data.size()) {
        auto n = read(fd, data.data() + off, data.size() - off);
        if (n <= 0) {
            close(fd);
            throw std::runtime_error("cannot read " + path.filename().string());
        }
        off += n;
    }
    close(fd);
    return data;
}
void writeAtomic(const std::filesystem::path &path, const std::string &bytes) {
    directories(path.parent_path());
    static std::mt19937_64 random{std::random_device{}()};
    auto temp = path.string() + ".tmp-" + std::to_string(random());
    int fd = open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        throw std::runtime_error("cannot stage " + path.filename().string());
    try {
        writeAll(fd, bytes);
        if (fsync(fd))
            throw std::runtime_error("sync failed");
    } catch (...) {
        close(fd);
        unlink(temp.c_str());
        throw;
    }
    close(fd);
    if (rename(temp.c_str(), path.c_str())) {
        unlink(temp.c_str());
        throw std::runtime_error("cannot commit " + path.filename().string());
    }
}
void appendPrivate(const std::filesystem::path &path, const std::string &line) {
    directories(path.parent_path());
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        throw std::runtime_error("cannot open " + path.filename().string());
    try {
        writeAll(fd, line);
    } catch (...) {
        close(fd);
        throw;
    }
    close(fd);
}
std::string localTime(std::time_t t) {
    std::tm tm{};
    localtime_r(&t, &tm);
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%S", &tm);
    return text;
}
std::optional<std::time_t> parseLocalTime(const std::string &text) {
    std::tm tm{};
    int seconds = 0, used = 0;
    if (text.size() != 16 && text.size() != 19)
        return std::nullopt;
    if (std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d%n", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour,
                    &tm.tm_min, &used) != 5 ||
        used != 16)
        return std::nullopt;
    if (text.size() == 19 && (text[16] != ':' || std::sscanf(text.c_str() + 17, "%2d", &seconds) != 1))
        return std::nullopt;
    if (tm.tm_mon < 1 || tm.tm_mon > 12 || tm.tm_mday < 1 || tm.tm_mday > 31 || tm.tm_hour > 23 || tm.tm_min > 59 ||
        seconds > 59 || tm.tm_year < 1970 || tm.tm_year > 9999)
        return std::nullopt;
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_sec = seconds;
    tm.tm_isdst = -1;
    auto t = std::mktime(&tm);
    if (t == -1)
        return std::nullopt;
    return t;
}
} // namespace hyprune::quests
