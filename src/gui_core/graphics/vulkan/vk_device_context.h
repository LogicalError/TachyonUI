#pragma once

#include <gui_core/gui_base_types.h>
#include <gui_core/graphics/gui_graphics_context.h>   // tachyon::DeviceScope
#include <gui_core/graphics/gui_image.h>
#include <gui_core/windows/gui_window_backend.h>      // tachyon::OSWindowHandle

#include <gui_core/graphics/vulkan/vk_image.h>
#include <gui_core/graphics/vulkan/vk_meshes.h>
#include <gui_core/graphics/vulkan/vk_rhi.h>
#include <gui_core/graphics/vulkan/vk_upload_worker.h>
#include <gui_core/graphics/vulkan/vk_descriptors.h>
#include <gui_core/memory/gui_allocation_hooks.h>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <deque>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <functional>
#include <source_location>
#include <span>

namespace tachyon
{
    class Buffer;
    class VisualColorStyle;
    struct VisualBoxStyle;
}

namespace tachyon::vulkan
{
    /// @addtogroup graphics_vulkan
    /// @{

    struct Style;


    /// Library internal: destroy functions that run later, newest first.
    struct DeletionQueue
    {
        /// The queued functions, oldest first.
        ::std::deque<::std::function<void()>> deletors;

        /// Queues a function for the next flush.
        void push_function(::std::function<void()>&& function)
        {
            deletors.push_back(function);
        }

        /// Runs and clears the queued functions.
        void flush()
        {
            // Reverse order: later resources may depend on earlier ones.
            for (auto it = deletors.rbegin(); it != deletors.rend(); it++)
            {
                (*it)();
            }

            deletors.clear();
        }
    };

    /// Timeline semaphores the GUI's frame submissions wait on (VulkanGraphicsContext::AddFrameWait).
    /// Every submission waits on all of them; the next frame drops those that were submitted.
    struct FrameWaits
    {
        /// Waits for `semaphore` to reach `value` before `stageMask`. Another wait on the same
        /// semaphore merges with it: the higher value, both stages. A null semaphore is ignored.
        void Add(VkSemaphore semaphore, uint64_t value, VkPipelineStageFlags2 stageMask)
        {
            if (semaphore == VK_NULL_HANDLE)
                return;
            for (size_t i = 0; i < waits.size(); ++i)
                if (waits[i].semaphore == semaphore)
                {
                    waits[i].value      = std::max(waits[i].value, value);
                    waits[i].stageMask |= stageMask;
                    // Submitted already: it's waited on again, by the next frame too.
                    if (i < submitted)
                        std::swap(waits[i], waits[--submitted]);
                    return;
                }
            waits.push_back({ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = semaphore,
                              .value = value, .stageMask = stageMask });
        }
        /// What a submission waits on now.
        std::span<const VkSemaphoreSubmitInfo> Waits() const { return waits; }
        /// A submission waited on all of them.
        void Submitted() { submitted = waits.size(); }
        /// A new frame: those submitted go, those added since stay.
        void NextFrame()
        {
            waits.erase(waits.begin(), waits.begin() + std::ptrdiff_t(submitted));
            submitted = 0;
        }
        /// All of them go (the device they were made on is going).
        void Clear() { waits.clear(); submitted = 0; }

    private:
        std::vector<VkSemaphoreSubmitInfo> waits;
        size_t submitted = 0;   ///< the first this many were submitted
    };

    /// The Vulkan device the GUI renders with: its own, or a host's (SetExternalDevice). It
    /// lives in VulkanGraphicsContext::deviceContext; set it up before GUIApplication::Initialize.
    class VulkanDeviceContext
    {
    public:
        /// Makes a VkSurfaceKHR for one of the window backend's windows with the given instance
        /// and allocation callbacks; VK_NULL_HANDLE on failure. The GUI destroys the surfaces.
        using SurfaceFactory = std::function<VkSurfaceKHR(VkInstance, const VkAllocationCallbacks*, tachyon::OSWindowHandle)>;
        /// How surfaces are made; set before initialization. SDLWindowBackend installs its own.
        void SetSurfaceFactory(SurfaceFactory factory) { m_surfaceFactory = std::move(factory); }

        /// Host memory functions for every Vulkan object, vk-bootstrap and VMA.
        /// Call before initialization.
        void SetAllocationHooks(const tachyon::AllocationHooks& hooks);
        /// What the vkCreate*/vkDestroy* calls pass; null when no hooks are set.
        const VkAllocationCallbacks* AllocationCallbacks() const { return m_hooks.IsSet() ? &m_allocationCallbacks : nullptr; }

