// What every caller of the window does with a frame's answer (vk/frame_step.h), driven with a
// stand-in for the window: the failures that matter come from a device or a compositor that no
// test can stage.
#include <cstdint>

#include "dream/render/vk/frame_step.h"

#include "doctest.h"

using namespace dream::render::vk;

namespace {

// Answers as it is told, and counts the rebuilds asked of it.
struct StandIn {
    FrameResult begin = FrameResult::Ok;
    FrameResult end = FrameResult::Ok;
    bool rebuild_works = true;
    int rebuilds = 0;

    FrameResult begin_frame(std::uint32_t&, VkCommandBuffer&) { return begin; }
    FrameResult end_frame(std::uint32_t) { return end; }
    bool recreate_swapchain() {
        ++rebuilds;
        return rebuild_works;
    }
};

FrameStart start(StandIn& window) {
    std::uint32_t image = 0;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    return start_frame(window, image, cmd);
}

}  // namespace

TEST_CASE("frame step: an ordinary frame is drawn and handed back, with no rebuild") {
    StandIn window;
    CHECK(start(window) == FrameStart::Draw);
    CHECK(finish_frame(window, 0));
    CHECK(window.rebuilds == 0);
}

TEST_CASE("frame step: a swapchain the window wants rebuilt is rebuilt, and the run goes on") {
    StandIn at_acquire;
    at_acquire.begin = FrameResult::Rebuild;
    CHECK(start(at_acquire) == FrameStart::Skip);
    CHECK(at_acquire.rebuilds == 1);

    StandIn at_present;
    at_present.end = FrameResult::Rebuild;
    CHECK(finish_frame(at_present, 0));
    CHECK(at_present.rebuilds == 1);
}

TEST_CASE("frame step: a failed acquire or present stops the caller without a rebuild") {
    // A lost device or surface. A rebuild over it would replace the window's error with its own,
    // or appear to work and leave the window frozen.
    StandIn at_acquire;
    at_acquire.begin = FrameResult::Fail;
    CHECK(start(at_acquire) == FrameStart::Stop);
    CHECK(at_acquire.rebuilds == 0);

    StandIn at_present;
    at_present.end = FrameResult::Fail;
    CHECK_FALSE(finish_frame(at_present, 0));
    CHECK(at_present.rebuilds == 0);
}

TEST_CASE("frame step: a rebuild that fails stops the caller") {
    StandIn at_acquire;
    at_acquire.begin = FrameResult::Rebuild;
    at_acquire.rebuild_works = false;
    CHECK(start(at_acquire) == FrameStart::Stop);

    StandIn at_present;
    at_present.end = FrameResult::Rebuild;
    at_present.rebuild_works = false;
    CHECK_FALSE(finish_frame(at_present, 0));
}
