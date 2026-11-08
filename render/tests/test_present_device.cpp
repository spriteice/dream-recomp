// The presenter on a real Vulkan device: what it draws into a window of a given shape, read back
// pixel by pixel. The picture has to land in one rectangle with its own shape, centred, and leave
// every other pixel of the window as the render pass cleared it.
//
// It needs a device, so it runs wherever one exists -- Mesa's software rasteriser, lavapipe, is
// enough -- and reports itself skipped (exit 77, which ctest reads as skipped) where none does,
// such as a CI runner with no Vulkan driver. The rule the presenter applies has its own test that
// needs no device (test_present_fit.cpp); this one shows the GPU draws what the rule says.
#define DOCTEST_CONFIG_IMPLEMENT
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "dream/render/vk/context.h"
#include "dream/render/vk/present.h"
#include "dream/render/vk/resources.h"

#include "doctest.h"

using namespace dream::render::vk;

namespace {

Context* g_ctx = nullptr;

// What the render pass clears to, and the launcher's own clear colour: opaque black.
constexpr std::uint32_t kClear = 0xFF000000u;

// RGBA8888 as the presenter takes it and the readback returns it: red in the lowest byte.
constexpr std::uint32_t kWhite = 0xFFFFFFFFu;
constexpr std::uint32_t kRed = 0xFF0000FFu;
constexpr std::uint32_t kGreen = 0xFF00FF00u;

// A picture with no black in it, so every pixel it reaches shows. Its last column is red and its
// last row green: those are the texels a draw that runs past the picture's right or bottom edge
// repeats, since the sampler clamps to the edge.
std::vector<std::uint32_t> make_picture(std::uint32_t width, std::uint32_t height) {
    std::vector<std::uint32_t> px(static_cast<std::size_t>(width) * height, kWhite);
    for (std::uint32_t y = 0; y < height; ++y)
        px[static_cast<std::size_t>(y) * width + width - 1] = kRed;
    for (std::uint32_t x = 0; x < width; ++x)
        px[static_cast<std::size_t>(height - 1) * width + x] = kGreen;
    return px;
}

// A window-sized colour target and everything needed to draw one frame into it and read it back.
// The render pass clears and stores as the window's does.
class Target {
public:
    explicit Target(Context& ctx) : ctx_(ctx) {}
    ~Target() { destroy(); }
    Target(const Target&) = delete;
    Target& operator=(const Target&) = delete;

    bool create(VkExtent2D extent) {
        extent_ = extent;
        if (!colour_.create(ctx_, VK_FORMAT_R8G8B8A8_UNORM, extent,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                            VK_IMAGE_ASPECT_COLOR_BIT))
            return false;
        VkAttachmentDescription colour{};
        colour.format = VK_FORMAT_R8G8B8A8_UNORM;
        colour.samples = VK_SAMPLE_COUNT_1_BIT;
        colour.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colour.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colour.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colour.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colour.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colour.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        const VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &ref;
        VkSubpassDependency after{};
        after.srcSubpass = 0;
        after.dstSubpass = VK_SUBPASS_EXTERNAL;
        after.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        after.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        after.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        after.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo rpci{};
        rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpci.attachmentCount = 1;
        rpci.pAttachments = &colour;
        rpci.subpassCount = 1;
        rpci.pSubpasses = &subpass;
        rpci.dependencyCount = 1;
        rpci.pDependencies = &after;
        if (vkCreateRenderPass(ctx_.device(), &rpci, nullptr, &render_pass_) != VK_SUCCESS)
            return false;
        const VkImageView view = colour_.view();
        VkFramebufferCreateInfo fbci{};
        fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass = render_pass_;
        fbci.attachmentCount = 1;
        fbci.pAttachments = &view;
        fbci.width = extent.width;
        fbci.height = extent.height;
        fbci.layers = 1;
        if (vkCreateFramebuffer(ctx_.device(), &fbci, nullptr, &framebuffer_) != VK_SUCCESS)
            return false;
        VkCommandPoolCreateInfo cpci{};
        cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cpci.queueFamilyIndex = ctx_.queue_family();
        if (vkCreateCommandPool(ctx_.device(), &cpci, nullptr, &pool_) != VK_SUCCESS)
            return false;
        VkCommandBufferAllocateInfo cbai{};
        cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool = pool_;
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(ctx_.device(), &cbai, &cmd_) != VK_SUCCESS)
            return false;
        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (vkCreateFence(ctx_.device(), &fci, nullptr, &fence_) != VK_SUCCESS)
            return false;
        return readback_.ensure(ctx_, static_cast<VkDeviceSize>(extent.width) * extent.height * 4,
                                VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    }