        /// Diagnostic hook for the GPU test harness: the instance is built with this debug
        /// callback instead of the default printing messenger, and optionally with
        /// synchronization validation, so tests can fail on validation errors.
        struct InstanceDebugConfig
        {
            PFN_vkDebugUtilsMessengerCallbackEXT callback = nullptr;   ///< Gets the validation messages
            void*                                userData = nullptr;   ///< Passed to callback
            bool                                 enableSyncValidation = false;   ///< adds sync validation
        };
        /// Sets the instance debug config; call before initialization (no effect after).
        void SetInstanceDebugConfig(const InstanceDebugConfig& cfg) { m_instanceDebugConfig = cfg; }

        // ---- Host-provided device ----------------------------------------

        /// A host's Vulkan handles for SetExternalDevice, so the GUI renders on the host's device
        /// (an engine hosting it as its editor, say) and can share resources with it. The GUI
        /// never destroys what it didn't create. The device must satisfy GetDeviceRequirements(),
        /// the instance must enable the window backend's surface extensions (SDL:
        /// SDL_Vulkan_GetInstanceExtensions).
        struct ExternalDevice
        {
            /// Must enable the window backend's surface extensions.
            VkInstance       instance = VK_NULL_HANDLE;
            /// The GPU `device` was made on.
            VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
            /// Made with GetDeviceRequirements() enabled.
            VkDevice         device = VK_NULL_HANDLE;
            /// Every GUI submit and present goes here; it supports graphics and presenting to the
            /// GUI's windows.
            VkQueue          graphicsQueue = VK_NULL_HANDLE;
            /// graphicsQueue's family.
            uint32_t         graphicsQueueFamily = 0;
            /// Optional. When null the GUI creates (and owns) its own allocator.
            /// Must be created with VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT.
            VmaAllocator     allocator = VK_NULL_HANDLE;
            /// Optional. Vulkan queues need external synchronization: if the host
            /// submits to graphicsQueue from other threads it must pass the mutex
            /// it locks around its own vkQueueSubmit/vkQueuePresentKHR calls, and
            /// the GUI will lock the same mutex. When null the GUI uses a private
            /// mutex, which only protects against its own threads.
            std::mutex*      queueMutex = nullptr;
        };
        /// Renders on the host's device instead of making one; call before initialization.
        void SetExternalDevice(const ExternalDevice& external) { m_externalDevice = external; }
        /// Whether SetExternalDevice was called; a lost host device isn't recreated.
        [[nodiscard]] bool UsesExternalDevice() const noexcept { return m_externalDevice.has_value(); }

        /// What a device must provide for the GUI to run on it.
        struct DeviceRequirements
        {
            uint32_t                         apiVersion = VK_API_VERSION_1_3;   ///< The lowest Vulkan version
            VkPhysicalDeviceFeatures         features{};     ///< Vulkan 1.0 features to enable
            VkPhysicalDeviceVulkan11Features features11{};   ///< Vulkan 1.1 features to enable
            VkPhysicalDeviceVulkan12Features features12{};   ///< Vulkan 1.2 features to enable
            VkPhysicalDeviceVulkan13Features features13{};   ///< Vulkan 1.3 features to enable
            std::vector<const char*>         deviceExtensions;   ///< Device extensions to enable
        };
        /// What a host's device must enable for the GUI. The feature structs have no pNext
        /// chain; a host chains them itself.
        [[nodiscard]] static DeviceRequirements GetDeviceRequirements();
        /// Checks that a physical device can support GetDeviceRequirements().
        /// Appends a line per missing capability to `problems` when non-null.
        /// This cannot verify what the host actually enabled at device creation.
        [[nodiscard]] static bool CheckPhysicalDeviceSupport(VkPhysicalDevice physicalDevice, std::string* problems);

        /// Locks the queue for the duration of a submit or present. Every
        /// vkQueueSubmit/vkQueuePresentKHR on graphicsQueue must hold this.
        [[nodiscard]] std::unique_lock<std::mutex> LockQueue() const { return std::unique_lock<std::mutex>(*m_queueMutex); }

        /// Waits until the device is idle, holding the queue lock (vkDeviceWaitIdle needs every
        /// queue externally synchronized).
        void WaitIdle() const;

