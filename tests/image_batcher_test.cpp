// ImageBatcher: where a draw is cut so each part reads no more images than a backend binds per
// draw (SDL GPU), and which images each part reads.

#include <gtest/gtest.h>

#include <gui_core/graphics/gui_image_batcher.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <vector>

namespace
{
    using namespace tachyon;

    template <class T>
    std::vector<std::byte> Bytes(const std::vector<T>& primitives)
    {
        std::vector<std::byte> bytes(primitives.size() * sizeof(T));
        std::memcpy(bytes.data(), primitives.data(), bytes.size());
        return bytes;
    }

    // Color style 1: solid, on gradient page slot 0. Color style 2: a texture (image 9, slot 8),
    // its gradient page slot 0 too. Color style 3: a gradient on page slot 3.
    struct Styles
    {
        std::vector<VisualColorStyle> color = std::vector<VisualColorStyle>(3);
        std::vector<VisualBoxStyle>   box   = std::vector<VisualBoxStyle>(2);

        Styles()
        {
            color[0].gradientPage = 0;
            color[1].gradientPage = 0;
            color[1].type         = ColorType::StretchedTexture;
            color[1].textureIndex = ImageIndex(9);
            color[2].gradientPage = 3;
            // Box style 0: nothing. Box style 1: a border in style 3, the background in style 2.
            box[1].borderColorIndex[0]  = VisualColorStyleIndex(3);
            box[1].backgroundColorIndex = VisualColorStyleIndex(2);
        }
    };

    QuadPrimitive Quad(uint32_t image, int16_t colorStyle = 1)
    {
        QuadPrimitive quad{};
        quad.textureIndex = ImageIndex(image);
        quad.colorIndex   = colorStyle;
        return quad;
    }

    std::set<uint32_t> Images(const ImageBatcher::Batch& batch)
    {
        return { batch.images.begin(), batch.images.begin() + batch.imageCount };
    }
}

TEST(ImageBatcher, OneTextureIsOneDraw)
{
    Styles styles;
    ImageBatcher batcher;
    batcher.SetStyles(styles.color, styles.box);
    const std::vector<QuadPrimitive> quads(50, Quad(7));
    const std::vector<std::byte> bytes = Bytes(quads);
    std::vector<ImageBatcher::Batch> batches;
    batcher.Split(PrimitiveKind::Quad, bytes, 10, 16, batches);

    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(batches[0].first, 10u) << "from the pass's first primitive";
    EXPECT_EQ(batches[0].count, 50u);
    EXPECT_EQ(Images(batches[0]), (std::set<uint32_t>{ 0, 6 })) << "the gradient page and the texture, as 0-based slots";
}

// Forty textures, each with the gradient page: fifteen quads a draw (sixteen images), in order.
TEST(ImageBatcher, ManyTexturesAreCutIntoConsecutiveDraws)
{
    Styles styles;
    ImageBatcher batcher;
    batcher.SetStyles(styles.color, styles.box);
    std::vector<QuadPrimitive> quads;
    for (uint32_t i = 0; i < 40; ++i)
        quads.push_back(Quad(20 + i));
    const std::vector<std::byte> bytes = Bytes(quads);
    std::vector<ImageBatcher::Batch> batches;
    batcher.Split(PrimitiveKind::Quad, bytes, 0, 16, batches);

    ASSERT_EQ(batches.size(), 3u);
    uint32_t next = 0;
    for (const ImageBatcher::Batch& batch : batches)
    {
        EXPECT_EQ(batch.first, next) << "consecutive, in order";
        EXPECT_LE(batch.imageCount, 16u);
        const std::set<uint32_t> images = Images(batch);
        for (uint32_t p = batch.first; p < batch.first + batch.count; ++p)
        {
            EXPECT_TRUE(images.contains(0)) << "the gradient page";
            EXPECT_TRUE(images.contains(20 + p - 1)) << "quad " << p << "'s texture";
        }
        next = batch.first + batch.count;
    }
    EXPECT_EQ(next, 40u) << "every quad drawn";
    EXPECT_EQ(batches[0].count, 15u);
    EXPECT_EQ(batches[1].count, 15u);
    EXPECT_EQ(batches[2].count, 10u);
}

// A box reads its color style's images and those of the color styles its box styles name,
// for the styles it blends in only.
TEST(ImageBatcher, ABoxReadsWhatItsStylesName)
{
    Styles styles;
    ImageBatcher batcher;
    batcher.SetStyles(styles.color, styles.box);
    BoxPrimitive box{};
    box.colorIndex = 0;
    box.styles[0]  = 0xFF000000u | 1u;   // box style 1, all of it
    box.styles[1]  = 0u;                 // box style 0, no share
    const std::vector<std::byte> bytes = Bytes(std::vector<BoxPrimitive>{ box });
    std::vector<ImageBatcher::Batch> batches;
    batcher.Split(PrimitiveKind::Box, bytes, 0, 16, batches);

    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(Images(batches[0]), (std::set<uint32_t>{ 0, 3, 8 }))
        << "style 3's page, style 2's page and texture";
}

// A glyph reads its atlas page; dots read no image.
TEST(ImageBatcher, GlyphsReadTheirAtlasPageAndDotsNothing)
{
    Styles styles;
    ImageBatcher batcher;
    batcher.SetStyles(styles.color, styles.box);
    GlyphPrimitive glyph{};
    glyph.textureIndex   = ImageIndex(2);
    glyph.textColorIndex = 1;
    std::vector<ImageBatcher::Batch> batches;
    batcher.Split(PrimitiveKind::Glyph, Bytes(std::vector<GlyphPrimitive>(5, glyph)), 0, 16, batches);
    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(Images(batches[0]), (std::set<uint32_t>{ 0, 1 }));

    batches.clear();
    batcher.Split(PrimitiveKind::Dot, Bytes(std::vector<DotPrimitive>(5)), 0, 16, batches);
    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(batches[0].count, 5u);
    EXPECT_EQ(batches[0].imageCount, 0u);
}

// No primitives, no draw.
TEST(ImageBatcher, NothingToDrawIsNoDraw)
{
    ImageBatcher batcher;
    std::vector<ImageBatcher::Batch> batches;
    batcher.Split(PrimitiveKind::Quad, {}, 0, 16, batches);
    EXPECT_TRUE(batches.empty());
}
