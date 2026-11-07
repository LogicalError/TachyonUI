// Pixel-level smoke tests on render-to-image windows: RenderWindowFrame renders
// offscreen (BGRA8) and ReadPixels copies the frame back, with no OS window.
// Assertions are structural only - a pixel inside a primitive
// must differ from one outside it, sampled from the same frame - never
// golden-image comparisons, so they hold across drivers and formats.

#include "gpu_test_harness.h"

#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_color_conversions.h>
#include <gui_core/graphics/gui_renderer.h>
#include <gui_core/graphics/vulkan/vk_text_atlas_manager.h>
#include <gui_core/graphics/gui_pixel_snap.h>
#include <gui_core/graphics/gui_primitive_renderer.h>
#include <gui_core/graphics/vulkan/vk_upload_worker.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace GpuTestHarness;

namespace
{
    class GpuReadbackTest : public GpuTest
    {
    protected:
        static constexpr tachyon::int2 kSize{ 320, 240 };

        tachyon::VisualColorStyleIndex solidColor{};
        tachyon::VisualColorStyleIndex gradientColor{};

        std::vector<uint8_t> framePixels = std::vector<uint8_t>(size_t(kSize.x) * kSize.y * 4);

        tachyon::vulkan::VulkanWindowContext* CreateReadbackWindow() { return CreateImageWindowContext(kSize); }

        void RegisterColorStyles()
        {
            solidColor = styleManager.RegisterColorStyle(
                tachyon::ColorStyle::Create(tachyon::color4b{ 255, 255, 255, 255 }));
            gradientColor = styleManager.RegisterColorStyle(
                tachyon::ColorStyle::Create(tachyon::color4b{ 255, 0, 0, 255 },
                                        tachyon::color4b{ 0, 255, 0, 255 },
                                        tachyon::color4b{ 0, 0, 255, 255 },
                                        tachyon::color4b{ 255, 0, 255, 255 },
                                        tachyon::ColorType::InterpolatedBox));
        }

        // Render one frame whose primitives are emitted by `emit`, then leave
        // the result in framePixels.
        template <typename EmitFn>
        void RenderFrame(tachyon::vulkan::VulkanWindowContext* ctx, EmitFn&& emit)
        {
            graphicsContext->PrepareFrame(styleManager, gradientManager, 0.0f);
            tachyon::GUIRenderer* r = graphicsContext->guiRenderer;
            r->Reset();
            if (r->BeginWindow(ctx->GetID()))
            {
                emit(r);
                r->EndWindow();
            }
            ctx->RenderWindowFrame(graphicsContext, tachyon::IntRect{ 0, 0, kSize.x, kSize.y }, false);
            ASSERT_TRUE(ctx->ReadPixels(framePixels));
        }

        // Raw 32-bit pixel of the last read frame (BGRA8, tightly packed).
        uint32_t PixelAt(const tachyon::vulkan::VulkanWindowContext*, int x, int y) const
        {
            const size_t off = (size_t(y) * kSize.x + size_t(x)) * 4;
            uint32_t v = 0;
            std::memcpy(&v, framePixels.data() + off, 4);
            return v;
        }

        static constexpr tachyon::IntRect kFullClip{ 0, 0, kSize.x, kSize.y };
    };

    TEST_F(GpuReadbackTest, QuadFillChangesCoveredPixels)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();