    public:
        /// An image nothing is uploaded to (undefined contents), for sampling only.
        tachyon::Image* CreateImage(tachyon::uint2 size, tachyon::ImageFormat format, tachyon::Sampler sampler, bool mipmapped = false) const;
        /// An image from `data` pixels (null: cleared). Doesn't wait for the GPU: the upload and
        /// mip generation are recorded at the start of the next window frame (RecordPendingUploads).
        tachyon::Image* CreateImage(const void* data, tachyon::uint2 size, tachyon::ImageFormat format, tachyon::Sampler sampler, bool mipmapped = false);
        /// Records the queued uploads into `cmd`, outside any rendering scope.
        void RecordPendingUploads(VkCommandBuffer cmd);

        /// Library internal: GPU-only storage for the packed `requests`, copied in from a staging
        /// buffer by the next RecordPendingUploads (see RHI::RenderCommandList::UploadResidentBuffers).
        tachyon::RHI::ResidentBufferId CreateResidentBuffer(std::span<const tachyon::RHI::UploadRequest> requests,
                                                            std::span<tachyon::DeviceAddress> addresses);
        /// Library internal: whether the buffer exists and has been copied to the GPU.
        tachyon::RHI::ResidentState GetResidentBufferState(tachyon::RHI::ResidentBufferId id) const;
        /// Library internal: frees the buffer now if its copy was never recorded, else when the
        /// frame deletion queue flushes.
        void ReleaseResidentBuffer(tachyon::RHI::ResidentBufferId id);
        /// Library internal: the resident buffers not released.
        size_t ResidentBufferCount() const { return m_residentBuffers.size(); }

        /// Library internal: copies into mapped memory, off the main thread. Everything a
        /// submit reads must be copied first: WaitForUploads before vkQueueSubmit.
        UploadWorker& Uploads() const { return m_uploads; }
        void WaitForUploads() const { m_uploads.Wait(); }
        /// Destroys the image's Vulkan objects now; doesn't delete the object. Only when no
        /// submitted frame can use it (debug builds check); DestroyImageAtEnd otherwise.
        void DestroyImage(tachyon::Image* img);
        /// After a device loss: frees the live images' Vulkan objects; the image objects stay.
        void ReleaseLiveImages();
        /// Makes the released images again on the new device, uploading the pixels they were
        /// made from.
        void RebuildLiveImages();
        /// Destroys the image and deletes the object when the scope's deletion queue flushes.
        void DestroyImageAtEnd(tachyon::Image* img, tachyon::DeviceScope scope,
                               const std::source_location& loc =
                               std::source_location::current());

        /// Library internal: the color styles in a GPU buffer for the shaders.
        tachyon::Buffer* UploadVisualColorStyles(::std::span<const tachyon::VisualColorStyle> colorStyles) const;
        /// Library internal: destroys the buffer's GPU objects now, when no submitted frame can
        /// use it.
        void DestroyVisualColorStyles(tachyon::Buffer* buffer);
        /// Library internal: destroys and deletes the buffer when the scope's queue flushes.
        void DestroyVisualColorStylesAtEnd(tachyon::Buffer* buffer, tachyon::DeviceScope scope,
                                           const std::source_location& loc =
                                           std::source_location::current());

        /// Library internal: the box styles in a GPU buffer for the shaders.
        tachyon::Buffer* UploadVisualBoxStyles(::std::span<const tachyon::VisualBoxStyle> styles) const;
        /// Library internal: destroys the buffer's GPU objects now, when no submitted frame can
        /// use it.
        void DestroyVisualBoxStyles(tachyon::Buffer* buffer);
        /// Library internal: destroys and deletes the buffer when the scope's queue flushes.
        void DestroyVisualBoxStylesAtEnd(tachyon::Buffer* buffer, tachyon::DeviceScope scope,
                                         const std::source_location& loc =
                                         std::source_location::current());

        /// Library internal: the timeline semaphores the frame's submissions wait on.
        FrameWaits frameWaits;

        /// Library internal: the deletion queue flushed at Destroy (DeviceScope::Application).
        /// What a frame may draw with goes through DestroyAfterGpu instead.
        DeletionQueue& GetDeletionQueue(tachyon::DeviceScope scope)
        {
            switch (scope)
            {
                case tachyon::DeviceScope::Application: return mainDeletionQueue;
                default: throw;
            }
        }

