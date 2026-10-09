#include "ipc/Text.hpp"
#include "surfaces/Layout.hpp"
#include <iostream>
using namespace hyprune;
using ipc::boundedText;
using ipc::codePoints;
namespace {
void check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
std::string repeat(const std::string &unit, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; i++)
        s += unit;
    return s;
}
bool strict(const std::string &s) {
    try {
        (void)world::Json(s).dump(); // nlohmann rejects invalid UTF-8 by default
        return true;
    } catch (const world::Json::exception &) {
        return false;
    }
}
// A ~10k code point title mixing 1-, 2-, 3- and 4-byte code points.
std::string hostile() {
    return repeat("terminal \xc3\xa9\xe6\xbc\xa2\xf0\x9f\x98\x80 ", 770);
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--frames") {
            // Frames for tests/wire-text.py: real schema validation per protocol.
            auto title = hostile();
            for (int minor = 1; minor <= 15; minor++) {
                world::Json changes = {{"surfaces",
                                        {{{"id", "surface-1"},
                                          {"kind", "window"},
                                          {"title", boundedText(title, ipc::TitleLimit)},
                                          {"logicalSize", {1280, 720}},
                                          {"focused", false}}}}};
                if (minor >= 5)
                    changes["apps"] = {{{"surfaceId", "surface-1"},
                                        {"workspaceId", 20},
                                        {"title", boundedText(title, ipc::TitleLimit)},
                                        {"appClass", boundedText(title, ipc::ClassLimit)},
                                        {"icon", boundedText(title, ipc::ClassLimit)}}};
                world::Json frame = {{"jsonrpc", "2.0"},
                                     {"method", "state.delta"},
                                     {"params",
                                      {{"subscriptionId", "s"},
                                       {"sessionId", "t"},
                                       {"baseRevision", 1},
                                       {"revision", 2},
                                       {"changes", changes}}}};
                std::cout
                    << world::Json{{"protocol", "0." + std::to_string(minor)}, {"method", ""}, {"frame", frame}}.dump()
                    << "\n";
            }
            world::Json notice = {{"jsonrpc", "2.0"},
                                  {"method", "runtime.notice"},
                                  {"params",
                                   {{"level", "warning"},
                                    {"code", "placement.relocated"},
                                    {"message", boundedText("Relocated " + title, ipc::MessageLimit)}}}};
            std::cout << world::Json{{"protocol", "0.15"}, {"method", ""}, {"frame", notice}}.dump() << "\n";
            return 0;
        }
        // Short, exact-limit and empty strings are untouched.
        check(boundedText("kitty", 512) == "kitty" && boundedText("", 4) == "", "short text changed");
        check(boundedText(repeat("a", 512), 512) == repeat("a", 512), "exact limit changed");
        // Over the limit: exactly `limit` code points, ellipsis last, prefix kept.
        auto a = boundedText(repeat("a", 10000), 512);
        check(codePoints(a) == 512 && a.ends_with("…") && a.starts_with(repeat("a", 511)), "ascii clamp");
        for (std::string unit : {"\xc3\xa9", "\xe6\xbc\xa2", "\xf0\x9f\x98\x80"}) {
            auto s = boundedText(repeat(unit, 3000), 256);
            check(codePoints(s) == 256 && strict(s) && s == repeat(unit, 255) + "…", "multibyte clamp");
            check(boundedText(repeat(unit, 256), 256) == repeat(unit, 256), "multibyte exact limit");
        }
        auto mixed = boundedText(hostile(), ipc::TitleLimit);
        check(codePoints(mixed) == ipc::TitleLimit && strict(mixed) && hostile().starts_with(mixed.substr(0, 400)),
              "mixed clamp");
        // Invalid UTF-8 never reaches the wire: stray continuation, overlong,
        // surrogate, out of range and a sequence cut at the end of input.
        for (std::string bad : {"a\x80z", "a\xc0\x80z", "a\xed\xa0\x80z", "a\xf4\x90\x80\x80z", "az\xe6\xbc"}) {
            auto s = boundedText(bad, 64);
            check(strict(s) && s.starts_with("a") && s.find("�") != std::string::npos, "invalid UTF-8 kept");
        }
        check(codePoints(boundedText(repeat("\xff", 9000), 512)) == 512, "invalid bytes counted unbounded");
        check(boundedText("abc", 0) == "", "zero limit");

        // Placement rules hold the bounded identity and still match the live window.
        surfaces::Layout layout;
        surfaces::Instance window{"surface-1", hostile(), hostile(), 20};
        window.title = boundedText(window.title, ipc::TitleLimit); // as Capture ingests it
        window.appClass = boundedText(window.appClass, ipc::ClassLimit);
        world::World w;
        w.id = "org.hyprune.text";
        w.areas = world::Json::array({{{"id", "home"}, {"workspaceSlot", "01"}, {"primaryAnchor", "wall"}}});
        surfaces::Surface plane;
        plane.width = 1.6f;
        plane.height = .9f;
        layout.mount(window, &w, plane);
        auto &rule = layout.placements.at(0).at("rule");
        check(codePoints(rule.at("title").get<std::string>()) == ipc::RuleLimit &&
                  codePoints(rule.at("appClass").get<std::string>()) == ipc::RuleLimit,
              "placement rule unbounded");
        check(layout.hasPlacement(window, &w), "bounded rule no longer matches its window");
        check(strict(layout.state(&w).dump()), "layout state not valid UTF-8");
        std::cout << "PASS wire text: UTF-8 safe code-point clamps, invalid bytes, bounded placement rules\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
