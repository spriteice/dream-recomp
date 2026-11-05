#include <cstdint>
#include <initializer_list>

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
