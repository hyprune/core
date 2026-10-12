#pragma once
// Word wrap for core-drawn captions (a character's `say` line). Pure: no GL, testable.
#include <functional>
#include <string>
#include <vector>
namespace hyprune::render {
// Splits `text` into lines no wider than `limit` (as `measure` says) at spaces. A word wider
// than the limit breaks between characters (never inside a UTF-8 sequence). At most
// `maxLines` lines; when text remains, the last line ends in "..." within the limit and
// `truncated` is set.
inline std::vector<std::string> wrapText(const std::string &text, float limit,
                                         const std::function<float(const std::string &)> &measure, int maxLines,
                                         bool *truncated = nullptr) {
    std::vector<std::string> lines;
    std::string current;
    auto flush = [&] {
        if (!current.empty())
            lines.push_back(current), current.clear();
    };
    auto continuation = [](char c) { return (static_cast<unsigned char>(c) & 0xc0) == 0x80; };
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == ' ') {
            ++i;
            continue;
        }
        size_t end = text.find(' ', i);
        std::string word = text.substr(i, end == std::string::npos ? std::string::npos : end - i);
        i = end == std::string::npos ? text.size() : end;
        std::string candidate = current.empty() ? word : current + " " + word;
        if (measure(candidate) <= limit) {
            current = std::move(candidate);
            continue;
        }
        flush();
        while (!word.empty() && measure(word) > limit) { // a word longer than a line
            size_t cut = 0, next = 0;
            while (next < word.size()) {
                size_t step = next + 1;
                while (step < word.size() && continuation(word[step]))
                    ++step;
                if (cut > 0 && measure(word.substr(0, step)) > limit)
                    break;
                cut = next = step;
            }
            lines.push_back(word.substr(0, cut));
            word.erase(0, cut);
        }
        current = word;
    }
    flush();
    bool cut = int(lines.size()) > maxLines;
    if (truncated)
        *truncated = cut;
    if (cut) {
        lines.resize(maxLines);
        auto &last = lines.back();
        while (!last.empty() && measure(last + "...") > limit) {
            last.pop_back();
            while (!last.empty() && continuation(last.back()))
                last.pop_back();
            if (!last.empty() && (static_cast<unsigned char>(last.back()) & 0xc0) == 0xc0)
                last.pop_back(); // a lead byte whose continuation went
        }
        last += "...";
    }
    return lines;
}
} // namespace hyprune::render
