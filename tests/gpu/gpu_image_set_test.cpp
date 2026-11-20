#include "gpu_test_harness.h"

#include <gui_core/graphics/vulkan/vk_descriptors.h>
#include <gui_core/graphics/vulkan/vk_graphics_context.h>
#include <gui_core/graphics/vulkan/vk_image.h>

#include <array>
#include <vector>

using namespace GpuTestHarness;

namespace
{
    tachyon::vulkan::VulkanImage FakeImage(uintptr_t view)
    {
        tachyon::vulkan::VulkanImage image{};
        image.imageView = reinterpret_cast<VkImageView>(view);
        return image;
    }
}

// The shader's texture array has a size; a slot past it can't be bound.
TEST(VulkanImageSet, RefusesMoreTexturesThanTheShaderArrayHolds)
{
    tachyon::vulkan::VulkanImageSet set;
    set.capacity = 300;
    tachyon::vulkan::VulkanImage image = FakeImage(1);
    for (uint32_t i = 0; i < set.capacity; ++i)
        ASSERT_NE(set.Add(&image), tachyon::ImageIndex::Invalid);

    EXPECT_EQ(set.Add(&image), tachyon::ImageIndex::Invalid);
    EXPECT_EQ(set.ReserveIndex(), tachyon::ImageIndex::Invalid);
    EXPECT_EQ(set.textures.size(), 300u);
}

TEST(VulkanImageSet, IgnoresIndicesItNeverHandedOut)
{
    tachyon::vulkan::VulkanImageSet set;
    tachyon::vulkan::VulkanImage image = FakeImage(1);
    const tachyon::ImageIndex index = set.Add(&image);

    EXPECT_FALSE(set.SetDescriptor(tachyon::ImageIndex(uint32_t(index) + 4), VkDescriptorImageInfo{}));
    EXPECT_FALSE(set.SetImage(tachyon::ImageIndex(uint32_t(index) + 4), &image));
    EXPECT_EQ(set.textures.size(), 1u);
}

TEST(VulkanImageSet, RemovedSlotsAreReused)
{
    tachyon::vulkan::VulkanImageSet set;
    tachyon::vulkan::VulkanImage a = FakeImage(1), b = FakeImage(2), c = FakeImage(3);
    const tachyon::ImageIndex ia = set.Add(&a);
    const tachyon::ImageIndex ib = set.Add(&b);

    set.Remove(ia);
    EXPECT_EQ(set.Add(&c), ia);
    EXPECT_EQ(set.textures[uint32_t(ia) - 1].imageView, c.imageView);
    EXPECT_EQ(set.textures[uint32_t(ib) - 1].imageView, b.imageView);
    EXPECT_EQ(set.textures.size(), 2u);
}

