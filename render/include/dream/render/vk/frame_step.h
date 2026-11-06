// What the window's frame calls answer, and what every caller does with the answer. The launcher,
// the probe and the viewer each did it by hand, and each could carry on past a failure: a lost
// device or surface, or a swapchain that could not be rebuilt, left a frozen window and nothing on
// the screen to say why. A template over the window, so a test can drive it with a stand-in: the
// sandbox has no Vulkan device, and the failures that matter come from hardware no test can stage.
#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

namespace dream::render::vk {

// What Window::begin_frame() and Window::end_frame() report.
enum class FrameResult {
    Ok,       // begin: the image is acquired and the command buffer open; end: presented
    Rebuild,  // the swapchain is to be rebuilt first: a resize, or a compositor changed the surface
    Fail,     // the window cannot go on, and its error() says why
};

// What a caller does after start_frame(): record the frame, skip it, or stop.
enum class FrameStart { Draw, Skip, Stop };

// Draw: record into `cmd` for `image`, then hand the frame to finish_frame(). Skip: there is no
// image this time, because the swapchain has just been rebuilt or the window is minimised, so try
// again next frame. Stop: the window cannot go on, and window.error() says why; end with it.
template <class Window>
FrameStart start_frame(Window& window, std::uint32_t& image, VkCommandBuffer& cmd) {
    switch (window.begin_frame(image, cmd)) {
        case FrameResult::Ok:
            return FrameStart::Draw;
        case FrameResult::Rebuild:
            return window.recreate_swapchain() ? FrameStart::Skip : FrameStart::Stop;
        case FrameResult::Fail:
            break;
    }
    return FrameStart::Stop;
}

// Submits and presents what start_frame() began, and rebuilds the swapchain after it when asked.
// False when the window cannot go on, with window.error() saying why.
template <class Window>
bool finish_frame(Window& window, std::uint32_t image) {
    switch (window.end_frame(image)) {
        case FrameResult::Ok:
            return true;
        case FrameResult::Rebuild:
            return window.recreate_swapchain();
        case FrameResult::Fail:
            break;
    }
    return false;
}

}  // namespace dream::render::vk
