#pragma once

#include <chrono>

#include <gui_core/windows/gui_window_backend.h>
#include <gui_core/windows/gui_os_window_manager.h>

#include <gui_core/graphics/vulkan/vk_types.h>
#include <gui_core/graphics/vulkan/vk_descriptors.h>
#include <gui_core/graphics/vulkan/vk_device_context.h>

namespace tachyon
{
    class GraphicsContext;
}


namespace tachyon::vulkan
{
    /// @addtogroup graphics_vulkan
    /// @{

    class VulkanGraphicsContext;

    constexpr unsigned int FRAME_OVERLAP = 2;

    struct FrameData
    {
        VkSemaphore swapchainSemaphore = {};
        VkFence renderFence = {};

        /// Holds imageDescriptors alone; cleared when that is made again.
        DescriptorAllocatorGrowable frameDescriptors = {};
        /// The image set's slots as this frame slot last wrote them, bound again while the set's
        /// version is imageDescriptorsVersion (0: none written). Rewritten only after the slot's
        /// fence, so no frame in flight has it bound.
        VkDescriptorSet imageDescriptors        = VK_NULL_HANDLE;
        uint64_t        imageDescriptorsVersion = 0;
        DeletionQueue flushQueue = {};
        DeletionQueue deletionQueue = {};

        VkCommandPool commandPool = {};
        VkCommandBuffer mainCommandBuffer = {};

        /// Fence-protected, persistently mapped upload arena for GUI primitive
        /// streams, reused each time this frame slot comes round again.
        Buffer          primitiveUploadBuffer {};
        VkDeviceAddress primitiveUploadAddress = 0;
        size_t          primitiveUploadCapacity = 0;
        size_t          primitiveUploadOffset = 0;

        /// Per-slot draw and depth images, so in-flight frames don't share them (frame N's
        /// blit reads drawImage while frame N+1 writes it). The slot's renderFence wait in
        /// BeginWindowFrame gates their reuse.
        VulkanImage drawImage = {};
        VulkanImage depthImage = {};
        /// The layout the slot's last commands left drawImage in: GENERAL after drawing,
        /// TRANSFER_SRC_OPTIMAL once copied out (to the swapchain, the host's image, or read back).
        VkImageLayout drawImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        /// GPU timestamps at the start and the end of the frame's drawing; none when the
        /// queue can't time.
        VkQueryPool timestamps        = VK_NULL_HANDLE;
        bool        timestampsWritten = false; ///< by the last frame in this slot
    };

    struct SwapchainState
    {
        // swapchain
        VkSwapchainKHR              swapchain = {};
        VkFormat                    swapchainImageFormat = {};
        VkExtent2D                  swapchainExtent = {};
        ::std::vector<VkImage>      swapchainImages;
        ::std::vector<VkImageView>  swapchainImageViews;
        // Signaled by the frame's submit, waited on by its present; one per image because
        // no fence covers the present's wait, so a frame slot's semaphore could still be pending.
        ::std::vector<VkSemaphore>  presentSemaphores;
        uint32_t                    swapchainImageIndex = 0;
        VkPresentModeKHR            presentMode = VK_PRESENT_MODE_FIFO_KHR;
    };

    struct WindowVulkanState
    {
        // commands
        FrameData       frames[FRAME_OVERLAP];
        VkCommandBuffer commandBuffer = {};
    };

    class VulkanWindowContext : public tachyon::IWindowContext
    {
    public:
        tachyon::ID                  id { tachyon::ID::Invalid()};
        VulkanDeviceContext*     deviceContext{ nullptr };
        tachyon::OSWindowHandle      osHandle { nullptr };
        void*                    nativeHandle { nullptr };
        tachyon::OnGuiFunc           onGui{};

        tachyon::WindowFlags         windowFlags = tachyon::WindowFlags::None;
                                 
        SwapchainState           swapchain = {};
        WindowVulkanState        state = {};
                                 
        // surface               
        VkSurfaceKHR             surface = {};
        VkExtent2D               windowExtent = {};
        VkExtent2D               drawExtent = {};
        /// The size the last frame was recorded for; without a swapchain, what's drawn.
        VkExtent2D               requestedExtent = {};
        /// The size the swapchain was last remade for: a window grown smaller than its
        /// swapchain asks again once, even on drivers that don't report it out of date.
        VkExtent2D               swapchainRequest = {};

        bool                     resize_requested{ false };
        bool                     freeze_rendering{ false };
        tachyon::int2                mouse_pos = { 0,0 };
        uint64_t                 frameNumber{ 0 };   ///< frames recorded; never wraps

