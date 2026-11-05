// The controller state the title reads in a frame, put together at vblank-out from whatever is
// producing input: the window's live controls, a recording being played back, and scripted
// presses. It is a function of its own so the order can be tested: the line --record-input writes
// for frame f is taken from this state right after it is assembled, so the live controls of frame
// f have to be in it already. The windowed launcher used to read them after writing the line, and
// every recording it made was one frame late: played back, the title saw each input a frame after
// the player gave it, and a play-through that reproduced exactly with every frame number lowered
// by one did not reproduce as recorded.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "dream/runtime/maple/maple.h"

namespace dream::boot {

using Pad = dream::maple::ControllerState;
using Replay = std::vector<std::pair<std::uint64_t, Pad>>;  // --replay-input: frame, state
using Presses = std::vector<std::pair<std::uint64_t, std::uint16_t>>;  // --press: frame, button
constexpr std::uint64_t kPressFrames = 8;  // how long a scripted press is held

// Sets `state` to what the title reads in frame `f`. `live` reads the window's controls when
// there is a window; a recording being played back is the input instead, so `live` is not read
// then. Scripted presses are held for kPressFrames frames over whatever else there is, and when
// nothing else produces input they also let the buttons go once a press ends.
inline void pad_for_frame(Pad& state, std::uint64_t f, const std::function<void(Pad&)>& live,
                          const Replay& replayed, std::size_t& replay_next,
                          const Presses& scripted) {
    if (live && replayed.empty())
        live(state);
    while (replay_next < replayed.size() && replayed[replay_next].first <= f)
        state = replayed[replay_next++].second;
    if (!scripted.empty()) {
        std::uint16_t held = 0;
        for (const auto& [start, bit] : scripted)
            if (f >= start && f < start + kPressFrames)
                held |= bit;
        if (held || !live)
            state.buttons = static_cast<std::uint16_t>(0xFFFFu & ~held);
    }
}

}  // namespace dream::boot