        // Solid quad over the left half; the right half stays background.
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x / 2, kSize.y }, kFullClip, solidColor);
        });

        const uint32_t inside  = PixelAt(ctx, kSize.x / 4,     kSize.y / 2);
        const uint32_t outside = PixelAt(ctx, kSize.x * 3 / 4, kSize.y / 2);
        EXPECT_NE(inside, outside)
            << "a solid quad must change the pixels it covers";
    }

    // A quad shows its color as given, like a box would: the quad shader used to convert
    // every color as if it were linear, which lightened anything custom-drawn.
    TEST_F(GpuReadbackTest, QuadKeepsItsColor)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        const tachyon::VisualColorStyleIndex color = styleManager.RegisterColorStyle(
            tachyon::ColorStyle::Create(tachyon::color4b{ 128, 64, 32, 255 }));

        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x, kSize.y }, kFullClip, color);
        });

        const uint32_t px = PixelAt(ctx, kSize.x / 2, kSize.y / 2);   // BGRA8
        const int b = int(px & 0xFF), g = int((px >> 8) & 0xFF), rr = int((px >> 16) & 0xFF);
        EXPECT_NEAR(rr, 128, 2);
        EXPECT_NEAR(g,  64, 2);
        EXPECT_NEAR(b,  32, 2);
    }

    // A textured quad stretches the part of the image its UV rect picks: of a two-pixel
    // image, red then blue, the left half is all red, the right half all blue, the whole
    // both, and the whole flipped both the other way round.
    TEST_F(GpuReadbackTest, ATexturedQuadDrawsThePartItsUVRectPicks)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        uint32_t pixels[2] = { 0xff0000ffu, 0xffff0000u };   // RGBA: red, blue
        tachyon::Image* image = vk->deviceContext.CreateImage(pixels, tachyon::uint2{ 2, 1 }, tachyon::ImageFormat::R8G8B8A8,
                                                              tachyon::Sampler::NearestRepeat, false);
        ASSERT_NE(image, nullptr);
        const tachyon::ImageIndex index = vk->RegisterImage(image);
        const tachyon::VisualColorStyleIndex stretched = styleManager.RegisterColorStyle(tachyon::ColorStyle::Create(
            tachyon::color4b{ 255, 255, 255, 255 }, tachyon::color4b{ 255, 255, 255, 255 }, tachyon::ColorType::StretchedTexture));

        // 'r' or 'b' for the pixel a quarter and three quarters across, '?' otherwise.
        const auto colors = [&](const tachyon::UVRect& uv) {
            for (int frame = 0; frame < 2; ++frame)   // the image's upload lands with the first
            {
                graphicsContext->BeginFrame();
                RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
                    r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x, kSize.y }, kFullClip, stretched, 0.0f, index, uv);
                });
            }
            std::string out;
            for (const int x : { kSize.x / 4, kSize.x * 3 / 4 })
            {
                const uint32_t px = PixelAt(ctx, x, kSize.y / 2);   // BGRA8
                const int b = int(px & 0xFF), g = int((px >> 8) & 0xFF), rr = int((px >> 16) & 0xFF);
                out += rr > 200 && g < 50 && b < 50 ? 'r' : b > 200 && g < 50 && rr < 50 ? 'b' : '?';
            }
            return out;
        };
        EXPECT_EQ(colors({}), "rb") << "the whole image";
        EXPECT_EQ(colors({ { 0.0f, 0.0f }, { 0.5f, 1.0f } }), "rr") << "its left half";
        EXPECT_EQ(colors({ { 0.5f, 0.0f }, { 1.0f, 1.0f } }), "bb") << "its right half";
        EXPECT_EQ(colors(tachyon::UVRect::FromPixels({ 1, 0, 1, 1 }, { 2, 1 })), "bb") << "its second pixel";
        EXPECT_EQ(colors({ { 1.0f, 0.0f }, { 0.0f, 1.0f } }), "br") << "flipped";
        EXPECT_FALSE(sink.HasErrors()) << sink.Dump();

        vk->UnregisterImage(index);
        vk->deviceContext.DestroyImageAtEnd(image, tachyon::DeviceScope::Application);
    }

    // A quad rounded to half its size is a disc (CustomDrawContext::RenderDisc, switch
    // knobs, slider thumbs): covered at every angle just inside its radius, empty just
    // outside, and the same in mirrored spots.
    TEST_F(GpuReadbackTest, QuadRoundedToHalfItsSizeIsADisc)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();

        constexpr int kD = 24, cx = 100, cy = 100;
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderQuad(tachyon::IntRect{ cx - kD / 2, cy - kD / 2, kD, kD }, kFullClip, solidColor, float(kD) / 2.0f);
        });

        const uint32_t empty = PixelAt(ctx, 10, 10);
        // Pixel centres sit at +0.5; sample at radius 9 (inside) and 11.5 along the diagonals
        // (inside a square's corner, outside the circle).
        for (int a = 0; a < 16; ++a)
        {
            const float t = float(a) * 3.14159265f / 8.0f;
            const int x = cx + int(std::floor(9.0f * std::cos(t))), y = cy + int(std::floor(9.0f * std::sin(t)));
            EXPECT_NE(PixelAt(ctx, x, y), empty) << "inside the disc at angle step " << a;
        }
        const int d = int(11.5f / 1.41421356f) + 1;
        EXPECT_EQ(PixelAt(ctx, cx + d,     cy + d),     empty);
        EXPECT_EQ(PixelAt(ctx, cx - d - 1, cy + d),     empty);
        EXPECT_EQ(PixelAt(ctx, cx + d,     cy - d - 1), empty);
        EXPECT_EQ(PixelAt(ctx, cx - d - 1, cy - d - 1), empty);
        // Mirrored edge pixels match.
        EXPECT_EQ(PixelAt(ctx, cx + 11, cy), PixelAt(ctx, cx - 12, cy));
        EXPECT_EQ(PixelAt(ctx, cx, cy + 11), PixelAt(ctx, cx, cy - 12));
    }

    TEST_F(GpuReadbackTest, DotRendersAtKnownPosition)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();

        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderDot(tachyon::float2{ 16, 16 }, kFullClip, 8.0f, tachyon::color4f{ 1, 1, 1, 1 });
        });

        const uint32_t atDot     = PixelAt(ctx, 16, 16);
        const uint32_t farCorner = PixelAt(ctx, kSize.x - 8, kSize.y - 8);
        EXPECT_NE(atDot, farCorner)
            << "the dot's center pixel must differ from untouched background";
    }

    TEST_F(GpuReadbackTest, GradientQuadVariesAcrossSpan)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();

        // Full-frame 4-corner interpolated quad: left third and right third
        // sample different ends of the gradient.
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x, kSize.y }, kFullClip, gradientColor);
        });

        const uint32_t left  = PixelAt(ctx, kSize.x / 6,     kSize.y / 2);
        const uint32_t right = PixelAt(ctx, kSize.x * 5 / 6, kSize.y / 2);
        EXPECT_NE(left, right)
            << "an interpolated quad must vary across its span";
    }

    TEST_F(GpuReadbackTest, ScopeCaptureReplayDrawsTranslated)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();

        tachyon::GUIRenderer* r = graphicsContext->guiRenderer;
        r->ClearScopeCaptures();
        const tachyon::ID scope = tachyon::ID::Invalid(); // any key for the capture map

        // Frame 1: capture a quad at the left edge (x = 0..40); EndScopeCapture
        // also draws it into the window this frame.
        RenderFrame(ctx, [&](tachyon::GUIRenderer* rr) {
            rr->BeginScopeCapture(scope);
            rr->SetTransform(tachyon::float3x2::Identity());
            rr->RenderQuad(tachyon::IntRect{ 0, 0, 40, kSize.y }, kFullClip, solidColor);
            rr->EndScopeCapture(scope);
        });
        const uint32_t bg = PixelAt(ctx, kSize.x - 20, kSize.y / 2);
        EXPECT_NE(PixelAt(ctx, 20, kSize.y / 2), bg) << "captured quad draws at the left this frame";

        // Frame 2: replay the cached quad translated right by 200px. It must now
        // cover x ~200..240 and have left the original left edge as background.
        RenderFrame(ctx, [&](tachyon::GUIRenderer* rr) {
            EXPECT_TRUE(rr->ReplayScopeCapture(scope, tachyon::int2{ 200, 0 }));
        });
        EXPECT_NE(PixelAt(ctx, 220, kSize.y / 2), bg) << "replayed quad appears at the translated position";
        EXPECT_EQ(PixelAt(ctx, 20,  kSize.y / 2), bg) << "the original position is background again";
    }

    TEST_F(GpuReadbackTest, LineRendersAlongPath)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();

        // Thick horizontal line at y=40; a pixel on the path differs from one
        // well below it.
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderLine(tachyon::int2{ 16, 40 }, tachyon::int2{ kSize.x - 16, 40 }, kFullClip,
                          tachyon::LineStyle{ .colorStyleIndex = solidColor, .lineWidth = 4.0f });
        });

        const uint32_t onLine  = PixelAt(ctx, kSize.x / 2, 40);
        const uint32_t offLine = PixelAt(ctx, kSize.x / 2, 120);
        EXPECT_NE(onLine, offLine)
            << "a line must change the pixels along its path";
    }

    // A triangle's soft edge is centered on its edge, and drawn even past its corners: a
    // pixel whose middle is 0.2 px inside an edge between pixels is about 70% covered.
    TEST_F(GpuReadbackTest, ATrianglesEdgeIsSoftHalfAPixelEitherSide)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();

        // Its left edge upright at x = 40.3, the triangle's leftmost point.
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderTriangle(tachyon::float2{ 40.3f, 20.0f }, tachyon::float2{ 40.3f, 100.0f },
                              tachyon::float2{ 120.0f, 100.0f }, kFullClip, solidColor);
        });

        auto green = [&](int x, int y) { return int((PixelAt(ctx, x, y) >> 8) & 0xFF); };
        const int outside = green(20, 90), inside = green(60, 90);
        ASSERT_GT(inside - outside, 100);
        const float edge = float(green(40, 90) - outside) / float(inside - outside);   // its middle at 40.5
        EXPECT_GT(edge, 0.55f);
        EXPECT_LT(edge, 0.85f);
        EXPECT_NEAR(green(39, 90), outside, 2) << "nothing a pixel out";
    }

    // A hard edge cuts straight: the pixel 0.2 px inside it is full, the one outside empty.
    TEST_F(GpuReadbackTest, ATrianglesHardEdgeIsNotSoft)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();

        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderTriangle(tachyon::float2{ 40.3f, 20.0f }, tachyon::float2{ 40.3f, 100.0f },
                              tachyon::float2{ 120.0f, 100.0f }, kFullClip, solidColor,
                              tachyon::ImageIndex::Invalid, tachyon::TriangleEdges::Edge01);
        });

        auto green = [&](int x, int y) { return int((PixelAt(ctx, x, y) >> 8) & 0xFF); };
        const int outside = green(20, 90), inside = green(60, 90);
        ASSERT_GT(inside - outside, 100);
        EXPECT_NEAR(green(40, 90), inside, 2);
        EXPECT_NEAR(green(39, 90), outside, 2);
        // Its other edges stay soft: the long one, y = x - 20.3, is 0.2 px from (70.5, 50.5).
        const int hypotenuse = green(70, 50);
        EXPECT_GT(hypotenuse, outside + 2);
        EXPECT_LT(hypotenuse, inside - 2);
    }

    // A polygon is a fan of triangles: no seam where they meet.
    TEST_F(GpuReadbackTest, APolygonHasNoSeamBetweenItsTriangles)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();

        // A square from (60, 40) to (160, 140), split along its diagonal y = x - 20.
        const tachyon::float2 square[] = { { 60, 40 }, { 160, 40 }, { 160, 140 }, { 60, 140 } };
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) { r->RenderFilledPolygon(square, kFullClip, solidColor); });

        auto green = [&](int x, int y) { return int((PixelAt(ctx, x, y) >> 8) & 0xFF); };
        const int inside = green(130, 60);
        ASSERT_GT(inside - green(20, 20), 100);
        for (int x = 70; x < 150; x += 10)
            EXPECT_NEAR(green(x, x - 20), inside, 2) << "on the diagonal at x = " << x;
    }

    // See-through, the triangles don't overlap either: the same everywhere inside, along
    // the diagonal too. (Moved 0.3 px down, so no pixel's middle is right on it.)
    TEST_F(GpuReadbackTest, ASeeThroughPolygonIsEvenAcrossItsTriangles)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        const auto halfWhite = styleManager.RegisterColorStyle(
            tachyon::ColorStyle::Create(tachyon::color4b{ 255, 255, 255, 128 }));

        const tachyon::float2 square[] = { { 60.0f, 40.3f }, { 160.0f, 40.3f }, { 160.0f, 140.3f }, { 60.0f, 140.3f } };
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) { r->RenderFilledPolygon(square, kFullClip, halfWhite); });

        auto green = [&](int x, int y) { return int((PixelAt(ctx, x, y) >> 8) & 0xFF); };
        const int upper = green(140, 60), lower = green(80, 120), outside = green(20, 20);
        ASSERT_GT(upper - outside, 50);
        EXPECT_NEAR(upper, outside + (255 - outside) * 128 / 255, 3) << "drawn once";
        EXPECT_NEAR(lower, upper, 2) << "both halves the same";
        for (int x = 70; x < 150; x += 10)
            for (int dy = -1; dy <= 1; ++dy)
                EXPECT_NEAR(green(x, x - 20 + dy), upper, 2) << "by the diagonal at x = " << x << ", " << dy;
    }

    // ---- Glyph atlas pages ----

    class GpuTextAtlasTest : public GpuReadbackTest
    {
    protected:
        tachyon::vulkan::VulkanTextAtlasManager atlas;

        void SetUp() override
        {
            GpuReadbackTest::SetUp();
            if (IsSkipped() || HasFatalFailure()) return;
            ASSERT_TRUE(atlas.Initialize(graphicsContext, graphicsContext->GetImageSet()));
        }

        static std::vector<uint8_t> Filled(tachyon::uint2 size, uint8_t value)
        {
            return std::vector<uint8_t>(size_t(size.x) * size.y * 4, value);
        }
    };

    // Glyphs are drawn at the size they were rasterized at, and the samplers only
    // read level 0, so atlas pages carry no mip chain.
    TEST_F(GpuTextAtlasTest, PagesHaveNoMipChain)
    {
        const tachyon::uint2 size{ 1024, 1024 };
        const auto pixels = Filled(size, 255);
        const tachyon::TextureIndex page = atlas.CreateTexture(pixels.data(), size, 4);
        ASSERT_NE(page, tachyon::TextureIndex::Invalid);

        auto* image = static_cast<tachyon::vulkan::VulkanImage*>(atlas.fontTextures[uint32_t(page) - 1].image);
        VmaAllocationInfo info{};
        vmaGetAllocationInfo(vk->deviceContext.allocator, image->allocation, &info);
        EXPECT_LT(info.size, VkDeviceSize(size.x) * size.y * 4 * 6 / 5)
            << "a full mip chain adds a third on top of level 0";
    }

    // An atlas update must not wait for the GPU: it's queued and recorded into the
    // next frame. Holding the queue lock proves it doesn't submit on its own.
    TEST_F(GpuTextAtlasTest, UpdatesDoNotWaitForTheGpu)
    {
        const tachyon::uint2 size{ 256, 256 };
        const auto pixels = Filled(size, 255);
        const tachyon::TextureIndex page = atlas.CreateTexture(pixels.data(), size, 4);
        ASSERT_NE(page, tachyon::TextureIndex::Invalid);

        std::atomic<bool> updated = false;
        std::thread updater;
        {
            auto queueLock = vk->deviceContext.LockQueue();
            updater = std::thread([&] { atlas.UpdateTexture(page, pixels.data(), size); updated = true; });
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            EXPECT_TRUE(updated) << "the update waited on the queue";
        }
        updater.join();
    }

    // A page created and then updated before any frame still reaches the screen,
    // validation-clean (sync validation checks the upload barriers).
    TEST_F(GpuTextAtlasTest, UpdatedPageIsSampledByTheNextFrame)
    {
        RegisterColorStyles();
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";

        const tachyon::uint2 size{ 64, 64 };
        const auto black = Filled(size, 0);
        const auto white = Filled(size, 255);
        const tachyon::TextureIndex page = atlas.CreateTexture(black.data(), size, 4);
        atlas.UpdateTexture(page, white.data(), size);
        const tachyon::ImageIndex imageIndex = atlas.GetImageIndex(page);
        atlas.SyncTextureIndices();
        const tachyon::VisualColorStyleIndex textured = styleManager.RegisterColorStyle(
            tachyon::ColorStyle::Create(tachyon::color4b{ 255, 255, 255, 255 }, tachyon::color4b{ 255, 255, 255, 255 },
                                    tachyon::ColorType::StretchedTexture));
        sink.Clear();

        for (int frame = 0; frame < 2; ++frame)
        {
            graphicsContext->BeginFrame();
            RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
                r->RenderQuad(tachyon::IntRect{ 40, 40, 80, 80 }, kFullClip, textured, 0.0f, imageIndex);
            });
        }

        EXPECT_NE(PixelAt(ctx, 80, 80), PixelAt(ctx, 200, 200)) << "the page's pixels never reached the screen";
        EXPECT_FALSE(sink.HasErrors()) << sink.Dump();
    }

    // ---- Image uploads (gradient pages, user images) ----

    // Creating an image with pixel data doesn't wait for the GPU either: the
    // upload (and its mipmaps) is recorded into the next frame.
    TEST_F(GpuReadbackTest, ImageUploadsDoNotWaitForTheGpu)
    {
        const tachyon::uint2 size{ 256, 256 };
        std::vector<uint8_t> pixels(size_t(size.x) * size.y * 4, 255);

        std::atomic<tachyon::Image*> image = nullptr;
        std::thread creator;
        {
            auto queueLock = vk->deviceContext.LockQueue();
            creator = std::thread([&] {
                image = graphicsContext->CreateImage(pixels.data(), size, tachyon::ImageFormat::R8G8B8A8,
                                                     tachyon::Sampler::LinearRepeat, true);
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            EXPECT_NE(image.load(), nullptr) << "creating the image waited on the queue";
        }
        creator.join();
        if (image.load() != nullptr)
            vk->deviceContext.DestroyImage(image.load());
    }

    // A mipmapped image created before any frame is filled (all levels) by the
    // next frame and sampled correctly, validation-clean.
    TEST_F(GpuReadbackTest, MipmappedImageIsSampledByTheNextFrame)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";

        const tachyon::uint2 size{ 64, 64 };
        std::vector<uint8_t> pixels(size_t(size.x) * size.y * 4, 255);
        tachyon::Image* image = graphicsContext->CreateImage(pixels.data(), size, tachyon::ImageFormat::R8G8B8A8,
                                                         tachyon::Sampler::LinearRepeat, true);
        ASSERT_NE(image, nullptr);
        const tachyon::ImageIndex imageIndex = graphicsContext->RegisterImage(image);
        const tachyon::VisualColorStyleIndex textured = styleManager.RegisterColorStyle(
            tachyon::ColorStyle::Create(tachyon::color4b{ 255, 255, 255, 255 }, tachyon::color4b{ 255, 255, 255, 255 },
                                    tachyon::ColorType::StretchedTexture));
        sink.Clear();

        for (int frame = 0; frame < 2; ++frame)
        {
            graphicsContext->BeginFrame();
            RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
                r->RenderQuad(tachyon::IntRect{ 40, 40, 80, 80 }, kFullClip, textured, 0.0f, imageIndex);
            });
        }

        EXPECT_NE(PixelAt(ctx, 80, 80), PixelAt(ctx, 200, 200)) << "the image's pixels never reached the screen";
        EXPECT_FALSE(sink.HasErrors()) << sink.Dump();
        graphicsContext->UnregisterImage(imageIndex);
        vk->deviceContext.DestroyImageAtEnd(image, tachyon::DeviceScope::Frame);
    }

    // ---- Borders under a fractional scale (HiDPI, node-view zoom) ----

    // A translucent fill (a vector field's axis label) meets an opaque border at the
    // rounded corners: no pixel there may be more saturated than the fill and the border.
    TEST_F(GpuReadbackTest, TranslucentFillDoesNotGlowAtRoundedCorners)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";

        tachyon::Style style{};
        style.borderWidths = tachyon::BoxEdges(1);
        style.borderRadius = tachyon::BoxCorners(6);
        for (int i = 0; i < tachyon::Style::kStateCount; ++i)
        {
            style.states[i].backgroundColor = tachyon::ColorStyle::Create(tachyon::color4b{ 239, 106, 106, 46 });
            style.states[i].SetBorderColor(tachyon::ColorStyle::Create(tachyon::color4b{ 42, 47, 57, 255 }));
        }
        const tachyon::VisualBoxStyleIndex box = styleManager.Register(style).visualBoxStyleIndex;
        constexpr int kX = 20, kY = 20, kW = 80, kH = 40;
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderBox(tachyon::IntRect{ kX, kY, kW, kH }, kFullClip, box);
        });

        // Redness: red over the other channels (BGRA8).
        auto redness = [](uint32_t p) { return int((p >> 16) & 0xFF) - int((p >> 8) & 0xFF); };
        const int fill   = redness(PixelAt(ctx, kX + kW / 2, kY + kH / 2));
        const int border = redness(PixelAt(ctx, kX, kY + kH / 2));
        const int limit  = std::max(fill, border) + 3;
        for (int y = kY; y < kY + 8; ++y)
            for (int x = kX; x < kX + 8; ++x)
                EXPECT_LE(redness(PixelAt(ctx, x, y)), limit) << "corner pixel " << x << "," << y << ": " << std::hex << (PixelAt(ctx, x, y) & 0xFFFFFF) << std::dec << " fill " << fill << " border " << border;
    }

    // A window with a swapchain reads back what it drew too (a popup's last frame, for the
    // stand-in that fades it).
    TEST_F(GpuReadbackTest, AWindowWithASwapchainReadsBack)
    {
        auto* ctx = CreateHiddenWindowContext(kSize, tachyon::WindowFlags::None);
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan surface available";
        const tachyon::VisualColorStyleIndex color = styleManager.RegisterColorStyle(
            tachyon::ColorStyle::Create(tachyon::color4b{ 30, 160, 90, 255 }));
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x, kSize.y }, kFullClip, color);
        });
        const uint32_t px = PixelAt(ctx, kSize.x / 2, kSize.y / 2);   // BGRA8
        EXPECT_NEAR(int((px >> 16) & 0xFF), 30, 2);
        EXPECT_NEAR(int((px >> 8) & 0xFF), 160, 2);
        EXPECT_NEAR(int(px & 0xFF), 90, 2);
        EXPECT_EQ(int(px >> 24), 255);
    }

    // The backend shows a stand-in for a drawn window with its last frame, and takes it away.
    TEST_F(GpuReadbackTest, TheBackendShowsAStandInForADrawnWindow)
    {
        auto* ctx = CreateHiddenWindowContext(kSize, tachyon::WindowFlags::None);
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan surface available";
        RegisterColorStyles();
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x, kSize.y }, kFullClip, solidColor);
        });
        const tachyon::OSWindowHandle handle = ctx->GetWindowHandle();
        const uint32_t fade = windowBackend->ShowFade(handle, ctx, tachyon::IntRect{ -20000, -20000, kSize.x, kSize.y }, 0.5f);
