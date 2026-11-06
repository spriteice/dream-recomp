#include <chrono>
#include <cstdint>

#include "dream/runtime/boot/pace.h"

#include "doctest.h"

using dream::boot::Pacer;
using namespace std::chrono_literals;

namespace {
using Clock = Pacer::Clock;
using Duration = Pacer::Duration;
constexpr std::uint64_t kFrameCycles = 200000000ull / 60;  // one 60 Hz frame of the guest's clock
const Duration kFrame = Pacer::guest_duration(kFrameCycles);
const Clock::time_point t0{Duration(1'000'000'000)};  // any fixed instant

bool near(Duration a, Duration b) {
    const auto d = a > b ? a - b : b - a;
    return d < 1ms;
}
}  // namespace

TEST_CASE("pacer: a frame ahead of the wall clock sleeps the difference") {
    Pacer p(t0);
    CHECK(near(p.plan(kFrameCycles, t0 + 5ms), kFrame - 5ms));
    CHECK(p.origin() == t0);
}

TEST_CASE("pacer: a frame a little late is made up by the next, not forgiven") {
    Pacer p(t0);
    // 10 ms behind: no sleep, and the reference stays, so the run catches up over the next frames.
    CHECK(p.plan(kFrameCycles, t0 + kFrame + 10ms) == Duration::zero());
    CHECK(p.origin() == t0);
    // The next frame comes 6 ms after it, which is early for the guest's clock: the catch-up ends.
    CHECK(near(p.plan(2 * kFrameCycles, t0 + kFrame + 16ms), 2 * kFrame - (kFrame + 16ms)));
}

TEST_CASE("pacer: a stall is dropped rather than paid back with a sprint") {
    Pacer p(t0);
    CHECK(near(p.plan(kFrameCycles, t0 + 5ms), kFrame - 5ms));
    // The second frame arrives half a second late: something stalled the host.
    const auto late = t0 + 2 * kFrame + 500ms;
    CHECK(p.plan(2 * kFrameCycles, late) == Duration::zero());
    // The reference moved by the lost time, so the frame after is paced again. Before this class
    // the launcher kept the old reference and the guest ran flat out for half a second of guest
    // time to catch up.
    CHECK(p.origin() == late - Pacer::guest_duration(2 * kFrameCycles));
    CHECK(p.origin() == t0 + 500ms);
    CHECK(near(p.plan(3 * kFrameCycles, late + 10ms), kFrame - 10ms));
}

TEST_CASE("pacer: time the guest was stopped for is owed back") {
    Pacer p(t0);
    p.paused(2s);
    CHECK(near(p.plan(kFrameCycles, t0 + 2s + 5ms), kFrame - 5ms));
    CHECK(p.origin() == t0 + 2s);
}

TEST_CASE("pacer: a guest more than a second ahead is re-based, not slept off") {
    Pacer p(t0);
    const std::uint64_t three_seconds = 3 * 200000000ull;
    CHECK(p.plan(three_seconds, t0 + 100ms) == Duration::zero());
    CHECK(p.origin() == t0 + 100ms - 3s);
    // and from there it is paced as usual
    CHECK(near(p.plan(three_seconds + kFrameCycles, t0 + 100ms + 5ms), kFrame - 5ms));
}

TEST_CASE("pacer: the guest's cycles convert at 200 MHz") {
    CHECK(Pacer::guest_duration(200000000ull) == 1s);
    CHECK(Pacer::guest_duration(kFrameCycles) == 16666665ns);
    CHECK(Pacer::guest_duration(1) == 5ns);
}
