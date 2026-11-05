#include <cstdint>
#include <initializer_list>

#include "dream/translator/analysis/switch.h"

#include "doctest.h"

using dream::translator::Image;
using dream::translator::recover_switch;
using dream::translator::SwitchTable;

namespace {

// SHC's absolute-table dispatch, as in Ikaruga's 0x0C08FE94:
//   +00 shll2 r1 / +02 mova tbl,r0 / +04 mov.l @(r0,r1),r0 / +06 jmp @r0 / +08 nop
//   +0C tbl: two entries / +14 rts, nop / +18 rts, nop
Image absolute_table(std::uint32_t base, std::uint32_t entry_segment) {
    Image img;
    img.base = base;
    auto put16 = [&](std::uint16_t v) {
        img.bytes.push_back(static_cast<std::uint8_t>(v));
        img.bytes.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    for (std::uint16_t w :
         std::initializer_list<std::uint16_t>{0x4108, 0xC702, 0x001E, 0x402B, 0x0009, 0x0000})
        put16(w);
    for (std::uint32_t off : {0x14u, 0x18u}) {
        const std::uint32_t entry = ((base + off) & 0x1FFFFFFFu) | entry_segment;
        put16(static_cast<std::uint16_t>(entry));
        put16(static_cast<std::uint16_t>(entry >> 16));
    }
    for (std::uint16_t w : std::initializer_list<std::uint16_t>{0x000B, 0x0009, 0x000B, 0x0009})
        put16(w);
    return img;
}

bool recover(const Image& img, SwitchTable& sw) {
    const std::uint32_t jump_pc = img.base + 6;
    return recover_switch(img, img.base, jump_pc, dream::sh4::decode(img.read16(jump_pc)), img.base,
                          img.base + static_cast<std::uint32_t>(img.bytes.size()), sw);
}

}  // namespace

TEST_CASE("switch: absolute P1 entries are rebased into an image modelled at 0x0C") {
    const Image img = absolute_table(0x0C010000u, 0x80000000u);
    SwitchTable sw;
    REQUIRE(recover(img, sw));
    CHECK(sw.table == 0x0C01000Cu);
    CHECK(sw.rebased);
    REQUIRE(sw.targets.size() == 2);
    CHECK(sw.targets[0] == 0x0C010014u);
    CHECK(sw.targets[1] == 0x0C010018u);
}

TEST_CASE("switch: absolute entries in the image's own segment are not rebased") {
    const Image img = absolute_table(0x8C010000u, 0x80000000u);
    SwitchTable sw;
    REQUIRE(recover(img, sw));
    CHECK_FALSE(sw.rebased);
    REQUIRE(sw.targets.size() == 2);
    CHECK(sw.targets[0] == 0x8C010014u);
}

TEST_CASE("switch: P4 entries are not an alias of RAM") {
    const Image img = absolute_table(0x0C010000u, 0xE0000000u);
    SwitchTable sw;
    CHECK_FALSE(recover(img, sw));
}

TEST_CASE("switch: the index check bounds the table") {
    // +00 mov #1,r2 / +02 cmp/hi r2,r1 / +04 nop / +06 shll2 r1 / +08 mova tbl,r0
    // +0A mov.l @(r0,r1),r0 / +0C jmp @r0 / +0E nop / +14 tbl: three in-range words / +20.. rts
    Image img;
    img.base = 0x8C010000u;
    auto put16 = [&](std::uint16_t v) {
        img.bytes.push_back(static_cast<std::uint8_t>(v));
        img.bytes.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    for (std::uint16_t w : std::initializer_list<std::uint16_t>{
             0xE201, 0x3126, 0x0009, 0x4108, 0xC702, 0x001E, 0x402B, 0x0009, 0x0000, 0x0000})
        put16(w);
    for (std::uint32_t off : {0x20u, 0x24u, 0x28u}) {
        put16(static_cast<std::uint16_t>(img.base + off));
        put16(static_cast<std::uint16_t>((img.base + off) >> 16));
    }
    for (std::uint16_t w :
         std::initializer_list<std::uint16_t>{0x000B, 0x0009, 0x000B, 0x0009, 0x000B, 0x0009})
        put16(w);
    const std::uint32_t jump_pc = img.base + 0x0C;
    SwitchTable sw;
    REQUIRE(recover_switch(img, img.base, jump_pc, dream::sh4::decode(img.read16(jump_pc)),
                           img.base, img.base + static_cast<std::uint32_t>(img.bytes.size()), sw));
    REQUIRE(sw.targets.size() == 2);
    CHECK(sw.targets[1] == 0x8C010024u);
}

TEST_CASE("switch: a bounded table keeps its cases inside a range that ends before others") {
    // tests/sh4/programs/p14_fragcase.s: mov #2,r1 / cmp/hs r1,r4 / bt bad / mov r4,r0 / shll r0
    // / mov r0,r1 / mova tbl,r0 / mov.w @(r0,r1),r0 / braf r0 / nop / +14 tbl: case 0 at +20 and
    // case 1 at +18 / +18 near / +1C bad / +20 far. The range [+00, +20) is the fragment.
    Image img;
    img.base = 0x8C040000u;
    for (std::uint16_t w : std::initializer_list<std::uint16_t>{
             0xE102, 0x3412, 0x890A, 0x6043, 0x4000, 0x6103, 0xC701, 0x001D, 0x0023, 0x0009, 0x000C,
             0x0004, 0x000B, 0xE014, 0x000B, 0xE0FF, 0x000B, 0xE00A}) {
        img.bytes.push_back(static_cast<std::uint8_t>(w));
        img.bytes.push_back(static_cast<std::uint8_t>(w >> 8));
    }
    const std::uint32_t jump_pc = img.base + 0x10;
    const auto jump = dream::sh4::decode(img.read16(jump_pc));
    SwitchTable sw;
    REQUIRE(recover_switch(img, img.base + 6, jump_pc, jump, img.base, img.base + 0x20, sw));
    REQUIRE(sw.targets.size() == 1);
    CHECK(sw.targets[0] == 0x8C040018u);
    // With the whole program as the range, both cases are local.
    SwitchTable all;
    REQUIRE(recover_switch(img, img.base + 6, jump_pc, jump, img.base,
                           img.base + static_cast<std::uint32_t>(img.bytes.size()), all));
    REQUIRE(all.targets.size() == 2);
    CHECK(all.targets[0] == 0x8C040020u);
    CHECK(all.targets[1] == 0x8C040018u);
}
