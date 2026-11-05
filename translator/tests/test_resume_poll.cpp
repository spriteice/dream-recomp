// The entry poll of a resumed body must record the resume address, not the entry, as the pc an
// interrupt returns to (docs/emitter-design.md, "Non-local returns").
#include <cstdint>
#include <initializer_list>
#include <string>

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