    void destroy() {
        VkDevice dev = ctx_.device();
        vkDeviceWaitIdle(dev);
        if (fence_)
            vkDestroyFence(dev, fence_, nullptr);
        if (pool_)
            vkDestroyCommandPool(dev, pool_, nullptr);
        if (framebuffer_)
            vkDestroyFramebuffer(dev, framebuffer_, nullptr);
        if (render_pass_)
            vkDestroyRenderPass(dev, render_pass_, nullptr);
        fence_ = VK_NULL_HANDLE;
        pool_ = VK_NULL_HANDLE;
        framebuffer_ = VK_NULL_HANDLE;
        render_pass_ = VK_NULL_HANDLE;
        readback_.destroy();
        colour_.destroy();
    }

    VkRenderPass render_pass() const noexcept { return render_pass_; }

    // One frame as the windowed launcher draws it: the upload outside the render pass, then the
    // render pass with the viewport and scissor covering the whole window, then the presenter's
    // draw. The window-wide viewport and scissor are what any caller may leave set; what the
    // presenter draws must not depend on them. Returns the frame, row 0 at the top.
    bool present(Presenter& presenter, const std::vector<std::uint32_t>& picture,
                 std::uint32_t width, std::uint32_t height, std::vector<std::uint32_t>& out) {
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd_, &bi);
        if (!presenter.upload(cmd_, 0, picture.data(), width, height))
            return false;
        VkClearValue clear{};
        clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        VkRenderPassBeginInfo rpbi{};
        rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpbi.renderPass = render_pass_;
        rpbi.framebuffer = framebuffer_;
        rpbi.renderArea.extent = extent_;
        rpbi.clearValueCount = 1;
        rpbi.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd_, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
        const VkViewport viewport{
            0.0f, 0.0f, static_cast<float>(extent_.width), static_cast<float>(extent_.height),
            0.0f, 1.0f};
        const VkRect2D scissor{{0, 0}, extent_};
        vkCmdSetViewport(cmd_, 0, 1, &viewport);
        vkCmdSetScissor(cmd_, 0, 1, &scissor);
        const bool drawn = presenter.draw(cmd_, extent_);
        vkCmdEndRenderPass(cmd_);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {extent_.width, extent_.height, 1};
        vkCmdCopyImageToBuffer(cmd_, colour_.image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               readback_.handle(), 1, &copy);
        vkEndCommandBuffer(cmd_);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd_;
        if (vkQueueSubmit(ctx_.queue(), 1, &si, fence_) != VK_SUCCESS)
            return false;
        if (vkWaitForFences(ctx_.device(), 1, &fence_, VK_TRUE, 10'000'000'000ull) != VK_SUCCESS)
            return false;
        const auto* px = static_cast<const std::uint32_t*>(readback_.mapped());
        out.assign(px, px + static_cast<std::size_t>(extent_.width) * extent_.height);
        return drawn;
    }

private:
    Context& ctx_;
    VkExtent2D extent_{};
    AttachmentImage colour_;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    HostBuffer readback_;
};

// Draws a `pw` x `ph` picture into a `tw` x `th` window and checks where it went, from the pixels
// alone: the pixels that aren't the clear colour must be one whole rectangle, centred, filling the
// window along one axis, with the picture's shape to the nearest pixel, and its right-most column
// and bottom row must be the picture's own edge texels.
void check_fit(std::uint32_t pw, std::uint32_t ph, std::uint32_t tw, std::uint32_t th) {
    CAPTURE(pw);
    CAPTURE(ph);
    CAPTURE(tw);
    CAPTURE(th);
    Target target(*g_ctx);
    REQUIRE(target.create({tw, th}));
    Presenter presenter;
    REQUIRE(presenter.create(*g_ctx, target.render_pass()));
    const std::vector<std::uint32_t> picture = make_picture(pw, ph);
    std::vector<std::uint32_t> px;
    REQUIRE(target.present(presenter, picture, pw, ph, px));

    const auto at = [&](std::uint32_t x, std::uint32_t y) {
        return px[static_cast<std::size_t>(y) * tw + x];
    };
    // The bounding box of everything drawn.
    std::uint32_t x0 = tw, y0 = th, x1 = 0, y1 = 0;
    for (std::uint32_t y = 0; y < th; ++y)
        for (std::uint32_t x = 0; x < tw; ++x)
            if (at(x, y) != kClear) {
                x0 = x < x0 ? x : x0;
                y0 = y < y0 ? y : y0;
                x1 = x + 1 > x1 ? x + 1 : x1;
                y1 = y + 1 > y1 ? y + 1 : y1;
            }
    REQUIRE(x0 < x1);
    REQUIRE(y0 < y1);
    const std::uint32_t w = x1 - x0, h = y1 - y0;
    CAPTURE(x0);
    CAPTURE(y0);
    CAPTURE(w);
    CAPTURE(h);

    // Every pixel inside the box is the picture's: none of the clear colour shows through, as it
    // does where a triangle's edge crosses the box.
    std::uint64_t holes = 0;
    for (std::uint32_t y = y0; y < y1; ++y)
        for (std::uint32_t x = x0; x < x1; ++x) holes += at(x, y) == kClear;
    CHECK(holes == 0);

    // Centred, so the bars either side differ by at most the one pixel an odd remainder leaves.
    const std::int64_t left = x0, right = static_cast<std::int64_t>(tw) - x1;
    const std::int64_t top = y0, bottom = static_cast<std::int64_t>(th) - y1;
    CHECK(std::llabs(left - right) <= 1);
    CHECK(std::llabs(top - bottom) <= 1);
    // One axis is filled from edge to edge.
    CHECK((w == tw || h == th));
    // The picture's shape, to within the half pixel of rounding either way: w/h against pw/ph,
    // cross-multiplied, and the half pixel is half of the other side's unit.
    const std::int64_t shape =
        static_cast<std::int64_t>(w) * ph - static_cast<std::int64_t>(h) * pw;
    CHECK(std::llabs(shape) * 2 <= static_cast<std::int64_t>(w == tw ? pw : ph));

    // The box's right-most column is the picture's red one and its bottom row the green one, so
    // the box is the picture itself and not texels repeated past its edges. Read as "more red than
    // green" rather than exactly red: a picture drawn smaller than itself is averaged, and its edge
    // column blends with the white beside it.
    const auto red = [](std::uint32_t c) { return c & 0xFFu; };
    const auto green = [](std::uint32_t c) { return (c >> 8) & 0xFFu; };
    const std::uint32_t right_edge = at(x1 - 1, (y0 + y1) / 2);
    const std::uint32_t bottom_edge = at((x0 + x1) / 2, y1 - 1);
    CHECK(red(right_edge) > green(right_edge));
    CHECK(green(bottom_edge) > red(bottom_edge));
}

}  // namespace

TEST_CASE("presenter: a 4:3 picture fills a 4:3 window") {
    check_fit(640, 480, 960, 720);
}

TEST_CASE("presenter: a wider window pillarboxes the picture, both bars black") {
    check_fit(640, 480, 1920, 1002);  // the maintainer's maximised window, SteamOS Desktop Mode
    check_fit(640, 480, 1920, 1080);  // 16:9
    check_fit(640, 480, 1280, 800);   // 16:10
    check_fit(640, 480, 2000, 1001);  // a width that doesn't divide evenly
}

TEST_CASE("presenter: a taller window letterboxes the picture, both bars black") {
    check_fit(640, 480, 800, 1000);
    check_fit(640, 480, 961, 1000);
}

TEST_CASE("presenter: a picture drawn larger than the window is fitted the same way") {
    check_fit(1280, 960, 1920, 1080);  // --scale 2, averaged on the way down where it is minified
    check_fit(1280, 960, 800, 450);
}

int main(int argc, char** argv) {
    Context ctx;
    if (!ctx.create({}, false)) {
        std::printf("skipped: no Vulkan device to present on (%s)\n", ctx.error().c_str());
        return 77;
    }
    std::printf("presenting on %s\n", ctx.caps().device_name.c_str());
    g_ctx = &ctx;
    doctest::Context dt(argc, argv);
    return dt.run();
}
