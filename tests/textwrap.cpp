// Caption word wrap (render/TextWrap.hpp): a `say` line up to 160 characters in at most four
// lines, words kept whole, long words broken between characters, never inside UTF-8.
#include "render/TextWrap.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
using hyprune::render::wrapText;
namespace {
void check(bool value, const std::string &why) {
    if (!value)
        throw std::runtime_error(why);
}
float measure(const std::string &s) {
    return 11.f * s.size();
} // 11 px a byte, like the label font at scale 1
bool validUtf8(const std::string &s) {
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i];
        size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!n || i + n > s.size())
            return false;
        for (size_t k = 1; k < n; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xc0) != 0x80)
                return false;
        i += n;
    }
    return true;
}
} // namespace
int main() {
    try {
        const float limit = 508; // a caption bubble: 520 px less the panel padding, 46 characters a line
        std::string say = "The north walk runs along the old freight line. Mind the step by the signal box, "
                          "and wait for me at the far end, right where the tall lamps stand lit.";
        check(say.size() == 150, "a 150-character say line");
        bool cut = true;
        auto lines = wrapText(say, limit, measure, 4, &cut);
        check(!cut && lines.size() >= 3 && lines.size() <= 4, "a 150-character line fits four lines, nothing cut");
        std::string joined;
        for (auto &l : lines) {
            check(measure(l) <= limit, "every line within the limit: " + l);
            check(l.find("...") == std::string::npos, "no ellipsis");
            joined += (joined.empty() ? "" : " ") + l;
        }
        check(joined == say, "all words, in order, nothing lost");
        // A word longer than a line breaks between characters.
        auto word = wrapText(std::string(60, 'x'), limit, measure, 4);
        check(word.size() == 2 && word[0].size() == 46 && word[1].size() == 14, "a long word breaks at the limit");
        // UTF-8: two-byte letters are never split.
        std::string umlauts;
        for (int k = 0; k < 40; ++k)
            umlauts += "\xc3\xa4"; // ä
        for (auto &l : wrapText(umlauts, limit, measure, 4))
            check(validUtf8(l) && measure(l) <= limit, "a multi-byte word breaks between characters");
        // More than four lines: the fourth ends in "..." within the limit.
        std::ostringstream longer;
        for (int k = 0; k < 60; ++k)
            longer << "word" << k << ' ';
        auto four = wrapText(longer.str(), limit, measure, 4, &cut);
        check(cut && four.size() == 4 && four[3].ends_with("...") && measure(four[3]) <= limit,
              "overflow: four lines, the last cut");
        check(wrapText("", limit, measure, 4).empty() &&
                  wrapText("Hello.", limit, measure, 4) == std::vector<std::string>{"Hello."},
              "empty and short lines");
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
    std::cout << "PASS caption wrap: 150 characters in four lines, long words, UTF-8, overflow\n";
}