#ifdef _WIN32
        ASSERT_NE(fade, 0u);
        windowBackend->MoveFade(fade, tachyon::IntRect{ -19990, -20000, kSize.x, kSize.y }, 1.0f);
        windowBackend->EndFade(fade);
        EXPECT_EQ(windowBackend->ShowFade(handle, nullptr, tachyon::IntRect{ 0, 0, 10, 10 }, 1.0f), 0u) << "nothing to show";
#else
        EXPECT_EQ(fade, 0u);
#endif
    }

    // ---- Blending between two states ----

    // A plain box in one color, in every state.
    tachyon::VisualBoxStyleIndex PlainBox(tachyon::StyleManager& sm, tachyon::color4b color)
    {
        tachyon::Style style{};
        for (int i = 0; i < tachyon::Style::kStateCount; ++i)
        {
            style.states[i].backgroundColor = tachyon::ColorStyle::Create(color);
            style.states[i].SetBorderColor(tachyon::ColorStyle::Create(color));
        }
        return sm.Register(style).visualBoxStyleIndex;
    }

    struct Rgb { float r, g, b; };
    Rgb Unpack(uint32_t p) { return { float((p >> 16) & 0xFF), float((p >> 8) & 0xFF), float(p & 0xFF) }; }

    // `t` of the way between two colors in Oklab, 0-255; OklabMid half way. SrgbMid half
    // way in sRGB.
    Rgb OklabAt(tachyon::color4b a, tachyon::color4b b, float t)
    {
        float l0, a0, b0, l1, a1, b1;
        tachyon::ColorConvert::RGBtoOklab(a.r / 255.0f, a.g / 255.0f, a.b / 255.0f, l0, a0, b0);
        tachyon::ColorConvert::RGBtoOklab(b.r / 255.0f, b.g / 255.0f, b.b / 255.0f, l1, a1, b1);
        auto lerp = [t](float x, float y) { return x + (y - x) * t; };
        const auto rgb = tachyon::ColorConvert::OklabToRGB(lerp(l0, l1), lerp(a0, a1), lerp(b0, b1));
        return { rgb[0] * 255.0f, rgb[1] * 255.0f, rgb[2] * 255.0f };
    }
    Rgb OklabMid(tachyon::color4b a, tachyon::color4b b) { return OklabAt(a, b, 0.5f); }
    Rgb SrgbMid(tachyon::color4b a, tachyon::color4b b)
    {
        return { 0.5f * (a.r + b.r), 0.5f * (a.g + b.g), 0.5f * (a.b + b.b) };
    }
    float Distance(Rgb a, Rgb b) { return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b); }

    // From blue to red (hues apart) the middle is the Oklab one: about as light and saturated
    // as the ends, not the darker, greyer sRGB average.
    TEST_F(GpuReadbackTest, ABlendBetweenHuesIsInOklab)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        const tachyon::color4b blue{ 0x6B, 0x9B, 0xFF, 255 }, red{ 0xEF, 0x6A, 0x6A, 255 };
        const uint32_t from = tachyon::GUIRenderer::StyleIndex(PlainBox(styleManager, blue), tachyon::ElementState::None);
        const uint32_t to   = tachyon::GUIRenderer::StyleIndex(PlainBox(styleManager, red),  tachyon::ElementState::None);
        ASSERT_GT(Distance(OklabMid(blue, red), SrgbMid(blue, red)), 12.0f) << "test setup: the two middles differ";

        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderBox(tachyon::IntRect{ 20, 20, 80, 40 }, kFullClip, from, to, 0.5f);
        });
        const Rgb mid = Unpack(PixelAt(ctx, 60, 40));
        EXPECT_LT(Distance(mid, OklabMid(blue, red)), 6.0f) << mid.r << "," << mid.g << "," << mid.b;

        // A quarter of the way: nearer the "from" color.
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderBox(tachyon::IntRect{ 20, 20, 80, 40 }, kFullClip, from, to, 0.25f);
        });
        const Rgb quarter = Unpack(PixelAt(ctx, 60, 40));
        EXPECT_LT(Distance(quarter, OklabAt(blue, red, 0.25f)), 6.0f) << quarter.r << "," << quarter.g << "," << quarter.b;

        // The ends are the colors themselves.
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderBox(tachyon::IntRect{ 20, 20, 80, 40 }, kFullClip, from, to, 1.0f);
        });
        EXPECT_LT(Distance(Unpack(PixelAt(ctx, 60, 40)), Rgb{ float(red.r), float(red.g), float(red.b) }), 3.0f);
    }

    // Greys (no hue) and one hue in two strengths blend in sRGB.
    TEST_F(GpuReadbackTest, ABlendWithinAHueIsInSrgb)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        const std::pair<tachyon::color4b, tachyon::color4b> pairs[] = {
            { { 0x10, 0x10, 0x10, 255 }, { 0xC0, 0xC0, 0xC0, 255 } },
            { { 0x1E, 0x2E, 0x50, 255 }, { 0x6B, 0x9B, 0xFF, 255 } },   // one blue, dark and light
            { { 0x18, 0x18, 0x10, 255 }, { 0x20, 0x40, 0xFF, 255 } },   // a grey, a touch yellow, to blue: hardly a hue to keep
        };
        for (const auto& [a, b] : pairs)
        {
            ASSERT_GT(Distance(OklabMid(a, b), SrgbMid(a, b)), 6.0f) << "test setup: the two middles differ";
            const uint32_t from = tachyon::GUIRenderer::StyleIndex(PlainBox(styleManager, a), tachyon::ElementState::None);
            const uint32_t to   = tachyon::GUIRenderer::StyleIndex(PlainBox(styleManager, b), tachyon::ElementState::None);
            RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
                r->RenderBox(tachyon::IntRect{ 20, 20, 80, 40 }, kFullClip, from, to, 0.5f);
            });
            const Rgb mid = Unpack(PixelAt(ctx, 60, 40));
            EXPECT_LT(Distance(mid, SrgbMid(a, b)), 4.0f) << mid.r << "," << mid.g << "," << mid.b;
        }
    }

    // Fading in from transparent adds no color of the transparent end: half way to blue over
    // white is blue at half strength over white, not darker.
    TEST_F(GpuReadbackTest, AFadeFromTransparentAddsNoColor)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        const tachyon::VisualColorStyleIndex white = styleManager.RegisterColorStyle(tachyon::ColorStyle::Create(tachyon::color4b{ 255, 255, 255, 255 }));
        const tachyon::color4b blue{ 0x6B, 0x9B, 0xFF, 255 };
        const uint32_t from = tachyon::GUIRenderer::StyleIndex(PlainBox(styleManager, tachyon::color4b{ 0, 0, 0, 0 }), tachyon::ElementState::None);
        const uint32_t to   = tachyon::GUIRenderer::StyleIndex(PlainBox(styleManager, blue), tachyon::ElementState::None);
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x, kSize.y }, kFullClip, white);
            r->RenderBox(tachyon::IntRect{ 20, 20, 80, 40 }, kFullClip, from, to, 0.5f);
        });
        const Rgb px = Unpack(PixelAt(ctx, 60, 40));
        const Rgb expected{ 0.5f * (blue.r + 255.0f), 0.5f * (blue.g + 255.0f), 0.5f * (blue.b + 255.0f) };
        EXPECT_LT(Distance(px, expected), 6.0f) << px.r << "," << px.g << "," << px.b;
    }

    // A translucent color drawn over a box's background (a swatch) keeps its color: half
    // red over black is half as bright, not full red.
    TEST_F(GpuReadbackTest, ATranslucentColorOverrideKeepsItsColor)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        const tachyon::VisualColorStyleIndex black = styleManager.RegisterColorStyle(tachyon::ColorStyle::Create(tachyon::color4b{ 0, 0, 0, 255 }));
        const tachyon::VisualColorStyleIndex halfRed = styleManager.RegisterColorStyle(tachyon::ColorStyle::Create(tachyon::color4b{ 255, 0, 0, 128 }));
        const tachyon::VisualBoxStyleIndex box = PlainBox(styleManager, tachyon::color4b{ 0, 0, 0, 0 });
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x, kSize.y }, kFullClip, black);
            r->RenderBox(tachyon::IntRect{ 20, 20, 80, 40 }, kFullClip, box, halfRed);
        });
        const Rgb px = Unpack(PixelAt(ctx, 60, 40));
        EXPECT_NEAR(px.r, 128.0f, 3.0f);
        EXPECT_NEAR(px.g, 0.0f, 2.0f);
    }

    // A box shadow fades across its edge over the blur: full inside, about half at the
    // edge, nearly gone a blur outside it, and steadily darker in between.
    TEST_F(GpuReadbackTest, ShadowFadesOverItsBlur)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();

        constexpr int kX = 100, kY = 60, kW = 120, kH = 120, kBlur = 16;
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderShadow(tachyon::IntRect{ kX, kY, kW, kH }, kFullClip, solidColor, 6.0f, kBlur);
        });

        auto level = [&](int x) { return int((PixelAt(ctx, x, kY + kH / 2) >> 8) & 0xFF); }; // green
        const int bg     = int((PixelAt(ctx, 4, 4) >> 8) & 0xFF);
        const int inside = level(kX + kW / 2);
        ASSERT_GT(inside - bg, 200) << "full inside the shape";
        const float atEdge = float(level(kX) - bg) / float(inside - bg);
        EXPECT_GT(atEdge, 0.25f);
        EXPECT_LT(atEdge, 0.85f);
        EXPECT_LE(level(kX - kBlur) - bg, (inside - bg) / 10) << "a blur outside the edge";
        EXPECT_EQ(level(kX - kBlur - 4), bg) << "past the quad, which extends one blur";
        for (int x = kX - kBlur; x < kX + kBlur; ++x)
            EXPECT_LE(level(x), level(x + 1) + 1) << "falls off steadily at " << x;
    }

    // A child placed just inside its parent's 1-unit border (a node's header)
    // must meet the border at any scale: no background shows between them,
    // whichever way the edges round to pixels.
    TEST_F(GpuReadbackTest, ChildMeetsParentBorderAtFractionalScale)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";

        // Shaped like a node: rounded box with a 1-unit border, and a header just
        // inside it with a rounded top and a bottom separator.
        tachyon::Style nodeStyle{};
        nodeStyle.borderWidths = tachyon::BoxEdges(1);
        nodeStyle.borderRadius = tachyon::BoxCorners(6);
        tachyon::Style headerStyle{};
        headerStyle.borderWidths = tachyon::BoxEdges(0);
        headerStyle.borderWidths.bottom = 1;
        headerStyle.borderRadius = tachyon::BoxCorners(5, 5, 0, 0);
        for (int i = 0; i < tachyon::Style::kStateCount; ++i)
        {
            nodeStyle.states[i].backgroundColor = tachyon::ColorStyle::Create(tachyon::color4b{ 0, 0, 0, 255 });
            nodeStyle.states[i].SetBorderColor(tachyon::ColorStyle::Create(tachyon::color4b{ 255, 255, 255, 255 }));
            headerStyle.states[i].backgroundColor = tachyon::ColorStyle::Create(tachyon::color4b{ 255, 0, 0, 255 });
            headerStyle.states[i].SetBorderColor(tachyon::ColorStyle::Create(tachyon::color4b{ 0, 0, 255, 255 }));
        }
        const tachyon::VisualBoxStyleIndex node   = styleManager.Register(nodeStyle).visualBoxStyleIndex;
        const tachyon::VisualBoxStyleIndex header = styleManager.Register(headerStyle).visualBoxStyleIndex;

        // Pan offsets in fractions of a unit put the edges at every sub-pixel phase.
        constexpr int kCases = 6;
        constexpr int kX = 20, kY = 20, kW = 60, kH = 60;
        for (const float kScale : { 1.0f, 1.25f, 1.5f, 2.0f })
        for (int c = 0; c < kCases; ++c)
        {
            const float pan = float(c) / float(kCases);
            // Snapped the way the render pass does: the node absolutely, the header to the node.
            auto toPx = [&](tachyon::IntRect r) {
                return tachyon::float4{ (r.x + pan) * kScale, (r.y + pan) * kScale,
                                    (r.x + r.w + pan) * kScale, (r.y + r.h + pan) * kScale };
            };
            const tachyon::IntRect nodeRect{ kX, kY, kW, kH }, headerRect{ kX + 1, kY + 1, kW - 2, 14 };
            const tachyon::IntRect nodeSnapped   = tachyon::SnapAbsolute(toPx(nodeRect));
            const tachyon::IntRect headerSnapped = tachyon::SnapToParent(toPx(nodeRect), nodeSnapped, toPx(headerRect));
            RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
                r->SetTransform(tachyon::float3x2::Scale(kScale) * tachyon::float3x2::Translate(pan, pan));
                r->RenderBox(nodeRect,   kFullClip, node,   tachyon::VisualColorStyleIndex{}, tachyon::ImageIndex::Invalid, nodeSnapped);
                r->RenderBox(headerRect, kFullClip, header, tachyon::VisualColorStyleIndex{}, tachyon::ImageIndex::Invalid, headerSnapped);
            });

            auto isBorder = [](uint32_t p) { return ((p >> 16) & 0xFF) > 150 && ((p >> 8) & 0xFF) > 150; };
            auto isHeader = [](uint32_t p) { return ((p >> 16) & 0xFF) > 150 && ((p >> 8) & 0xFF) < 100; };

            // Down the middle: border rows, then header rows, with nothing dark between.
            const int px = int((kX + kW / 2 + pan) * kScale);
            int topBorderRows = 0;
            bool sawGap = false, reachedHeader = false;
            std::string column;
            for (int py = int((kY + pan) * kScale) - 2; py < int((kY + 6 + pan) * kScale); ++py)
            {
                const uint32_t p = PixelAt(ctx, px, py);
                char buf[24];
                std::snprintf(buf, sizeof(buf), " %d:%06x", py, p & 0xFFFFFF);
                column += buf;
                if (reachedHeader) continue;
                if (isBorder(p))      ++topBorderRows;
                else if (isHeader(p)) reachedHeader = true;
                else if (topBorderRows > 0) sawGap = true;
            }
            // Across the left side below the header: the side border's columns.
            const int py = int((kY + 40 + pan) * kScale);
            int sideBorderCols = 0;
            for (int x = int((kX + pan) * kScale) - 2; x < int((kX + 4 + pan) * kScale); ++x)
                if (isBorder(PixelAt(ctx, x, py))) ++sideBorderCols;

            // The header's bottom border is at its bottom: blue right above the node background.
            bool bottomLineAtBottom = false;
            for (int y = int((kY + 1 + 14 + pan) * kScale) - 3; y <= int((kY + 1 + 14 + pan) * kScale) + 1; ++y)
            {
                const uint32_t above = PixelAt(ctx, px, y), below = PixelAt(ctx, px, y + 1);
                if ((above & 0xFF) > 150 && ((above >> 16) & 0xFF) < 100 && ((below >> 16) & 0xFF) < 100 && (below & 0xFF) < 100)
                    bottomLineAtBottom = true;
            }
            EXPECT_TRUE(bottomLineAtBottom) << "header border-bottom not drawn at the bottom, scale " << kScale << " pan " << pan;

            EXPECT_TRUE(reachedHeader) << "scale " << kScale << " pan " << pan << column;
            EXPECT_FALSE(sawGap) << "background between the top border and the header, scale " << kScale << " pan " << pan << column;
            // A 1-unit border is the same whole number of pixels everywhere.
            const int expected = std::max(1, tachyon::SnapPx(kScale));
            EXPECT_EQ(topBorderRows, expected) << "top border, scale " << kScale << " pan " << pan << column;
            EXPECT_EQ(sideBorderCols, expected) << "side border, scale " << kScale << " pan " << pan;
        }
    }

    // A host image set as a render-to-image window's target receives its frames:
    // the copy and its barriers are sync-validation clean, and the image ends up
    // holding the window's pixels.
    TEST_F(GpuReadbackTest, ImageTargetReceivesTheWindowsFrames)
    {
        auto& dc = vk->deviceContext;
        tachyon::ID id{};
        id.value.dynamicParts.mode      = tachyon::IDMode::Static;
        id.value.dynamicParts.hashValue = 9300;
        auto* ctx = static_cast<tachyon::vulkan::VulkanWindowContext*>(graphicsContext->CreateWindowContext(
            nullptr, nullptr, id, kSize, tachyon::WindowFlags::RenderToImage, [] {}));
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";

        // The host's image is RGBA, so the copy also converts from the GUI's BGRA.
        VkImageCreateInfo imageInfo{ .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.imageType   = VK_IMAGE_TYPE_2D;
        imageInfo.format      = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.extent      = { uint32_t(kSize.x), uint32_t(kSize.y), 1 };
        imageInfo.mipLevels   = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples     = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.usage       = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        VkImage target = VK_NULL_HANDLE;
        VmaAllocation targetAllocation = nullptr;
        ASSERT_EQ(vmaCreateImage(dc.allocator, &imageInfo, &allocInfo, &target, &targetAllocation, nullptr), VK_SUCCESS);
        vk->SetWindowImageTarget(id, target, VkExtent2D{ uint32_t(kSize.x), uint32_t(kSize.y) });

        RegisterColorStyles();
        sink.Clear();
        for (int i = 0; i < 3; ++i)
            RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
                r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x / 2, kSize.y }, kFullClip, solidColor, 0.0f);
            });
        EXPECT_FALSE(sink.HasErrors()) << sink.Dump();

        // Read the target back as a host would: after the GUI's frame, out of
        // SHADER_READ_ONLY and back into it.
        const size_t bytes = size_t(kSize.x) * kSize.y * 4;
        tachyon::vulkan::Buffer readback = dc.CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);
        VkCommandBuffer cmd = dc.immCommandBuffer;
        ASSERT_EQ(vkResetFences(dc.device, 1, &dc.immFence), VK_SUCCESS);
        ASSERT_EQ(vkResetCommandBuffer(cmd, 0), VK_SUCCESS);
        VkCommandBufferBeginInfo begin{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin);
        const auto transition = [&](VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                    VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
        {
            VkImageMemoryBarrier2 barrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
            barrier.srcStageMask = srcStage; barrier.srcAccessMask = srcAccess;
            barrier.dstStageMask = dstStage; barrier.dstAccessMask = dstAccess;
            barrier.oldLayout = from; barrier.newLayout = to;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = target;
            barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            VkDependencyInfo dependency{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
            dependency.imageMemoryBarrierCount = 1;
            dependency.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(cmd, &dependency);
        };
        transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = imageInfo.extent;
        vkCmdCopyImageToBuffer(cmd, target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &region);
        transition(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_PIPELINE_STAGE_2_COPY_BIT, 0, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        VkMemoryBarrier2 toHost{ .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
        toHost.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;   toHost.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toHost.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;   toHost.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        VkDependencyInfo hostDependency{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        hostDependency.memoryBarrierCount = 1;
        hostDependency.pMemoryBarriers = &toHost;
        vkCmdPipelineBarrier2(cmd, &hostDependency);
        vkEndCommandBuffer(cmd);
        VkCommandBufferSubmitInfo cmdInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
        cmdInfo.commandBuffer = cmd;
        VkSubmitInfo2 submit{ .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cmdInfo;
        {
            auto lock = dc.LockQueue();
            ASSERT_EQ(vkQueueSubmit2(dc.graphicsQueue, 1, &submit, dc.immFence), VK_SUCCESS);
        }
        tachyon::vulkan::WaitForFence(dc.device, dc.immFence);
        EXPECT_FALSE(sink.HasErrors()) << sink.Dump();

        vmaInvalidateAllocation(dc.allocator, readback.allocation, 0, VK_WHOLE_SIZE);
        void* mapped = nullptr;
        vmaMapMemory(dc.allocator, readback.allocation, &mapped);
        const auto* rgba = static_cast<const uint8_t*>(mapped);
        for (const tachyon::int2 p : { tachyon::int2{ kSize.x / 4, kSize.y / 2 }, tachyon::int2{ kSize.x * 3 / 4, kSize.y / 2 } })
        {
            const size_t off = (size_t(p.y) * kSize.x + size_t(p.x)) * 4;
            EXPECT_EQ(rgba[off + 0], framePixels[off + 2]) << "red at " << p.x;
            EXPECT_EQ(rgba[off + 1], framePixels[off + 1]) << "green at " << p.x;
            EXPECT_EQ(rgba[off + 2], framePixels[off + 0]) << "blue at " << p.x;
            EXPECT_EQ(rgba[off + 3], framePixels[off + 3]) << "alpha at " << p.x;
        }
        vmaUnmapMemory(dc.allocator, readback.allocation);

        dc.DestroyBuffer(readback);
        graphicsContext->DestroyWindowContext(ctx);
        dc.WaitIdle();
        vmaDestroyImage(dc.allocator, target, targetAllocation);
    }
    // ---- Circles --------------------------------------------------------------

    // The green channel of a BGRA8 pixel.
    int Green(uint32_t px) { return int((px >> 8) & 0xFF); }

    // A ring is round: covered right on its radius at every angle, empty a few pixels
    // inside and outside. Four Bezier curves bulged or flattened it at the diagonals.
    TEST_F(GpuReadbackTest, CircleIsRound)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();

        constexpr float kCx = 160.0f, kCy = 120.0f, kR = 80.0f;
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderCircle(tachyon::float2{ kCx, kCy }, kR, kFullClip, tachyon::LineStyle{ .colorStyleIndex = solidColor, .lineWidth = 2.0f });
        });

        const int bg = Green(PixelAt(ctx, 2, 2));
        for (int i = 0; i < 32; ++i)
        {
            const float a = float(i) * 6.2831853f / 32.0f;
            auto at = [&](float radius) {
                return Green(PixelAt(ctx, int(std::floor(kCx + std::cos(a) * radius)), int(std::floor(kCy + std::sin(a) * radius))));
            };
            EXPECT_GT(at(kR) - bg, 100) << "on the ring at angle " << i;
            EXPECT_LE(at(kR - 3.0f) - bg, 8) << "inside at angle " << i;
            EXPECT_LE(at(kR + 3.0f) - bg, 8) << "outside at angle " << i;
        }
    }

    // A gradient runs once around, clockwise from the right: darker to lighter.
    TEST_F(GpuReadbackTest, CircleGradientRunsAround)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        const tachyon::VisualColorStyleIndex ramp = styleManager.RegisterColorStyle(
            tachyon::ColorStyle::Create(tachyon::color4b{ 0, 0, 0, 255 }, tachyon::color4b{ 255, 255, 255, 255 },
                                        tachyon::ColorType::LinearGradient));

        constexpr int kCx = 160, kCy = 120, kR = 80;
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderCircle(tachyon::float2{ float(kCx), float(kCy) }, float(kR), kFullClip,
                            tachyon::LineStyle{ .colorStyleIndex = ramp, .lineWidth = 6.0f });
        });

        const int right  = Green(PixelAt(ctx, kCx + kR, kCy + 4));   // just past the start
        const int bottom = Green(PixelAt(ctx, kCx, kCy + kR));
        const int left   = Green(PixelAt(ctx, kCx - kR, kCy));
        const int top    = Green(PixelAt(ctx, kCx, kCy - kR));
        EXPECT_LT(right, bottom);
        EXPECT_LT(bottom, left);
        EXPECT_LT(left, top);
        EXPECT_LT(right, 40);
        EXPECT_GT(top, 150);
    }

    // Dashes go round the ring, and the clip rect cuts it.
    TEST_F(GpuReadbackTest, CircleDashesAndClip)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();

        constexpr float kCx = 160.0f, kCy = 120.0f, kR = 80.0f;
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderCircle(tachyon::float2{ kCx, kCy }, kR, tachyon::IntRect{ 0, 0, 160, kSize.y },
                            tachyon::LineStyle{ .colorStyleIndex = solidColor, .lineWidth = 3.0f, .dashSize = 10.0f });
        });

        const int bg = Green(PixelAt(ctx, 2, 2));
        int changes = 0;
        bool wasOn = false;
        for (int i = 0; i <= 400; ++i)
        {
            const float a = 1.5707963f + float(i) * 3.1415926f / 400.0f;   // the left half, bottom to top
            const bool on = Green(PixelAt(ctx, int(std::floor(kCx + std::cos(a) * kR)), int(std::floor(kCy + std::sin(a) * kR)))) - bg > 100;
            if (i > 0 && on != wasOn) ++changes;
            wasOn = on;
        }
        // Half the ring is about 251 pixels long: 12 or 13 dashes and gaps of 10.
        EXPECT_GE(changes, 20);
        EXPECT_LE(changes, 28);
        int onRight = 0;
        for (int i = 0; i < 400; ++i)
        {
            const float a = -1.4f + float(i) * 2.8f / 400.0f;   // the right half, clear of the clip edge
            if (Green(PixelAt(ctx, int(std::floor(kCx + std::cos(a) * kR)), int(std::floor(kCy + std::sin(a) * kR)))) != bg)
                ++onRight;
        }
        EXPECT_EQ(onRight, 0) << "the right half is clipped";
    }

    // The radius follows the transform's scale; the line width stays in pixels.
    TEST_F(GpuReadbackTest, CircleFollowsTheTransform)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();

        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->SetTransform(tachyon::float3x2::Translate(160.0f, 120.0f) * tachyon::float3x2::Scale(2.0f));
            r->RenderCircle(tachyon::float2{ 0.0f, 0.0f }, 40.0f, kFullClip, tachyon::LineStyle{ .colorStyleIndex = solidColor, .lineWidth = 2.0f });
        });

        const int bg = Green(PixelAt(ctx, 2, 2));
        EXPECT_GT(Green(PixelAt(ctx, 160 + 80, 120)) - bg, 100) << "radius 40 at scale 2";
        EXPECT_LE(Green(PixelAt(ctx, 160 + 40, 120)) - bg, 8) << "not at the unscaled radius";
        EXPECT_LE(Green(PixelAt(ctx, 160 + 84, 120)) - bg, 8) << "a 2px line, not 4";
    }

    // ---- SDFGradient --------------------------------------------------------

    // SDFGradient runs by depth inside the shape, from its edge (the first color) to its
    // deepest point (the last): a quad's middle, a stroke's center line, a triangle's incenter.
    TEST_F(GpuReadbackTest, SdfGradientRunsFromTheEdgeToTheMiddle)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        const tachyon::VisualColorStyleIndex depth = styleManager.RegisterColorStyle(
            tachyon::ColorStyle::Create(tachyon::color4b{ 0, 0, 0, 255 }, tachyon::color4b{ 255, 255, 255, 255 },
                                        tachyon::ColorType::SDFGradient));

        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderQuad(tachyon::IntRect{ 20, 20, 100, 100 }, kFullClip, depth);
            r->RenderLine(tachyon::int2{ 150, 30 }, tachyon::int2{ 300, 30 }, kFullClip,
                          tachyon::LineStyle{ .colorStyleIndex = depth, .lineWidth = 20.0f });
            r->RenderTriangle(tachyon::float2{ 150, 220 }, tachyon::float2{ 300, 220 }, tachyon::float2{ 225, 90 }, kFullClip, depth);
        });

        // Quad: dark at the edge, light in the middle, lighter the deeper.
        EXPECT_LT(Green(PixelAt(ctx, 21, 70)), 40);
        EXPECT_GT(Green(PixelAt(ctx, 70, 70)), 200);
        for (int x = 22; x < 68; x += 4)
            EXPECT_LE(Green(PixelAt(ctx, x, 70)), Green(PixelAt(ctx, x + 4, 70)) + 2) << "quad at " << x;

        // Line: light on its center line, dark at its edges.
        EXPECT_GT(Green(PixelAt(ctx, 225, 30)), 200);
        EXPECT_LT(Green(PixelAt(ctx, 225, 21)), 60);

        // Triangle: its incenter (the deepest point) is the lightest; near an edge it's dark.
        const float base = 150.0f, height = 130.0f;
        const float side = std::sqrt(75.0f * 75.0f + height * height);
        const float inradius = (base * height) / (base + 2.0f * side);
        const int incenterY = int(220.0f - inradius);
        EXPECT_GT(Green(PixelAt(ctx, 225, incenterY)), 200);
        EXPECT_LT(Green(PixelAt(ctx, 225, 218)), 60);
    }
    // ---- Dots ---------------------------------------------------------------

    // A dot, drawn as one triangle, is a round disc: covered inside its radius in every
    // direction, empty outside it, with dots side by side drawn as their own.
    TEST_F(GpuReadbackTest, DotIsARoundDisc)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";

        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderDot(tachyon::float2{ 80.0f, 120.0f }, kFullClip, 40.0f, tachyon::color4f{ 1, 1, 1, 1 });
            r->RenderDot(tachyon::float2{ 200.0f, 120.0f }, kFullClip, 20.0f, tachyon::color4f{ 1, 1, 1, 1 });
        });

        const int bg = Green(PixelAt(ctx, 2, 2));
        for (int i = 0; i < 24; ++i)
        {
            const float a = float(i) * 6.2831853f / 24.0f;
            auto at = [&](float cx, float radius) {
                return Green(PixelAt(ctx, int(std::floor(cx + std::cos(a) * radius)), int(std::floor(120.0f + std::sin(a) * radius))));
            };
            EXPECT_GT(at(80.0f, 16.0f) - bg, 200) << "inside the big dot at angle " << i;
            EXPECT_LE(at(80.0f, 23.0f) - bg, 4) << "outside the big dot at angle " << i;
            EXPECT_GT(at(200.0f, 7.0f) - bg, 200) << "inside the small dot at angle " << i;
            EXPECT_LE(at(200.0f, 13.0f) - bg, 4) << "outside the small dot at angle " << i;
        }
    }

    // The clip rect cuts a dot along a straight edge without bending the rest of it, from
    // the side and from above.
    TEST_F(GpuReadbackTest, DotIsClippedPerPixel)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";

        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderDot(tachyon::float2{ 160.0f, 120.0f }, tachyon::IntRect{ 0, 0, 160, kSize.y }, 60.0f, tachyon::color4f{ 1, 1, 1, 1 });
            r->RenderDot(tachyon::float2{ 260.0f, 120.0f }, tachyon::IntRect{ 200, 120, 120, 120 }, 60.0f, tachyon::color4f{ 1, 1, 1, 1 });
        });

        const int bg = Green(PixelAt(ctx, 2, 2));
        for (int y = 96; y <= 144; y += 4)
        {
            EXPECT_GT(Green(PixelAt(ctx, 159, y)) - bg, 200) << "up to the clip edge at y " << y;
            EXPECT_EQ(Green(PixelAt(ctx, 160, y)), bg) << "nothing past it at y " << y;
        }
        // The kept half is still round: its edge where the circle is.
        for (int i = 0; i < 12; ++i)
        {
            const float a = 1.7f + float(i) * 2.9f / 12.0f;   // the left half
            EXPECT_GT(Green(PixelAt(ctx, int(std::floor(160.0f + std::cos(a) * 26.0f)), int(std::floor(120.0f + std::sin(a) * 26.0f)))) - bg, 200);
            EXPECT_LE(Green(PixelAt(ctx, int(std::floor(160.0f + std::cos(a) * 33.0f)), int(std::floor(120.0f + std::sin(a) * 33.0f)))) - bg, 4);
        }
        // Clipped from above: nothing over the edge, the lower half whole.
        for (int x = 236; x <= 284; x += 4)
        {
            EXPECT_EQ(Green(PixelAt(ctx, x, 119)), bg) << "nothing above the clip edge at x " << x;
            EXPECT_GT(Green(PixelAt(ctx, x, 121)) - bg, 200) << "from the clip edge down at x " << x;
        }
        for (int i = 0; i < 12; ++i)
        {
            const float a = 0.15f + float(i) * 2.8f / 12.0f;   // the lower half
            EXPECT_GT(Green(PixelAt(ctx, int(std::floor(260.0f + std::cos(a) * 26.0f)), int(std::floor(120.0f + std::sin(a) * 26.0f)))) - bg, 200)
                << "the lower half at angle " << i;
        }
    }

    // Dots after other primitives in the same frame are their own pass, which starts
    // part-way into the dots: each is drawn where it was put, in the order it was drawn.
    TEST_F(GpuReadbackTest, DotPassesAfterOtherPrimitives)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        const tachyon::VisualColorStyleIndex red = styleManager.RegisterColorStyle(
            tachyon::ColorStyle::Create(tachyon::color4b{ 255, 0, 0, 255 }));

        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderDot(tachyon::float2{ 40.0f, 40.0f }, kFullClip, 20.0f, tachyon::color4f{ 1, 1, 1, 1 });
            r->RenderDot(tachyon::float2{ 80.0f, 40.0f }, kFullClip, 20.0f, tachyon::color4f{ 1, 1, 1, 1 });
            r->RenderQuad(tachyon::IntRect{ 20, 140, 80, 40 }, kFullClip, red);
            r->RenderDot(tachyon::float2{ 40.0f, 160.0f }, kFullClip, 20.0f, tachyon::color4f{ 1, 1, 1, 1 });
            r->RenderDot(tachyon::float2{ 80.0f, 160.0f }, kFullClip, 20.0f, tachyon::color4f{ 1, 1, 1, 1 });
        });

        const int bg = Green(PixelAt(ctx, 2, 2));
        EXPECT_GT(Green(PixelAt(ctx, 40, 40)) - bg, 200);
        EXPECT_GT(Green(PixelAt(ctx, 80, 40)) - bg, 200);
        EXPECT_GT(Green(PixelAt(ctx, 40, 160)), 200) << "drawn over the quad drawn before it";
        EXPECT_GT(Green(PixelAt(ctx, 80, 160)), 200) << "drawn over the quad drawn before it";
        EXPECT_LT(Green(PixelAt(ctx, 60, 160)), 20) << "the red quad between the dots";
    }
    // ---- Indexed quads ------------------------------------------------------

    // Quad-shaped primitives are drawn from a shared index buffer, four vertices each. A
    // pass bigger than the buffer grows it mid-frame; the passes before and after it, and
    // every primitive in it, still land where they were put.
    TEST_F(GpuReadbackTest, IndexedQuadsBeyondTheIndexBuffer)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();

        constexpr int kW = 100, kH = 60;   // 6000 one-pixel quads in one pass
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderQuad(tachyon::IntRect{ 4, 4, 8, 8 }, kFullClip, solidColor);
            r->RenderDot(tachyon::float2{ 300.0f, 220.0f }, kFullClip, 4.0f, tachyon::color4f{ 1, 1, 1, 1 });
            for (int y = 0; y < kH; ++y)
                for (int x = 0; x < kW; ++x)
                    r->RenderQuad(tachyon::IntRect{ 100 + x, 100 + y, 1, 1 }, kFullClip, solidColor);
            r->RenderDot(tachyon::float2{ 300.0f, 200.0f }, kFullClip, 4.0f, tachyon::color4f{ 1, 1, 1, 1 });
            r->RenderQuad(tachyon::IntRect{ 20, 4, 8, 8 }, kFullClip, solidColor);
        });

        const int bg = Green(PixelAt(ctx, 2, 30));
        int missing = 0;
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x)
                if (Green(PixelAt(ctx, 100 + x, 100 + y)) - bg < 200)
                    ++missing;
        EXPECT_EQ(missing, 0) << "every quad of the big pass";
        EXPECT_EQ(Green(PixelAt(ctx, 100 + kW, 120)), bg) << "nothing past the last column";
        EXPECT_GT(Green(PixelAt(ctx, 8, 8)) - bg, 200) << "the pass before it";
        EXPECT_GT(Green(PixelAt(ctx, 24, 8)) - bg, 200) << "the pass after it";
        EXPECT_EQ(Green(PixelAt(ctx, 16, 8)), bg);

        // The next frame draws from the grown buffer.
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderQuad(tachyon::IntRect{ 40, 40, 8, 8 }, kFullClip, solidColor);
        });
        EXPECT_GT(Green(PixelAt(ctx, 44, 44)) - bg, 200);
        EXPECT_EQ(Green(PixelAt(ctx, 120, 120)), bg) << "last frame's quads are gone";
    }
    // ---- Cached scopes on the GPU -----------------------------------------------

    // A cached scope's primitives go to the GPU once: the frame that captures it uploads
    // them, the replays after it upload nothing and draw them, moved, from the GPU copy,
    // clip rects included (here a dot clipped by its own rect and a circle).
    TEST_F(GpuReadbackTest, CachedScopeIsDrawnFromTheGpu)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();
        auto* renderer = static_cast<tachyon::PrimitiveRenderer*>(graphicsContext->guiRenderer);
        renderer->ClearScopeCaptures();
        const tachyon::ID scope = tachyon::ID::Invalid();

        auto capture = [&](tachyon::GUIRenderer* r) {
            r->BeginScopeCapture(scope);
            r->RenderQuad(tachyon::IntRect{ 10, 10, 30, 30 }, kFullClip, solidColor);
            r->RenderDot(tachyon::float2{ 70.0f, 25.0f }, tachyon::IntRect{ 0, 0, 70, kSize.y }, 20.0f, tachyon::color4f{ 1, 1, 1, 1 });
            r->RenderCircle(tachyon::float2{ 40.0f, 80.0f }, 20.0f, kFullClip, tachyon::LineStyle{ .colorStyleIndex = solidColor, .lineWidth = 2.0f });
            r->EndScopeCapture(scope);
        };
        RenderFrame(ctx, capture);
        const int bg = Green(PixelAt(ctx, kSize.x - 2, kSize.y - 2));
        EXPECT_GT(renderer->LastFrameUploadBytes(), 0u) << "the capture frame uploads the primitives";
        EXPECT_GT(Green(PixelAt(ctx, 25, 25)) - bg, 200);
        EXPECT_EQ(renderer->ResidentScopeCount(), 1u);

        for (int frame = 0; frame < 3; ++frame)
        {
            RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
                EXPECT_TRUE(r->ReplayScopeCapture(scope, tachyon::int2{ 150, 100 }));
            });
            EXPECT_EQ(renderer->LastFrameUploadBytes(), 0u) << "a replay uploads nothing, frame " << frame;
            EXPECT_GT(Green(PixelAt(ctx, 175, 125)) - bg, 200) << "the quad, moved";
            EXPECT_EQ(Green(PixelAt(ctx, 25, 25)), bg) << "not where it was";
            EXPECT_GT(Green(PixelAt(ctx, 215, 125)) - bg, 200) << "the dot's left half, moved";
            EXPECT_EQ(Green(PixelAt(ctx, 225, 125)), bg) << "its clip rect moved with it";
            EXPECT_GT(Green(PixelAt(ctx, 210, 180)) - bg, 100) << "the circle, moved";
        }
        EXPECT_EQ(vk->deviceContext.ResidentBufferCount(), 1u);
    }

    // Capturing a scope again replaces its GPU copy; dropping captures frees theirs.
    TEST_F(GpuReadbackTest, CachedScopeGpuCopiesAreFreed)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();
        auto* renderer = static_cast<tachyon::PrimitiveRenderer*>(graphicsContext->guiRenderer);
        renderer->ClearScopeCaptures();
        const tachyon::ID first  = tachyon::ID{ .value{ .integer = 1 } };
        const tachyon::ID second = tachyon::ID{ .value{ .integer = 2 } };
        const size_t before = vk->deviceContext.ResidentBufferCount();

        auto captureAt = [&](tachyon::ID scope, int x) {
            return [&, scope, x](tachyon::GUIRenderer* r) {
                r->BeginScopeCapture(scope);
                r->RenderQuad(tachyon::IntRect{ x, 10, 20, 20 }, kFullClip, solidColor);
                r->EndScopeCapture(scope);
            };
        };
        RenderFrame(ctx, captureAt(first, 10));
        RenderFrame(ctx, captureAt(second, 60));
        EXPECT_EQ(vk->deviceContext.ResidentBufferCount(), before + 2);

        // Captured again, elsewhere: the replay draws the new primitives.
        RenderFrame(ctx, captureAt(first, 110));
        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) { EXPECT_TRUE(r->ReplayScopeCapture(first, tachyon::int2{ 0, 0 })); });
        const int bg = Green(PixelAt(ctx, kSize.x - 2, kSize.y - 2));
        EXPECT_GT(Green(PixelAt(ctx, 120, 20)) - bg, 200);
        EXPECT_EQ(Green(PixelAt(ctx, 20, 20)), bg);
        EXPECT_EQ(vk->deviceContext.ResidentBufferCount(), before + 2) << "the old copy is freed";

        // Only `first` keeps being drawn: `second` is dropped after a while, then `first` by a clear.
        for (uint64_t frame = 0; frame <= tachyon::PrimitiveRenderer::kEvictAfterFrames; ++frame)
            RenderFrame(ctx, [&](tachyon::GUIRenderer* r) { r->ReplayScopeCapture(first, tachyon::int2{ 0, 0 }); });
        EXPECT_FALSE(renderer->HasScopeCapture(second));
        EXPECT_TRUE(renderer->HasScopeCapture(first));
        EXPECT_EQ(vk->deviceContext.ResidentBufferCount(), before + 1);

        renderer->ClearScopeCaptures();
        RenderFrame(ctx, [](tachyon::GUIRenderer*) {});
        EXPECT_EQ(vk->deviceContext.ResidentBufferCount(), before);
    }
    // ---- Occluders --------------------------------------------------------------

    // An occluder hides what was drawn before it under its rect and nothing drawn after it.
    // (In the app an opaque box drawn right after covers that rect, so nothing changes on
    // screen; here nothing covers it, so the skipped pixels show.)
    TEST_F(GpuReadbackTest, OccluderHidesWhatWasDrawnBeforeIt)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();

        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderQuad(tachyon::IntRect{ 20, 20, 200, 100 }, kFullClip, solidColor);                 // before
            r->RenderLine(tachyon::int2{ 20, 140 }, tachyon::int2{ 220, 140 }, kFullClip,
                          tachyon::LineStyle{ .colorStyleIndex = solidColor, .lineWidth = 6.0f });       // before
            r->RenderOccluder(tachyon::IntRect{ 100, 0, 60, 200 }, kFullClip);
            r->RenderQuad(tachyon::IntRect{ 140, 160, 40, 40 }, kFullClip, solidColor);                // after
        });

        const int bg = Green(PixelAt(ctx, kSize.x - 2, kSize.y - 2));
        EXPECT_EQ(PixelAt(ctx, 130, 30) & 0xFFFFFF, PixelAt(ctx, kSize.x - 2, kSize.y - 2) & 0xFFFFFF) << "the occluder draws no color";
        EXPECT_GT(Green(PixelAt(ctx, 60, 70)) - bg, 200) << "drawn before, outside the occluder";
        EXPECT_EQ(Green(PixelAt(ctx, 130, 70)), bg) << "drawn before, under the occluder: skipped";
        EXPECT_EQ(Green(PixelAt(ctx, 130, 140)), bg) << "a line too";
        EXPECT_GT(Green(PixelAt(ctx, 200, 140)) - bg, 200);
        EXPECT_GT(Green(PixelAt(ctx, 150, 180)) - bg, 200) << "drawn after, under the occluder: shown";
    }

    // The clip rect bounds an occluder like any primitive.
    TEST_F(GpuReadbackTest, OccluderIsClipped)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();

        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            r->RenderQuad(tachyon::IntRect{ 20, 20, 200, 100 }, kFullClip, solidColor);
            r->RenderOccluder(tachyon::IntRect{ 0, 0, kSize.x, kSize.y }, tachyon::IntRect{ 100, 0, 40, kSize.y });
        });
        const int bg = Green(PixelAt(ctx, kSize.x - 2, kSize.y - 2));
        EXPECT_EQ(Green(PixelAt(ctx, 120, 70)), bg);
        EXPECT_GT(Green(PixelAt(ctx, 60, 70)) - bg, 200);
        EXPECT_GT(Green(PixelAt(ctx, 160, 70)) - bg, 200);
    }

    // Draw order runs on through a cached scope: its occluder hides what the window drew
    // before the replay, not what it draws after; drawn from the GPU copy or not.
    TEST_F(GpuReadbackTest, ReplayedOccludersKeepTheDrawOrder)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();
        auto* renderer = static_cast<tachyon::PrimitiveRenderer*>(graphicsContext->guiRenderer);
        renderer->ClearScopeCaptures();
        const tachyon::ID scope = tachyon::ID::Invalid();

        auto frame = [&](bool capture) {
            return [&, capture](tachyon::GUIRenderer* r) {
                for (int i = 0; i < 8; ++i)   // more drawn before the scope than the scope holds
                    r->RenderQuad(tachyon::IntRect{ 4 + 6 * i, kSize.y - 10, 4, 4 }, kFullClip, solidColor);
                r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x, 60 }, kFullClip, solidColor);   // before the scope
                if (capture)
                {
                    r->BeginScopeCapture(scope);
                    r->RenderQuad(tachyon::IntRect{ 300, 200, 10, 10 }, kFullClip, solidColor);
                    r->RenderOccluder(tachyon::IntRect{ 100, 0, 60, kSize.y }, kFullClip);
                    r->EndScopeCapture(scope);
                }
                else
                    EXPECT_TRUE(r->ReplayScopeCapture(scope, tachyon::int2{ 0, 0 }));
                r->RenderQuad(tachyon::IntRect{ 0, 100, kSize.x, 60 }, kFullClip, solidColor); // after the scope
            };
        };
        for (int f = 0; f < 3; ++f)
        {
            RenderFrame(ctx, frame(f == 0));
            const int bg = Green(PixelAt(ctx, kSize.x - 2, kSize.y - 2));
            EXPECT_EQ(Green(PixelAt(ctx, 130, 30)), bg) << "the window's earlier quad, under the scope's occluder; frame " << f;
            EXPECT_GT(Green(PixelAt(ctx, 60, 30)) - bg, 200) << "frame " << f;
            EXPECT_GT(Green(PixelAt(ctx, 130, 130)) - bg, 200) << "the window's later quad shows; frame " << f;
        }
    }
    // ---- Uploads off the main thread ---------------------------------------------

    // A frame's primitives are copied on the upload worker; the frame waits for them before
    // it is submitted, even when the worker is still busy with other work (here, held up).
    TEST_F(GpuReadbackTest, FrameWaitsForItsUploads)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        RegisterColorStyles();

        std::vector<std::byte> scratch(8 * 1024 * 1024), source(scratch.size(), std::byte(1));
        tachyon::vulkan::UploadWorker& uploads = vk->deviceContext.Uploads();
        uploads.Copy(scratch.data(), source.data(), source.size());
        uploads.Then([] { std::this_thread::sleep_for(std::chrono::milliseconds(300)); });
        ASSERT_TRUE(uploads.Busy());

        RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
            for (int i = 0; i < 2000; ++i)   // more than is copied inline
                r->RenderQuad(tachyon::IntRect{ 20 + (i % 100) * 2, 20 + (i / 100) * 2, 2, 2 }, kFullClip, solidColor);
        });
        const int bg = Green(PixelAt(ctx, kSize.x - 2, kSize.y - 2));
        EXPECT_GT(Green(PixelAt(ctx, 21, 21)) - bg, 200);
        EXPECT_GT(Green(PixelAt(ctx, 217, 57)) - bg, 200) << "the last quad";
        EXPECT_FALSE(uploads.Busy());
    }
    // ---- GPU time ----------------------------------------------------------------

    // Each frame's drawing is timed on the GPU.
    TEST_F(GpuReadbackTest, FramesAreTimedOnTheGpu)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        if (ctx->timestampPeriodNs <= 0.0f)
            GTEST_SKIP() << "the graphics queue can't time";
        RegisterColorStyles();
        for (int f = 0; f < 4; ++f)
            RenderFrame(ctx, [&](tachyon::GUIRenderer* r) { r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x, kSize.y }, kFullClip, solidColor); });
        EXPECT_GT(ctx->LastGpuFrameSeconds(), 0.0);
        EXPECT_LT(ctx->LastGpuFrameSeconds(), 1.0);
    }

    // What the GPU spends on boxes: on their vertices alone (every box clipped away, so no
    // fragment runs) against small visible ones. Disabled by default; run with
    //   gui_gpu_tests --gtest_also_run_disabled_tests --gtest_filter=*BoxCostBench*
    TEST_F(GpuReadbackTest, DISABLED_BoxCostBench)
    {
        auto* ctx = CreateReadbackWindow();
        if (ctx == nullptr)
            GTEST_SKIP() << "no Vulkan device available";
        if (ctx->timestampPeriodNs <= 0.0f)
            GTEST_SKIP() << "the graphics queue can't time";
        RegisterColorStyles();
        tachyon::Style boxStyle{};
        boxStyle.borderWidths = tachyon::BoxEdges(1);
        boxStyle.borderRadius = tachyon::BoxCorners(2);
        for (tachyon::StyleState& s : boxStyle.states)
        {
            s.backgroundColor = tachyon::ColorStyle::Create(tachyon::color4b{ 40, 80, 120, 255 });
            s.SetBorderColor(tachyon::ColorStyle::Create(tachyon::color4b{ 200, 200, 200, 255 }));
        }
        const tachyon::StyleRef style = styleManager.Register(boxStyle);
        const uint32_t gpuStyle = (uint32_t(style.visualBoxStyleIndex) - 1) * tachyon::Style::kStateCount;

        auto measure = [&](const char* name, int count, tachyon::IntRect clip) {
            double total = 0.0;
            constexpr int kFrames = 20;
            for (int f = 0; f < kFrames + 4; ++f)
            {
                RenderFrame(ctx, [&](tachyon::GUIRenderer* r) {
                    for (int i = 0; i < count; ++i)
                        r->RenderBox(tachyon::IntRect{ (i * 7) % (kSize.x - 6), ((i * 7) / (kSize.x - 6) * 7) % (kSize.y - 6), 6, 6 },
                                     clip, gpuStyle, gpuStyle, 1.0f);
                });
                if (f >= 4)
                    total += ctx->LastGpuFrameSeconds();
            }
            std::printf("BOXCOST %-26s %6d boxes: %.3f ms GPU per frame\n", name, count, total / kFrames * 1000.0);
        };
        measure("empty frame", 0, kFullClip);
        measure("clipped away (vertices)", 40000, tachyon::IntRect{ 0, 0, 0, 0 });
        measure("visible 6x6", 40000, kFullClip);
    }
}
