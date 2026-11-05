#include "dream/translator/analysis/constprop.h"

#include "dream/translator/sh4/decoder.h"

namespace dream::translator {
namespace {

using sh4::Instr;
using sh4::Op;

// Instructions that never write a general register (the `n` field is a source, an FP register or
// an address). Anything else is assumed to write Rn.
bool reads_only(const Instr& i) {
    switch (i.op) {
        case Op::MOV_B_S:
        case Op::MOV_W_S:
        case Op::MOV_L_S:
        case Op::MOV_B_S_R0:
        case Op::MOV_W_S_R0:
        case Op::MOV_L_S_R0:
        case Op::MOV_B_S_DISP0:
        case Op::MOV_W_S_DISP0:
        case Op::MOV_L_S_DISP:
        case Op::MOV_B_S_GBR:
        case Op::MOV_W_S_GBR:
        case Op::MOV_L_S_GBR:
        case Op::MOVCA_L:
        case Op::FMOV_STORE:
        case Op::FMOV_STORE_R0:
        case Op::CMP_EQ:
        case Op::CMP_EQ_I:
        case Op::CMP_HS:
        case Op::CMP_GE:
        case Op::CMP_HI:
        case Op::CMP_GT:
        case Op::CMP_STR:
        case Op::CMP_PZ:
        case Op::CMP_PL:
        case Op::TST:
        case Op::TST_I:
        case Op::TST_B_GBR:
        case Op::AND_B_GBR:
        case Op::OR_B_GBR:
        case Op::XOR_B_GBR:
        case Op::DIV0S:
        case Op::DIV0U:
        case Op::MUL_L:
        case Op::MULS_W:
        case Op::MULU_W:
        case Op::DMULS_L:
        case Op::DMULU_L:
        case Op::JSR:
        case Op::JMP:
        case Op::BRAF:
        case Op::BSRF:
        case Op::BSR:
        case Op::BRA:
        case Op::BT:
        case Op::BF:
        case Op::BT_S:
        case Op::BF_S:
        case Op::RTS:
        case Op::RTE:
        case Op::NOP:
        case Op::TRAPA:
        case Op::PREF:
        case Op::OCBI:
        case Op::OCBP:
        case Op::OCBWB:
        case Op::CLRT:
        case Op::SETT:
        case Op::CLRS:
        case Op::SETS:
        case Op::CLRMAC:
        case Op::LDTLB:
        case Op::SLEEP:
        case Op::TAS_B:
        case Op::LDS_FPSCR:
        case Op::LDS_FPUL:
        case Op::LDS_MACH:
        case Op::LDS_MACL:
        case Op::LDS_PR:
        case Op::LDC_SR:
        case Op::LDC_GBR:
        case Op::LDC_VBR:
        case Op::LDC_SSR:
        case Op::LDC_SPC:
        case Op::LDC_DBR:
        case Op::LDC_SGR:
        case Op::LDC_BANK:
        case Op::FADD:
        case Op::FSUB:
        case Op::FMUL:
        case Op::FDIV:
        case Op::FMAC:
        case Op::FCMP_EQ:
        case Op::FCMP_GT:
        case Op::FABS:
        case Op::FNEG:
        case Op::FSQRT:
        case Op::FSRRA:
        case Op::FLDI0:
        case Op::FLDI1:
        case Op::FLDS:
        case Op::FSTS:
        case Op::FLOAT:
        case Op::FTRC:
        case Op::FCNVSD:
        case Op::FCNVDS:
        case Op::FSCA:
        case Op::FIPR:
        case Op::FTRV:
        case Op::FMOV:
        case Op::FSCHG:
        case Op::FRCHG:
        case Op::FMOV_LOAD:
        case Op::FMOV_LOAD_R0:
            return true;
        default:
            return false;
    }
}

// The register file the walk builds: a value per register, and whether the block wrote it at all.
// Writes go through operator[] and reads through at(), so every case that assigns or resets a
// register marks it written without a line of its own.
struct Regs {
    std::optional<std::uint32_t> v[16];
    bool written[16] = {};
    std::optional<std::uint32_t>& operator[](unsigned k) {
        written[k & 15] = true;
        return v[k & 15];
    }
    const std::optional<std::uint32_t>& at(unsigned k) const { return v[k & 15]; }
};

}  // namespace

BlockConstant block_constant(const Image& img, std::uint32_t block_start, std::uint32_t pc,
                             unsigned reg, bool alias_or) {
    Regs r;
    auto known = [&](unsigned k) { return r.at(k).has_value(); };
    auto val = [&](unsigned k) { return *r.at(k); };
    for (std::uint32_t p = block_start; p < pc; p += 2) {
        if (!img.contains(p, 2))
            return {RegState::Unknown, 0};  // the block runs off the image: nothing it built holds
        const Instr i = sh4::decode(img.read16(p));
        const unsigned n = i.n, m = i.m;
        switch (i.op) {
            case Op::MOV_I:
                r[n] = static_cast<std::uint32_t>(i.imm);
                break;
            case Op::MOV_L_PCREL: {
                const std::uint32_t a = sh4::pcrel_target(i, p);
                if (img.contains(a, 4))
                    r[n] = img.read32(a);
                else
                    r[n].reset();
                break;
            }
            case Op::MOV_W_PCREL: {
                const std::uint32_t a = sh4::pcrel_target(i, p);
                if (img.contains(a, 2))
                    r[n] = static_cast<std::uint32_t>(
                        static_cast<std::int32_t>(static_cast<std::int16_t>(img.read16(a))));
                else
                    r[n].reset();
                break;
            }
            case Op::MOVA:
                r[0] = sh4::pcrel_target(i, p);
                break;
            case Op::MOV:
                r[n] = r.at(m);
                break;
            case Op::ADD_I:
                if (known(n))
                    r[n] = val(n) + static_cast<std::uint32_t>(i.imm);
                else
                    r[n].reset();
                break;
            case Op::ADD:
                if (known(n) && known(m))
                    r[n] = val(n) + val(m);
                else
                    r[n].reset();
                break;
            case Op::SUB:
                if (known(n) && known(m))
                    r[n] = val(n) - val(m);
                else
                    r[n].reset();
                break;
            case Op::OR:
                if (known(n) && known(m))
                    r[n] = val(n) | val(m);
                else if (alias_or && known(n) && !known(m))
                    r[n] = val(n);  // the alias bits the unknown operand adds are discarded anyway
                else
                    r[n].reset();
                break;
            case Op::AND:
                if (known(n) && known(m))
                    r[n] = val(n) & val(m);
                else
                    r[n].reset();
                break;
            case Op::XOR:
                if (known(n) && known(m))
                    r[n] = val(n) ^ val(m);
                else
                    r[n].reset();
                break;
            case Op::OR_I:
                if (n == 0 && known(0))
                    r[0] = val(0) | static_cast<std::uint32_t>(i.imm & 0xFF);
                else
                    r[0].reset();
                break;
            case Op::AND_I:
                if (known(0))
                    r[0] = val(0) & static_cast<std::uint32_t>(i.imm & 0xFF);
                else
                    r[0].reset();
                break;
            case Op::XOR_I:
                if (known(0))
                    r[0] = val(0) ^ static_cast<std::uint32_t>(i.imm & 0xFF);
                else
                    r[0].reset();
                break;
            case Op::SHLL:
            case Op::SHAL:
                if (known(n))
                    r[n] = val(n) << 1;
                else
                    r[n].reset();
                break;
            case Op::SHLL2:
                if (known(n))
                    r[n] = val(n) << 2;
                else
                    r[n].reset();
                break;
            case Op::SHLL8:
                if (known(n))
                    r[n] = val(n) << 8;
                else
                    r[n].reset();
                break;
            case Op::SHLL16:
                if (known(n))
                    r[n] = val(n) << 16;
                else
                    r[n].reset();
                break;
            case Op::SHLR:
                if (known(n))
                    r[n] = val(n) >> 1;
                else
                    r[n].reset();
                break;
            case Op::SHLR2:
                if (known(n))
                    r[n] = val(n) >> 2;
                else
                    r[n].reset();
                break;
            case Op::SHLR8:
                if (known(n))
                    r[n] = val(n) >> 8;
                else
                    r[n].reset();
                break;
            case Op::SHLR16:
                if (known(n))
                    r[n] = val(n) >> 16;
                else
                    r[n].reset();
                break;
            case Op::SHAR:
                if (known(n))
                    r[n] = static_cast<std::uint32_t>(static_cast<std::int32_t>(val(n)) >> 1);
                else
                    r[n].reset();
                break;
            case Op::NOT:
                if (known(m))
                    r[n] = ~val(m);
                else
                    r[n].reset();
                break;
            case Op::NEG:
                if (known(m))
                    r[n] = 0u - val(m);
                else
                    r[n].reset();
                break;
            case Op::EXTU_B:
                if (known(m))
                    r[n] = val(m) & 0xFFu;
                else
                    r[n].reset();
                break;
            case Op::EXTU_W:
                if (known(m))
                    r[n] = val(m) & 0xFFFFu;
                else
                    r[n].reset();
                break;
            case Op::EXTS_B:
                if (known(m))
                    r[n] = static_cast<std::uint32_t>(static_cast<std::int8_t>(val(m)));
                else
                    r[n].reset();
                break;
            case Op::EXTS_W:
                if (known(m))
                    r[n] = static_cast<std::uint32_t>(static_cast<std::int16_t>(val(m)));
                else
                    r[n].reset();
                break;
            case Op::SWAP_W:
                if (known(m))
                    r[n] = (val(m) >> 16) | (val(m) << 16);
                else
                    r[n].reset();
                break;
            case Op::SWAP_B:
                if (known(m)) {
                    const std::uint32_t v = val(m);
                    r[n] = (v & 0xFFFF0000u) | ((v >> 8) & 0xFFu) | ((v & 0xFFu) << 8);
                } else
                    r[n].reset();
                break;
            case Op::DT:
                if (known(n))
                    r[n] = val(n) - 1;
                else
                    r[n].reset();
                break;
            // Post-increment loads and MAC write the pointer(s) as well as the destination.
            // Post-increment loads: the destination is unknown, the pointer advances.
            case Op::MOV_B_LI:
                r[n].reset();
                if (n != m) {
                    if (known(m))
                        r[m] = val(m) + 1;
                    else
                        r[m].reset();
                }
                break;
            case Op::MOV_W_LI:
                r[n].reset();
                if (n != m) {
                    if (known(m))
                        r[m] = val(m) + 2;
                    else
                        r[m].reset();
                }
                break;
            case Op::MOV_L_LI:
                r[n].reset();
                if (n != m) {
                    if (known(m))
                        r[m] = val(m) + 4;
                    else
                        r[m].reset();
                }
                break;
            case Op::MAC_L:
            case Op::MAC_W:
                r[n].reset();
                r[m].reset();
                break;
            case Op::FMOV_LOAD_INC:
                r[m].reset();
                break;
            // Pre-decrement stores move a known pointer by the access size; FMOV depends on SZ.
            case Op::MOV_B_SD:
                if (known(n))
                    r[n] = val(n) - 1;
                else
                    r[n].reset();
                break;
            case Op::MOV_W_SD:
                if (known(n))
                    r[n] = val(n) - 2;
                else
                    r[n].reset();
                break;
            case Op::MOV_L_SD:
                if (known(n))
                    r[n] = val(n) - 4;
                else
                    r[n].reset();
                break;
            case Op::FMOV_STORE_DEC:
                r[n].reset();
                break;
            default:
                if (!reads_only(i))
                    r[n].reset();
                break;
        }
    }
    const unsigned k = reg & 15;
    if (!r.written[k])
        return {RegState::Untouched, 0};
    if (r.at(k))
        return {RegState::Constant, *r.at(k)};
    return {RegState::Unknown, 0};
}

std::optional<std::uint32_t> constant_at(const Image& img, std::uint32_t block_start,
                                         std::uint32_t pc, unsigned reg, bool alias_or) {
    const BlockConstant c = block_constant(img, block_start, pc, reg, alias_or);
    if (c.state == RegState::Constant)
        return c.value;
    return std::nullopt;
}

}  // namespace dream::translator
