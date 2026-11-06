// Which semaphore each of the window's frames may use (vk/frame_sync.h), pinned by running the
// frames against a model of the GPU and the presentation engine, since the test machine has no
// Vulkan device. The frames run as begin_frame() and end_frame() do: wait on the slot's fence,
// acquire with the slot's semaphore, submit waiting on it and signalling the present's semaphore,
// present.
#include <cstdint>
#include <deque>
#include <vector>

#include "dream/render/vk/frame_sync.h"

#include "doctest.h"

using namespace dream::render::vk;

namespace {

// Which semaphore a frame's submit signals for its present, from the frame's slot and its image.
using PresentRule = std::uint32_t (*)(std::uint32_t frame_slot, std::uint32_t image);

std::uint32_t the_windows_rule(std::uint32_t, std::uint32_t image) {
    return present_semaphore(image);
}

// How soon the GPU runs a submit: at once, or only when its slot's fence is waited on.
enum class Gpu { Eager, Lazy };
// How soon the presentation engine takes a queued present: only when the window has no image left
// to acquire, one each frame, or every one it can.
enum class Display { Slowest, OnePerFrame, Eager };

// Long enough for every slot to meet every image many times over.
constexpr int kFrames = 64;

struct Semaphore {
    // Signals not yet waited on. A binary semaphore may only hold one; a second is the
    // specification's undefined behaviour, kept as a count here so the model can go on.
    int signals = 0;
    // The image of the last present that waits on it, and whether an acquire of that image has
    // since shown the wait to be over: the one proof the specification offers, and what the
    // validation layers ask for before a submit signals the semaphore again.
    int waited_on_by = -1;
    bool wait_known_over = true;
};

class Model {
public:
    Model(std::uint32_t images, Gpu gpu, Display display, PresentRule rule)
        : gpu_(gpu),
          display_(display),
          rule_(rule),
          acquire_(kFramesInFlight),
          present_(images + kFramesInFlight),
          fence_signalled_(kFramesInFlight, true) {
        for (std::uint32_t i = 0; i < images; ++i) free_images_.push_back(i);
    }

    // What the window must never do. An acquire with a semaphore that still holds a signal or has
    // a wait queued on it (VUID-vkAcquireNextImageKHR-semaphore-01286).
    int acquire_reused = 0;
    // A submit that signals a semaphore a present has not yet waited on.
    int signalled_twice = 0;
    // A submit that signals a semaphore whose last present nothing has shown to be over
    // (VUID-vkQueueSubmit-pSignalSemaphores-00067).
    int signal_unproven = 0;
    // The model itself could go no further: nothing to run and nothing to show.
    bool stuck = false;

    void run(int frames) {
        std::uint32_t slot = 0;
        for (int f = 0; f < frames && !stuck; ++f) {
            frame(slot);
            slot = (slot + 1) % kFramesInFlight;
        }
    }

private:
    struct Submit {
        std::uint32_t slot, wait, signal;
    };
    struct Present {
        std::uint32_t image, wait;
    };

    bool gpu_runs_one() {
        if (submits_.empty())
            return false;
        const Submit s = submits_.front();
        submits_.pop_front();
        --acquire_[s.wait].signals;
        if (present_[s.signal].signals++ > 0)
            ++signalled_twice;
        fence_signalled_[s.slot] = true;
        return true;
    }

    // The oldest queued present, once its semaphore holds a signal: the engine waits on it, shows
    // the image and gives back the one it replaces.
    bool engine_takes_one() {
        if (presents_.empty() || present_[presents_.front().wait].signals == 0)
            return false;
        const Present p = presents_.front();
        presents_.pop_front();
        --present_[p.wait].signals;
        if (shown_ >= 0)
            free_images_.push_back(static_cast<std::uint32_t>(shown_));
        shown_ = static_cast<int>(p.image);
        return true;
    }

    void frame(std::uint32_t slot) {
        // begin_frame(): the slot's fence, then the acquire, which waits for the engine to give an
        // image back when it holds them all.
        while (!fence_signalled_[slot])
            if (!gpu_runs_one()) {
                stuck = true;
                return;
            }
        while (free_images_.empty())
            if (!engine_takes_one() && !gpu_runs_one()) {
                stuck = true;
                return;
            }
        const std::uint32_t image = free_images_.front();
        free_images_.pop_front();
        for (Semaphore& s : present_)
            if (s.waited_on_by == static_cast<int>(image))
                s.wait_known_over = true;
        Semaphore& acquired = acquire_[acquire_semaphore(slot)];
        if (acquired.signals > 0)
            ++acquire_reused;
        ++acquired.signals;
        fence_signalled_[slot] = false;

        // end_frame(): the submit, then the present.
        const std::uint32_t signal = rule_(slot, image);
        if (!present_[signal].wait_known_over)
            ++signal_unproven;
        submits_.push_back({slot, acquire_semaphore(slot), signal});
        if (gpu_ == Gpu::Eager)
            while (gpu_runs_one()) {
            }
        presents_.push_back({image, signal});
        present_[signal].waited_on_by = static_cast<int>(image);
        present_[signal].wait_known_over = false;
        switch (display_) {
            case Display::Slowest:
                break;
            case Display::OnePerFrame:
                engine_takes_one();
                break;
            case Display::Eager:
                while (engine_takes_one()) {
                }
                break;
        }
    }

    Gpu gpu_;
    Display display_;
    PresentRule rule_;
    std::vector<Semaphore> acquire_, present_;
    std::vector<bool> fence_signalled_;
    std::deque<std::uint32_t> free_images_;  // in the order the engine gave them back
    std::deque<Submit> submits_;
    std::deque<Present> presents_;
    int shown_ = -1;
};

}  // namespace

TEST_CASE("frame sync: a submit only signals a semaphore no present can still be waiting on") {
    // The window asks for minImageCount + 1 images: two to five for a minImageCount of one to four.
    for (std::uint32_t images = 2; images <= 5; ++images)
        for (Gpu gpu : {Gpu::Eager, Gpu::Lazy})
            for (Display display : {Display::Slowest, Display::OnePerFrame, Display::Eager}) {
                CAPTURE(images);
                CAPTURE(static_cast<int>(gpu));
                CAPTURE(static_cast<int>(display));
                Model m(images, gpu, display, the_windows_rule);
                m.run(kFrames);
                CHECK_FALSE(m.stuck);
                CHECK(m.acquire_reused == 0);
                CHECK(m.signalled_twice == 0);
                CHECK(m.signal_unproven == 0);
            }
}

TEST_CASE("frame sync: the model sees the reuse of one present semaphore per frame slot") {
    // The window's rule before this one. With three images, two frames in flight and a display that
    // lags, the third frame signals the first frame's semaphore before its present has waited on
    // it. With a display that keeps up the signals don't land twice, but nothing shows they
    // couldn't, which is what the validation layers report.
    const PresentRule per_slot = [](std::uint32_t frame_slot, std::uint32_t) { return frame_slot; };
    Model lagging(3, Gpu::Eager, Display::Slowest, per_slot);
    lagging.run(kFrames);
    CHECK_FALSE(lagging.stuck);
    CHECK(lagging.signalled_twice > 0);
    CHECK(lagging.signal_unproven > 0);

    Model keeping_up(3, Gpu::Eager, Display::Eager, per_slot);
    keeping_up.run(kFrames);
    CHECK_FALSE(keeping_up.stuck);
    CHECK(keeping_up.signalled_twice == 0);
    CHECK(keeping_up.signal_unproven > 0);
}
