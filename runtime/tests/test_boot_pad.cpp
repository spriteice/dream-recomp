#include <cstddef>
#include <cstdint>

#include "dream/runtime/boot/pad_frame.h"

#include "doctest.h"

using dream::boot::Pad;
using dream::boot::pad_for_frame;
using dream::boot::Presses;
using dream::boot::Replay;

TEST_CASE("pad_for_frame: the live controls of the frame are in the state that gets recorded") {
    // --record-input writes frame f's line from the state right after this call, so the controls
    // read for frame f must already be in it. Read after the line, as the windowed launcher once
    // did, every recording came out one frame late.
    Pad state;
    std::size_t next = 0;
    const auto live = [](Pad& p) {
        p.buttons = 0xFFF7;  // start
        p.joy_x = 200;
    };
    pad_for_frame(state, 10, live, {}, next, {});
    CHECK(state.buttons == 0xFFF7);
    CHECK(state.joy_x == 200);
}

TEST_CASE(
    "pad_for_frame: a recording being played back is the input, and the controls are not read") {
    Pad a;
    a.buttons = 0xFFFB;  // A
    const Replay rec{{5, a}};
    Pad state;
    std::size_t next = 0;
    bool read = false;
    const auto live = [&read](Pad&) { read = true; };
    pad_for_frame(state, 4, live, rec, next, {});
    CHECK(state.buttons == 0xFFFF);
    CHECK(next == 0);
    pad_for_frame(state, 5, live, rec, next, {});
    CHECK(state.buttons == 0xFFFB);
    CHECK(next == 1);
    pad_for_frame(state, 6, live, rec, next, {});
    CHECK(state.buttons == 0xFFFB);  // a line holds until the next one
    CHECK_FALSE(read);
}

TEST_CASE(
    "pad_for_frame: a scripted press is held eight frames over the live controls, then lets go") {
    const Presses presses{{100, dream::maple::kStart}};
    const std::uint16_t start_held = static_cast<std::uint16_t>(0xFFFFu & ~dream::maple::kStart);
    Pad state;
    std::size_t next = 0;
    const auto live = [](Pad& p) { p.buttons = 0xFFFB; };  // the player holds A throughout
    pad_for_frame(state, 99, live, {}, next, presses);
    CHECK(state.buttons == 0xFFFB);
    pad_for_frame(state, 100, live, {}, next, presses);
    CHECK(state.buttons == start_held);
    pad_for_frame(state, 107, live, {}, next, presses);
    CHECK(state.buttons == start_held);
    pad_for_frame(state, 108, live, {}, next, presses);
    CHECK(state.buttons == 0xFFFB);
    // Headless, nothing else produces input: the buttons are released when the press ends.
    Pad headless;
    pad_for_frame(headless, 100, nullptr, {}, next, presses);
    CHECK(headless.buttons == start_held);
    pad_for_frame(headless, 108, nullptr, {}, next, presses);
    CHECK(headless.buttons == 0xFFFF);
}
