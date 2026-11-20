#include "gtest/gtest.h"
#include <gui_core/style/gui_gradient_manager.h>
#include <gui_core/style/gui_style.h>
#include <gui_core/gui_base_types.h>
#include <gui_core/graphics/null/gui_null_graphics_context.h>
#include <algorithm>
#include <vector>

namespace tachyon
{

    static color4b ReadPixel(const GradientManager& gm, const AtlasLocation& loc, uint16_t colOffset = 0)
    {
        auto page = gm.PageTextureData(loc.page);
        size_t width = gm.Width();
        return page[size_t(loc.row) * width + size_t(loc.col + colOffset)];
    }

    // --------------------------------------------------------------------
    // Core lifetime behavior
    // --------------------------------------------------------------------

    TEST(GradientManagerTest, AcquireGradient_EmptyReturnsInvalid)
    {
        GradientManager gm(8);
        Gradient g; // default: type == None, stopCount == 0
        EXPECT_FALSE(gm.AcquireGradient(g).IsValid());

        g.type = GradientType::Linear;
        g.stopCount = 0;
        EXPECT_FALSE(gm.AcquireGradient(g).IsValid());
    }

    TEST(GradientManagerTest, AcquireGradient_WritesRowAndDedups)
    {
        const size_t width = 8;
        GradientManager gm(width);

        Gradient g;
        g.type = GradientType::Linear;
        g.AddStop(color4f{ 1, 1, 1, 1 }, 0.0f);
        g.AddStop(color4f{ 0, 0, 0, 1 }, 1.0f);

        GradientHandle h0 = gm.AcquireGradient(g);
        ASSERT_TRUE(h0.IsValid());

        AtlasLocation loc0 = gm.GetLocation(h0);
        EXPECT_EQ(loc0.width, width);
        EXPECT_EQ(loc0.col, 0);
        EXPECT_EQ(loc0.page, 0);

        // Row pixels should be non-zero after write.
        bool any = false;
        for (uint16_t c = 0; c < width; ++c)
        {
            color4b px = ReadPixel(gm, loc0, c);
            if (px.r || px.g || px.b || px.a) { any = true; break; }
        }
        EXPECT_TRUE(any);

        // Dedup: acquiring identical content yields the same handle (refcount bumped).
        GradientHandle h1 = gm.AcquireGradient(g);
        EXPECT_EQ(h0, h1);
        EXPECT_EQ(gm.GetRefcount(h0), 2u);

        // Distinct gradient -> different slot.
        Gradient g2 = g;
        g2.AddStop(color4f{ 0.5f, 0.5f, 0.5f, 1.0f }, 0.5f);
        GradientHandle h2 = gm.AcquireGradient(g2);
        ASSERT_TRUE(h2.IsValid());
        EXPECT_NE(h0, h2);
    }

    TEST(GradientManagerTest, SolidColor_DedupsAndReleases)
    {
        GradientManager gm(16);

        GradientHandle h0 = gm.AcquireSolidColor(color4b{ 100, 50, 25, 255 });
        GradientHandle h1 = gm.AcquireSolidColor(color4b{ 100, 50, 25, 255 });
        ASSERT_TRUE(h0.IsValid());
        EXPECT_EQ(h0, h1);
        EXPECT_EQ(gm.GetRefcount(h0), 2u);

        EXPECT_FALSE(gm.Release(h0));  // still held by h1
        EXPECT_TRUE(gm.IsValid(h1));
        EXPECT_EQ(gm.GetRefcount(h1), 1u);

        EXPECT_TRUE(gm.Release(h1));   // reclaimed
        EXPECT_FALSE(gm.IsValid(h0));
        EXPECT_FALSE(gm.IsValid(h1));
    }

