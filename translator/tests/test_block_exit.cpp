// A block's exit is written once, where the block ends, so nothing follows an unconditional jump
// out of it. MSVC rejects code after one (C4702, an error under /WX): the windows-x64 CI job failed
// on p13_slotcase, where the not-taken path of a bt/s whose slot is a switch case was followed by a
// dead "falls off the end of the function range" call.
#include <cstdint>
#include <initializer_list>
#include <sstream>
#include <string>
#include <vector>

#include "dream/translator/emit.h"

#include "doctest.h"

using dream::translator::emit_unit;
using dream::translator::EmitOptions;
using dream::translator::FunctionSpec;
using dream::translator::Image;

namespace {

constexpr std::uint32_t kBase = 0x8C010000;
// The word of tests/sh4/programs/p13_slotcase.s at +0x14: bt/s .Lout, whose slot (+0x16) is the
// switch's second case.
constexpr std::size_t kBranchWord = 0x14 / 2;

// p13_slotcase.s, assembled (tests/sh4/README.md has the commands).
std::vector<std::uint16_t> slotcase() {
    return {0xE101, 0x3416, 0x890B, 0xD107, 0x6043, 0x4008, 0x031E, 0x432B,
            0xE000, 0x2558, 0x8D01, 0x7001, 0x700A, 0x000B, 0x0009, 0x000B,
            0xE0FF, 0x0009, 0x0028, 0x8C01, 0x0012, 0x8C01, 0x0016, 0x8C01};
}

std::string emit(const std::vector<std::uint16_t>& words) {
    Image img;
    img.base = kBase;
    for (std::uint16_t w : words) {
        img.bytes.push_back(static_cast<std::uint8_t>(w));
        img.bytes.push_back(static_cast<std::uint8_t>(w >> 8));
    }
    FunctionSpec f;
    f.entry = kBase;
    f.end = kBase + 0x24;
    f.name = "slotcase";
    return emit_unit(img, {f}, EmitOptions{}).source;
}

std::size_t indent(const std::string& line) {
    const auto p = line.find_first_not_of(' ');
    return p == std::string::npos ? line.size() : p;
}

bool starts(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

bool ends(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Statements of a function body that no path reaches: anything after an unconditional `goto` or
// `return` at the same level, before the next label. A braced block, or a switch whose every arm
// jumps, exits when its last statement does. Indentation gives the nesting, four spaces a level,
// as the emitter writes it.
std::vector<std::string> unreachable(const std::string& source) {
    struct Level {
        bool exited = false;
        bool is_switch = false, conditional = false;
        bool every_arm_exits = true, has_default = false;
    };
    std::vector<std::string> found;
    std::vector<Level> levels;
    std::istringstream in(source);
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t ind = indent(line);
        const std::string text = line.substr(ind);
        if (text.empty() || starts(text, "//"))
            continue;
        if (ind == 0) {
            // A label is a way in, so code after it is reachable again; anything else at column 0
            // opens or closes a function.
            levels.assign(ends(text, "{") ? 1 : 0, Level{});
            if (ends(text, ":") || ends(text, ":;"))
                levels.assign(1, Level{});
            continue;
        }
        const std::size_t depth = ind / 4;
        if (levels.size() < depth)
            continue;  // not code the emitter nests this way
        if (starts(text, "}")) {
            // Closes the level the line that opened it started: a plain block exits with its last
            // statement, a switch when every arm and its default jump.
            if (levels.size() == depth + 1) {
                const Level inner = levels.back();
                levels.pop_back();
                const bool exits =
                    inner.is_switch ? inner.has_default && inner.every_arm_exits : inner.exited;
                if (!inner.conditional && exits)
                    levels.back().exited = true;
            }
            continue;
        }
        levels.resize(depth);
        Level& here = levels.back();
        if (here.is_switch && (starts(text, "case ") || starts(text, "default:"))) {
            const bool jumps = text.find("goto ") != std::string::npos || ends(text, "return;");
            here.every_arm_exits = here.every_arm_exits && jumps;
            here.has_default = here.has_default || starts(text, "default:");
            continue;
        }
        if (here.exited) {
            found.push_back(line);
            continue;
        }
        const bool conditional = starts(text, "if ") || starts(text, "if(");
        if (!conditional && (starts(text, "goto ") || ends(text, "return;")))
            here.exited = true;
        if (ends(text, "{")) {
            Level inner;
            inner.is_switch = starts(text, "switch ");
            inner.conditional = conditional;
            levels.push_back(inner);
        }
    }
    return found;
}

std::string joined(const std::vector<std::string>& lines) {
    std::string s;
    for (const auto& l : lines) s += l + "\n";
    return s;
}

}  // namespace

TEST_CASE("the not-taken path of a bt/s whose slot is a switch case has one exit") {
    const std::string src = emit(slotcase());
    INFO(src);
    CHECK_MESSAGE(unreachable(src).empty(), joined(unreachable(src)));
    // The not-taken path has run the slot, so it skips the slot's own block.
    CHECK(src.find("goto L_8c010018;") != std::string::npos);
    CHECK(src.find("falls off the end of the function range") == std::string::npos);
}

TEST_CASE("a bra whose slot is a switch case is followed by nothing") {
    // +0x14 becomes `bra .Lout` (0xA001): entering at the slot still runs it and falls through.
    auto words = slotcase();
    words[kBranchWord] = 0xA001;
    const std::string src = emit(words);
    INFO(src);
    CHECK_MESSAGE(unreachable(src).empty(), joined(unreachable(src)));
    CHECK(src.find("goto L_8c01001a;") != std::string::npos);
}

TEST_CASE("a call whose slot is a switch case returns past the slot's block") {
    // +0x14 becomes `bsr .Lout` (0xB001): the call returns to +0x18, after the slot, so its block
    // still needs its exit.
    auto words = slotcase();
    words[kBranchWord] = 0xB001;
    const std::string src = emit(words);
    INFO(src);
    CHECK_MESSAGE(unreachable(src).empty(), joined(unreachable(src)));
    CHECK(src.find("goto L_8c010018;") != std::string::npos);
    CHECK(src.find("falls off the end of the function range") == std::string::npos);
}
