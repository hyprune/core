#include "PNG.hpp"
#include <fcntl.h>
#include <png.h>
#include <stdexcept>
#include <unistd.h>
namespace hyprune::render {
void writePNG(const std::filesystem::path &path, int width, int height, const std::vector<uint8_t> &rgba) {
    if (width <= 0 || height <= 0 || rgba.size() != size_t(width) * height * 4)
        throw std::runtime_error("invalid capture pixels");
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    image.width = width;
    image.height = height;
    image.format = PNG_FORMAT_RGBA;
    png_alloc_size_t bytes = 0;
    if (!png_image_write_to_memory(&image, nullptr, &bytes, 0, rgba.data(), 0, nullptr))
        throw std::runtime_error(image.message);
    std::vector<uint8_t> encoded(bytes);
    if (!png_image_write_to_memory(&image, encoded.data(), &bytes, 0, rgba.data(), 0, nullptr))
        throw std::runtime_error(image.message);
    std::filesystem::create_directories(path.parent_path());
    auto pattern = path.string() + ".tmp-XXXXXX";
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back(0);
    int fd = mkostemp(name.data(), O_CLOEXEC);
    if (fd < 0)
        throw std::runtime_error("cannot stage PNG");
    size_t done = 0;
    while (done < bytes) {
        auto n = write(fd, encoded.data() + done, bytes - done);
        if (n <= 0) {
            close(fd);
            unlink(name.data());
            throw std::runtime_error("PNG write failed");
        }
        done += n;
    }
    if (fsync(fd)) {
        close(fd);
        unlink(name.data());
        throw std::runtime_error("PNG sync failed");
    }
    close(fd);
    if (rename(name.data(), path.c_str())) {
        unlink(name.data());
        throw std::runtime_error("PNG rename failed");
    }
}
} // namespace hyprune::render
