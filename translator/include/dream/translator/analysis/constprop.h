// Block-local constant propagation (WP1.4/WP3.1): the value a register holds at `pc`, when it was
// built inside the block from immediates, literal-pool loads, `mova` and simple arithmetic. Used
// for call targets (`jsr @rN` after `mov.l @(disp,pc)`, or the position-independent SHC idiom
// `mov.l off,rN ; mova base,r0 ; add r0,rN ; or r14,rN ; jsr @rN`) and for FPSCR loads.
#pragma once

#include <cstdint>
#include <optional>

#include "dream/translator/emit.h"

namespace dream::translator {

// What a block has done to a register by `pc`. Untouched: no instruction in [block_start, pc)
// wrote it, so it holds whatever came in, and a caller may ask the predecessors. Constant: the
// block built the value given. Unknown: the block wrote it from something this analysis cannot
// see, a load or an untraced register, so neither the block nor its predecessors can say what it
// holds. Telling the last two apart is what keeps a predecessor's constant from standing in for a
// value the block has since replaced.
enum class RegState { Untouched, Constant, Unknown };

struct BlockConstant {
    RegState state = RegState::Untouched;
    std::uint32_t value = 0;  // set when state is Constant
};

// Walks [block_start, pc) forward. With `alias_or`, an OR with an unknown register keeps the known
// operand: the only such OR in call sequences sets P1/P2 alias bits, which the alias-normalising
// callers discard anyway.
BlockConstant block_constant(const Image& img, std::uint32_t block_start, std::uint32_t pc,
                             unsigned reg, bool alias_or);

// The value alone: nothing when the register is not a compile-time constant at `pc`, whether the
// block left it alone or wrote it from something unknown.
std::optional<std::uint32_t> constant_at(const Image& img, std::uint32_t block_start,
                                         std::uint32_t pc, unsigned reg, bool alias_or);

}  // namespace dream::translator
