// Where the presenter puts the guest's picture in a window of any shape (vk/present_fit.h): the
// rectangle it sets the viewport and scissor to. test_present_device.cpp shows a GPU drawing
// there; this pins the rule on every machine, with no GPU.
#include <cstdint>
#include <cstdlib>

#include "dream/render/vk/present_fit.h"

#include "doctest.h"

using namespace dream::render::vk;

namespace {

bool same(const PresentRect& a, const PresentRect& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

// What every fit must be, whatever the sizes: inside the window, at least a pixel each way, one
// side filling the window, centred to within the odd pixel, and the picture's shape to within the
// half pixel of rounding.
void check_rules(std::uint32_t pw, std::uint32_t ph, std::uint32_t tw, std::uint32_t th) {
    CAPTURE(pw);
    CAPTURE(ph);
    CAPTURE(tw);
    CAPTURE(th);
    const PresentRect r = fit_picture(pw, ph, tw, th);
    REQUIRE(r.width >= 1);
    REQUIRE(r.height >= 1);
    CHECK(std::uint64_t{r.x} + r.width <= tw);
    CHECK(std::uint64_t{r.y} + r.height <= th);
    CHECK((r.width == tw || r.height == th));
    const std::int64_t right = std::int64_t{tw} - r.x - r.width;
    const std::int64_t bottom = std::int64_t{th} - r.y - r.height;
    CHECK(right - std::int64_t{r.x} >= 0);  // an odd pixel goes right, not left
    CHECK(right - std::int64_t{r.x} <= 1);
    CHECK(bottom - std::int64_t{r.y} >= 0);
    CHECK(bottom - std::int64_t{r.y} <= 1);
    // w/h against pw/ph, cross-multiplied: the rounded side is within half of one of its pixels,
    // unless it was held to the one-pixel floor or the window's edge.
    const std::int64_t shape = std::int64_t{r.width} * ph - std::int64_t{r.height} * pw;
    const bool fills_height = r.height == th;
    const std::uint32_t rounded = fills_height ? r.width : r.height;
    const std::uint32_t limit = fills_height ? tw : th;
    if (rounded > 1 && rounded < limit)
        CHECK(std::llabs(shape) * 2 <= std::int64_t{fills_height ? ph : pw});
}

}  // namespace

TEST_CASE("present fit: a picture of the window's shape fills it") {
    CHECK(same(fit_picture(640, 480, 640, 480), {0, 0, 640, 480}));
    CHECK(same(fit_picture(640, 480, 960, 720), {0, 0, 960, 720}));   // the launcher's first window
    CHECK(same(fit_picture(1280, 960, 640, 480), {0, 0, 640, 480}));  // --scale 2
}

TEST_CASE("present fit: a wider window gets bars left and right, the picture centred") {
    // The maintainer's maximised window on SteamOS's Desktop Mode, 8 November 2026: the picture
    // must stop at x = 1628, where the old presenter went on drawing to the window's right edge.
    CHECK(same(fit_picture(640, 480, 1920, 1002), {292, 0, 1336, 1002}));
    CHECK(same(fit_picture(640, 480, 1920, 1080), {240, 0, 1440, 1080}));  // 16:9
    CHECK(same(fit_picture(640, 480, 1280, 800), {106, 0, 1067, 800}));    // 16:10, rounded up
    CHECK(same(fit_picture(640, 480, 2000, 1001), {332, 0, 1335, 1001}));  // 1334.67, rounded up
}

TEST_CASE("present fit: a taller window gets bars above and below") {
    CHECK(same(fit_picture(640, 480, 800, 1000), {0, 200, 800, 600}));
    CHECK(same(fit_picture(640, 480, 961, 1000), {0, 139, 961, 721}));  // 720.75, rounded up
    CHECK(same(fit_picture(640, 480, 1000, 1000), {0, 125, 1000, 750}));
}

TEST_CASE("present fit: a window a pixel across still gets a picture a pixel across") {
    CHECK(same(fit_picture(640, 480, 1, 1), {0, 0, 1, 1}));
    CHECK(same(fit_picture(640, 480, 1, 1000), {0, 499, 1, 1}));  // 0.75 rounds to 1
    CHECK(same(fit_picture(640, 480, 1000, 1), {499, 0, 1, 1}));  // 1.33 rounds to 1
    CHECK(same(fit_picture(640, 480, 3, 1), {1, 0, 1, 1}));
}

TEST_CASE("present fit: an empty picture or window has nothing to fill") {
    CHECK(same(fit_picture(0, 480, 1920, 1080), {}));
    CHECK(same(fit_picture(640, 0, 1920, 1080), {}));
    CHECK(same(fit_picture(640, 480, 0, 1080), {}));
    CHECK(same(fit_picture(640, 480, 1920, 0), {}));
}

TEST_CASE("present fit: sizes whose products overflow 32 bits") {
    CHECK(same(fit_picture(2560, 1920, 65535, 65535), {0, 8192, 65535, 49151}));
    CHECK(same(fit_picture(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 1), {0x7FFFFFFFu, 0, 1, 1}));
    check_rules(0xFFFFFFFFu, 3, 0xFFFFFFFFu, 0xFFFFFFFFu);
}

TEST_CASE("present fit: the rules hold for every window up to 64 pixels, and common ones") {
    const std::uint32_t pictures[][2] = {{640, 480}, {1280, 960}, {2560, 1920}, {320, 240},
                                         {720, 576}, {640, 240},  {1, 1},       {3, 7}};
    for (const auto& p : pictures) {
        for (std::uint32_t tw = 1; tw <= 64; ++tw)
            for (std::uint32_t th = 1; th <= 64; ++th) check_rules(p[0], p[1], tw, th);
        const std::uint32_t windows[][2] = {{1920, 1080}, {1920, 1002}, {1280, 800}, {2560, 1440},
                                            {3840, 2160}, {1080, 1920}, {800, 1280}, {1366, 768}};
        for (const auto& w : windows) check_rules(p[0], p[1], w[0], w[1]);
    }
}
