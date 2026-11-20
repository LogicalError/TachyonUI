// A lost Vulkan device on a real GPU: the app's images are made again under their indices, and
// the UI draws on. Vulkan only: the loss is simulated through the Vulkan backend.

#include <gtest/gtest.h>

#include <gui_controls/gui_application_internal.h>
#include <gui_core/graphics/vulkan/vk_graphics_context.h>

#include <chrono>
#include <cstdlib>
#include <cstddef>
#include <thread>
#include <vector>

namespace
{
    // A render-to-image window showing a red image of the app's.
    class DeviceLossApp : public tachyon::GUIApplicationInternals
    {
    public:
        tachyon::ID         windowID = tachyon::ID::Invalid();
        tachyon::Image*     image    = nullptr;
        tachyon::ImageIndex red      = tachyon::ImageIndex::Invalid;

        void OnGUI() override
        {
            if (BeginWindow(windowID, "Image", theme.windowStyleRef, tachyon::LayoutOverride{}.Size(240, 120),
                            tachyon::ElementFlags::WindowRenderToImage))
                Image(red, tachyon::LayoutOverride{}.Size(tachyon::Pixels{ 1000 }, tachyon::Pixels{ 1000 }));
            EndWindow();
        }

        // handOver: the image is dropped for the application's lifetime at once (as an app
        // does that only draws it by its index), before any frame uploads it.
        bool Start(bool handOver = false)
        {
            if (!Initialize())
                return false;
            windowID = idManager.CreateStaticID();
            std::vector<uint8_t> pixels(4 * 4 * 4);
            for (size_t i = 0; i < pixels.size(); i += 4) { pixels[i] = 255; pixels[i + 3] = 255; }   // RGBA red
            image = graphicsContext->CreateImage(pixels.data(), tachyon::uint2{ 4, 4 }, tachyon::ImageFormat::R8G8B8A8,
                                                 tachyon::Sampler::LinearClampToEdge);
            red = graphicsContext->RegisterImage(image);
            const bool made = image != nullptr && red != tachyon::ImageIndex::Invalid;
            if (made && handOver)
            {
                graphicsContext->DestroyImageAtEnd(image, tachyon::DeviceScope::Application);
                image = nullptr;
            }
            return made;
        }

        // The window's center pixel is red (BGRA8).
        bool CenterIsRed()
        {
            const tachyon::IntRect rect = windowManager->GetWindowRect(windowID);
            if (rect.w <= 0) return false;
            std::vector<uint8_t> pixels(size_t(rect.w) * rect.h * 4);
            if (!ReadWindowPixels(windowID, pixels)) return false;
            const size_t c = (size_t(rect.h / 2) * rect.w + size_t(rect.w / 2)) * 4;
            return pixels[c + 2] > 200 && pixels[c + 1] < 50 && pixels[c] < 50;
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
    };

    // A lost device is made again before the next frame (#47): the app's image keeps its
    // index and pixels, and the window draws on.
    TEST(GpuApplicationInit, ALostDeviceIsMadeAgainAndTheUIDrawsOn)
    {
        DeviceLossApp app;
        if (!app.Start())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        ASSERT_TRUE(app.Frames(3));
        ASSERT_TRUE(app.CenterIsRed()) << "the image draws before the loss";

        auto* vulkan = static_cast<tachyon::vulkan::VulkanGraphicsContext*>(app.graphicsContext);
        vulkan->SimulateDeviceLoss();
        EXPECT_TRUE(app.graphicsContext->DeviceLost());
        ASSERT_TRUE(app.Frames(3)) << "the frames go on";
        EXPECT_FALSE(app.graphicsContext->DeviceLost());
        EXPECT_TRUE(app.CenterIsRed()) << "the image draws from its index on the new device";

        app.graphicsContext->DestroyImageAtEnd(app.image, tachyon::DeviceScope::Application);
        app.Cleanup();
    }

    // An image handed over at once (dropped for the application's lifetime, before any frame)
    // still gets its pixels: only a frame-scope drop gives up its upload.
    TEST(GpuApplicationInit, AnImageHandedOverAtOnceStillGetsItsPixels)
    {
        DeviceLossApp app;
        if (!app.Start(/*handOver*/ true))
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        ASSERT_TRUE(app.Frames(3));
        EXPECT_TRUE(app.CenterIsRed());
        app.Cleanup();
    }

