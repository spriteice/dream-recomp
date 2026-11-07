// Host floating-point environment control, driven by the guest's FPSCR (ADR 16).
//
// The SH-4 exposes two things the host must mirror for bit-exact results: the rounding mode
// (FPSCR.RM: 00 round to nearest, 01 round toward zero) and denormal flushing (FPSCR.DN: 1 means
// denormalised inputs and results are treated as zero). x86-64 implements these in MXCSR (RC bits,
// FTZ and DAZ), ARM64 in FPCR (RMode, FZ). Nothing else in the runtime touches those registers.
//
// The SH-4's reset FPSCR, which Katana's startup stub also sets, is 0x00040001: DN set, RM = toward
// zero. A disc's bootstrap hands a game 0x00040000, round to nearest with DN set, and a game that
// never sets RM keeps that (docs/boot-state.md). Either way the guest's mode is not the host's
// default, which is why this layer exists at all: a host running with its own rounding would
// disagree with the console on the last bit of almost every division.
#pragma once

#include <cstdint>

namespace dream::fenv {

enum class Rounding : std::uint8_t { Nearest = 0, TowardZero = 1 };

struct Mode {
    Rounding rounding = Rounding::TowardZero;
    bool flush_denormals = true;

    friend bool operator==(const Mode&, const Mode&) = default;
};

inline constexpr std::uint32_t kFpscrRmMask = 0x3u;
inline constexpr std::uint32_t kFpscrDnBit = 1u << 18;
inline constexpr std::uint32_t kKatanaDefaultFpscr = 0x00040001u;

// Decode the two relevant fields of an FPSCR value. RM values 10 and 11 are reserved on SH-4; they
// are mapped to toward-zero, matching hardware behaviour as documented by Renesas.
constexpr Mode from_fpscr(std::uint32_t fpscr) noexcept {
    return Mode{(fpscr & kFpscrRmMask) == 0 ? Rounding::Nearest : Rounding::TowardZero,
                (fpscr & kFpscrDnBit) != 0};
}

// Program the host FP control register(s) for the calling thread.
void apply(Mode mode) noexcept;

// Read back what the host is currently set to (for tests and for the interrupt path, which saves
// and restores the mode around a handler).
Mode current() noexcept;

// Host defaults: nearest, no flushing. Call when handing control back to host library code that
// expects a sane environment (e.g. the renderer).
void reset_host() noexcept;

// The host's defaults for as long as it lives, and the mode it found put back when it ends, on
// every path out, an exception's included. The guest's mode belongs to guest code only: host code
// that runs on the guest's thread, in a hook or after the run, would otherwise round its own
// arithmetic toward zero and flush its denormals. That showed first as the run report printing
// 0.996 of real time as "0.9x" (printf follows the rounding mode), but it reaches everything a
// hook runs: the window, the GPU driver, the pacer, the controls, the sound queue.
class HostScope {
public:
    HostScope() noexcept : saved_(current()) { reset_host(); }
    ~HostScope() { apply(saved_); }
    HostScope(const HostScope&) = delete;
    HostScope& operator=(const HostScope&) = delete;

private:
    Mode saved_;
};

}  // namespace dream::fenv