    TEST(GradientManagerTest, Release_ReclaimsGradientRowViaFreeList)
    {
        const size_t width = 8;
        GradientManager gm(width);

        Gradient g;
        g.type = GradientType::Linear;
        g.AddStop(color4f{ 1, 0, 0, 1 }, 0.0f);
        g.AddStop(color4f{ 0, 0, 1, 1 }, 1.0f);

        GradientHandle h = gm.AcquireGradient(g);
        AtlasLocation loc = gm.GetLocation(h);
        ASSERT_TRUE(h.IsValid());

        EXPECT_TRUE(gm.Release(h));
        EXPECT_FALSE(gm.IsValid(h));

        // A different gradient should reuse the freed row.
        Gradient g2;
        g2.type = GradientType::Linear;
        g2.AddStop(color4f{ 0, 1, 0, 1 }, 0.0f);
        g2.AddStop(color4f{ 1, 1, 0, 1 }, 1.0f);

        GradientHandle h2 = gm.AcquireGradient(g2);
        AtlasLocation loc2 = gm.GetLocation(h2);
        EXPECT_EQ(loc2.row, loc.row);
        EXPECT_EQ(loc2.page, loc.page);
        EXPECT_TRUE(gm.Release(h2));
    }

    TEST(GradientManagerTest, SmallColors_PackIntoSharedRow)
    {
        const size_t width = 16;
        GradientManager gm(width);

        GradientHandle s1 = gm.AcquireSolidColor(color4b{ 1, 2, 3, 4 });
        GradientHandle s2 = gm.AcquireSolidColor(color4b{ 5, 6, 7, 8 });
        AtlasLocation a = gm.GetLocation(s1);
        AtlasLocation b = gm.GetLocation(s2);
        EXPECT_EQ(a.row, b.row);
        EXPECT_EQ(a.page, b.page);
        EXPECT_LT(a.col, b.col);
        EXPECT_EQ(a.width, 1u);
        EXPECT_EQ(b.width, 1u);

        // 4-color block should also land on (possibly same) small row.
        GradientHandle box = gm.AcquireColors(
            color4b{ 9, 9, 9, 9 }, color4b{ 10, 10, 10, 10 },
            color4b{ 11, 11, 11, 11 }, color4b{ 12, 12, 12, 12 });
        AtlasLocation loc = gm.GetLocation(box);
        EXPECT_EQ(loc.width, 4u);

        gm.Release(s1);
        gm.Release(s2);
        gm.Release(box);
    }

    TEST(GradientManagerTest, Generation_InvalidatesStaleHandles)
    {
        GradientManager gm(8);
        GradientHandle h = gm.AcquireSolidColor(color4b{ 10, 20, 30, 40 });
        GradientHandle stale = h;
        ASSERT_TRUE(gm.Release(h));
        EXPECT_FALSE(gm.IsValid(stale));

        // Reacquiring may reuse the same slot but a new generation - the old
        // stale handle must NOT resolve to the new slot.
        GradientHandle h2 = gm.AcquireSolidColor(color4b{ 99, 99, 99, 99 });
        EXPECT_TRUE(gm.IsValid(h2));
        if (h2.SlotIndex() == stale.SlotIndex())
        {
            EXPECT_NE(h2.Generation(), stale.Generation());
        }
        EXPECT_FALSE(gm.IsValid(stale));
        gm.Release(h2);
    }

    TEST(GradientManagerTest, AddRef_BalancesWithRelease)
    {
        GradientManager gm(8);
        GradientHandle h = gm.AcquireSolidColor(color4b{ 50, 50, 50, 255 });
        gm.AddRef(h);
        gm.AddRef(h);
        EXPECT_EQ(gm.GetRefcount(h), 3u);
        EXPECT_FALSE(gm.Release(h));
        EXPECT_FALSE(gm.Release(h));
        EXPECT_TRUE(gm.Release(h));
    }

    // --------------------------------------------------------------------
    // Multi-page atlas growth
    // --------------------------------------------------------------------

    TEST(GradientManagerTest, Pages_GrowInHeightBeforeAllocatingNewPage)
    {
        const size_t width = 4;
        GradientManager gm(width);

        // Allocate many unique gradients; initial height is small so the page
        // must either grow or a new page must be added. Within kMaxPageHeight
        // it should stay on page 0.
        std::vector<GradientHandle> handles;
        for (int i = 0; i < 200; ++i)
        {
            Gradient g;
            g.type = GradientType::Linear;
            g.AddStop(color4b{ byte(i & 0xFF), 0, 0, 255 }, 0.0f);
            g.AddStop(color4b{ 0, byte(i & 0xFF), 0, 255 }, 1.0f);
            handles.push_back(gm.AcquireGradient(g));
        }
        EXPECT_EQ(gm.PageCount(), 1u);
        EXPECT_GE(gm.PageHeight(0), 200u);

        for (auto h : handles) gm.Release(h);
    }

