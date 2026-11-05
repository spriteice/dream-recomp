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

TEST_CASE("discover: a predecessor's constant is read at the edge, not at the end of its walk") {
    // F loads G into r2, branches to L on a flag, then loads H into r2 and returns. L calls through
    // r2. The only way into L is the branch, where r2 holds G; H is what F leaves in r2 on the path
    // that never reaches L. The walk from F runs straight on past the branch to the `rts`, so the
    // code it walked is not a basic block: asked at its end, F says H.
    //   +00 F: mov.l litG,r2 / tst r0,r0 / bt L / mov.l litH,r2 / rts / nop
    //   +0C L: jsr @r2 / nop / rts / nop
    //   +14 litG: .long G / +18 litH: .long H / +1C G: rts / nop / +20 H: rts / nop
    Image img;
    img.base = 0x8C010000u;
    const std::uint32_t g = img.base + 0x1C, h = img.base + 0x20;
    for (std::uint16_t w : std::initializer_list<std::uint16_t>{
             0xD204, 0x2008, 0x8902, 0xD204, 0x000B, 0x0009, 0x420B, 0x0009, 0x000B, 0x0009})
        put16(img, w);
    put32(img, g);
    put32(img, h);
    for (std::uint16_t w : std::initializer_list<std::uint16_t>{0x000B, 0x0009, 0x000B, 0x0009})
        put16(img, w);
    DiscoverOptions opt;
    opt.seeds = {0x8C010000u};
    opt.follow_pointers = false;
    opt.aggressive_sweep = false;
    const auto r = dream::translator::discover(img, opt);
    const auto* fn = find(r, g);
    REQUIRE(fn != nullptr);
    CHECK(fn->origin == "call");
    CHECK(find(r, h) == nullptr);
    CHECK(r.unresolved.empty());
}

TEST_CASE("discover: a call reached two ways takes both callees, whichever way was walked first") {
    // F loads G into r2 and goes two ways: a conditional branch and an unconditional one, one to K
    // and one to J. K loads H into r2 and falls into J, which calls through r2. Both G and H are
    // real callees. Which arm goes where decides whether the walk reaches J first (then K's
    // fall-through into code already walked was no edge at all) or K first (then J's call sat
    // inside K's walk and read H from it, blind to the branch that arrives with G).
    //   +00 F: mov.l litG,r2 / tst r0,r0 / bt X / bra Y / nop
    //   +0A K: mov.l litH,r2 / nop / nop
    //   +10 J: jsr @r2 / nop / rts / nop
    //   +18 litG: .long G / +1C litH: .long H / +20 G: rts / nop / +24 H: rts / nop
    const auto build = [](bool branch_to_k) {
        Image img;
        img.base = 0x8C010000u;
        for (std::uint16_t w : std::initializer_list<std::uint16_t>{0xD205, 0x2008}) put16(img, w);
        put16(img, branch_to_k ? 0x8901 : 0x8904);  // bt K / bt J
        put16(img, branch_to_k ? 0xA003 : 0xA000);  // bra J / bra K
        for (std::uint16_t w : std::initializer_list<std::uint16_t>{0x0009, 0xD204, 0x0009, 0x0009,
                                                                    0x420B, 0x0009, 0x000B, 0x0009})
            put16(img, w);
        put32(img, img.base + 0x20);
        put32(img, img.base + 0x24);
        for (std::uint16_t w : std::initializer_list<std::uint16_t>{0x000B, 0x0009, 0x000B, 0x0009})
            put16(img, w);
        return img;
    };
    DiscoverOptions opt;
    opt.seeds = {0x8C010000u};
    opt.follow_pointers = false;
    opt.aggressive_sweep = false;
    for (bool branch_to_k : {true, false}) {
        CAPTURE(branch_to_k);
        const Image img = build(branch_to_k);
        const auto r = dream::translator::discover(img, opt);
        const auto* g = find(r, img.base + 0x20);
        const auto* h = find(r, img.base + 0x24);
        REQUIRE(g != nullptr);
        REQUIRE(h != nullptr);
        CHECK(g->origin == "call");
        CHECK(h->origin == "call");
        CHECK(r.unresolved.empty());
    }
}
