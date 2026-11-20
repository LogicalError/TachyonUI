// The text atlas pages a backend keeps in its image set (ImageSetTextAtlasManager, as Direct3D
// 12 and SDL GPU use): changed rows go into a page's image rather than a new one.

#include <gtest/gtest.h>

#include <gui_core/graphics/gui_image.h>
#include <gui_core/graphics/gui_image_set_text_atlas.h>
#include <gui_core/graphics/null/gui_null_graphics_context.h>

#include <vector>

namespace tachyon
{
    namespace
    {
        // Counts the images made and the rows written into them; `partial` false refuses
        // UpdateImageRows, as a backend without it does.
        class RecordingContext : public NullGraphicsContext
        {
        public:
            bool partial = true;
            int  created = 0;
            struct Rows { uint32_t first, count; };
            std::vector<Rows> updates;

            Image* CreateImage(void* data, uint2 size, ImageFormat format, Sampler sampler, bool mipmapped) override
            {
                ++created;
                return NullGraphicsContext::CreateImage(data, size, format, sampler, mipmapped);
            }
            bool UpdateImageRows(Image*, uint32_t firstRow, uint32_t rowCount, const void*) override
            {
                if (!partial)
                    return false;
                updates.push_back({ firstRow, rowCount });
                return true;
            }
        };

        // An image set that hands out indices and remembers what each points at.
        class FakeImageSet : public ImageSet
        {
        public:
            std::vector<Image*> images;
            void       Clear() override { images.clear(); }
            ImageIndex Add(Image* image) override { images.push_back(image); return ImageIndex(uint32_t(images.size())); }
            void       Remove(ImageIndex) override {}
            ImageIndex ReserveIndex() { return Add(nullptr); }
            void       SetImage(ImageIndex index, Image* image) { images[uint32_t(index) - 1] = image; }
        };

        using Atlas = ImageSetTextAtlasManager<FakeImageSet, Image>;
    }

    // Glyphs added to a page change some of its rows: those go into the page's image, which
    // its slot keeps pointing at.
    TEST(ImageSetTextAtlas, ChangedRowsGoIntoThePagesImage)
    {
        RecordingContext gpu;
        FakeImageSet     set;
        Atlas            atlas;
        ASSERT_TRUE(atlas.Initialize(&gpu, &set));
        const uint2 size{ 64, 64 };
        std::vector<uint8_t> pixels(size_t(size.x) * size.y, 0);
        const TextureIndex page = atlas.CreateTexture(pixels.data(), size, 1);
        const ImageIndex index = atlas.GetImageIndex(page);
        atlas.SyncTextureIndices();
        Image* const image = set.images[uint32_t(index) - 1];
        ASSERT_NE(image, nullptr);
        EXPECT_EQ(gpu.created, 1);

        atlas.UpdateTextureRows(page, pixels.data(), size, 8, 4);
        atlas.SyncTextureIndices();
        EXPECT_EQ(gpu.created, 1) << "no new image";
        ASSERT_EQ(gpu.updates.size(), 1u);
        EXPECT_EQ(gpu.updates[0].first, 8u);
        EXPECT_EQ(gpu.updates[0].count, 4u);
        EXPECT_EQ(set.images[uint32_t(index) - 1], image);

        gpu.DestroyImageAtEnd(image, DeviceScope::Frame);
    }

    // A page that grew is a new image; so is one on a backend that can't write rows.
    TEST(ImageSetTextAtlas, AGrownPageOrABackendWithoutRowWritesMakesANewImage)
    {
        RecordingContext gpu;
        FakeImageSet     set;
        Atlas            atlas;
        ASSERT_TRUE(atlas.Initialize(&gpu, &set));
        std::vector<uint8_t> pixels(size_t(128) * 128, 0);
        const TextureIndex page = atlas.CreateTexture(pixels.data(), uint2{ 64, 64 }, 1);
        const ImageIndex index = atlas.GetImageIndex(page);

        atlas.UpdateTextureRows(page, pixels.data(), uint2{ 128, 128 }, 0, 8);
        EXPECT_EQ(gpu.created, 2) << "grown: a new image";
        EXPECT_TRUE(gpu.updates.empty());

        gpu.partial = false;
        atlas.UpdateTextureRows(page, pixels.data(), uint2{ 128, 128 }, 0, 8);
        EXPECT_EQ(gpu.created, 3) << "no row writes: a new image";

        atlas.SyncTextureIndices();
        gpu.DestroyImageAtEnd(set.images[uint32_t(index) - 1], DeviceScope::Frame);
    }
}
