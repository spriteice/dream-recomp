// The entry poll of a resumed body must record the resume address, not the entry, as the pc an
// interrupt returns to (docs/emitter-design.md, "Non-local returns").
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "dream/translator/emit.h"

#include "doctest.h"

using dream::translator::emit_unit;
using dream::translator::EmitOptions;
using dream::translator::FunctionSpec;
using dream::translator::Image;

TEST_CASE("a resumed body's entry poll records resume_pc as the interrupted pc") {
    // +00 sts.l pr,@-r15 / +02 bsr +08 / +04 nop / +06 lds.l @r15+,pr / +08 rts / +0A nop
    // +0C rts / +0E nop: one call whose return address (+06) is a resume point.
    Image img;
    img.base = 0x0C010000;
    for (std::uint16_t w : std::initializer_list<std::uint16_t>{0x4F22, 0xB003, 0x0009, 0x4F26,
                                                                0x000B, 0x0009, 0x000B, 0x0009}) {
        img.bytes.push_back(static_cast<std::uint8_t>(w));
        img.bytes.push_back(static_cast<std::uint8_t>(w >> 8));
    }
    FunctionSpec outer;
    outer.entry = 0x0C010000;
    outer.end = 0x0C01000C;
    FunctionSpec inner;
    inner.entry = 0x0C01000C;
    inner.end = 0x0C010010;
    EmitOptions opt;
    const auto r = emit_unit(img, {outer, inner}, opt);
    const std::string poll =
        "if (c.cycles >= c.next_event) { c.pc = resume_pc ? resume_pc : 0x0c010000u; "
        "deliver_irq(c, m); }";
    CHECK(r.source.find(poll) != std::string::npos);
    // The old form named the entry unconditionally.
    CHECK(r.source.find("{ c.pc = 0x0c010000u; deliver_irq(c, m); }") == std::string::npos);
    // The call's return address is a resume label.
    CHECK(r.source.find("case 0x0c010006u:") != std::string::npos);
}

namespace {
// The one-call image above, at `base`.
Image one_call_image(std::uint32_t base) {
    Image img;
    img.base = base;
    for (std::uint16_t w : std::initializer_list<std::uint16_t>{0x4F22, 0xB003, 0x0009, 0x4F26,
                                                                0x000B, 0x0009, 0x000B, 0x0009}) {
        img.bytes.push_back(static_cast<std::uint8_t>(w));
        img.bytes.push_back(static_cast<std::uint8_t>(w >> 8));
    }
    return img;
}

std::vector<FunctionSpec> one_call_specs(std::uint32_t base) {
    FunctionSpec outer;
    outer.entry = base;
    outer.end = base + 0x0C;
    outer.name = "outer";
    FunctionSpec inner;
    inner.entry = base + 0x0C;
    inner.end = base + 0x10;
    inner.name = "inner";
    return {outer, inner};
}
}  // namespace

TEST_CASE("a function's resume labels go in the function table, the same set as its switch") {
    // Ranges overlap, so the runtime chooses the function to resume in by its labels, not its
    // range: the list must hold exactly the addresses the resume switch has cases for.
    const auto r = emit_unit(one_call_image(0x0C010000), one_call_specs(0x0C010000), EmitOptions{});
    CHECK(r.source.find("case 0x0c010000u: goto L_0c010000;") != std::string::npos);
    CHECK(r.source.find("case 0x0c010006u: goto L_0c010006;") != std::string::npos);
    CHECK(r.source.find(
              "static const std::uint32_t outer__labels[] = {0x0c010000u, 0x0c010006u, };") !=
          std::string::npos);
    CHECK(r.source.find("&outer__resume, outer__labels, 2},") != std::string::npos);
    CHECK(r.source.find("&inner__resume, inner__labels, 1},") != std::string::npos);
}

TEST_CASE("a resumed body matches its labels through any RAM alias of the address") {
    // Linked at P1: the switch keeps the linked addresses and rebases P0-P3 to P1, and the table's
    // list holds the physical addresses the runtime looks up by.
    const auto r = emit_unit(one_call_image(0x8C010000), one_call_specs(0x8C010000), EmitOptions{});
    CHECK(r.source.find("switch (resume_pc < 0xE0000000u ? (resume_pc & 0x1FFFFFFFu) | 0x80000000u "
                        ": resume_pc) {") != std::string::npos);
    CHECK(r.source.find("case 0x8c010006u: goto L_8c010006;") != std::string::npos);
    CHECK(r.source.find(
              "static const std::uint32_t outer__labels[] = {0x0c010000u, 0x0c010006u, };") !=
          std::string::npos);
    // The old switch compared the raw address, so 0x0c010006 missed a label linked as 0x8c010006.
    CHECK(r.source.find("switch (resume_pc) {") == std::string::npos);
}