    // An image handed over at once is still the app's after a lost device: it is made again,
    // with its pixels, under its index.
    TEST(GpuApplicationInit, AnImageHandedOverAtOnceIsMadeAgainAfterADeviceLoss)
    {
        DeviceLossApp app;
        if (!app.Start(/*handOver*/ true))
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        ASSERT_TRUE(app.Frames(3));
        ASSERT_TRUE(app.CenterIsRed());

        auto* vulkan = static_cast<tachyon::vulkan::VulkanGraphicsContext*>(app.graphicsContext);
        vulkan->SimulateDeviceLoss();
        ASSERT_TRUE(app.Frames(3)) << "the frames go on";
        EXPECT_FALSE(app.graphicsContext->DeviceLost());
        EXPECT_TRUE(app.CenterIsRed()) << "the image draws from its index on the new device";
        app.Cleanup();
    }

    // Images made again while the upload worker is busy get their pixels when it reaches
    // them, after the rebuild has returned and the memory it let go of is used again.
    TEST(GpuApplicationInit, ImagesMadeAgainWhileTheUploadWorkerIsBusyKeepTheirPixels)
    {
        DeviceLossApp app;
        if (!app.Start())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        ASSERT_TRUE(app.Frames(3));
        ASSERT_TRUE(app.CenterIsRed());

        auto* vulkan = static_cast<tachyon::vulkan::VulkanGraphicsContext*>(app.graphicsContext);
        tachyon::vulkan::VulkanDeviceContext& device = vulkan->deviceContext;
        device.WaitIdle();
        device.ReleaseLiveImages();
        std::vector<std::byte> scratch(8 * 1024 * 1024), source(scratch.size(), std::byte(1));
        device.Uploads().Copy(scratch.data(), source.data(), source.size());
        device.Uploads().Then([] { std::this_thread::sleep_for(std::chrono::milliseconds(200)); });
        ASSERT_TRUE(device.Uploads().Busy());
        device.RebuildLiveImages();
        vulkan->vulkanImageSet->RefreshDescriptors();
        std::vector<std::vector<uint8_t>> reused;
        for (int i = 0; i < 64; i++)
            reused.emplace_back(4 * 4 * 4, uint8_t(0));

        ASSERT_TRUE(app.Frames(2));
        EXPECT_TRUE(app.CenterIsRed()) << "the image is copied from its own pixels";

        app.graphicsContext->DestroyImageAtEnd(app.image, tachyon::DeviceScope::Application);
        app.Cleanup();
    }

    // Text after a lost device: the glyph atlas's pages are made again and its slots point
    // at them, so the label still draws.
    class DeviceLossTextApp : public DeviceLossApp
    {
    public:
        void OnGUI() override
        {
            if (BeginWindow(windowID, "Text", theme.windowStyleRef, tachyon::LayoutOverride{}.Size(240, 120),
                            tachyon::ElementFlags::WindowRenderToImage))
                Text(idManager.AllocateDynamicID("label"), "WWWWWWWWWWWWWWWW");
            EndWindow();
        }
        // The pixels unlike the corner's (the window background): the text's.
        size_t InkPixels()
        {
            const tachyon::IntRect rect = windowManager->GetWindowRect(windowID);
            if (rect.w <= 0) return 0;
            std::vector<uint8_t> pixels(size_t(rect.w) * rect.h * 4);
            if (!ReadWindowPixels(windowID, pixels)) return 0;
            size_t ink = 0;
            for (size_t i = 0; i + 3 < pixels.size(); i += 4)
                if (std::abs(int(pixels[i]) - int(pixels[0])) + std::abs(int(pixels[i + 1]) - int(pixels[1])) > 60)
                    ink++;
            return ink;
        }
    };

    TEST(GpuApplicationInit, TextDrawsAfterADeviceLoss)
    {
        DeviceLossTextApp app;
        if (!app.Start())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        ASSERT_TRUE(app.Frames(3));
        const size_t before = app.InkPixels();
        ASSERT_GT(before, 50u) << "the label draws before the loss";

        static_cast<tachyon::vulkan::VulkanGraphicsContext*>(app.graphicsContext)->SimulateDeviceLoss();
        ASSERT_TRUE(app.Frames(3));
        EXPECT_FALSE(app.graphicsContext->DeviceLost());
        EXPECT_GT(app.InkPixels(), before / 2) << "the glyphs draw from the remade atlas";

        app.graphicsContext->DestroyImageAtEnd(app.image, tachyon::DeviceScope::Application);
        app.Cleanup();
    }
}
