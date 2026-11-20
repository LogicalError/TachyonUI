#include <functional>
#include <chrono>
#include <utility>

#include <VkBootstrap.h>
#define VMA_DEBUG_LOG_FORMAT
#include <vk_mem_alloc.h>

#include <gui_core/gui_base_types.h>
#include <gui_core/diagnostics/gui_profiler.h>


#include <gui_core/graphics/vulkan/vk_types.h>
#include <gui_core/graphics/vulkan/vk_initializers.h>
#include <gui_core/graphics/vulkan/vk_image.h>
#include <gui_core/graphics/vulkan/vk_images.h>
#include <gui_core/graphics/vulkan/vk_descriptors.h>
#include <gui_core/graphics/vulkan/vk_device_context.h>
#include <gui_core/graphics/vulkan/vk_graphics_context.h>
#include <gui_core/graphics/vulkan/vk_window_context.h>
#include <gui_core/graphics/vulkan/vk_defaults.h>
#include <gui_core/windows/gui_layered_window.h>
#include <gui_core/graphics/gui_graphics_context.h>
#include <gui_core/diagnostics/gui_logging.h>

static constexpr tachyon::Diagnostics::LogCategory kLogVulkan = tachyon::Diagnostics::MakeLogCategory("vulkan");

using namespace tachyon::vulkan;



FrameData& VulkanWindowContext::GetCurrentFrame()
{
    return state.frames[frameNumber % FRAME_OVERLAP];
}

FrameData& VulkanWindowContext::GetLastFrame()
{
    return state.frames[(frameNumber + FRAME_OVERLAP - 1) % FRAME_OVERLAP];
}

