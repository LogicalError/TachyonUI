// The timeline semaphores a GUI frame's submissions wait on (VulkanGraphicsContext::AddFrameWait):
// what a submission waits on, how waits on one semaphore merge, and which ones the next
// frame drops.

#include <gtest/gtest.h>

#include <gui_core/graphics/vulkan/vk_device_context.h>

#include <cstdint>

namespace
{
    using tachyon::vulkan::FrameWaits;

    VkSemaphore Semaphore(uintptr_t n) { return reinterpret_cast<VkSemaphore>(n); }

    TEST(FrameWaits, ASubmissionWaitsOnEveryOneAdded)
    {
        FrameWaits waits;
        EXPECT_TRUE(waits.Waits().empty());
        waits.Add(Semaphore(1), 5, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
        waits.Add(Semaphore(2), 9, VK_PIPELINE_STAGE_2_TRANSFER_BIT);
        waits.Add(VK_NULL_HANDLE, 3, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
        ASSERT_EQ(waits.Waits().size(), 2u) << "not a null one";
        const VkSemaphoreSubmitInfo& first = waits.Waits()[0];
        EXPECT_EQ(first.sType, VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        EXPECT_EQ(first.pNext, nullptr);
        EXPECT_EQ(first.semaphore, Semaphore(1));
        EXPECT_EQ(first.value, 5u);
        EXPECT_EQ(first.stageMask, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
        EXPECT_EQ(first.deviceIndex, 0u);
        EXPECT_EQ(waits.Waits()[1].semaphore, Semaphore(2));
        EXPECT_EQ(waits.Waits()[1].value, 9u);
    }

    // Twice on one semaphore: once, for the higher value, before both stages.
    TEST(FrameWaits, WaitsOnOneSemaphoreMerge)
    {
        FrameWaits waits;
        waits.Add(Semaphore(1), 7, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
        waits.Add(Semaphore(1), 4, VK_PIPELINE_STAGE_2_TRANSFER_BIT);
        ASSERT_EQ(waits.Waits().size(), 1u);
        EXPECT_EQ(waits.Waits()[0].value, 7u) << "the higher value";
        EXPECT_EQ(waits.Waits()[0].stageMask, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT);
        waits.Add(Semaphore(1), 8, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
        EXPECT_EQ(waits.Waits()[0].value, 8u);
    }

    // Every submission of a frame waits on them; the next frame drops those submitted and
    // keeps those added since.
    TEST(FrameWaits, TheNextFrameDropsThoseSubmitted)
    {
        FrameWaits waits;
        waits.Add(Semaphore(1), 1, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
        waits.Submitted();
        EXPECT_EQ(waits.Waits().size(), 1u) << "another window's submission of the frame waits on it too";
        waits.Submitted();
        waits.Add(Semaphore(2), 2, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);   // after the frame's submissions
        waits.NextFrame();
        ASSERT_EQ(waits.Waits().size(), 1u);
        EXPECT_EQ(waits.Waits()[0].semaphore, Semaphore(2)) << "kept for the next submission";

        // A frame that submits nothing keeps them.
        waits.NextFrame();
        ASSERT_EQ(waits.Waits().size(), 1u);
        waits.Submitted();
        waits.NextFrame();
        EXPECT_TRUE(waits.Waits().empty());
    }

    // A semaphore submitted and waited on again: one wait, kept for the next frame.
    TEST(FrameWaits, ASubmittedSemaphoreWaitedOnAgainStays)
    {
        FrameWaits waits;
        waits.Add(Semaphore(1), 1, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
        waits.Add(Semaphore(2), 1, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
        waits.Submitted();
        waits.Add(Semaphore(1), 2, VK_PIPELINE_STAGE_2_TRANSFER_BIT);
        ASSERT_EQ(waits.Waits().size(), 2u) << "not twice in a submission";
        waits.NextFrame();
        ASSERT_EQ(waits.Waits().size(), 1u);
        EXPECT_EQ(waits.Waits()[0].semaphore, Semaphore(1));
        EXPECT_EQ(waits.Waits()[0].value, 2u);
        EXPECT_EQ(waits.Waits()[0].stageMask, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    }

    TEST(FrameWaits, ClearDropsAll)
    {
        FrameWaits waits;
        waits.Add(Semaphore(1), 1, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
        waits.Submitted();
        waits.Add(Semaphore(2), 1, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
        waits.Clear();
        EXPECT_TRUE(waits.Waits().empty());
        waits.Add(Semaphore(3), 1, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
        waits.NextFrame();
        EXPECT_EQ(waits.Waits().size(), 1u) << "nothing counted as submitted after it";
    }
}
