// GradientF: float stops (HDR kept), up to 64, each blending to the next linearly, smoothly or
// in a step; the same blending as Gradient's for the stops they share. A row of its pixels in
// the gradient atlas, for its previews.

#include <gtest/gtest.h>

#include <gui_core/style/gui_gradient_manager.h>
#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_style_gradient.h>

#include <vector>

namespace
{
    using namespace tachyon;

    GradientF BlackToWhite(StopInterpolation interpolation = StopInterpolation::Linear)
    {
        GradientF g;
        g.AddStop(color4f{ 0, 0, 0, 1 }, 0.0f, interpolation);
        g.AddStop(color4f{ 1, 1, 1, 1 }, 1.0f);
        return g;
    }

    TEST(GradientFTest, ItBlendsLinearlyBetweenItsStops)
    {
        const GradientF g = BlackToWhite();
        EXPECT_FLOAT_EQ(g.Sample(0.0f).r, 0.0f);
        EXPECT_FLOAT_EQ(g.Sample(0.25f).r, 0.25f);
        EXPECT_FLOAT_EQ(g.Sample(1.0f).r, 1.0f);
        EXPECT_FLOAT_EQ(g.Sample(-1.0f).r, 0.0f) << "t clamped";
        EXPECT_FLOAT_EQ(g.Sample(2.0f).r, 1.0f);

        GradientF none;
        EXPECT_EQ(none.Sample(0.5f), (color4f{ 0, 0, 0, 0 })) << "no stops: transparent";
    }

    TEST(GradientFTest, ItKeepsColorsOverOne)
    {
        GradientF g;
        g.AddStop(color4f{ 0.0f, 0.0f, 0.0f, 1.0f }, 0.0f);
        g.AddStop(color4f{ 4.0f, 2.0f, 0.5f, 1.0f }, 1.0f);
        const color4f end = g.Sample(1.0f);
        EXPECT_FLOAT_EQ(end.r, 4.0f);
        EXPECT_FLOAT_EQ(g.Sample(0.5f).r, 2.0f) << "HDR blends as it is";
        std::vector<color4f> row(3);
        g.WriteTo(std::span<color4f>(row));
        EXPECT_FLOAT_EQ(row[2].g, 2.0f);
        std::vector<color4b> shown(3);
        g.WriteTo(std::span<color4b>(shown));
        EXPECT_EQ(shown[2].r, 255) << "shown clamped";
        EXPECT_EQ(shown[1].r, 255);
    }

    // Each stop blends to the next its own way: linearly, eased, or held to the next stop.
    TEST(GradientFTest, EachStopBlendsToTheNextItsOwnWay)
    {
        EXPECT_FLOAT_EQ(BlackToWhite(StopInterpolation::Smooth).Sample(0.25f).r, 0.15625f) << "smoothstep of a quarter";
        EXPECT_FLOAT_EQ(BlackToWhite(StopInterpolation::Smooth).Sample(0.5f).r, 0.5f);
        EXPECT_FLOAT_EQ(BlackToWhite(StopInterpolation::Constant).Sample(0.99f).r, 0.0f) << "held to the next stop";
        EXPECT_FLOAT_EQ(BlackToWhite(StopInterpolation::Constant).Sample(1.0f).r, 1.0f) << "the next stop's color at it";

        // Three stops: a step, then a straight blend.
        GradientF g;
        g.AddStop(color4f{ 1, 0, 0, 1 }, 0.0f, StopInterpolation::Constant);
        g.AddStop(color4f{ 0, 1, 0, 1 }, 0.5f, StopInterpolation::Linear);
        g.AddStop(color4f{ 0, 0, 1, 1 }, 1.0f);
        EXPECT_EQ(g.Sample(0.4f), (color4f{ 1, 0, 0, 1 }));
        EXPECT_EQ(g.Sample(0.5f), (color4f{ 0, 1, 0, 1 })) << "at the next stop: its color";
        EXPECT_FLOAT_EQ(g.Sample(0.75f).g, 0.5f);
        EXPECT_FLOAT_EQ(g.Sample(0.75f).b, 0.5f);
    }

    TEST(GradientFTest, StopsInAnyOrderAreSorted)
    {
        GradientF g;
        g.AddStop(color4f{ 1, 1, 1, 1 }, 1.0f);
        g.AddStop(color4f{ 0, 0, 0, 1 }, 0.0f, StopInterpolation::Smooth);
        EXPECT_FLOAT_EQ(g.Sample(0.25f).r, 0.15625f) << "the smooth stop is the first";
        EXPECT_FLOAT_EQ(g.stops[0].offset, 1.0f) << "the stops themselves stay as they are";
    }

    TEST(GradientFTest, ItHoldsUpTo64Stops)
    {
        GradientF g;
        for (int i = 0; i < GradientF::kMaxStops; ++i)
            ASSERT_TRUE(g.AddStop(color4f{ float(i) / 63.0f, 0, 0, 1 }, float(i) / 63.0f)) << i;
        EXPECT_FALSE(g.AddStop(color4f{ 1, 1, 1, 1 }, 0.5f)) << "full";
        EXPECT_EQ(g.StopCount(), 64);
        EXPECT_FALSE(g.AddStop(color4f{}, 0.5f));

        g.stops.push_back(GradientStop{ 0.0f, color4f{ 0, 1, 0, 1 } });   // past the limit, in code
        EXPECT_EQ(g.StopCount(), 64);
        EXPECT_FLOAT_EQ(g.Sample(0.0f).g, 0.0f) << "the stop past the limit is left out";
        GradientF clamped;
        clamped.AddStop(color4f{ 1, 0, 0, 1 }, -3.0f);
        EXPECT_FLOAT_EQ(clamped.stops[0].offset, 0.0f) << "offsets are clamped";

        // Offsets set past 0..1 in code are sampled as at its ends.
        GradientF outside;
        outside.stops = { GradientStop{ -1.0f, color4f{ 0, 0, 0, 1 } }, GradientStop{ 2.0f, color4f{ 1, 1, 1, 1 } } };
        EXPECT_FLOAT_EQ(outside.Sample(0.0f).r, 0.0f) << "the first stop's color at 0";
        EXPECT_FLOAT_EQ(outside.Sample(0.5f).r, 0.5f);
    }

