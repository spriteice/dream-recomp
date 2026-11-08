// Where the presenter puts the guest's picture in the window: the largest rectangle with the
// picture's shape that fits, centred, in whole pixels. A window of another shape gets black bars
// on both sides, or above and below, rather than a stretched picture. There is no GPU in it, so a
// test can pin it on any machine.
//
// The presenter draws one triangle twice the size of the area it means to fill and relies on
// clipping to cut it down, so the rectangle has to be the area the GPU clips to: the presenter
// sets the viewport and the scissor to it. It once shrank the triangle in clip space instead and
// left both at the whole window, and in any window not of the picture's shape the part of the
// triangle past the picture's right or bottom edge was drawn too, as the picture's edge texels
// repeated out to the window's edge and cut off by the triangle's long side.
#pragma once

#include <cstdint>

namespace dream::render::vk {

struct PresentRect {
    std::uint32_t x = 0, y = 0, width = 0, height = 0;
};

// The rectangle a `picture_width` x `picture_height` picture fills in a `target_width` x
// `target_height` window. One side fills the window; the other keeps the picture's shape, rounded
// to the nearest pixel and never below one, and is centred, with any odd pixel left over going to
// the right or bottom bar. Empty when either size is.
constexpr PresentRect fit_picture(std::uint32_t picture_width, std::uint32_t picture_height,
                                  std::uint32_t target_width,
                                  std::uint32_t target_height) noexcept {
    if (picture_width == 0 || picture_height == 0 || target_width == 0 || target_height == 0)
        return {};
    // Compared and scaled in 64 bits: the products of two 32-bit sizes don't fit in 32.
    const std::uint64_t pw = picture_width, ph = picture_height;
    const std::uint64_t tw = target_width, th = target_height;
    PresentRect r;
    if (tw * ph > th * pw) {
        // The window is wider than the picture: full height, bars left and right.
        std::uint64_t w = (th * pw + ph / 2) / ph;
        w = w < 1 ? 1 : (w > tw ? tw : w);
        r.width = static_cast<std::uint32_t>(w);
        r.height = target_height;
        r.x = static_cast<std::uint32_t>((tw - w) / 2);
    } else {
        // Taller, or the same shape: full width, bars above and below (of no height when the
        // shapes match).
        std::uint64_t h = (tw * ph + pw / 2) / pw;
        h = h < 1 ? 1 : (h > th ? th : h);
        r.width = target_width;
        r.height = static_cast<std::uint32_t>(h);
        r.y = static_cast<std::uint32_t>((th - h) / 2);
    }
    return r;
}

}  // namespace dream::render::vk
