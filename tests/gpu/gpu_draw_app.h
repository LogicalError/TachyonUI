#pragma once

// A window with a button, a label and a cached scope, drawn by a graphics backend and compared
// with Vulkan's drawing of it: for the tests of the other backends.

#include <gui_controls/gui_application_internal.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <vector>

namespace GpuDrawApp
{
    using namespace tachyon;

    class DrawApp : public GUIApplicationInternals
    {
    public:
        ID      windowID = ID::Invalid(), buttonID = ID::Invalid(), labelID = ID::Invalid(), cachedID = ID::Invalid();
        IntRect windowRect{}, buttonRect{}, labelRect{};
        int     frame = 0;

        void OnGUI() override
        {
            if (BeginWindow(windowID, "Draw", theme.windowStyleRef, LayoutOverride{}.Size(320, 160)))
            {
                Button(buttonID, "Button", nullptr, LayoutOverride{}.Size(120, 40));
                Text(labelID, "A label drawn with the text atlas");
                // Kept on the GPU, and rebuilt every other frame: its GPU copies are made,
                // drawn from, and let go while frames are in flight.
                if (BeginCached(cachedID, uint64_t(frame / 2)))
                    Text(idManager.AllocateDynamicID("cached"), "A cached scope");
                EndCached();
            }
            EndWindow();
            if (++frame >= kFrames)
                applicationQuit = true;
        }
        static constexpr int kFrames = 12;

        // False when no window system or no GPU runs `api` (the app fell back to another).
        bool Start(GraphicsAPI api, std::unique_ptr<GraphicsContext> backend = {})
        {
            InitializeOptions options;
            options.graphicsAPI = api;
            if (backend)
                SetGraphicsBackend(std::move(backend));
            if (!Initialize(options) || graphicsContext->GetAPI() != api)
                return false;
            windowID = idManager.CreateStaticID();
            buttonID = idManager.CreateStaticID();
            labelID  = idManager.CreateStaticID();
            cachedID = idManager.CreateStaticID();
            m_frame.OnPostLayout([this](FrameContext&)
            {
                auto rectOf = [this](ID id) {
                    const ElementRef ref = elementStorage.FindElementRefByID(id);
                    return ref.IsValid() ? elementStorage.GetElementRectPtr(ref)->rect : IntRect{};
                };
                windowRect = rectOf(windowID);
                buttonRect = rectOf(buttonID);
                labelRect  = rectOf(labelID);
            });
            transitionManager.SetEnabled(false);
            SetRedrawMode(RedrawMode::Always);
            return true;
        }

        // The window's last frame, BGRA; empty when it can't be read.
        std::vector<uint8_t> Pixels(IntRect& rect)
        {
            rect = windowManager->GetWindowRect(windowID);
            std::vector<uint8_t> pixels(size_t(std::max(rect.w, 0)) * std::max(rect.h, 0) * 4);
            if (pixels.empty() || !ReadWindowPixels(windowID, pixels))
                pixels.clear();
            return pixels;
        }
    };

    struct Rgb
    {
        int r, g, b;
        int Sum() const { return r + g + b; }
    };

    // A pixel of a BGRA frame `width` wide, at a layout position (the window's rect is its origin).
    inline Rgb At(const std::vector<uint8_t>& bgra, int width, const IntRect& window, int x, int y)
    {
        const size_t o = (size_t(y - window.y) * size_t(width) + size_t(x - window.x)) * 4;
        return { bgra[o + 2], bgra[o + 1], bgra[o] };
    }

    // What a DrawApp drew: its window's last frame and where its controls are.
    struct Drawn
    {
        std::vector<uint8_t> pixels;
        IntRect rect{}, window{}, button{}, label{};
    };

    // Call after Run, before Cleanup.
    inline Drawn Capture(DrawApp& app)
    {
        Drawn drawn;
        drawn.pixels = app.Pixels(drawn.rect);
        drawn.window = app.windowRect;
        drawn.button = app.buttonRect;
        drawn.label  = app.labelRect;
        return drawn;
    }

    // What was drawn looks as it should, and as it does on Vulkan where Vulkan runs: the same
    // pixels, but for rounding. Call once the app that drew it is cleaned up.
    inline void ExpectDrawnAsOnVulkan(const Drawn& drawn)
    {
        const std::vector<uint8_t>& pixels = drawn.pixels;
        ASSERT_FALSE(pixels.empty());
        const IntRect rect = drawn.rect, window = drawn.window, button = drawn.button, label = drawn.label;

        // The window's background is drawn, the button stands out from it, and the label has glyphs.
        const Rgb background = At(pixels, rect.w, window, window.x + window.w - 4, window.y + window.h - 4);
        EXPECT_GT(background.Sum(), 0) << "the window's background is drawn";
        const Rgb buttonFill = At(pixels, rect.w, window, button.x + 4, button.y + button.h / 2);
        EXPECT_GT(std::abs(buttonFill.Sum() - background.Sum()), 9) << "the button stands out";
        int glyphPixels = 0;
        for (int y = label.y; y < label.y + label.h; ++y)
            for (int x = label.x; x < label.x + label.w; ++x)
                glyphPixels += std::abs(At(pixels, rect.w, window, x, y).Sum() - background.Sum()) > 60;
        EXPECT_GT(glyphPixels, 50) << "the label's glyphs are drawn";

        // The same app on Vulkan, where it runs: the same pixels, but for rounding.
        DrawApp vulkan;
        if (!vulkan.Start(GraphicsAPI::Vulkan) || !vulkan.Run())
        {
            vulkan.Cleanup();
            return;
        }
        IntRect vulkanRect{};
        const std::vector<uint8_t> expected = vulkan.Pixels(vulkanRect);
        vulkan.Cleanup();
        ASSERT_EQ(vulkanRect.w, rect.w);
        ASSERT_EQ(vulkanRect.h, rect.h);
        ASSERT_EQ(expected.size(), pixels.size());
        size_t differing = 0;
        for (size_t i = 0; i < pixels.size(); i += 4)
            for (size_t c = 0; c < 3; ++c)
                if (std::abs(int(pixels[i + c]) - int(expected[i + c])) > 2)
                {
                    ++differing;
                    break;
                }
        EXPECT_EQ(differing, 0u) << "pixels that differ from Vulkan's, of " << pixels.size() / 4;
    }
}
