// Real-time pacing for a windowed run. The guest's clock is the reference: the run sleeps while
// it is ahead of the wall clock, and time it has lost is dropped rather than paid back. The
// launcher used to pace against the moment the window opened and only ever slept when ahead, so
// after any stall (a shader compiled on first use, a slow first present under a compositor) the
// guest was behind and ran as fast as the host could until it had caught up, which a player saw as
// the game running too fast and would hear as the sound racing. A frame that took too long is
// gone; pretending otherwise only moves the damage. It is a class of its own so the arithmetic can
// be tested with a clock the test controls.
#pragma once

#include <chrono>
#include <cstdint>

namespace dream::boot {

class Pacer {
public:
    using Clock = std::chrono::steady_clock;
    using Duration = Clock::duration;
    static constexpr std::uint64_t kNanosecondsPerCycle = 5;  // the SH-4's 200 MHz clock
    // Behind the wall clock by more than this and the lost time is dropped. A little over two
    // frames, so ordinary jitter (a frame late, made up by the next) still averages out and a
    // stall does not.
    static constexpr Duration kMaxDebt = std::chrono::milliseconds(40);
    // Ahead by more than this is not a run to slow down but a reference that is wrong (one of the
    // clocks jumped): re-based rather than slept off.
    static constexpr Duration kMaxLead = std::chrono::seconds(1);

    explicit Pacer(Clock::time_point origin) noexcept : origin_(origin) {}

    // The guest was stopped for `d` of host time (the binding screen): owed back, never caught up.
    void paused(Duration d) noexcept { origin_ += d; }

    // Once per guest frame, with the guest's cycle count and the host time now: how long to sleep
    // before the guest runs on. Zero when it is level or behind; behind by more than kMaxDebt
    // moves the reference forward so the run continues at the guest's own pace from here.
    Duration plan(std::uint64_t guest_cycles, Clock::time_point now) noexcept {
        const Clock::time_point target = origin_ + guest_duration(guest_cycles);
        if (target > now) {
            const Duration lead = target - now;
            if (lead > kMaxLead) {
                origin_ -= lead;
                return Duration::zero();
            }
            return lead;
        }
        const Duration debt = now - target;
        if (debt > kMaxDebt)
            origin_ += debt;
        return Duration::zero();
    }

    // The host time that stands for guest time zero, as the plan above has moved it.
    Clock::time_point origin() const noexcept { return origin_; }

    // Integer arithmetic, so the answer doesn't depend on the floating-point rounding mode, which
    // another part of the program (or a test) may have set.
    static Duration guest_duration(std::uint64_t cycles) noexcept {
        return std::chrono::duration_cast<Duration>(
            std::chrono::nanoseconds(static_cast<std::int64_t>(cycles * kNanosecondsPerCycle)));
    }

private:
    Clock::time_point origin_;
};

}  // namespace dream::boot
