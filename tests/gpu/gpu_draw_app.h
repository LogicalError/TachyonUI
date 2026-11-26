#pragma once

// Windows drawn by a graphics backend and compared with Vulkan's drawing of them, for the tests of
// the other backends: one with a button, a label and a cached scope, and one with every color type.

#include <gui_controls/gui_application_internal.h>

#include <gtest/gtest.h>

#include <algorithm>
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

    // A window with a shape for each color type the shaders resolve: the gradients, read from the
    // gradient atlas, the interpolated box and triangle, and an image stretched and repeated.
    class ColorTypesApp : public GUIApplicationInternals
    {
    public:
        ID  windowID = ID::Invalid(), areaID = ID::Invalid();
        int frame = 0;
        static constexpr int kFrames = 3;

        void OnGUI() override
        {
            if (BeginWindow(windowID, "Colors", theme.windowStyleRef, LayoutOverride{}.Size(360, 160)))
            {
                BeginArea(areaID, LayoutOverride::FixedSize(tachyon::Pixels{ 352 }, tachyon::Pixels{ 96 }));
                (void)elementStorage.AddCustomDrawAfter(elementStorage.FindElementRefByID(areaID),
                                                        [this](CustomDrawContext& c) { Draw(c); });
                EndArea();
            }
            EndWindow();
            if (++frame >= kFrames)
                applicationQuit = true;
        }

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
            areaID   = idManager.CreateStaticID();

            const color4b red{ 255, 0, 0, 255 }, green{ 0, 255, 0, 255 }, blue{ 0, 0, 255, 255 }, white{ 255, 255, 255, 255 };
            for (const ColorType type : { ColorType::LinearGradient, ColorType::CircularAngularGradient,
                                          ColorType::EllipsoidAngularGradient, ColorType::RadialGradient, ColorType::SDFGradient })
                m_quads.push_back(styleManager.RegisterColorStyle(ColorStyle::Create(red, blue, type)));
            m_quads.push_back(styleManager.RegisterColorStyle(ColorStyle::Create(green)));
            m_quads.push_back(styleManager.RegisterColorStyle(ColorStyle::Create(red, green, blue, white, ColorType::InterpolatedBox)));
            m_triangle  = styleManager.RegisterColorStyle(ColorStyle::Create(red, green, blue, ColorType::InterpolatedTriangle));
            m_stretched = styleManager.RegisterColorStyle(ColorStyle::Create(white, white, ColorType::StretchedTexture));
            m_repeated  = styleManager.RegisterColorStyle(ColorStyle::Create(white, white, ColorType::RepeatedTexture));

            // A 4x4 image, its texels each a different color (RGBA).
            uint32_t pixels[16];
            for (uint32_t i = 0; i < 16; ++i)
                pixels[i] = 0xff000000u | (i * 16u) | ((255u - i * 16u) << 8) | ((i & 3u) * 80u << 16);
            m_created = graphicsContext->CreateImage(pixels, uint2{ 4, 4 }, ImageFormat::R8G8B8A8, Sampler::NearestRepeat);
            m_image   = m_created != nullptr ? graphicsContext->RegisterImage(m_created) : ImageIndex::Invalid;
            if (m_image == ImageIndex::Invalid)
                return false;
            transitionManager.SetEnabled(false);
            SetRedrawMode(RedrawMode::Always);
            return true;
        }

        void Release()
        {
            if (m_created != nullptr && graphicsContext != nullptr)
                graphicsContext->DestroyImageAtEnd(m_created, DeviceScope::Application);
            m_created = nullptr;
            Cleanup();
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

    private:
        std::vector<VisualColorStyleIndex> m_quads;
        VisualColorStyleIndex m_triangle{}, m_stretched{}, m_repeated{};
        tachyon::Image*       m_created = nullptr;   // qualified: Image is also the app's control
        ImageIndex            m_image   = ImageIndex::Invalid;

        // Rows of seven 40-pixel cells, 4 pixels apart.
        void Draw(CustomDrawContext& c) const
        {
            int cell = 0;
            auto next = [&cell] {
                const IntRect r{ .position = { 4 + (cell % 7) * 44, 4 + (cell / 7) * 44 }, .size = { 40, 40 } };
                ++cell;
                return r;
            };
            for (const VisualColorStyleIndex style : m_quads)
                c.RenderQuad(next(), style);
            const IntRect t = next();
            c.RenderTriangle(float2{ float(t.x), float(t.y + t.h) }, float2{ float(t.x + t.w), float(t.y + t.h) },
                             float2{ float(t.x) + float(t.w) * 0.5f, float(t.y) }, m_triangle);
            c.RenderQuad(next(), m_stretched, 0.0f, m_image);
            c.RenderQuad(next(), m_repeated, 0.0f, m_image);
        }
    };

    // A window and a transparent one beside it, like a drag preview, which its OnGUI stops
    // declaring once `closePreview` is set.
    class PreviewApp : public GUIApplicationInternals
    {
    public:
        ID   mainID = ID::Invalid(), previewID = ID::Invalid();
        bool closePreview = false;

        void OnGUI() override
        {
            if (BeginWindow(mainID, "Main", theme.windowStyleRef, LayoutOverride{}.Size(320, 160)))
                Text(idManager.AllocateDynamicID("label"), "The main window");
            EndWindow();
            if (!closePreview)
            {
                if (BeginWindow(previewID, theme.windowStyleRef, LayoutOverride{}.Size(120, 40),
                                ElementFlags::WindowNoCaption | ElementFlags::WindowIsTransparent))
                    Text(idManager.AllocateDynamicID("preview"), "Preview");
                EndWindow();
            }
        }

        bool Start(GraphicsAPI api, std::unique_ptr<GraphicsContext> backend)
        {
            InitializeOptions options;
            options.graphicsAPI = api;
            if (backend)
                SetGraphicsBackend(std::move(backend));
            if (!Initialize(options) || graphicsContext->GetAPI() != api)
                return false;
            mainID    = idManager.CreateStaticID();
            previewID = idManager.CreateStaticID();
            transitionManager.SetEnabled(false);
            SetRedrawMode(RedrawMode::Always);
            return true;
        }

        void Frames(int count)
        {
            for (int i = 0; i < count && UpdateFrame(); ++i)
                if (RecordFrame())
                    PresentFrame();
        }
    };

    // A window's live-resize redraw runs a whole app frame, which may close that very window
    // (a drag preview whose drag ended in it) and delete its context: the redraw stops there,
    // and the app goes on. Before, the context used itself after it was deleted.
    inline void ExpectARedrawThatClosesItsWindowStopsThere(GraphicsAPI api, std::unique_ptr<GraphicsContext> backend = {})
    {
        PreviewApp app;
        if (!app.Start(api, std::move(backend)))
        {
            app.Cleanup();
            GTEST_SKIP() << "no window system, or no GPU runs this backend";
        }
        app.Frames(4);
        IWindowContext* preview = app.windowManager->GetWindowContext(app.previewID);
        ASSERT_NE(preview, nullptr) << "the preview window is open";

        app.closePreview = true;
        preview->RenderWindowFrame(app.graphicsContext, app.windowManager->GetWindowRect(app.previewID), /*resizing*/ true);
        EXPECT_EQ(app.windowManager->GetWindowContext(app.previewID), nullptr) << "the frame closed the preview";

        app.Frames(4);
        EXPECT_NE(app.windowManager->GetWindowContext(app.mainID), nullptr) << "the main window draws on";
        app.Cleanup();
    }

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

    // `pixels`, a frame `rect` in size, as an App (DrawApp, ColorTypesApp) draws them on Vulkan
    // where Vulkan runs, else on the native Metal backend: the same, but for rounding. Call once
    // the app that drew them is cleaned up.
    template <class App>
    void ExpectSameAsOnVulkan(const std::vector<uint8_t>& pixels, IntRect rect)
    {
        IntRect              vulkanRect{};
        std::vector<uint8_t> expected;
        for (const GraphicsAPI reference : { GraphicsAPI::Vulkan, GraphicsAPI::Metal })
        {
            if (reference == GraphicsAPI::Metal && !TACHYON_METAL)
                continue;
            App app;
            if (app.Start(reference) && app.Run())
                expected = app.Pixels(vulkanRect);
            if constexpr (requires { app.Release(); })
                app.Release();
            else
                app.Cleanup();
            if (!expected.empty())
                break;
        }
#if TACHYON_METAL
        ASSERT_FALSE(expected.empty()) << "the native Metal backend draws the reference where it's built";
#endif
        if (expected.empty())
            return;
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

    // What was drawn looks as it should, and as it does on Vulkan where Vulkan runs (else on the
    // native Metal backend): the same pixels, but for rounding. Call once the app that drew it is
    // cleaned up.
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
        ExpectSameAsOnVulkan<DrawApp>(pixels, rect);
    }

    // Every color type draws on the backend `api` as it does on Vulkan.
    inline void ExpectColorTypesAsOnVulkan(GraphicsAPI api, std::unique_ptr<GraphicsContext> backend = {})
    {
        ColorTypesApp app;
        if (!app.Start(api, std::move(backend)))
        {
            app.Release();
            GTEST_SKIP() << "no window system, or no GPU runs this backend";
        }
        ASSERT_TRUE(app.Run());
        IntRect rect{};
        const std::vector<uint8_t> pixels = app.Pixels(rect);
        app.Release();
        ASSERT_FALSE(pixels.empty());

        // The shapes are drawn: their colors are strong, the window's background gray.
        size_t colored = 0;
        for (size_t i = 0; i < pixels.size(); i += 4)
            colored += std::max({ pixels[i], pixels[i + 1], pixels[i + 2] }) - std::min({ pixels[i], pixels[i + 1], pixels[i + 2] }) > 100;
        EXPECT_GT(colored, 8000u) << "pixels in a strong color";
        ExpectSameAsOnVulkan<ColorTypesApp>(pixels, rect);
    }
}