    // The same stops blend as a Gradient's do, in every color space.
    TEST(GradientFTest, ItBlendsAsAGradientDoes)
    {
        for (const ColorSpace space : { ColorSpace::sRGB, ColorSpace::LinearRGB, ColorSpace::HSL, ColorSpace::HSV, ColorSpace::Lab, ColorSpace::Oklab })
        {
            Gradient g = Gradient::Linear(0.0f);
            g.colorSpace = space;
            g.AddStop(color4b{ 200, 20, 40, 255 }, 0.0f);
            g.AddStop(color4b{ 10, 120, 250, 128 }, 0.6f);
            g.AddStop(color4b{ 250, 250, 0, 0 }, 1.0f);
            std::vector<color4b> expected(64), got(64);
            g.WriteTo(std::span<color4b>(expected));
            GradientF::From(g).WriteTo(std::span<color4b>(got));
            EXPECT_EQ(got, expected) << int(space);
        }
    }

    TEST(GradientFTest, ItConvertsToAndFromAGradient)
    {
        Gradient g = Gradient::Linear(0.0f);
        g.colorSpace = ColorSpace::Oklab;
        g.AddStop(color4b{ 255, 0, 0, 255 }, 0.0f);
        g.AddStop(color4b{ 0, 0, 255, 255 }, 1.0f);
        const GradientF f = GradientF::From(g);
        ASSERT_EQ(f.stops.size(), 2u);
        EXPECT_EQ(f.colorSpace, ColorSpace::Oklab);
        EXPECT_EQ(f.stops[1].color, (color4f{ 0, 0, 1, 1 }));
        EXPECT_EQ(f.ToGradient(), g);

        GradientF many;
        many.AddStop(color4f{ 3.0f, 0.5f, -1.0f, 1.0f }, 0.0f, StopInterpolation::Constant);
        for (int i = 1; i < 12; ++i)
            many.AddStop(color4f{ 0, 0, 0, 1 }, float(i) / 11.0f);
        const Gradient eight = many.ToGradient();
        EXPECT_EQ(eight.stopCount, Gradient::kMaxStops) << "the first 8";
        EXPECT_EQ(eight.stops[0].GetColor4b(), (color4b{ 255, 128, 0, 255 })) << "clamped to 0..1";
        EXPECT_EQ(eight.type, GradientType::Linear);
    }

    // A row of pixels in the atlas: the same pixels share a slot, others get their own,
    // resampled to the atlas width.
    TEST(GradientRowPixelsTest, ARowOfPixelsGoesIntoTheAtlas)
    {
        GradientManager atlas(8);
        EXPECT_FALSE(atlas.AcquireRowPixels({}).IsValid());

        const std::vector<color4b> red(8, color4b{ 255, 0, 0, 255 });
        const GradientHandle a = atlas.AcquireRowPixels(red);
        ASSERT_TRUE(a.IsValid());
        const GradientHandle b = atlas.AcquireRowPixels(red);
        EXPECT_EQ(a, b) << "the same pixels share a slot";
        EXPECT_EQ(atlas.GetRefcount(a), 2);

        std::vector<color4b> ramp(4);
        for (int i = 0; i < 4; ++i)
            ramp[size_t(i)] = color4b{ uint8_t(i * 80), 0, 0, 255 };
        const GradientHandle c = atlas.AcquireRowPixels(ramp);
        ASSERT_TRUE(c.IsValid());
        EXPECT_NE(c, a);
        const AtlasLocation at = atlas.GetLocation(c);
        const std::span<const color4b> page = atlas.PageTextureData(at.page);
        const color4b* row = page.data() + size_t(at.row) * atlas.Width() + at.col;
        for (int i = 0; i < 8; ++i)
            EXPECT_EQ(row[i].r, ramp[size_t(i / 2)].r) << "resampled to the atlas width, texel " << i;
    }

    // A color style takes a row as its ramp in place of its gradient's.
    TEST(GradientRowPixelsTest, AColorStyleTakesARowAsItsRamp)
    {
        GradientManager atlas(4);
        ColorStyle style = ColorStyle::Create(Gradient::Linear(color4b{ 0, 0, 0, 255 }, color4b{ 0, 0, 0, 255 }), ColorType::LinearGradient);
        const std::vector<color4b> row = { { 10, 0, 0, 255 }, { 20, 0, 0, 255 }, { 30, 0, 0, 255 }, { 40, 0, 0, 255 } };
        GradientHandle handle;
        (void)style.ToVisualStyle(atlas, ImageIndex::Invalid, handle, row);
        ASSERT_TRUE(handle.IsValid());
        const AtlasLocation at = atlas.GetLocation(handle);
        const color4b* texels = atlas.PageTextureData(at.page).data() + size_t(at.row) * atlas.Width() + at.col;
        EXPECT_EQ(texels[0].r, 10);
        EXPECT_EQ(texels[3].r, 40);

        GradientHandle own;
        (void)style.ToVisualStyle(atlas, ImageIndex::Invalid, own);
        EXPECT_NE(own, handle) << "without a row: the gradient's own";
    }
}