        /// The one way to free, while frames run, anything a frame may use (a buffer, an image,
        /// a staging copy the upload worker fills): `destroy` runs once the GPU has finished
        /// every frame submitted until the end of the frame it's dropped in, every window's.
        /// However many frames go by without a window drawn: the GPU's progress decides, not
        /// a count of frames. Any thread.
        void DestroyAfterGpu(std::function<void()> destroy);
        /// For DeviceScope::Frame, DestroyAfterGpu; for Application, at Destroy.
        void Defer(tachyon::DeviceScope scope, std::function<void()> destroy);

        /// Library internal, once a frame before it's built (all of the last frame's windows
        /// submitted): what was dropped since the last call waits for the last submission, and
        /// what the GPU has passed is destroyed.
        void AdvanceFrameAndFlushDeletions();

        /// Library internal: the signal every frame submission adds, the submit timeline at
        /// its next value. Call with the queue locked (LockQueue), right before the submit, so
        /// the values reach the queue in order.
        VkSemaphoreSubmitInfo NextSubmitSignal();

        /// True when the GPU has finished every frame submitted: a resource can go now.
        bool FramesDone() const;
        /// Debug builds: where an image dropped (DestroyImageAtEnd) and not destroyed yet was
        /// dropped, which no frame may record with; null for any other, and in release builds.
        const std::source_location* WasDropped(VkImageView view) const;

    public:
        std::string      shaderDirectory;  ///< Where to load the compiled shaders from; empty uses the built-in ones
        /// Library internal: the GUI's pipelines.
        GUIRenderContext renderContext;

        /// The instance; the host's with SetExternalDevice.
        VkInstance       instance;
        /// The device; the host's with SetExternalDevice.
        VkDevice         device;
        /// The allocator; the host's when it passed one.
        VmaAllocator     allocator;
        /// The GPU the device runs on.
        VkPhysicalDevice chosenGPU;

        /// Library internal: the immediate-submit fence.
        VkFence         immFence;
        /// Library internal.
        VkCommandBuffer immCommandBuffer;
        /// Library internal.
        VkCommandPool   immCommandPool;

        /// The queue every GUI submit and present goes to; the host's with SetExternalDevice.
        VkQueue         graphicsQueue;
        /// graphicsQueue's family.
        uint32_t        graphicsQueueFamily;

        /// Library internal: the layout of the descriptor set the shaders sample images from.
        VkDescriptorSetLayout singleImageDescriptorLayout;

        /// Image slots asked for (VulkanGraphicsContext::SetImageSlotCount); set before Initialize.
        uint32_t         requestedImageSlots = 4096;
        /// The image slots there are: the ones asked for, or fewer when the device holds
        /// fewer sampled images per shader stage. Set by Initialize.
        uint32_t         imageSlots          = 0;

        /// For Sampler::LinearRepeat.
        VkSampler defaultSamplerLinear;
        /// For Sampler::NearestRepeat.
        VkSampler defaultSamplerNearest;
        /// For Sampler::LinearClampToEdge.
        VkSampler defaultSamplerLinearClampToEdge;

        /// Flushed at Destroy (DeviceScope::Application).
        DeletionQueue mainDeletionQueue;
        /// A timeline every frame submission signals, one value further each.
        VkSemaphore submitTimeline = VK_NULL_HANDLE;
        /// Frames advanced so far.
        uint64_t frameNumber{ 0 };



        /// The default debug messenger, when the GUI made the instance with one.
        VkDebugUtilsMessengerEXT debug_messenger;
        /// Library internal: reused for the window contexts' command pools.
        VkCommandPoolCreateInfo commandPoolInfo;
        /// Library internal: reused for the window contexts' fences.
        VkFenceCreateInfo fenceCreateInfo;

        /// Whether the device is up (made or adopted).
        bool initialized{ false };
        tachyon::Error initError{};   ///< why the bring-up failed, when it did

        /// Library internal: finishes the bring-up that the first CreateSurface does; false when
        /// that failed (see initError).
        bool Initialize();
        void Flush();  ///< Waits for the GPU and flushes the frame deletion queues
        void Destroy(); ///< Waits for the GPU, flushes every deletion queue and destroys what the GUI made

        /// A surface for the window, through the surface factory. The first call also makes the
        /// device (or adopts the host's) on a GPU that can present to it. VK_NULL_HANDLE on failure.
        VkSurfaceKHR CreateSurface(tachyon::OSWindowHandle windowHandle);

    private:
        /// Creates the command pool, fence, descriptor layout and samplers, for owned
        /// and external devices alike. On failure destroys what it created, returns false.
        bool InitializeSharedResources();
        bool AdoptExternalDevice();

