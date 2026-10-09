#pragma once
#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
namespace hyprune::ipc {
// Wire limits for strings core projects from outside its control (Wayland
// titles/classes, compositor names, user config, file errors). They mirror the
// IPC schema `maxLength`s, identical in every version 0.1-0.15.
inline constexpr size_t TitleLimit = 512, ClassLimit = 256, RuleLimit = 256, OutputNameLimit = 128,
                        DescriptionLimit = 1024, SubmapLimit = 256, MessageLimit = 4096, PathLimit = 4096,
                        FailureLimit = 512, RecordingErrorLimit = 256;
// Code points as JSON Schema maxLength counts them (valid UTF-8 input).
inline size_t codePoints(std::string_view text) {
    size_t n = 0;
    for (unsigned char c : text)
        n += (c & 0xc0) != 0x80;
    return n;
}
// Valid UTF-8 of at most `limit` code points. Invalid bytes become U+FFFD; a
// longer string keeps whole code points and ends with U+2026 (counted).
inline std::string boundedText(std::string_view text, size_t limit) {
    std::string out;
    out.reserve(std::min(text.size(), limit * 4));
    size_t count = 0, cut = std::string::npos;
    for (size_t i = 0; i < text.size();) {
        auto byte = [&](size_t k) { return i + k < text.size() ? (unsigned char)text[i + k] : 0; };
        auto tail = [&](size_t k, unsigned lo = 0x80, unsigned hi = 0xbf) { return byte(k) >= lo && byte(k) <= hi; };
        unsigned char c = byte(0);
        size_t length = c < 0x80                                                                     ? 1
                        : c >= 0xc2 && c <= 0xdf && tail(1)                                          ? 2
                        : c == 0xe0 && tail(1, 0xa0) && tail(2)                                      ? 3
                        : ((c >= 0xe1 && c <= 0xec) || c == 0xee || c == 0xef) && tail(1) && tail(2) ? 3
                        : c == 0xed && tail(1, 0x80, 0x9f) && tail(2)                                ? 3
                        : c == 0xf0 && tail(1, 0x90) && tail(2) && tail(3)                           ? 4
                        : c >= 0xf1 && c <= 0xf3 && tail(1) && tail(2) && tail(3)                    ? 4
                        : c == 0xf4 && tail(1, 0x80, 0x8f) && tail(2) && tail(3)                     ? 4
                                                                                                     : 0;
        if (++count == limit)
            cut = out.size();
        if (count > limit) {
            out.resize(limit ? cut : 0);
            if (limit)
                out += "…";
            return out;
        }
        if (length) {
            out.append(text.substr(i, length));
            i += length;
        } else {
            out += "�";
            ++i;
        }
    }
    return out;
}
} // namespace hyprune::ipc