    // --------------------------------------------------------------------
    // Uploads: only the rows written go up
    // --------------------------------------------------------------------

    namespace
    {
        // Counts the images made and the rows written into them; `partial` false refuses
        // UpdateImageRows, as a backend without it does.
        class RecordingContext : public NullGraphicsContext
        {
        public:
            bool partial = true;
            int  created = 0;
            struct Rows { uint32_t first, count; std::vector<color4b> pixels; };
            std::vector<Rows> updates;
            uint32_t width = 0;

            Image* CreateImage(void* data, uint2 size, ImageFormat format, Sampler sampler, bool mipmapped) override
            {
                ++created;
                width = size.x;
                return NullGraphicsContext::CreateImage(data, size, format, sampler, mipmapped);
            }
            bool UpdateImageRows(Image*, uint32_t firstRow, uint32_t rowCount, const void* data) override
            {
                if (!partial)
                    return false;
                const auto* pixels = static_cast<const color4b*>(data);
                updates.push_back({ firstRow, rowCount, std::vector<color4b>(pixels, pixels + size_t(rowCount) * width) });
                return true;
            }
        };
    }

    // A color added to a page already on the GPU sends its row alone, not the page.
    TEST(GradientManagerTest, AColorAddedSendsOnlyItsRow)
    {
        GradientManager gm(8);
        RecordingContext gpu;
        Gradient a; a.type = GradientType::Linear; a.AddStop(color4b{ 255, 0, 0, 255 }, 0.0f); a.AddStop(color4b{ 0, 0, 255, 255 }, 1.0f);
        const GradientHandle ha = gm.AcquireGradient(a);
        Image* const image = gm.GetGradientImage(&gpu, 0);
        ASSERT_NE(image, nullptr);
        EXPECT_EQ(gpu.created, 1);
        EXPECT_TRUE(gpu.updates.empty());
        EXPECT_EQ(gm.GetGradientImage(&gpu, 0), image) << "nothing written: nothing sent";
        EXPECT_TRUE(gpu.updates.empty());

        Gradient b; b.type = GradientType::Linear; b.AddStop(color4b{ 255, 0, 0, 255 }, 0.0f); b.AddStop(color4b{ 0, 255, 0, 255 }, 1.0f);
        const GradientHandle hb = gm.AcquireGradient(b);
        const uint32_t row = gm.GetLocation(hb).row;
        EXPECT_EQ(gm.GetGradientImage(&gpu, 0), image) << "the same image, written into";
        EXPECT_EQ(gpu.created, 1);
        ASSERT_EQ(gpu.updates.size(), 1u);
        EXPECT_EQ(gpu.updates[0].first, row);
        EXPECT_EQ(gpu.updates[0].count, 1u);
        const auto page = gm.PageTextureData(0);
        EXPECT_TRUE(std::equal(gpu.updates[0].pixels.begin(), gpu.updates[0].pixels.end(), page.begin() + ptrdiff_t(row) * 8))
            << "the row's pixels as they are now";

        gm.Release(ha);
        gm.Release(hb);
        gm.ReleaseAll(&gpu);
    }

    // A page that grew is a bigger image, made again; a backend that can't write part of an
    // image makes it again too.
    TEST(GradientManagerTest, AGrownPageOrABackendWithoutRowWritesMakesTheImageAgain)
    {
        GradientManager gm(4);
        RecordingContext gpu;
        std::vector<GradientHandle> handles;
        auto add = [&](int i) {
            Gradient g; g.type = GradientType::Linear;
            g.AddStop(color4b{ byte(i), 0, 0, 255 }, 0.0f); g.AddStop(color4b{ 0, byte(i), 0, 255 }, 1.0f);
            handles.push_back(gm.AcquireGradient(g));
        };
        add(0);
        gm.GetGradientImage(&gpu, 0);
        const size_t height = gm.PageHeight(0);
        for (int i = 1; gm.PageHeight(0) == height; ++i)
            add(i);
        gm.GetGradientImage(&gpu, 0);
        EXPECT_EQ(gpu.created, 2) << "grown: a new image";
        EXPECT_TRUE(gpu.updates.empty());

        gpu.partial = false;
        add(1000);
        gm.GetGradientImage(&gpu, 0);
        EXPECT_EQ(gpu.created, 3) << "no row writes: a new image";

        for (auto h : handles) gm.Release(h);
        gm.ReleaseAll(&gpu);
    }

