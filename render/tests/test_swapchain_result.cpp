// The swapchain rule, which the GPU-less test machine can still pin: what a frame does with each
// answer to acquire and present. The window applies it in begin_frame() and end_frame().
#include "dream/render/vk/swapchain_result.h"

#include "doctest.h"

using namespace dream::render::vk;

TEST_CASE("swapchain: a suboptimal acquire is drawn and presented, and the rebuild comes after") {
    // The image is acquired and its semaphore will be signalled. Dropping the frame leaves that
    // semaphore signalled for the next acquire, which the specification forbids; the window did
    // exactly that before this rule existed.
    CHECK(after_acquire(VK_SUBOPTIMAL_KHR) == AfterAcquire::DrawThenRebuild);
    CHECK(after_present(VK_SUCCESS, true) == AfterPresent::Rebuild);
}

TEST_CASE("swapchain: an out-of-date acquire got no image and is rebuilt at once") {
    CHECK(after_acquire(VK_ERROR_OUT_OF_DATE_KHR) == AfterAcquire::Rebuild);
}

TEST_CASE("swapchain: an ordinary frame is drawn and shown") {
    CHECK(after_acquire(VK_SUCCESS) == AfterAcquire::Draw);
    CHECK(after_present(VK_SUCCESS, false) == AfterPresent::Shown);
}

TEST_CASE("swapchain: a present the engine wants redone asks for a rebuild; an error is an error") {
    CHECK(after_present(VK_SUBOPTIMAL_KHR, false) == AfterPresent::Rebuild);
    CHECK(after_present(VK_ERROR_OUT_OF_DATE_KHR, false) == AfterPresent::Rebuild);
    CHECK(after_present(VK_ERROR_DEVICE_LOST, false) == AfterPresent::Fail);
    CHECK(after_acquire(VK_ERROR_DEVICE_LOST) == AfterAcquire::Fail);
    CHECK(after_acquire(VK_ERROR_SURFACE_LOST_KHR) == AfterAcquire::Fail);
}
