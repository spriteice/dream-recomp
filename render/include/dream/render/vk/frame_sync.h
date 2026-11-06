// Which of the window's semaphores, and which staging buffer, each frame uses. There is no GPU in
// it, so a test can run the window's frames against a model of the GPU and the presentation engine:
// the sandbox has no Vulkan device, and the hazards depend on how far the GPU and the display lag,
// which no test machine can set.
//
// A frame in flight owns a command buffer, a fence and the semaphore its acquire signals, and the
// frames take the slots in turn. begin_frame() waits on the slot's fence before using any of them
// again, and the fence covers everything the slot's last submit did, its wait on that semaphore
// included, so the acquire's semaphore can stay with the slot.
//
// The semaphore the submit signals for the present to wait on cannot. The fence says nothing about
// the present: the presentation engine waits on that semaphore when it comes to the present, which
// can be after the slot has come round again, and with three or more swapchain images it often
// is. The one thing that shows the engine is done with a present is getting its image back from
// vkAcquireNextImageKHR. So the present's semaphore goes with the image: the frame that acquires an
// image signals that image's semaphore, whose last present the acquire has just shown to be over.
// Taken per frame slot, as the window once did, the submit two frames on could signal a semaphore
// a present was still waiting on (VUID-vkQueueSubmit-pSignalSemaphores-00067).
//
// Memory the CPU writes for a frame to copy from follows the same fence. When begin_frame()
// returns, the slot's last submit is done, but the other slot's, the frame just before, may still
// be copying out of its own. So a buffer the CPU fills each frame is one per frame in flight, and a
// frame writes only its own slot's: one shared buffer, as the presenter once had, was rewritten
// while the previous frame was still copying from it.
#pragma once

#include <cstdint>

namespace dream::render::vk {

inline constexpr std::uint32_t kFramesInFlight = 2;

// The semaphore the acquire of the frame in `frame_slot` signals and its submit waits on: one per
// frame in flight.
constexpr std::uint32_t acquire_semaphore(std::uint32_t frame_slot) noexcept {
    return frame_slot;
}

// The semaphore the submit of the frame that acquired `image_index` signals and its present waits
// on: one per swapchain image, made with the swapchain.
constexpr std::uint32_t present_semaphore(std::uint32_t image_index) noexcept {
    return image_index;
}

// The staging buffer the frame in `frame_slot` fills on the CPU and its submit copies from: one per
// frame in flight.
constexpr std::uint32_t upload_buffer(std::uint32_t frame_slot) noexcept {
    return frame_slot;
}

}  // namespace dream::render::vk