        /// When the last vkQueuePresentKHR returned. The frame-build time runs from here to
        /// just before the next present: the CPU work, without the vsync wait.
        std::chrono::steady_clock::time_point lastPresentEnd{};
        bool                     havePresentEnd{ false };
        double                   lastBuildSeconds{ 0.0 };
        bool                     lastBuildValid{ false };
        /// How long this frame's swapchain image took to acquire; counted with present's wait.
        double                   acquireSeconds{ 0.0 };
        /// The last presented frame's swapchain wait: acquiring its image and presenting it.
        double                   lastSwapchainWaitSeconds{ 0.0 };
        /// Recorded and submitted, waiting for PresentWindowFrame.
        bool                     presentPending{ false };
        FrameData*               readbackPending{ nullptr };

        /// Transparent window: each frame is read back for the Windows layered window.
        bool                     needsReadback{ false };
        bool                     renderToImage{ false };      ///< WindowFlags::RenderToImage: no surface
        VkImage                  targetImage{ VK_NULL_HANDLE }; ///< host image each frame is copied into
        VkExtent2D               targetExtent{};
        bool                     targetWritten{ false };       ///< targetImage holds a previous frame
        tachyon::vulkan::AllocatedBuffer  readbackBuffer = {};
        std::vector<uint8_t>     cachedBitmap;

        FrameData& GetCurrentFrame();
        FrameData& GetLastFrame();

		bool IsTransparent() const override { return (windowFlags & tachyon::WindowFlags::Transparent) != tachyon::WindowFlags::None; }

        void CreateSwapchain();
        void DestroySwapchain();
        void ResizeSwapchain(tachyon::int2 size);
        void FlushWindowContext();

        /// Creates a draw and depth image pair sized to windowExtent; each FRAME_OVERLAP slot
        /// has its own.
        void CreateDrawDepthImages(VulkanImage& drawImage, VulkanImage& depthImage, VkFormat colorFormat);
        void DestroyDrawDepthImages(VulkanImage& drawImage, VulkanImage& depthImage);

        VkCommandBuffer BeginWindowFrame(FrameData& currentFrame, VulkanGraphicsContext* graphics = nullptr);
        void EndWindowFrame(FrameData& currentFrame);

        /// Copies `frame`'s draw image into the readback buffer in a submit of its own, waits,
        /// and reads it (ReadPixels of a window whose frames aren't copied out themselves).
        void PerformReadback(FrameData& frame);
        /// Records the copy of `frame`'s draw image into the readback buffer, made visible to the
        /// host, into `cmd`.
        void RecordReadbackCopy(VkCommandBuffer cmd, FrameData& frame);
        /// The readback buffer into cachedBitmap, once the copy into it is done.
        void ReadBackBuffer();
        /// Readbacks that needed a submit of their own (PerformReadback).
        uint64_t immediateReadbacks = 0;

        /// GPU time of the drawing of this window's most recent finished frame, in seconds; 0
        /// until one finished, or when the device can't time.
        double LastGpuFrameSeconds() const { return lastGpuFrameSeconds; }
        double lastGpuFrameSeconds = 0.0;
        float  timestampPeriodNs   = 0.0f; ///< nanoseconds per timestamp tick; 0: no timestamps
        bool ReadPixels(std::span<uint8_t> bgra) override;
        /// The host image each frame is copied into (render-to-image windows); VK_NULL_HANDLE stops.
        void SetImageTarget(VkImage image, VkExtent2D extent);
        void TransitionTarget(VkCommandBuffer cmd, VkImageLayout from, VkImageLayout to,
                              VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                              VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

        static VulkanWindowContext* Create(tachyon::ID id, void* nativeHandle, tachyon::OSWindowHandle osHandle, tachyon::int2 size, tachyon::WindowFlags windowFlags, VulkanDeviceContext* deviceContext, tachyon::OnGuiFunc onGUI);
        void Destroy();


        tachyon::ID GetID() const override { return id; }

        void RenderWindowFrame(tachyon::GraphicsContext* graphicsContext, tachyon::IntRect rect, bool resizing) override;
        void RecordWindowFrame(tachyon::GraphicsContext* graphicsContext, tachyon::IntRect rect) override;
        std::optional<double> PresentWindowFrame() override;
        /// The last presented frame's wait on the swapchain, in seconds (PresentWindowFrame's).
        double LastSwapchainWaitSeconds() const { return lastSwapchainWaitSeconds; }
        bool NeedsRecord() const override { return resize_requested; }
        void RecordFrame(tachyon::GraphicsContext* graphicsContext, tachyon::IntRect rect, bool resizing);
        
        void UpdateTransparentWindow() override;

        tachyon::OSWindowHandle GetWindowHandle() const override { return osHandle; }
    };

    /// @}
}