void VulkanWindowContext::CreateDrawDepthImages(VulkanImage& drawImage, VulkanImage& depthImage, VkFormat colorFormat)
{
    VkExtent3D extent { windowExtent.width, windowExtent.height, 1 };

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    drawImage.imageFormat = colorFormat;
    drawImage.imageExtent = extent;
    VkImageCreateInfo drawInfo = tachyon::vulkan::image_create_info(colorFormat, kDrawImageUsage, extent);
    vmaCreateImage(deviceContext->allocator, &drawInfo, &allocInfo, &drawImage.image, &drawImage.allocation, nullptr);
    vmaSetAllocationName(deviceContext->allocator, drawImage.allocation, "Window::DrawImage::CreateImage");
    VkImageViewCreateInfo drawView = tachyon::vulkan::imageview_create_info(colorFormat, drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
    TACHYON_VK_CHECK(vkCreateImageView(deviceContext->device, &drawView, deviceContext->AllocationCallbacks(), &drawImage.imageView));

    depthImage.imageFormat = depthImageFormat;
    depthImage.imageExtent = extent;
    VkImageCreateInfo depthInfo =
        tachyon::vulkan::image_create_info(depthImageFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, extent);
    vmaCreateImage(deviceContext->allocator, &depthInfo, &allocInfo, &depthImage.image, &depthImage.allocation, nullptr);
    vmaSetAllocationName(deviceContext->allocator, depthImage.allocation, "Window::DepthImage::CreateImage");
    VkImageViewCreateInfo depthView = tachyon::vulkan::imageview_create_info(depthImageFormat, depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);
    TACHYON_VK_CHECK(vkCreateImageView(deviceContext->device, &depthView, deviceContext->AllocationCallbacks(), &depthImage.imageView));
}

void VulkanWindowContext::DestroyDrawDepthImages(VulkanImage& drawImage, VulkanImage& depthImage)
{
    vkDestroyImageView(deviceContext->device, drawImage.imageView, deviceContext->AllocationCallbacks());
    vmaDestroyImage(deviceContext->allocator, drawImage.image, drawImage.allocation);
    drawImage = {};

    vkDestroyImageView(deviceContext->device, depthImage.imageView, deviceContext->AllocationCallbacks());
    vmaDestroyImage(deviceContext->allocator, depthImage.image, depthImage.allocation);
    depthImage = {};
}

void VulkanWindowContext::CreateSwapchain()
{
    uint32_t width = windowExtent.width;
    uint32_t height = windowExtent.height;

    needsReadback = false;
    
    // Layered windows (Windows) and render-to-image windows have no surface and no swapchain.
    if (surface == VK_NULL_HANDLE)
    {
        needsReadback = !renderToImage;   // layered windows read back every frame
        
        // Extent is still used for size checks.
        swapchain.swapchainExtent.width = width;
        swapchain.swapchainExtent.height = height;
        
        // BGRA8 matches the readback buffer, so readback needs no conversion.
        VkFormat layeredWindowFormat = VK_FORMAT_B8G8R8A8_UNORM;
        
        for (auto& frame : state.frames)
            CreateDrawDepthImages(frame.drawImage, frame.depthImage, layeredWindowFormat);

        VkDeviceSize bufferSize = width * height * 4;
        
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        
        vmaCreateBuffer(deviceContext->allocator, &bufferInfo, &allocInfo,
                       &readbackBuffer.buffer, &readbackBuffer.allocation, nullptr);
        vmaSetAllocationName(deviceContext->allocator, readbackBuffer.allocation, "Window::ReadbackBuffer");
        
        cachedBitmap.resize(bufferSize);
        return;
    }
    
    vkb::SwapchainBuilder swapchainBuilder{ deviceContext->chosenGPU, deviceContext->device, surface };
    swapchainBuilder.set_allocation_callbacks(const_cast<VkAllocationCallbacks*>(deviceContext->AllocationCallbacks()));

    // Prefer alpha compositing modes that allow transparent windows.
    VkSurfaceCapabilitiesKHR surfaceCaps{};
    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(deviceContext->chosenGPU, surface, &surfaceCaps) == VK_SUCCESS)
    {
        const VkCompositeAlphaFlagsKHR supported = surfaceCaps.supportedCompositeAlpha;

        if ((supported & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) != 0)
            compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        else if ((supported & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) != 0)
            compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
        else if ((supported & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) != 0)
            compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        else if ((supported & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0)
            compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

        // Opaque-only surfaces composite transparent windows via CPU readback.
        const bool isTransparent = (windowFlags & tachyon::WindowFlags::Transparent) != tachyon::WindowFlags::None;
        needsReadback = isTransparent && (compositeAlpha == VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR);
    }

    swapchain.swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

    // Prefer MAILBOX: no tearing, and the newest frame replaces the waiting one,
    // so present doesn't block. Present then doesn't pace the loop to the refresh;
    // GUIApplication::Run sleeps the rest of the period instead (PaceFrame). It
    // needs triple buffering. Otherwise
    // FIFO, double-buffered (the only guaranteed mode, blocking to the refresh)
    // rather than vk-bootstrap's larger default.
    VkPresentModeKHR presentMode   = VK_PRESENT_MODE_FIFO_KHR;
    uint32_t         desiredImages = vkb::SwapchainBuilder::DOUBLE_BUFFERING;
    {
        uint32_t modeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(deviceContext->chosenGPU, surface, &modeCount, nullptr);
        std::vector<VkPresentModeKHR> modes(modeCount);
        if (modeCount > 0)
            vkGetPhysicalDeviceSurfacePresentModesKHR(deviceContext->chosenGPU, surface, &modeCount, modes.data());
        for (VkPresentModeKHR m : modes)
            if (m == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                presentMode   = VK_PRESENT_MODE_MAILBOX_KHR;
                desiredImages = vkb::SwapchainBuilder::TRIPLE_BUFFERING;
                break;
            }
    }

    auto built = swapchainBuilder
        .set_desired_format(VkSurfaceFormatKHR{ .format = swapchain.swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
        .set_composite_alpha_flags(compositeAlpha)
        .set_desired_present_mode(presentMode)
        .set_desired_min_image_count(desiredImages)
        .set_desired_extent(width, height)
        .set_old_swapchain(swapchain.swapchain)
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build();
    // No swapchain (the surface lost, the window going away between the size check and
    // here): nothing drawn until the next resize tries again.
    auto images = built ? built.value().get_images() : decltype(built.value().get_images()){ std::error_code{} };
    auto views  = built && images ? built.value().get_image_views() : decltype(built.value().get_image_views()){ std::error_code{} };
    if (!built || !images || !views)
    {
        TACHYON_LOG_ERROR(kLogVulkan, "VulkanWindowContext: the swapchain couldn't be made");
        if (built)
        {
            if (views) built.value().destroy_image_views(views.value());
            vkb::destroy_swapchain(built.value());
        }
        swapchain.swapchain = VK_NULL_HANDLE;
        freeze_rendering    = true;
        return;
    }
    vkb::Swapchain vkbSwapchain = built.value();

    swapchain.presentMode = vkbSwapchain.present_mode;

    swapchain.swapchainExtent = vkbSwapchain.extent;
    swapchain.swapchain = vkbSwapchain.swapchain;
    swapchain.swapchainImages = images.value();
    swapchain.swapchainImageViews = views.value();

    const VkSemaphoreCreateInfo semaphoreCreateInfo = tachyon::vulkan::semaphore_create_info();
    swapchain.presentSemaphores.resize(swapchain.swapchainImages.size());
    for (VkSemaphore& semaphore : swapchain.presentSemaphores)
        TACHYON_VK_CHECK(vkCreateSemaphore(deviceContext->device, &semaphoreCreateInfo, deviceContext->AllocationCallbacks(), &semaphore));

    // Per-slot draw + depth images so the FRAME_OVERLAP in-flight frames don't
    // share them (frame N's blit-read must not race frame N+1's transition-write).
    for (auto& frame : state.frames)
        CreateDrawDepthImages(frame.drawImage, frame.depthImage, drawImageFormat);

    if (needsReadback)
    {
        VkDeviceSize bufferSize = windowExtent.width * windowExtent.height * 4; // BGRA
        
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        
        vmaCreateBuffer(deviceContext->allocator, &bufferInfo, &allocInfo,
                       &readbackBuffer.buffer, &readbackBuffer.allocation, nullptr);
        vmaSetAllocationName(deviceContext->allocator, readbackBuffer.allocation, "Window::ReadbackBuffer");
        
        cachedBitmap.resize(bufferSize);
    }
}

void VulkanWindowContext::DestroySwapchain()
{
    for (size_t i = 0; i < swapchain.swapchainImageViews.size(); i++)
    {
        vkDestroyImageView(deviceContext->device, swapchain.swapchainImageViews[i], deviceContext->AllocationCallbacks());
        swapchain.swapchainImageViews[i] = VK_NULL_HANDLE;
    }
    swapchain.swapchainImageViews.clear();
    swapchain.swapchainImages.clear();

    for (VkSemaphore semaphore : swapchain.presentSemaphores)
        vkDestroySemaphore(deviceContext->device, semaphore, deviceContext->AllocationCallbacks());
    swapchain.presentSemaphores.clear();

    vkDestroySwapchainKHR(deviceContext->device, swapchain.swapchain, deviceContext->AllocationCallbacks());
    swapchain.swapchain = VK_NULL_HANDLE;

    for (auto& frame : state.frames)
        DestroyDrawDepthImages(frame.drawImage, frame.depthImage);

    if (readbackBuffer.buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(deviceContext->allocator, readbackBuffer.buffer, readbackBuffer.allocation);
        readbackBuffer.buffer = VK_NULL_HANDLE;
        readbackBuffer.allocation = VK_NULL_HANDLE;
    }
    cachedBitmap.clear();
}

void VulkanWindowContext::ResizeSwapchain(tachyon::int2 size)
{
    WaitForFence(deviceContext->device, deviceContext->immFence);

    for (auto& frame : state.frames)
    {
        WaitForFence(deviceContext->device, frame.renderFence);
        frame.flushQueue.flush();
    }

    // No fence covers a present's semaphore wait; drain the queue so the old
    // swapchain's present semaphores can be destroyed below.
    {
        auto queueLock = deviceContext->LockQueue();
        TACHYON_VK_CHECK(vkQueueWaitIdle(deviceContext->graphicsQueue));
    }

    VkSwapchainKHR oldSwapchain = swapchain.swapchain;
    std::vector<VkImageView> oldSwapchainImageViews = std::move(swapchain.swapchainImageViews);
    for (VkSemaphore semaphore : swapchain.presentSemaphores)
        vkDestroySemaphore(deviceContext->device, semaphore, deviceContext->AllocationCallbacks());
    swapchain.presentSemaphores.clear();
    std::array<VulkanImage, FRAME_OVERLAP> oldDrawImages;
    std::array<VulkanImage, FRAME_OVERLAP> oldDepthImages;
    for (unsigned int i = 0; i < FRAME_OVERLAP; ++i)
    {
        oldDrawImages[i]  = state.frames[i].drawImage;
        oldDepthImages[i] = state.frames[i].depthImage;
    }
    AllocatedBuffer oldReadbackBuffer = readbackBuffer;

    if (size.x <= 0 || size.y <= 0)
    {
        // Minimize: don't recreate the swapchain, but still queue the old
        // resources for deletion below. Null the live handles (copied into the
        // old* locals above) so a later DestroySwapchain doesn't double-free.
        freeze_rendering = true;
        swapchain.swapchain = VK_NULL_HANDLE;
        for (auto& frame : state.frames)
        {
            frame.drawImage  = {};
            frame.depthImage = {};
        }
        readbackBuffer   = {};
    }
    else
    {
        freeze_rendering = false;
        windowExtent.width = size.x;
        windowExtent.height = size.y;

        readbackBuffer = {};   // the old one goes below; CreateSwapchain makes one where it reads back every frame
        CreateSwapchain();
    }

    GetCurrentFrame().flushQueue.push_function([
        device = deviceContext->device,
        callbacks = deviceContext->AllocationCallbacks(),
        allocator = deviceContext->allocator,
        oldSwapchain,
        oldSwapchainImageViews = std::move(oldSwapchainImageViews),
        oldDrawImages,
        oldDepthImages,
        oldReadbackBuffer]()
        {
            for (auto imageView : oldSwapchainImageViews)
            {
                vkDestroyImageView(device, imageView, callbacks);
            }

            for (const auto& img : oldDrawImages)
            {
                if (img.imageView != VK_NULL_HANDLE)
                    vkDestroyImageView(device, img.imageView, callbacks);
                if (img.image != VK_NULL_HANDLE)
                    vmaDestroyImage(allocator, img.image, img.allocation);
            }
            for (const auto& img : oldDepthImages)
            {
                if (img.imageView != VK_NULL_HANDLE)
                    vkDestroyImageView(device, img.imageView, callbacks);
                if (img.image != VK_NULL_HANDLE)
                    vmaDestroyImage(allocator, img.image, img.allocation);
            }

            if (oldReadbackBuffer.buffer != VK_NULL_HANDLE)
            {
                vmaDestroyBuffer(allocator, oldReadbackBuffer.buffer, oldReadbackBuffer.allocation);
            }

            if (oldSwapchain != VK_NULL_HANDLE)
            {
                vkDestroySwapchainKHR(device, oldSwapchain, callbacks);
            }
        });
}

void VulkanWindowContext::FlushWindowContext()
{
    deviceContext->WaitIdle();
    WaitForFence(deviceContext->device, deviceContext->immFence);

    for (auto& frame : state.frames)
    {
        WaitForFence(deviceContext->device, frame.renderFence);
        frame.flushQueue.flush();
    }
}


VkCommandBuffer VulkanWindowContext::BeginWindowFrame(FrameData& currentFrame, VulkanGraphicsContext* graphics)
{
    WaitForFence(deviceContext->device, currentFrame.renderFence);
    currentFrame.flushQueue.flush();
    if (currentFrame.timestampsWritten)
    {
        // That frame is done (its fence), so its timestamps are there.
        uint64_t ticks[2] = {};
        if (vkGetQueryPoolResults(deviceContext->device, currentFrame.timestamps, 0, 2, sizeof(ticks), ticks, sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
            lastGpuFrameSeconds = double(ticks[1] - ticks[0]) * double(timestampPeriodNs) * 1e-9;
        currentFrame.timestampsWritten = false;
    }
    currentFrame.primitiveUploadOffset = 0;

    if (swapchain.swapchain != VK_NULL_HANDLE)
    {
        // The swapchain image is acquired late, in EndWindowFrame; drawExtent
        // only needs the swapchain extent.
        drawExtent.height = std::min(swapchain.swapchainExtent.height, currentFrame.drawImage.imageExtent.height);
        drawExtent.width  = std::min(swapchain.swapchainExtent.width,  currentFrame.drawImage.imageExtent.width);
    }
    else
    {
        // The draw image only grows, so it can be larger than the window: draw (and read
        // back, and present) the size asked for. A layered window takes the size it's
        // presented at, and a larger one would undo every resize down.
        const VkExtent3D image = currentFrame.drawImage.imageExtent;
        drawExtent.width  = requestedExtent.width  > 0 ? std::min(requestedExtent.width,  image.width)  : image.width;
        drawExtent.height = requestedExtent.height > 0 ? std::min(requestedExtent.height, image.height) : image.height;
    }

    TACHYON_VK_CHECK(vkResetFences(deviceContext->device, 1, &currentFrame.renderFence));
    TACHYON_VK_CHECK(vkResetCommandBuffer(currentFrame.mainCommandBuffer, 0));

    state.commandBuffer = currentFrame.mainCommandBuffer;

    VkCommandBufferBeginInfo cmdBeginInfo = tachyon::vulkan::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    TACHYON_VK_CHECK(vkBeginCommandBuffer(state.commandBuffer, &cmdBeginInfo));

    if (currentFrame.timestamps != VK_NULL_HANDLE)
    {
        vkCmdResetQueryPool(state.commandBuffer, currentFrame.timestamps, 0, 2);
        vkCmdWriteTimestamp2(state.commandBuffer, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, currentFrame.timestamps, 0);
    }

    // Queued texture uploads (e.g. glyph atlas pages) run ahead of this frame's drawing.
    deviceContext->RecordPendingUploads(state.commandBuffer);
    // So do the app's scenes this window shows (a 3D viewport), outside its rendering.
    if (graphics != nullptr)
        graphics->RecordScenes(state.commandBuffer, id);

    // A color attachment, in the layout drivers can keep compressed (DCC), not GENERAL.
    tachyon::vulkan::transition_image(state.commandBuffer, currentFrame.drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    currentFrame.drawImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    tachyon::vulkan::transition_image(state.commandBuffer, currentFrame.depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    // Layered and render-to-image windows clear to transparent, others to opaque black.
    float clearAlpha = (needsReadback || renderToImage) ? 0.0f : 1.0f;
    VkClearValue clearValue = { .color = { { 0.0f, 0.0f, 0.0f, clearAlpha } } };

    VkRenderingAttachmentInfo colorAttachment = tachyon::vulkan::attachment_info(currentFrame.drawImage.imageView, &clearValue, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo depthAttachment = tachyon::vulkan::depth_attachment_info(currentFrame.depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkRenderingInfo renderInfo = tachyon::vulkan::rendering_info(drawExtent, &colorAttachment, &depthAttachment);

    vkCmdBeginRendering(state.commandBuffer, &renderInfo);


    return state.commandBuffer;
}


void VulkanWindowContext::EndWindowFrame(FrameData& currentFrame)
{
    // What the frame reads from mapped memory was copied on the upload worker while its
    // commands were recorded; the submit below needs it done. Part of building the frame.
    deviceContext->WaitForUploads();

    // Build time = CPU work since the previous present returned, captured
    // before this frame blocks on its swapchain image and present.
    if (havePresentEnd)
    {
        double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - lastPresentEnd).count();
        lastBuildValid = (dt > 0.0 && dt < 0.25);
        if (lastBuildValid)
            lastBuildSeconds = dt;
    }
    vkCmdEndRendering(state.commandBuffer);
    if (currentFrame.timestamps != VK_NULL_HANDLE)
    {
        vkCmdWriteTimestamp2(state.commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, currentFrame.timestamps, 1);
        currentFrame.timestampsWritten = true;
    }

    if (swapchain.swapchain != VK_NULL_HANDLE)
    {
        // Late acquire, after the offscreen frame is recorded: lower display latency. Under
        // FIFO some drivers (AMD's on Windows) hold the vsync wait here rather than in
        // present: timed as a wait, and counted with present's (PresentWindowFrame).
        VkResult acq;
        {
            TACHYON_PROFILE_SCOPE_WAIT("Acquire (vsync wait)");
            const auto acquireStart = std::chrono::steady_clock::now();
            acq = vkAcquireNextImageKHR(deviceContext->device, swapchain.swapchain, 1000000000, currentFrame.swapchainSemaphore, nullptr, &swapchain.swapchainImageIndex);
            acquireSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - acquireStart).count();
        }
        if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR)
        {
            // No image (out of date, timed out, or an error such as a lost surface): still
            // submit the offscreen work so renderFence (already reset by BeginWindowFrame)
            // signals for the next frame's wait. Nothing may wait on the acquire semaphore,
            // which nothing signals.
            if (acq == VK_ERROR_OUT_OF_DATE_KHR || acq == VK_ERROR_SURFACE_LOST_KHR)
                resize_requested = true;
            else if (acq != VK_TIMEOUT && acq != VK_NOT_READY)
                TACHYON_VK_CHECK(acq);
            TACHYON_VK_CHECK(vkEndCommandBuffer(state.commandBuffer));
            VkCommandBufferSubmitInfo abortCmd = tachyon::vulkan::command_buffer_submit_info(state.commandBuffer);
            VkSemaphoreSubmitInfo abortSignal{};
            VkSubmitInfo2 abortSubmit = tachyon::vulkan::submit_info(&abortCmd, &abortSignal, nullptr);
            const std::span<const VkSemaphoreSubmitInfo> hostWaits = deviceContext->frameWaits.Waits();
            abortSubmit.waitSemaphoreInfoCount = uint32_t(hostWaits.size());
            abortSubmit.pWaitSemaphoreInfos    = hostWaits.data();
            {
                // The timeline's next value under the queue lock: the values reach it in order.
                auto queueLock = deviceContext->LockQueue();
                abortSignal = deviceContext->NextSubmitSignal();
                TACHYON_VK_CHECK(vkQueueSubmit2(deviceContext->graphicsQueue, 1, &abortSubmit, currentFrame.renderFence));
            }
            deviceContext->frameWaits.Submitted();
            frameNumber++;
            return;   // nothing to present
        }
        if (acq == VK_SUBOPTIMAL_KHR)
            resize_requested = true;

        tachyon::vulkan::transition_image(state.commandBuffer, currentFrame.drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        currentFrame.drawImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        // After the acquire semaphore's wait (at TRANSFER, below), not racing it.
        tachyon::vulkan::transition_image(state.commandBuffer, swapchain.swapchainImages[swapchain.swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                             VK_PIPELINE_STAGE_2_TRANSFER_BIT);

        tachyon::vulkan::copy_image_to_image(state.commandBuffer, currentFrame.drawImage.image, swapchain.swapchainImages[swapchain.swapchainImageIndex], drawExtent, swapchain.swapchainExtent);

        tachyon::vulkan::transition_image(state.commandBuffer, swapchain.swapchainImages[swapchain.swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }
    else if (renderToImage)
    {
        // Ready to read back, and the source of the copy into the host's image.
        tachyon::vulkan::transition_image(state.commandBuffer, currentFrame.drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        currentFrame.drawImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        if (targetImage != VK_NULL_HANDLE)
        {
            // The host reads the image in its fragment shaders after this submission,
            // and moves it out of SHADER_READ_ONLY only after those reads.
            TransitionTarget(state.commandBuffer, targetWritten ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                             VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
            tachyon::vulkan::copy_image_to_image(state.commandBuffer, currentFrame.drawImage.image, targetImage, drawExtent, targetExtent);
            TransitionTarget(state.commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                             VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            targetWritten = true;
        }
    }
    // A window read back every frame (a layered one) copies its frame out here, in the frame's
    // own submit: PresentWindowFrame reads it once the frame's fence has passed.
    const bool copiedOut = needsReadback && readbackBuffer.buffer != VK_NULL_HANDLE;
    if (copiedOut)
        RecordReadbackCopy(state.commandBuffer, currentFrame);

    TACHYON_VK_CHECK(vkEndCommandBuffer(state.commandBuffer));

    // Wait on the acquire semaphore and the host's (AddFrameWait), signal the image's present
    // semaphore.
    VkCommandBufferSubmitInfo cmdinfo = tachyon::vulkan::command_buffer_submit_info(state.commandBuffer);

    std::vector<VkSemaphoreSubmitInfo> waits(deviceContext->frameWaits.Waits().begin(), deviceContext->frameWaits.Waits().end());
    // The submit timeline's next value (taken under the queue lock, below), and the image's
    // present semaphore.
    std::array<VkSemaphoreSubmitInfo, 2> signals{};
    VkSubmitInfo2 submit = tachyon::vulkan::submit_info(&cmdinfo, &signals[0], nullptr);
    if (swapchain.swapchain != VK_NULL_HANDLE)
    {
        // Wait on the acquire semaphore at TRANSFER: the swapchain image's first use this
        // frame is the transition and blit above, whose barrier starts at TRANSFER too, so
        // the frame's drawing doesn't wait for the presentation engine. (At
        // COLOR_ATTACHMENT_OUTPUT the transition would race the acquire.)
        waits.push_back(tachyon::vulkan::semaphore_submit_info(VK_PIPELINE_STAGE_2_TRANSFER_BIT, currentFrame.swapchainSemaphore));
        // Signaled after everything, the blit into the image and its move to PRESENT_SRC
        // included (ALL_GRAPHICS has no transfer stage).
        signals[1] = tachyon::vulkan::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                                   swapchain.presentSemaphores[swapchain.swapchainImageIndex]);
        submit.signalSemaphoreInfoCount = 2;
        submit.pSignalSemaphoreInfos    = signals.data();
    }
    submit.waitSemaphoreInfoCount = uint32_t(waits.size());
    submit.pWaitSemaphoreInfos    = waits.data();

    {
        // The timeline's next value under the queue lock: the values reach it in order.
        auto queueLock = deviceContext->LockQueue();
        signals[0] = deviceContext->NextSubmitSignal();
        if (const VkResult submitted = vkQueueSubmit2(deviceContext->graphicsQueue, 1, &submit, currentFrame.renderFence); submitted != VK_SUCCESS)
        {
            // The fence was reset and won't signal: the device is remade rather than waited on.
            ::tachyon::vulkan::OnVulkanError(submitted, "vkQueueSubmit2");
            ::tachyon::vulkan::SetDeviceLost(true);
        }
    }
    deviceContext->frameWaits.Submitted();


    // Nothing to present or read back from a submit that failed (the device is being remade).
    const bool submitted = !::tachyon::vulkan::IsDeviceLost();
    presentPending = submitted && swapchain.swapchain != VK_NULL_HANDLE;
    readbackPending = submitted && copiedOut ? &currentFrame : nullptr;
    frameNumber++;
}

std::optional<double> VulkanWindowContext::PresentWindowFrame()
{
    std::optional<double> presentSeconds;
    // Present waits on the image's present semaphore (rendering finished).
    if (presentPending)
    {
        presentPending = false;
        VkPresentInfoKHR presentInfo = tachyon::vulkan::present_info();

        presentInfo.pSwapchains = &swapchain.swapchain;
        presentInfo.swapchainCount = 1;

        presentInfo.pWaitSemaphores = &swapchain.presentSemaphores[swapchain.swapchainImageIndex];
        presentInfo.waitSemaphoreCount = 1;

        presentInfo.pImageIndices = &swapchain.swapchainImageIndex;

        VkResult presentResult;
        {
            // Under FIFO present usually blocks until a vsync frees an image (some
            // drivers stall in the acquire or frame fence instead); MAILBOX doesn't
            // block. Timed as a wait, not work.
            TACHYON_PROFILE_SCOPE_WAIT("Present (vsync wait)");
            auto queueLock = deviceContext->LockQueue();
            const auto presentStart = std::chrono::steady_clock::now();   // not the lock's wait
            presentResult  =vkQueuePresentKHR(deviceContext->graphicsQueue, &presentInfo);
            // The frame's whole swapchain wait: the driver holds the vsync in one of the two.
            presentSeconds = acquireSeconds + std::chrono::duration<double>(std::chrono::steady_clock::now() - presentStart).count();
            lastSwapchainWaitSeconds = *presentSeconds;
        }
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
            resize_requested = true;
        else
            TACHYON_VK_CHECK(presentResult);
    }

    // The frame copied itself out: its pixels are there once it's done.
    if (FrameData* frame = std::exchange(readbackPending, nullptr))
    {
        WaitForFence(deviceContext->device, frame->renderFence);
        ReadBackBuffer();
    }

    // Layered windows don't redraw on SDL expose events, so update them explicitly.
    if ((windowFlags & tachyon::WindowFlags::Transparent) != tachyon::WindowFlags::None)
        UpdateTransparentWindow();

    // Build time is measured from here to the next frame's submit.
    lastPresentEnd = std::chrono::steady_clock::now();
    havePresentEnd = true;
    return presentSeconds;
}

void VulkanWindowContext::PerformReadback(FrameData& frame)
{
    VkCommandBuffer cmd = deviceContext->immCommandBuffer;
    TACHYON_VK_CHECK(vkResetFences(deviceContext->device, 1, &deviceContext->immFence));
    TACHYON_VK_CHECK(vkResetCommandBuffer(cmd, 0));

    VkCommandBufferBeginInfo beginInfo = tachyon::vulkan::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    TACHYON_VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
    RecordReadbackCopy(cmd, frame);
    TACHYON_VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdInfo = tachyon::vulkan::command_buffer_submit_info(cmd);
    VkSubmitInfo2 submit = tachyon::vulkan::submit_info(&cmdInfo, nullptr, nullptr);
    {
        auto queueLock = deviceContext->LockQueue();
        TACHYON_VK_CHECK(vkQueueSubmit2(deviceContext->graphicsQueue, 1, &submit, deviceContext->immFence));
    }
    WaitForFence(deviceContext->device, deviceContext->immFence);
    ++immediateReadbacks;
    ReadBackBuffer();
}

void VulkanWindowContext::RecordReadbackCopy(VkCommandBuffer cmd, FrameData& frame)
{
    const VulkanImage& drawImage = frame.drawImage;
    // A layered window's frame is still a color attachment when it's copied out; copied out already
    // (a swapchain's or the host's image, or read back before), it stays as it is.
    if (frame.drawImageLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        tachyon::vulkan::transition_image(cmd, drawImage.image, frame.drawImageLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        frame.drawImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {drawExtent.width, drawExtent.height, 1};

    vkCmdCopyImageToBuffer(cmd, drawImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          readbackBuffer.buffer, 1, &region);

    // Make the copy visible to the host read below (the fence alone doesn't).
    VkMemoryBarrier2 toHost { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
    toHost.srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT;
    toHost.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toHost.dstStageMask  = VK_PIPELINE_STAGE_2_HOST_BIT;
    toHost.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    VkDependencyInfo dependency { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers    = &toHost;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

void VulkanWindowContext::ReadBackBuffer()
{
    // Map and copy to CPU cache; invalidate first in case the memory is cached but not coherent.
    TACHYON_VK_CHECK(vmaInvalidateAllocation(deviceContext->allocator, readbackBuffer.allocation, 0, VK_WHOLE_SIZE));
    void* mapped;
    vmaMapMemory(deviceContext->allocator, readbackBuffer.allocation, &mapped);
    memcpy(cachedBitmap.data(), mapped, cachedBitmap.size());
    vmaUnmapMemory(deviceContext->allocator, readbackBuffer.allocation);
}

bool VulkanWindowContext::ReadPixels(std::span<uint8_t> bgra)
{
    if (frameNumber == 0 || freeze_rendering)
        return false;
    FrameData& last = GetLastFrame();
    if (bgra.size() < size_t(drawExtent.width) * drawExtent.height * 4)
        return false;
    // A window whose frames copy themselves out has the last one's pixels, or will once it's
    // done.
    if (needsReadback && readbackBuffer.buffer != VK_NULL_HANDLE && !cachedBitmap.empty())
    {
        if (FrameData* frame = std::exchange(readbackPending, nullptr))
        {
            WaitForFence(deviceContext->device, frame->renderFence);
            ReadBackBuffer();
        }
        std::memcpy(bgra.data(), cachedBitmap.data(), size_t(drawExtent.width) * drawExtent.height * 4);
        return true;
    }
    // A window with a swapchain reads back only now and then (a popup's fade): its buffer
    // is made the first time.
    if (readbackBuffer.buffer == VK_NULL_HANDLE)
    {
        const VkDeviceSize bufferSize = VkDeviceSize(windowExtent.width) * windowExtent.height * 4;
        VkBufferCreateInfo bufferInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size  = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        if (vmaCreateBuffer(deviceContext->allocator, &bufferInfo, &allocInfo,
                            &readbackBuffer.buffer, &readbackBuffer.allocation, nullptr) != VK_SUCCESS)
            return false;
        vmaSetAllocationName(deviceContext->allocator, readbackBuffer.allocation, "Window::ReadbackBuffer");
        cachedBitmap.resize(bufferSize);
    }
    WaitForFence(deviceContext->device, last.renderFence);
    PerformReadback(last);
    std::memcpy(bgra.data(), cachedBitmap.data(), size_t(drawExtent.width) * drawExtent.height * 4);
    return true;
}

void VulkanWindowContext::SetImageTarget(VkImage image, VkExtent2D extent)
{
    targetImage   = image;
    targetExtent  = extent;
    targetWritten = false;
}

void VulkanWindowContext::TransitionTarget(VkCommandBuffer cmd, VkImageLayout from, VkImageLayout to,
                                           VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                           VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
{
    VkImageMemoryBarrier2 barrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    barrier.srcStageMask        = srcStage;
    barrier.srcAccessMask       = srcAccess;
    barrier.dstStageMask        = dstStage;
    barrier.dstAccessMask       = dstAccess;
    barrier.oldLayout           = from;
    barrier.newLayout           = to;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = targetImage;
    barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VkDependencyInfo dependency{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers    = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

void VulkanWindowContext::UpdateTransparentWindow()
{
    if (needsReadback && !cachedBitmap.empty())
        tachyon::ShowLayeredPixels(nativeHandle, cachedBitmap, { drawExtent.width, drawExtent.height });
}

void VulkanWindowContext::RenderWindowFrame(tachyon::GraphicsContext* graphicsContext, tachyon::IntRect rect, bool resizing)
{
    RecordFrame(graphicsContext, rect, resizing);
    PresentWindowFrame();
}

void VulkanWindowContext::RecordWindowFrame(tachyon::GraphicsContext* graphicsContext, tachyon::IntRect rect)
{
    RecordFrame(graphicsContext, rect, false);
}

void VulkanWindowContext::RecordFrame(tachyon::GraphicsContext* graphicsContext, tachyon::IntRect rect, bool resizing)
{
    // A frame recorded and not presented yet (a live-resize redraw in between) goes first,
    // or its acquired image is never given back.
    if (presentPending || readbackPending)
        PresentWindowFrame();
    if (onGui && graphicsContext != nullptr)
    {
        uint32_t requestedWidth  = rect.size.x > 0 ? static_cast<uint32_t>(rect.size.x) : 0u;
        uint32_t requestedHeight = rect.size.y > 0 ? static_cast<uint32_t>(rect.size.y) : 0u;
        requestedExtent = { requestedWidth, requestedHeight };

        // A swapchain window made smaller: remade at the new size, once per size asked for
        // (the surface may settle on another extent than asked, which mustn't loop).
        const bool shrunk = swapchain.swapchain != VK_NULL_HANDLE && requestedWidth > 0 && requestedHeight > 0
            && (requestedWidth != swapchainRequest.width || requestedHeight != swapchainRequest.height)
            && (requestedWidth < swapchain.swapchainExtent.width || requestedHeight < swapchain.swapchainExtent.height);
        // All FRAME_OVERLAP draw images share one extent, so any slot's is representative.
        if (shrunk ||
            requestedWidth > swapchain.swapchainExtent.width ||
            requestedHeight > swapchain.swapchainExtent.height ||
            requestedWidth > GetCurrentFrame().drawImage.imageExtent.width ||
            requestedHeight > GetCurrentFrame().drawImage.imageExtent.height)
        {
            resize_requested = true;
        }

		tachyon::vulkan::VulkanGraphicsContext* vulkanGraphicsContext = dynamic_cast<tachyon::vulkan::VulkanGraphicsContext*>(graphicsContext);

        if (resizing)
        {
            bool hasResizeTarget = vulkanGraphicsContext->StartResizing(id);

            // Resize must update layout immediately, even without a resize target.
            vulkanGraphicsContext->Reset();

            onGui();

            // onGui() may have changed VisualColorStyles; re-upload before rendering.
            vulkanGraphicsContext->RePrepareFrame();

            if (hasResizeTarget)
            {
                vulkanGraphicsContext->StopResizing();
            }
        }

        // Resize must also run while frozen: restoring a minimized window is
        // the operation that recreates its swapchain and unfreezes rendering.
        if (resize_requested)
        {
            TACHYON_PROFILE_SCOPE("ResizeSwapchain");
            ResizeSwapchain(rect.size);
            swapchainRequest = requestedExtent;
            resize_requested = false;
        }

        // A zero-size resize (minimize) freezes rendering and clears the live
        // draw/depth images. Do not record this frame against cleared handles.
        if (!freeze_rendering)
        {
            TACHYON_PROFILE_SCOPE("Window render");

            FrameData* currentFrame;
            {
                TACHYON_PROFILE_SCOPE("BeginWindowFrame");
                currentFrame = &GetCurrentFrame();
            }
            // BeginWindowFrame sets drawExtent.
            VkCommandBuffer cmd;
            {
                TACHYON_PROFILE_SCOPE("BeginWindowFrame");
                cmd = BeginWindowFrame(*currentFrame, vulkanGraphicsContext);
            }
            // A zero-extent frame still begins the command buffer, so it must still be ended.
            if (cmd != nullptr)
            {
                if (drawExtent.width != 0 && drawExtent.height != 0)
                {
                    TACHYON_PROFILE_SCOPE("Record commands");
                    vulkanGraphicsContext->RenderWindowFrame(cmd, *currentFrame, id, { drawExtent.width, drawExtent.height });
                }
                {
                    TACHYON_PROFILE_SCOPE("EndWindowFrame");
                    EndWindowFrame(*currentFrame);
                }
                if (lastBuildValid)
                    vulkanGraphicsContext->SetFrameBuildSeconds(static_cast<float>(lastBuildSeconds));
            }
        }
    }
}

VulkanWindowContext* VulkanWindowContext::Create(tachyon::ID id, void* nativeHandle, tachyon::OSWindowHandle osHandle, tachyon::int2 size, tachyon::WindowFlags windowFlags, VulkanDeviceContext* deviceContext, tachyon::OnGuiFunc onGUI)
{
    const bool renderToImage = (windowFlags & tachyon::WindowFlags::RenderToImage) != tachyon::WindowFlags::None;
    if (osHandle == nullptr && !renderToImage)
        return nullptr;

    auto context = new tachyon::vulkan::VulkanWindowContext();
    context->id = id;
    context->deviceContext = deviceContext;
    context->onGui = std::move(onGUI);
    context->windowFlags = windowFlags;
    context->osHandle = osHandle;
    context->nativeHandle = nativeHandle;
    context->renderToImage = renderToImage;

    // On Windows, layered (WS_EX_LAYERED) windows get no surface and present via
    // UpdateLayeredWindow; elsewhere transparent windows use surface alpha compositing.
    bool shouldCreateSurface = !context->renderToImage;
    if (tachyon::IsLayeredWindow(nativeHandle))
    {
        shouldCreateSurface = false;
        context->needsReadback = true;
    }
    
    if (shouldCreateSurface)
    {
        context->surface = context->deviceContext->CreateSurface(context->osHandle);
        // No surface for a window that needs one: no context, rather than a layered-window
        // stand-in that reads back every frame and shows nothing.
        if (context->surface == VK_NULL_HANDLE)
        {
            TACHYON_LOG_ERROR(kLogVulkan, "VulkanWindowContext: the window's surface couldn't be made");
            delete context;
            return nullptr;
        }
    } else
    {
        context->surface = VK_NULL_HANDLE;
    }
    
    context->windowExtent = VkExtent2D(size.x, size.y);
    context->drawExtent = VkExtent2D(size.x, size.y);
        
    context->CreateSwapchain();


    // Commands

    for (uint32_t i = 0; i < tachyon::vulkan::FRAME_OVERLAP; i++)
    {
        TACHYON_VK_CHECK(vkCreateCommandPool(context->deviceContext->device, &context->deviceContext->commandPoolInfo, context->deviceContext->AllocationCallbacks(), &context->state.frames[i].commandPool));

        VkCommandBufferAllocateInfo cmdAllocInfo = tachyon::vulkan::command_buffer_allocate_info(context->state.frames[i].commandPool, 1);

        TACHYON_VK_CHECK(vkAllocateCommandBuffers(context->deviceContext->device, &cmdAllocInfo, &context->state.frames[i].mainCommandBuffer));

        context->state.frames[i].deletionQueue.push_function([context, i]()
                                                    {
                                                        vkDestroyCommandPool(context->deviceContext->device, context->state.frames[i].commandPool, context->deviceContext->AllocationCallbacks());
                                                    });
    }

    // GPU frame timing, where the graphics queue can time.
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(context->deviceContext->chosenGPU, &properties);
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(context->deviceContext->chosenGPU, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(context->deviceContext->chosenGPU, &familyCount, families.data());
        const uint32_t family = context->deviceContext->graphicsQueueFamily;
        if (family < familyCount && families[family].timestampValidBits != 0 && properties.limits.timestampPeriod > 0.0f)
            context->timestampPeriodNs = properties.limits.timestampPeriod;
    }

    // Per frame data

    for (uint32_t i = 0; i < FRAME_OVERLAP; i++)
    {
        auto& frame = context->state.frames[i];
        if (context->timestampPeriodNs > 0.0f)
        {
            const VkQueryPoolCreateInfo poolInfo{ .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 2 };
            TACHYON_VK_CHECK(vkCreateQueryPool(context->deviceContext->device, &poolInfo, context->deviceContext->AllocationCallbacks(), &frame.timestamps));
            // The handles only: [=] would copy the whole FrameData, deletion queue and all.
            frame.deletionQueue.push_function([context, pool = frame.timestamps]()
                                              {
                                                  vkDestroyQueryPool(context->deviceContext->device, pool, context->deviceContext->AllocationCallbacks());
                                              });
        }
        // The frame slot's one set of image descriptors, as big as the image set may grow.
        std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes =
        {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, context->deviceContext->imageSlots },
        };

        frame.frameDescriptors = DescriptorAllocatorGrowable{};
        frame.frameDescriptors.init(context->deviceContext->device, 1, frame_sizes, context->deviceContext->AllocationCallbacks());
        frame.imageDescriptors        = VK_NULL_HANDLE;
        frame.imageDescriptorsVersion = 0;

        TACHYON_VK_CHECK(vkCreateFence(context->deviceContext->device, &context->deviceContext->fenceCreateInfo, context->deviceContext->AllocationCallbacks(), &frame.renderFence));

        VkSemaphoreCreateInfo semaphoreCreateInfo = tachyon::vulkan::semaphore_create_info();

        TACHYON_VK_CHECK(vkCreateSemaphore(context->deviceContext->device, &semaphoreCreateInfo, context->deviceContext->AllocationCallbacks(), &frame.swapchainSemaphore));

        frame.deletionQueue.push_function([context, i, semaphore = frame.swapchainSemaphore, fence = frame.renderFence]()
                                          {
                                              vkDestroySemaphore(context->deviceContext->device, semaphore, context->deviceContext->AllocationCallbacks());

                                              vkDestroyFence(context->deviceContext->device, fence, context->deviceContext->AllocationCallbacks());

                                              context->state.frames[i].frameDescriptors.clear_pools(context->deviceContext->device);
                                              context->state.frames[i].frameDescriptors.destroy_pools(context->deviceContext->device);
                                          });
    }
    return context;
}

void VulkanWindowContext::Destroy()
{
    // Frames still in flight use this window's fences, semaphores and command
    // pools; no fence covers a present's semaphore wait, so drain the queue too.
    for (auto& frame : state.frames)
        WaitForFence(deviceContext->device, frame.renderFence);
    {
        auto queueLock = deviceContext->LockQueue();
        TACHYON_VK_CHECK(vkQueueWaitIdle(deviceContext->graphicsQueue));
    }

    for (auto& frame : state.frames)
    {
        frame.flushQueue.flush();
        if (frame.primitiveUploadBuffer.buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(deviceContext->allocator,
                             frame.primitiveUploadBuffer.buffer,
                             frame.primitiveUploadBuffer.allocation);
            frame.primitiveUploadBuffer = {};
            frame.primitiveUploadAddress = 0;
            frame.primitiveUploadCapacity = 0;
            frame.primitiveUploadOffset = 0;
        }
        frame.deletionQueue.flush();
    }

    DestroySwapchain();

    vkDestroySurfaceKHR(deviceContext->instance, surface, deviceContext->AllocationCallbacks());
}
