// The Inspector's gradient and curve fields on a real device: a gradient's see-through parts
// over a checkerboard, a band over where an HDR gradient passes 1.0, and a curve field
// as a 44px plot with its curve in the accent color, an identity line behind color curves and
// an open button while hovered.

#include <gtest/gtest.h>

#include <gui_controls/gui_application_internal.h>

#include "gpu_injected_input.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    using namespace tachyon;

    struct Rgb { int r = 0, g = 0, b = 0; };
    int Distance(Rgb a, Rgb b) { return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b); }
    Rgb FromColor(color4f c) { return { int(std::lround(c.r * 255.0f)), int(std::lround(c.g * 255.0f)), int(std::lround(c.b * 255.0f)) }; }

    class InspectorFieldsApp : public GUIApplicationInternals
    {
    public:
        ID windowID   = ID::Invalid();
        ID gradientID = ID::Invalid();
        ID curveID    = ID::Invalid();
        ID colorsID   = ID::Invalid();
        ID fadeID     = ID::Invalid();
        ID fadeInID   = ID::Invalid();
        GradientF gradient, fade, fadeIn;
        CurveView curve, colors;

        void OnGUI() override
        {
            if (BeginWindow(windowID, "Fields", theme.mainWindowStyleRef, LayoutOverride{}.Size(520, 320),
                            ElementFlags::WindowRenderToImage))
            {
                // Each in a row 300 wide, as in an inspector.
                auto row = [this](const char* key, auto&& field) {
                    BeginHorizontal(idManager.AllocateDynamicID(key), theme.emptyHorzStyleRef, LayoutOverride::FixedWidth(300).NoShrink());
                    field();
                    EndHorizontal();
                };
                row("gradientRow", [this] { GradientField(gradientID, gradient); });
                row("fadeRow",     [this] { GradientField(fadeID, fade); });
                row("fadeInRow",   [this] { GradientField(fadeInID, fadeIn); });
                row("curveRow",    [this] { CurveField(curveID, curve); });
                row("colorsRow",   [this] { CurveField(colorsID, colors); });
            }
            EndWindow();
        }

        bool Start()
        {
            UseInjectedInputOnly(*this);
            if (!Initialize())
                return false;
            windowID   = idManager.CreateStaticID();
            gradientID = idManager.CreateStaticID();
            curveID    = idManager.CreateStaticID();
            colorsID   = idManager.CreateStaticID();
            fadeID     = idManager.CreateStaticID();
            // Red, opaque for its first half, then fading out.
            fade.AddStop(color4f{ 1.0f, 0.0f, 0.0f, 1.0f }, 0.0f);
            fade.AddStop(color4f{ 1.0f, 0.0f, 0.0f, 1.0f }, 0.5f);
            fade.AddStop(color4f{ 1.0f, 0.0f, 0.0f, 0.0f }, 1.0f);
            // From a see-through blue (a color that counts for nothing) in to red.
            fadeInID = idManager.CreateStaticID();
            fadeIn.AddStop(color4f{ 0.0f, 0.0f, 1.0f, 0.0f }, 0.0f);
            fadeIn.AddStop(color4f{ 1.0f, 0.0f, 0.0f, 1.0f }, 1.0f);
            // Red, green, then 8 times white: brighter than 1 past the middle.
            gradient.colorSpace = ColorSpace::LinearRGB;
            gradient.AddStop(color4f{ 1.0f, 0.0f, 0.0f, 1.0f }, 0.0f);
            gradient.AddStop(color4f{ 0.0f, 1.0f, 0.0f, 1.0f }, 0.5f);
            gradient.AddStop(color4f{ 8.0f, 8.0f, 8.0f, 1.0f }, 1.0f);
            // One value, level at a half; and a color's R, G, B, all at 0.
            static constexpr float2 kHalf[] = { { 0.0f, 0.5f }, { 1.0f, 0.5f } };
            static constexpr float2 kZero[] = { { 0.0f, 0.0f }, { 1.0f, 0.0f } };
            curve.channels = { CurveChannel{ .name = "Value", .curve = MakeCurve(kHalf) } };
            for (CurveChannelColor c : { CurveChannelColor::Red, CurveChannelColor::Green, CurveChannelColor::Blue })
                colors.channels.push_back(CurveChannel{ .name = "C", .color = c, .curve = MakeCurve(kZero) });
            SetRedrawMode(RedrawMode::Always);
            return true;
        }

        bool Frames(int count)
        {
            for (int i = 0; i < count; i++)
            {
                if (!UpdateFrame() || !RecordFrame()) return false;
                PresentFrame();
            }
            return true;
        }

        IntRect RectOf(ID id) { return elementStorage.GetElementRectUnsafe(elementStorage.FindElementRefByID(id)).rect; }
        IntRect InnerOf(ID id) { return elementStorage.GetElementRectUnsafe(elementStorage.FindElementRefByID(id)).innerRect; }

        std::vector<uint8_t> pixels;
        int width = 0;
        bool Read()
        {
            const IntRect rect = windowManager->GetWindowRect(windowID);
            width = rect.w;
            pixels.assign(size_t(std::max(rect.w, 0)) * size_t(std::max(rect.h, 0)) * 4, 0);
            return !pixels.empty() && ReadWindowPixels(windowID, pixels);
        }
        Rgb At(int x, int y) const
        {
            const uint8_t* p = pixels.data() + (size_t(y) * size_t(width) + size_t(x)) * 4;   // BGRA
            return { p[2], p[1], p[0] };
        }
        // The brightest pixel from y - 1 to y + 1 at x.
        Rgb BrightestAround(int x, int y) const
        {
            Rgb best = At(x, y - 1);
            for (int dy = 0; dy <= 1; ++dy)
            {
                const Rgb p = At(x, y + dy);
                if (p.r + p.g + p.b > best.r + best.g + best.b) best = p;
            }
            return best;
        }
    };

    class GpuInspectorFields : public ::testing::Test
    {
    protected:
        InspectorFieldsApp app;
        void SetUp() override
        {
            if (!app.Start())
                GTEST_SKIP() << "no Vulkan device available";
            ASSERT_TRUE(app.Frames(4));
            ASSERT_TRUE(app.Read());
        }
        void TearDown() override { app.Cleanup(); }
        int Px(int css) { return app.styleManager.Px(css); }
    };

    // The swatch shows only the gradient, as a color swatch shows a color: opaque in its top
    // half, at its alpha over a checkerboard in its bottom half. No stops under it.
    TEST_F(GpuInspectorFields, AGradientSwatchShowsItsAlphaOverACheckerboardInItsBottomHalf)
    {
        const IntRect bar = app.RectOf(app.fadeID);
        const int topRow    = bar.y + bar.h / 4;
        const int bottomRow = bar.y + bar.h * 3 / 4;
        // The most two pixels differ along a stretch of a row, a few checker squares long.
        auto spread = [&](float t, int y) {
            const int from = bar.x + int(t * float(bar.w));
            int most = 0;
            for (int x = from; x < from + Px(24); ++x)
                most = std::max(most, Distance(app.At(x, y), app.At(from, y)));
            return most;
        };
        EXPECT_LT(spread(0.1f, bottomRow), 12) << "opaque: one color";
        EXPECT_GT(spread(0.9f, bottomRow), 20) << "see-through: the checkerboard shows below";
        EXPECT_LT(spread(0.9f, topRow), 12) << "the top half is opaque";
        EXPECT_LT(Distance(app.At(bar.x + bar.w * 9 / 10, topRow), Rgb{ 255, 0, 0 }), 40) << "the color itself, without its alpha";

        // Fading in from a fully see-through stop, the top half is the first visible color from
        // its very start: the see-through stop's own color isn't shown.
        const IntRect fadeIn = app.RectOf(app.fadeInID);
        for (int x : { fadeIn.x + Px(2), fadeIn.x + fadeIn.w / 2 })
            EXPECT_LT(Distance(app.At(x, fadeIn.y + fadeIn.h / 4), Rgb{ 255, 0, 0 }), 40) << "red at " << x - fadeIn.x;

        // Under the other gradient's stops (0, a half, 1): the background.
        const IntRect stops = app.RectOf(app.gradientID);
        const int under = stops.y + stops.h + Px(4);
        const Rgb background = app.At(app.width - 10, under);
        for (float t : { 0.0f, 0.5f, 1.0f })
        {
            const int x = std::clamp(stops.x + int(t * float(stops.w)), stops.x + Px(5), stops.x + stops.w - Px(5));
            EXPECT_LT(Distance(app.At(x, under), background), 10) << "nothing under the bar at " << t;
        }
    }

    // A band over the bar where the gradient is brighter than 1: here its second half.
    TEST_F(GpuInspectorFields, AnHdrGradientHasABandOverItsPartBrighterThanOne)
    {
        const IntRect bar = app.RectOf(app.gradientID);
        const int y = bar.y - Px(4);
        const Rgb background = app.At(bar.x + bar.w / 4, y);
        const Rgb over = app.At(bar.x + bar.w * 9 / 10, y);
        EXPECT_GT(Distance(over, background), 200) << "a band over the part past 1.0";
        EXPECT_LT(Distance(background, app.At(app.width - 10, y)), 10) << "none over the part up to 1.0";
    }

    TEST_F(GpuInspectorFields, ACurveFieldIsA44PxPlotWithItsCurveInTheAccentColor)
    {
        const IntRect field = app.RectOf(app.curveID);
        EXPECT_EQ(field.h, Px(44));
        const IntRect inner = app.InnerOf(app.curveID);
        const Rgb line = app.BrightestAround(inner.x + inner.w / 4, inner.y + inner.h / 2);
        EXPECT_LT(Distance(line, FromColor(app.theme.GetColor("--accent", color4f{}))), 60)
            << line.r << "," << line.g << "," << line.b;
    }

    // Color curves have a dashed identity line behind them; a single value's curve has none.
    TEST_F(GpuInspectorFields, ColorCurvesHaveADashedIdentityLine)
    {
        auto onDiagonal = [&](ID id, float from, float to, int& ink, int& gaps) {
            const IntRect r = app.InnerOf(id);
            const Rgb plot = app.At(r.x + 2, r.y + 2);
            ink = gaps = 0;
            for (int x = r.x + int(float(r.w) * from); x < r.x + int(float(r.w) * to); ++x)
            {
                const float t = float(x - r.x) / float(r.w);
                const Rgb p = app.BrightestAround(x, r.y + r.h - int(std::lround(t * float(r.h))));
                (Distance(p, plot) > 120 ? ink : gaps)++;
            }
        };
        int ink = 0, gaps = 0;
        onDiagonal(app.colorsID, 0.3f, 0.7f, ink, gaps);
        EXPECT_GT(ink, 10) << "the identity line";
        EXPECT_GT(gaps, 5) << "dashed";
        onDiagonal(app.curveID, 0.15f, 0.35f, ink, gaps);
        EXPECT_EQ(ink, 0) << "no identity line behind one value";
    }

    // Hovered, the field shows a small open button in its top-right corner.
    TEST_F(GpuInspectorFields, AHoveredCurveFieldShowsAnOpenButton)
    {
        const IntRect field = app.RectOf(app.curveID);
        const int2 button{ field.x + field.w - Px(4) - Px(15), field.y + Px(4) + Px(9) };   // left of its chevron
        const Rgb plot = app.At(field.x + Px(20), field.y + Px(10));
        EXPECT_LT(Distance(app.At(button.x, button.y), plot), 10) << "no button until hovered";

        app.InjectMouseMove(app.windowID, int2{ field.x + field.w / 2, field.y + field.h / 2 });
        ASSERT_TRUE(app.Frames(4));
        ASSERT_TRUE(app.Read());
        EXPECT_GT(Distance(app.At(button.x, button.y), plot), 30) << "the button, hovered";
    }
}
