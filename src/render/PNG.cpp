#include "PNG.hpp"
#include <fcntl.h>
#include <png.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace hyprune::render {
PNG readAtlasPNG(const std::filesystem::path &path) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat st{};
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != getuid() || st.st_size < 8 ||
        st.st_size > 4 * 1048576) {
        if (fd >= 0)
            close(fd);
        throw std::runtime_error("unsafe/oversized local icon atlas");
    }
    std::vector<uint8_t> bytes(st.st_size);
    size_t done = 0;
    while (done < bytes.size()) {
        auto n = read(fd, bytes.data() + done, bytes.size() - done);
        if (n <= 0) {
            close(fd);
            throw std::runtime_error("atlas read failed");
        }
        done += n;
    }
    close(fd);
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, bytes.data(), bytes.size()))
        throw std::runtime_error("atlas PNG header");
    if (!image.width || !image.height || image.width > 1024 || image.height > 1024) {
        png_image_free(&image);
        throw std::runtime_error("atlas dimensions exceed 1024");
    }
    image.format = PNG_FORMAT_RGBA;
    PNG result{int(image.width), int(image.height), std::vector<uint8_t>(PNG_IMAGE_SIZE(image))};
    if (!png_image_finish_read(&image, nullptr, result.rgba.data(), 0, nullptr)) {
        png_image_free(&image);
        throw std::runtime_error("atlas PNG decode");
    }
    png_image_free(&image);
    return result;
}
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
