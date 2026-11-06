// What a frame does with the result of acquiring its swapchain image, and with the result of
// presenting it. The rule is here, with no GPU in it, so a test can pin it: the sandbox has no
// Vulkan device, and the case that matters comes from a compositor changing the surface under a
// running window, which no test can stage.
//
// VK_SUBOPTIMAL_KHR from vkAcquireNextImageKHR is a success with a remark: the image is acquired
// and the semaphore passed in will be signalled, so the frame draws and presents it as usual and
// the swapchain is rebuilt after. The window used to treat it as a failure and rebuild at once,
// which left that semaphore signalled with nothing waiting on it; the next acquire reused it, which
// the specification forbids (VUID-vkAcquireNextImageKHR-semaphore-01286) and a driver may answer
// with a fault. VK_ERROR_OUT_OF_DATE_KHR acquires nothing and signals nothing, so for it a rebuild
// at once is right.
#pragma once

#include <vulkan/vulkan_core.h>

namespace dream::render::vk {

enum class AfterAcquire { Draw, DrawThenRebuild, Rebuild, Fail };
enum class AfterPresent { Shown, Rebuild, Fail };

constexpr AfterAcquire after_acquire(VkResult r) noexcept {
    switch (r) {
        case VK_SUCCESS:
            return AfterAcquire::Draw;
        case VK_SUBOPTIMAL_KHR:
            return AfterAcquire::DrawThenRebuild;
        case VK_ERROR_OUT_OF_DATE_KHR:
            return AfterAcquire::Rebuild;
        default:
            return AfterAcquire::Fail;
    }
}

// `rebuild_wanted` is what the acquire asked for. A present the engine answered with SUBOPTIMAL or
// OUT_OF_DATE asks for the same, and either way the semaphores are consumed (the queue operation is
// enqueued even when the presentation engine rejects it), so a rebuild after it is safe.
constexpr AfterPresent after_present(VkResult r, bool rebuild_wanted) noexcept {
    switch (r) {
        case VK_SUCCESS:
            return rebuild_wanted ? AfterPresent::Rebuild : AfterPresent::Shown;
        case VK_SUBOPTIMAL_KHR:
        case VK_ERROR_OUT_OF_DATE_KHR:
            return AfterPresent::Rebuild;
        default:
            return AfterPresent::Fail;
    }
}

}  // namespace dream::render::vk
