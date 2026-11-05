#include <cstdint>
#include <initializer_list>
#include <string>

#include "dream/translator/analysis/discover.h"

#include "doctest.h"

using dream::translator::DiscoverOptions;
using dream::translator::Image;

namespace {

void put16(Image& img, std::uint16_t v) {
    img.bytes.push_back(static_cast<std::uint8_t>(v));
    img.bytes.push_back(static_cast<std::uint8_t>(v >> 8));
}

void put32(Image& img, std::uint32_t v) {
    put16(img, static_cast<std::uint16_t>(v));
    put16(img, static_cast<std::uint16_t>(v >> 16));
}

const dream::translator::DiscoveredFunction* find(const dream::translator::DiscoverResult& r,
                                                  std::uint32_t entry) {
    for (const auto& f : r.functions)
        if (f.entry == entry)
            return &f;
    return nullptr;
}

}  // namespace

TEST_CASE("discover: a call found after the pointer pass keeps the callee whole") {
    // Only literal pointers reach anything. The pointer pass queues H, F and G in address order
    // and walks them in reverse, so G, a pointer into the middle of F, is a function before F is
    // walked; F, walked as a pointer target, stops at G. H then calls F, which is a stronger
    // reason than a pointer to believe F starts there, and a call target is not cut by a pointer
    // entry inside it.
    //   +00 .long H / +04 .long 0
    //   +08 H: mov.l lit,r1 / jsr @r1 / nop / rts / nop / nop / +14 lit: .long F
    //   +18 F: mov #1,r0 / +1A G: mov #2,r1 / mov #3,r2 / rts / nop / +22 nop / +24 .long G
    Image img;
    img.base = 0x8C010000u;
    const std::uint32_t h = img.base + 0x08, f = img.base + 0x18, g = img.base + 0x1A;
    put32(img, h);
    put32(img, 0);
    for (std::uint16_t w :
         std::initializer_list<std::uint16_t>{0xD102, 0x410B, 0x0009, 0x000B, 0x0009, 0x0009})
        put16(img, w);
    put32(img, f);
    for (std::uint16_t w :
         std::initializer_list<std::uint16_t>{0xE001, 0xE102, 0xE203, 0x000B, 0x0009, 0x0009})
        put16(img, w);
    put32(img, g);

    DiscoverOptions opt;
    opt.aggressive_sweep = false;
    const auto r = dream::translator::discover(img, opt);
    const auto* fn = find(r, f);
    REQUIRE(fn != nullptr);
    CHECK(fn->origin == "call");
    CHECK(fn->end == img.base + 0x22);
    CHECK(r.upgraded == 1);
    REQUIRE(find(r, g) != nullptr);  // still a function of its own: the pointer is real
    CHECK(find(r, g)->origin == "pointer");
}

TEST_CASE("discover: a pointer to a long function with no early exit is code") {
    // +00 .long F / +04 F: 250 x `mov #1,r0`, then rts / nop. Nothing else reaches F.
    Image img;
    img.base = 0x8C010000u;
    const std::uint32_t f = img.base + 4;
    constexpr unsigned kBodyInstructions = 250;  // past the 200-instruction window this replaces
    put32(img, f);
    for (unsigned i = 0; i < kBodyInstructions; ++i) put16(img, 0xE001);
    put16(img, 0x000B);
    put16(img, 0x0009);

    DiscoverOptions opt;
    opt.aggressive_sweep = false;
    const auto r = dream::translator::discover(img, opt);
    const auto* fn = find(r, f);
    REQUIRE(fn != nullptr);
    CHECK(fn->origin == "pointer");
    CHECK(fn->end == f + 2 * (kBodyInstructions + 2));
}

TEST_CASE("discover: a register the block reloads is not a call target from its predecessors") {
    // F builds r2 from a literal and branches to L on a flag; the fall-through returns, so L is a
    // block of its own with F as its only predecessor. L reloads r2 from memory before `jsr @r2`,
    // so what F put in r2 is gone, and F's constant must not name G as the callee. With the reload
    // replaced by a nop, F's constant does reach the call and G is found through it.
    //   +00 F: mov.l lit,r2 / tst r0,r0 / bt L / rts / nop / nop
    //   +0C L: mov.l @r2,r2 (or nop) / jsr @r2 / nop / rts / nop / nop
    //   +18 lit: .long G / +1C G: rts / nop
    const auto build = [](bool reload) {
        Image img;
        img.base = 0x8C010000u;
        const std::uint32_t g = img.base + 0x1C;
        for (std::uint16_t w :
             std::initializer_list<std::uint16_t>{0xD205, 0x2008, 0x8902, 0x000B, 0x0009, 0x0009})
            put16(img, w);
        put16(img, reload ? 0x6222 : 0x0009);
        for (std::uint16_t w :
             std::initializer_list<std::uint16_t>{0x420B, 0x0009, 0x000B, 0x0009, 0x0009})
            put16(img, w);
        put32(img, g);
        put16(img, 0x000B);
        put16(img, 0x0009);
        return img;
    };
    DiscoverOptions opt;
    opt.seeds = {0x8C010000u};
    opt.follow_pointers = false;
    opt.aggressive_sweep = false;

    SUBCASE("the reload hides what the predecessor built") {
        const Image img = build(true);
        const auto r = dream::translator::discover(img, opt);
        CHECK(find(r, img.base + 0x1C) == nullptr);
        REQUIRE(r.unresolved.size() == 1);
        CHECK(r.unresolved[0].address == img.base + 0x0E);
        CHECK(r.unresolved[0].in_function == img.base);
        CHECK(std::string(r.unresolved[0].op) == "JSR");
        CHECK(std::string(r.unresolved[0].reason) == dream::translator::kReasonNoConstant);
    }
    SUBCASE("without the reload the predecessor's constant is the target") {
        const Image img = build(false);
        const auto r = dream::translator::discover(img, opt);
        const auto* fn = find(r, img.base + 0x1C);
        REQUIRE(fn != nullptr);
        CHECK(fn->origin == "call");
        CHECK(r.unresolved.empty());
    }
}