    // --------------------------------------------------------------------
    // ClearAll / external holders need to re-register
    // --------------------------------------------------------------------

    TEST(GradientManagerTest, ClearAll_StaleHandlesAndResetsAllocator)
    {
        GradientManager gm(8);
        auto h0 = gm.AcquireSolidColor(color4b{ 1, 1, 1, 255 });
        auto h1 = gm.AcquireSolidColor(color4b{ 2, 2, 2, 255 });
        EXPECT_TRUE(gm.IsValid(h0));
        EXPECT_TRUE(gm.IsValid(h1));

        gm.ClearAll();
        EXPECT_FALSE(gm.IsValid(h0));
        EXPECT_FALSE(gm.IsValid(h1));

        auto h2 = gm.AcquireSolidColor(color4b{ 3, 3, 3, 255 });
        EXPECT_TRUE(gm.IsValid(h2));
        gm.Release(h2);
    }

    // --------------------------------------------------------------------
    // Page table change tracking (for the renderer upload path)
    // --------------------------------------------------------------------

    TEST(GradientManagerTest, PageTable_TracksChangesAndClears)
    {
        GradientManager gm(4);
        EXPECT_TRUE(gm.HasPageTableChanged()); // freshly constructed
        gm.ClearPageTableChanged();
        EXPECT_FALSE(gm.HasPageTableChanged());

        gm.SetPagePhysicalImageIndex(0, 42);
        EXPECT_TRUE(gm.HasPageTableChanged());
        EXPECT_EQ(gm.GetPagePhysicalImageIndex(0), 42);
        gm.ClearPageTableChanged();

        // Same value again: no change.
        gm.SetPagePhysicalImageIndex(0, 42);
        EXPECT_FALSE(gm.HasPageTableChanged());

        gm.SetPagePhysicalImageIndex(0, 7);
        EXPECT_TRUE(gm.HasPageTableChanged());
    }

    // --------------------------------------------------------------------
    // Refcount overflow safety
    // --------------------------------------------------------------------

    // A leaky caller must not wrap the 16-bit refcount (the slot would then be
    // freed while still referenced); it saturates, pinning the slot instead.
    TEST(GradientManagerTest, UnbalancedAcquiresNeverFreeALiveSlot)
    {
        GradientManager gm(8);
        const color4b c{ 10, 20, 30, 255 };

        GradientHandle h0 = gm.AcquireSolidColor(c); // ours; refcount 1
        ASSERT_TRUE(gm.IsValid(h0));

        // 65535 leaked acquires - enough to wrap uint16_t back to 0.
        for (int i = 0; i < 65535; ++i)
            (void)gm.AcquireSolidColor(c);

        // A legitimate balanced pair on the same content.
        GradientHandle h1 = gm.AcquireSolidColor(c);
        gm.Release(h1);

        EXPECT_TRUE(gm.IsValid(h0))
            << "slot was freed while a live handle referenced it (refcount wrapped)";
    }

    // Content dedup must distinguish colour order and repeated colours.
    TEST(GradientManagerTest, AcquireColors_OrderAndRepeatsAreDistinct)
    {
        GradientManager gm(16);
        const color4b red  { 255, 0, 0, 255 };
        const color4b blue { 0, 0, 255, 255 };

        GradientHandle rb = gm.AcquireColors(red, blue);
        GradientHandle br = gm.AcquireColors(blue, red);
        EXPECT_NE(rb, br);
        EXPECT_EQ(ReadPixel(gm, gm.GetLocation(br), 0), blue);

        GradientHandle rr = gm.AcquireColors(red, red);
        GradientHandle bb = gm.AcquireColors(blue, blue);
        EXPECT_NE(rr, bb);
        EXPECT_EQ(ReadPixel(gm, gm.GetLocation(bb), 0), blue);

        GradientHandle rbrb = gm.AcquireColors(red, blue, red, blue);
        GradientHandle brbr = gm.AcquireColors(blue, red, blue, red);
        EXPECT_NE(rbrb, brbr);
    }

}