// A window frame writes its descriptor set again only when the set's version moved: every
// change moves it, and setting a slot to the image it holds is no change.
TEST(VulkanImageSet, ItsVersionMovesWhenASlotChanges)
{
    tachyon::vulkan::VulkanImageSet set;
    tachyon::vulkan::VulkanImage a = FakeImage(1), b = FakeImage(2);
    uint64_t version = set.Version();
    const auto moved = [&]
    {
        const uint64_t now = set.Version();
        const bool changed = now != version;
        version = now;
        return changed;
    };

    const tachyon::ImageIndex ia = set.Add(&a);
    EXPECT_TRUE(moved()) << "added";
    EXPECT_TRUE(set.SetImage(ia, &a));
    EXPECT_FALSE(moved()) << "the image it holds";
    EXPECT_TRUE(set.SetImage(ia, &b));
    EXPECT_TRUE(moved()) << "another image";
    const VkDescriptorImageInfo hosts{ .imageView = b.imageView, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    EXPECT_TRUE(set.SetDescriptor(ia, hosts));
    EXPECT_TRUE(moved()) << "a host's descriptor";
    EXPECT_TRUE(set.SetDescriptor(ia, hosts));
    EXPECT_TRUE(moved()) << "a host's again: its view may be a new one with the old handle";
    set.ReserveIndex();
    EXPECT_TRUE(moved()) << "reserved: one more slot";
    set.Remove(ia);
    EXPECT_TRUE(moved()) << "removed";
    set.Clear();
    EXPECT_TRUE(moved()) << "cleared";
}

// A window frame remembers the version it wrote, whichever set that was: two sets never hand
// out the same one.
TEST(VulkanImageSet, TwoSetsNeverShareAVersion)
{
    tachyon::vulkan::VulkanImageSet first, second;
    EXPECT_NE(first.Version(), second.Version());
    EXPECT_NE(first.Version(), 0u) << "0 is a frame that wrote none";
    tachyon::vulkan::VulkanImage a = FakeImage(1);
    first.Add(&a);
    second.Add(&a);
    EXPECT_NE(first.Version(), second.Version());
}

// A window draws into its draw image as a color attachment and copies it out; nothing writes it
// as a storage image, which would keep drivers from compressing it (DCC).
TEST(VulkanWindowContext, TheDrawImageIsNoStorageImage)
{
    constexpr VkImageUsageFlags usage = tachyon::vulkan::VulkanWindowContext::kDrawImageUsage;
    EXPECT_EQ(usage & VK_IMAGE_USAGE_STORAGE_BIT, 0u);
    EXPECT_NE(usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0u);
    EXPECT_NE(usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 0u);
}

// Asked for before the device is made, the image slots are as many as asked for (one at
// least), whatever the device holds; after, as many as there are.
TEST(VulkanImageSet, TheSlotCountIsSetBeforeTheDevice)
{
    tachyon::vulkan::VulkanGraphicsContext vulkan;
    EXPECT_EQ(vulkan.GetImageSlotCount(), 4096u) << "by default";
    vulkan.SetImageSlotCount(1000);
    EXPECT_EQ(vulkan.GetImageSlotCount(), 1000u);
    vulkan.SetImageSlotCount(0);
    EXPECT_EQ(vulkan.GetImageSlotCount(), 1u);
}

// A frame's descriptor pool holds a set of every image slot, however few sets it's for:
// a driver may refuse a set bigger than what's left in its pool.
TEST(VulkanImageSet, APoolHoldsAtLeastItsMinimum)
{
    const tachyon::vulkan::DescriptorAllocatorGrowable::PoolSizeRatio samplers{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4, 8192 };
    EXPECT_EQ(samplers.CountFor(1000), 8192u) << "the minimum, over four per set";
    EXPECT_EQ(samplers.CountFor(4000), 16000u) << "four per set, over the minimum";
    const tachyon::vulkan::DescriptorAllocatorGrowable::PoolSizeRatio buffers{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 };
    EXPECT_EQ(buffers.CountFor(1000), 3000u) << "no minimum";
}

namespace
{
    // A device made with `slots` image slots asked for.
    template <uint32_t slots>
    class GpuImageSlotsTest : public GpuTest
    {
    protected:
        void BeforeInitialize() override { vk->SetImageSlotCount(slots); }

        // A 1x1 image of `rgba`, registered.
        tachyon::ImageIndex Register(uint32_t rgba)
        {
            tachyon::Image* image = vk->deviceContext.CreateImage(&rgba, tachyon::uint2{ 1, 1 }, tachyon::ImageFormat::R8G8B8A8,
                                                                  tachyon::Sampler::LinearClampToEdge, false);
            images.push_back(image);
            return image != nullptr ? vk->RegisterImage(image) : tachyon::ImageIndex::Invalid;
        }

        void TearDown() override
        {
            for (tachyon::Image* image : images)
                if (image != nullptr) vk->deviceContext.DestroyImageAtEnd(image, tachyon::DeviceScope::Application);
            GpuTest::TearDown();
        }

        std::vector<tachyon::Image*> images;
    };
    using GpuManyImageSlotsTest = GpuImageSlotsTest<8192>;
    using GpuFewImageSlotsTest  = GpuImageSlotsTest<300>;
    using GpuImageDescriptorTest = GpuImageSlotsTest<4096>;
}

// Each frame slot keeps the descriptor set it wrote and binds it again while the image set
// is unchanged. An image registered later is written once into each slot's set, and draws.
TEST_F(GpuImageDescriptorTest, AFrameWritesTheImageSlotsOnlyWhenTheyChanged)
{
    auto* window = CreateImageWindowContext({ 64, 64 });
    ASSERT_NE(window, nullptr);
    const tachyon::ImageIndex grey = Register(0xff808080u);
    ASSERT_NE(grey, tachyon::ImageIndex::Invalid);
    const auto white = styleManager.RegisterColorStyle(tachyon::ColorStyle::Create(
        tachyon::color4b{ 255, 255, 255, 255 }, tachyon::color4b{ 255, 255, 255, 255 }, tachyon::ColorType::StretchedTexture));
    const tachyon::IntRect full{ 0, 0, 64, 64 };
    const auto frames = [&](int count, tachyon::ImageIndex image)
    {
        for (int frame = 0; frame < count; ++frame)
        {
            graphicsContext->BeginFrame();
            graphicsContext->PrepareFrame(styleManager, gradientManager, 0.0f);
            tachyon::GUIRenderer* renderer = graphicsContext->guiRenderer;
            renderer->Reset();
            if (renderer->BeginWindow(window->GetID()))
            {
                renderer->RenderQuad(full, full, white, 0.0f, image);
                renderer->EndWindow();
            }
            window->RenderWindowFrame(graphicsContext, full, false);
        }
    };

    frames(4, grey);   // the uploads and the gradient pages settle
    const uint64_t settled = vk->ImageDescriptorSetWrites();
    frames(4, grey);
    EXPECT_EQ(vk->ImageDescriptorSetWrites(), settled) << "nothing changed: no writes";

    const tachyon::ImageIndex red = Register(0xff0000ffu);   // RGBA: red, opaque
    ASSERT_NE(red, tachyon::ImageIndex::Invalid);
    frames(4, red);
    EXPECT_EQ(vk->ImageDescriptorSetWrites(), settled + tachyon::vulkan::FRAME_OVERLAP) << "once for each frame slot";

    std::vector<uint8_t> pixels(64 * 64 * 4);
    ASSERT_TRUE(window->ReadPixels(pixels));
    const size_t centre = (size_t(32) * 64 + 32) * 4;
    EXPECT_TRUE(pixels[centre + 2] > 200 && pixels[centre + 1] < 50 && pixels[centre] < 50)
        << "the new image, red: " << int(pixels[centre + 2]) << "," << int(pixels[centre + 1]) << "," << int(pixels[centre]);
    EXPECT_FALSE(sink.HasErrors()) << sink.Dump();
}

// Thousands of images registered, more than a frame's first descriptor pool holds: the
// last one's slot is drawn from like the first's.
TEST_F(GpuManyImageSlotsTest, AnImageInALateSlotIsDrawn)
{
    if (vk->GetImageSlotCount() < 7001)
        GTEST_SKIP() << "the device holds " << vk->GetImageSlotCount() << " image slots";
    auto* window = CreateImageWindowContext({ 64, 64 });
    ASSERT_NE(window, nullptr);

    const tachyon::ImageIndex grey = Register(0xff808080u);
    ASSERT_NE(grey, tachyon::ImageIndex::Invalid);
    for (int i = 1; i < 7000; ++i)
        ASSERT_NE(vk->RegisterImage(images[0]), tachyon::ImageIndex::Invalid) << i;
    const tachyon::ImageIndex red = Register(0xff0000ffu);   // RGBA: red, opaque
    ASSERT_EQ(uint32_t(red), 7001u);

    const auto white = styleManager.RegisterColorStyle(tachyon::ColorStyle::Create(
        tachyon::color4b{ 255, 255, 255, 255 }, tachyon::color4b{ 255, 255, 255, 255 }, tachyon::ColorType::StretchedTexture));
    const tachyon::IntRect full{ 0, 0, 64, 64 };
    for (int frame = 0; frame < 3; ++frame)   // the uploads land with the first
    {
        graphicsContext->BeginFrame();
        graphicsContext->PrepareFrame(styleManager, gradientManager, 0.0f);
        tachyon::GUIRenderer* renderer = graphicsContext->guiRenderer;
        renderer->Reset();
        if (renderer->BeginWindow(window->GetID()))
        {
            renderer->RenderQuad(tachyon::IntRect{ 0, 0, 32, 64 }, full, white, 0.0f, red);
            renderer->RenderQuad(tachyon::IntRect{ 32, 0, 32, 64 }, full, white, 0.0f, grey);
            renderer->EndWindow();
        }
        window->RenderWindowFrame(graphicsContext, full, false);
    }
    EXPECT_FALSE(sink.HasErrors()) << sink.Dump();
    for (const std::string& warning : sink.warnings)   // some drivers allocate past it, others fail
        EXPECT_EQ(warning.find("only has a total of"), std::string::npos) << "a set bigger than its pool: " << warning;

    std::vector<uint8_t> pixels(64 * 64 * 4);
    ASSERT_TRUE(window->ReadPixels(pixels));
    const auto bgra = [&](int x, int y) { const size_t i = (size_t(y) * 64 + size_t(x)) * 4; return std::array<int, 3>{ pixels[i + 2], pixels[i + 1], pixels[i] }; };
    const auto left = bgra(16, 32), right = bgra(48, 32);
    EXPECT_TRUE(left[0] > 200 && left[1] < 50 && left[2] < 50)
        << "the late slot's image, red: " << left[0] << "," << left[1] << "," << left[2];
    EXPECT_TRUE(right[0] > 60 && right[0] < 200 && right[0] == right[1] && right[1] == right[2])
        << "the first slot's, grey: " << right[0] << "," << right[1] << "," << right[2];
}

// Fewer slots asked for: that many, and no more.
TEST_F(GpuFewImageSlotsTest, TheImageSetHoldsTheSlotsAskedFor)
{
    ASSERT_EQ(vk->GetImageSlotCount(), 300u);
    auto* set = static_cast<tachyon::vulkan::VulkanImageSet*>(vk->GetImageSet());
    EXPECT_EQ(set->capacity, 300u);
    auto* window = CreateImageWindowContext({ 32, 32 });
    ASSERT_NE(window, nullptr);
    RenderOneFrameWithDot(window, { 32, 32 });   // the gradient pages take their slots

    const tachyon::ImageIndex first = Register(0xffffffffu);
    ASSERT_NE(first, tachyon::ImageIndex::Invalid);
    while (set->textures.size() < 300)
        ASSERT_NE(vk->RegisterImage(images[0]), tachyon::ImageIndex::Invalid);
    EXPECT_EQ(vk->RegisterImage(images[0]), tachyon::ImageIndex::Invalid);

    graphicsContext->BeginFrame();
    RenderOneFrameWithDot(window, { 32, 32 });   // every slot bound
    EXPECT_FALSE(sink.HasErrors()) << sink.Dump();
}

// An unregistered image's slot (or a reserved one never set) must not be written into the frame's descriptor
// set: its view is gone, and a null view needs the nullDescriptor feature.
TEST_F(GpuTest, UnregisteredImageSlotIsNotBoundNextFrame)
{
    auto* ctx = CreateImageWindowContext({ 320, 240 });
    if (ctx == nullptr)
        GTEST_SKIP() << "no Vulkan device available";

    auto& dc = vk->deviceContext;
    uint32_t pixel = 0xffffffffu;
    tachyon::Image* img = dc.CreateImage(&pixel, tachyon::uint2{ 1, 1 }, tachyon::ImageFormat::R8G8B8A8,
                                     tachyon::Sampler::LinearClampToEdge, false);
    ASSERT_NE(img, nullptr);
    const tachyon::ImageIndex index = vk->RegisterImage(img);
    ASSERT_NE(index, tachyon::ImageIndex::Invalid);
    RenderOneFrameWithDot(ctx, { 320, 240 });

    vk->UnregisterImage(index);
    dc.DestroyImageAtEnd(img, tachyon::DeviceScope::Frame);
    static_cast<tachyon::vulkan::VulkanImageSet*>(vk->GetImageSet())->ReserveIndex(); // reserved, never set
    sink.Clear();
    for (int i = 0; i < 3; ++i)
    {
        graphicsContext->BeginFrame();
        RenderOneFrameWithDot(ctx, { 320, 240 });
    }

    EXPECT_FALSE(sink.HasErrors()) << sink.Dump();
}
