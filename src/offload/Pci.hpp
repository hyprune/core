#pragma once
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <sys/sysmacros.h>
namespace hyprune::offload {
// PCI address (e.g. 0000:65:00.0) of the GPU behind a DRM fd, or "".
inline std::string pciOfFd(int fd) {
    struct stat st{};
    if (fd < 0 || fstat(fd, &st) || !S_ISCHR(st.st_mode))
        return {};
    std::error_code error;
    auto device = std::filesystem::canonical(
        "/sys/dev/char/" + std::to_string(major(st.st_rdev)) + ":" + std::to_string(minor(st.st_rdev)) + "/device", error);
    return error ? std::string{} : device.filename().string();
}
} // namespace hyprune::offload