        SurfaceFactory m_surfaceFactory;
        tachyon::AllocationHooks  m_hooks;
        VkAllocationCallbacks m_allocationCallbacks{};
        InstanceDebugConfig m_instanceDebugConfig;
        std::optional<ExternalDevice> m_externalDevice;
        bool m_ownsInstance  = true;
        bool m_ownsDevice    = true;
        bool m_ownsAllocator = true;

        struct PendingUpload { Buffer staging; VkImage image; VkExtent3D extent; bool mipmapped; };
        /// Guards m_pendingUploads and m_liveImages: images are made on any thread.
        mutable std::mutex         m_imagesMutex;
        std::vector<PendingUpload> m_pendingUploads;

        mutable UploadWorker m_uploads;

        /// A resident buffer, and its staging copy until the copy is recorded.
        struct ResidentBuffer { Buffer buffer; Buffer staging; size_t size = 0; bool ready = false; };
        std::unordered_map<tachyon::RHI::ResidentBufferId, ResidentBuffer> m_residentBuffers;
        tachyon::RHI::ResidentBufferId m_nextResidentBuffer = 1;
        mutable std::unordered_set<VulkanImage*> m_liveImages;   ///< made by CreateImage, not yet destroyed

        std::mutex  m_ownedQueueMutex;
        std::mutex* m_queueMutex = &m_ownedQueueMutex;

        /// What frames dropped (DestroyAfterGpu), guarded by m_deletionMutex: since the last
        /// AdvanceFrameAndFlushDeletions, and before that, each batch with the submission it
        /// waits for, oldest first.
        struct DroppedAfter { uint64_t after = 0; DeletionQueue queue; };
        mutable std::mutex         m_deletionMutex;
        /// Debug builds: the views of the images dropped and not destroyed yet, and where they
        /// were dropped (WasDropped).
        std::unordered_map<VkImageView, std::source_location> m_droppedViews;
        void NoteDropped(VkImageView view, const std::source_location& where);
        void NoteDestroyed(VkImageView view);
        DeletionQueue              m_dropped;
        std::deque<DroppedAfter>   m_droppedAfter;
        /// The value the last frame submission signals; set under the queue lock.
        std::atomic<uint64_t>      m_submitValue{ 0 };
        /// Runs every dropped destroy now, the GPU being idle (Flush, Destroy, a device loss).
        void DestroyAllDropped();

    public:

        /// Library internal: a VMA buffer.
        Buffer CreateBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) const;
        /// Library internal: destroys a CreateBuffer buffer now. Only when no submitted frame can
        /// use it: from DestroyAfterGpu, with the GPU idle, or one never recorded
        /// (DestroyUnsubmittedBuffer). Debug builds check it.
        void DestroyBuffer(const Buffer& buffer) const;
        /// Library internal: destroys a buffer no command buffer was recorded with, now.
        void DestroyUnsubmittedBuffer(const Buffer& buffer) const;

        /// Library internal: a host-visible, persistently mapped storage buffer with a device
        /// address, filled by memcpy (no staging, no blocking submit). Empty, with `outAddress`
        /// 0, for size 0.
        Buffer CreateMappedStorageBuffer(const void* data, size_t size, VkDeviceAddress& outAddress) const;

        /// Library internal: a VMA image with its view.
        VulkanImage CreateImage(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, VkSampler sampler, bool mipmapped = false) const;
        /// Library internal: destroys the image and its view now; as DestroyBuffer, only when no
        /// submitted frame can use it.
        void DestroyImage(const VulkanImage& img);

        /// Library internal: the box styles in GPU buffers. Per-primitive uploads go through the RHI.
        VisualBoxStyleBuffers   UploadMesh(::std::span<const tachyon::VisualBoxStyle> visualBoxStyles) const;
        /// Library internal: the color styles in GPU buffers.
        VisualColorStyleBuffers UploadMesh(::std::span<const tachyon::VisualColorStyle> colorStyles) const;
        /// Library internal: destroys the style buffers now.
        void  DestroyMesh(const VisualBoxStyleBuffers& mesh) const;
        /// Library internal: destroys the style buffers now.
        void  DestroyMesh(const VisualColorStyleBuffers& mesh) const;


        /// Library internal: a shader module from SPIR-V words.
        VkShaderModule CreateShader(::std::span<uint32_t> spirvBuffer) const;
        /// Library internal.
        void DestroyShader(const VkShaderModule& shader) const;
    };
    /// @}
}