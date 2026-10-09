#pragma once
#include "ipc/Server.hpp"
#include <algorithm>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
namespace hyprune::daemon {
// World-derived default homes are published but never saved as user data.
inline ipc::Json durableLayout(ipc::Json mounts, const ipc::Json &derived) {
    std::erase_if(mounts.at("bindings").get_ref<ipc::Json::array_t &>(), [&](auto &b) {
        return std::find(derived.begin(), derived.end(), b.at("id")) != derived.end();
    });
    return mounts;
}
// Only hypruned reads/writes state; packages and compositor callbacks never do.
class LayoutStore {
    std::filesystem::path path;
    ipc::Json last = {{"bindings", ipc::Json::array()}, {"placements", ipc::Json::array()}};

  public:
    LayoutStore() {
        if (const char *override = getenv("HYPRUNE_LAYOUT_FILE"))
            path = override;
        else if (const char *state = getenv("XDG_STATE_HOME"))
            path = std::filesystem::path(state) / "hyprune/layout.json";
        else if (const char *home = getenv("HOME"))
            path = std::filesystem::path(home) / ".local/state/hyprune/layout.json";
        if (path.empty())
            throw std::runtime_error("no layout state path");
        std::filesystem::create_directories(path.parent_path());
        int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0)
            return;
        struct stat st{};
        if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != getuid() || st.st_size > 65536) {
            close(fd);
            throw std::runtime_error("unsafe layout file");
        }
        std::string data(st.st_size, '\0');
        size_t off = 0;
        while (off < data.size()) {
            auto n = read(fd, data.data() + off, data.size() - off);
            if (n <= 0) {
                close(fd);
                throw std::runtime_error("layout read failed");
            }
            off += n;
        }
        close(fd);
        auto candidate = ipc::Json::parse(data);
        if (candidate.value("version", 0) != 1)
            throw std::runtime_error("unsupported layout version");
        candidate.erase("version");
        last = candidate;
    }
    ipc::Json load() const { return last; }
    void save(const ipc::Json &mounts) {
        ipc::Json value = {{"bindings", mounts.at("bindings")}, {"placements", mounts.at("placements")}};
        if (value == last)
            return;
        auto encoded = value;
        encoded["version"] = 1;
        auto bytes = encoded.dump(2) + "\n";
        if (bytes.size() > 65536)
            throw std::runtime_error("layout persistence budget");
        auto temp = path.string() + ".tmp-" + ipc::randomId();
        int fd = open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd < 0)
            throw std::runtime_error("cannot stage layout");
        size_t off = 0;
        while (off < bytes.size()) {
            auto n = write(fd, bytes.data() + off, bytes.size() - off);
            if (n <= 0) {
                close(fd);
                unlink(temp.c_str());
                throw std::runtime_error("layout write failed");
            }
            off += n;
        }
        if (fsync(fd)) {
            close(fd);
            unlink(temp.c_str());
            throw std::runtime_error("layout sync failed");
        }
        close(fd);
        if (rename(temp.c_str(), path.c_str())) {
            unlink(temp.c_str());
            throw std::runtime_error("layout commit failed");
        }
        int dir = open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (dir >= 0) {
            fsync(dir);
            close(dir);
        }
        last = value;
    }
};
} // namespace hyprune::daemon
