// SVG gradients through the NanoSVG bridge: an icon rasterized in color at 1:1, its pixels
// sampled. Gradients in objectBoundingBox units (the SVG default) take plain numbers as a
// fraction of the shape's box, as in the user-space form.

#include <gtest/gtest.h>

#include <gui_core/text/skribidi/internal/nanosvg_bridge.h>

#include <skribidi/skb_common.h>
#include <skribidi/skb_icon_collection.h>
#include <skribidi/skb_rasterizer.h>

#include <string>
#include <vector>

namespace
{
    struct Rgba { int r, g, b, a; };

    // A 16 x 16 icon holding `body`, rasterized in color.
    class ColorIcon
    {
    public:
        explicit ColorIcon(const std::string& body)
        {
            const std::string svg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16">)"
                                  + body + "</svg>";
            skb_icon_collection_t* icons = skb_icon_collection_create();
            const skb_icon_handle_t handle = tachyon::RegisterNanoSVG(icons, "test", svg.data(), svg.size());
            pixels.assign(size_t(kSide * kSide * 4), 0);
            if (handle)
            {
                skb_rasterizer_config_t config = skb_rasterizer_get_default_config();
                skb_rasterizer_t* rasterizer = skb_rasterizer_create(&config);
                skb_temp_alloc_t* temp = skb_temp_alloc_create(64 * 1024);
                skb_image_t image{ pixels.data(), kSide, kSide, kSide * 4, 4 };
                skb_rasterizer_draw_color_icon(rasterizer, temp, skb_icon_collection_get_icon(icons, handle),
                                               skb_vec2_t{ 1.f, 1.f }, SKB_RASTERIZE_ALPHA_MASK, 0, 0, &image);
                skb_temp_alloc_destroy(temp);
                skb_rasterizer_destroy(rasterizer);
            }
            skb_icon_collection_destroy(icons);
        }

        Rgba At(int x, int y) const
        {
            const uint8_t* p = &pixels[size_t((y * kSide + x) * 4)];
            return { p[0], p[1], p[2], p[3] };
        }

    private:
        static constexpr int kSide = 16;
        std::vector<uint8_t> pixels;
    };

    const char* const kStops = R"(<stop offset="0" stop-color="#ff0000"/><stop offset="1" stop-color="#0000ff"/>)";

    // Red in the top-left corner, blue in the bottom-right, a mix in the middle.
    void ExpectRedToBlue(const ColorIcon& icon)
    {
        const Rgba first = icon.At(1, 1), middle = icon.At(8, 8), last = icon.At(14, 14);
        EXPECT_GT(first.r, 180);
        EXPECT_LT(first.b, 75);
        EXPECT_GT(last.b, 180);
        EXPECT_LT(last.r, 75);
        EXPECT_GT(middle.r, 60);
        EXPECT_GT(middle.b, 60);
    }

    TEST(SvgGradient, ABoundingBoxLinearGradientSpansTheShape)
    {
        const ColorIcon icon(std::string(R"(<defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1">)") + kStops +
                             R"svg(</linearGradient></defs><rect width="16" height="16" fill="url(#g)"/>)svg");
        ExpectRedToBlue(icon);
    }

    TEST(SvgGradient, AUserSpaceLinearGradientSpansItsPoints)
    {
        const ColorIcon icon(std::string(R"(<defs><linearGradient id="g" gradientUnits="userSpaceOnUse" x1="0" y1="0" x2="16" y2="16">)") + kStops +
                             R"svg(</linearGradient></defs><rect width="16" height="16" fill="url(#g)"/>)svg");
        ExpectRedToBlue(icon);
    }

    // A box that isn't at the origin: the fractions are of its own box.
    TEST(SvgGradient, ABoundingBoxGradientFollowsTheShapesBox)
    {
        const ColorIcon icon(std::string(R"(<defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="0">)") + kStops +
                             R"svg(</linearGradient></defs><rect x="8" y="0" width="8" height="16" fill="url(#g)"/>)svg");
        EXPECT_GT(icon.At(8, 8).r, 180) << "its left edge is the start";
        EXPECT_GT(icon.At(15, 8).b, 180) << "its right edge the end";
    }

    // Red at the centre, blue at the edge of a circle half the box wide.
    TEST(SvgGradient, ABoundingBoxRadialGradientSpansTheShape)
    {
        const ColorIcon icon(std::string(R"(<defs><radialGradient id="g" cx="0.5" cy="0.5" r="0.5">)") + kStops +
                             R"svg(</radialGradient></defs><rect width="16" height="16" fill="url(#g)"/>)svg");
        const Rgba centre = icon.At(8, 8), edge = icon.At(8, 0);
        EXPECT_GT(centre.r, 180);
        EXPECT_LT(centre.b, 75);
        EXPECT_GT(edge.b, 150);
    }
}
