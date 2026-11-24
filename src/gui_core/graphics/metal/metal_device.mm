#include <gui_core/graphics/metal/metal_device.h>

#include <gui_core/diagnostics/gui_logging.h>

#include <algorithm>
#include <bit>
#include <cstring>

static constexpr tachyon::Diagnostics::LogCategory kLogMetal = tachyon::Diagnostics::MakeLogCategory("metal");

namespace tachyon::metal
{
    uint32_t SamplerSlot(Sampler sampler)
    {
        switch (sampler)
        {
        case Sampler::LinearRepeat:      return 1;
        case Sampler::LinearClampToEdge: return 2;
        default:                         return 0;   // NearestRepeat
        }
    }

    // ---- Commits ---------------------------------------------------------------------------

    uint64_t MetalGraphicsContext::Impl::Commit(id<MTLCommandBuffer> commandBuffer)
    {
        const uint64_t value = ++submitted;
        [commandBuffer commit];
        std::lock_guard lock(inFlightMutex);
        inFlight.emplace_back(value, commandBuffer);
        return value;
    }

    uint64_t MetalGraphicsContext::Impl::Completed()
    {
        std::lock_guard lock(inFlightMutex);
        // The queue runs its command buffers in order: the done ones are at the front.
        while (!inFlight.empty())
        {
            id<MTLCommandBuffer> commandBuffer = inFlight.front().second;
            const MTLCommandBufferStatus status = commandBuffer.status;
            if (status != MTLCommandBufferStatusCompleted && status != MTLCommandBufferStatusError)
                break;
            if (status == MTLCommandBufferStatusError)
            {
                NSError* error = commandBuffer.error;
                const bool lost = error != nil && (error.code == MTLCommandBufferErrorDeviceRemoved ||
                                                   error.code == MTLCommandBufferErrorTimeout ||
                                                   error.code == MTLCommandBufferErrorPageFault);
                const std::string reason = error != nil ? std::string(error.localizedDescription.UTF8String) : std::string("unknown");
                if (lost && !removed.exchange(true))
                    TACHYON_LOG_ERROR(kLogMetal, "a frame failed, and the device with it: {}", reason);
                else if (!lost)
                    TACHYON_LOG_ERROR(kLogMetal, "a frame failed: {}", reason);
            }
            completed = inFlight.front().first;
            inFlight.pop_front();
        }
        if (inFlight.empty())
            completed = submitted;
        return completed;
    }

    void MetalGraphicsContext::Impl::Wait(uint64_t value)
    {
        if (value == 0 || Completed() >= value)
            return;
        // The command buffers up to the value, the last of which completes after the others.
        id<MTLCommandBuffer> last = nil;
        {
            std::lock_guard lock(inFlightMutex);
            for (const auto& [counted, commandBuffer] : inFlight)
            {
                if (counted > value)
                    break;
                last = commandBuffer;
            }
        }
        if (last != nil)
            [last waitUntilCompleted];
        Completed();
    }

    void MetalGraphicsContext::Impl::WaitIdle()
    {
        Wait(submitted);
    }

    // ---- Memory ----------------------------------------------------------------------------

    Buffer MetalGraphicsContext::Impl::CreateBuffer(size_t size)
    {
        Buffer buffer;
        buffer.buffer = [device newBufferWithLength:std::max<size_t>(size, 16)
                                            options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
        if (buffer.buffer == nil)
        {
            TACHYON_LOG_ERROR(kLogMetal, "a buffer of {} bytes failed", size);
            return {};
        }
        buffer.size   = buffer.buffer.length;
        buffer.handle = nextBufferHandle++;
        buffers.emplace(buffer.handle, buffer.buffer);
        return buffer;
    }

    void MetalGraphicsContext::Impl::ReleaseBuffer(Buffer& buffer)
    {
        if (buffer.handle != 0)
            buffers.erase(buffer.handle);
        buffer = {};
    }

    std::pair<id<MTLBuffer>, NSUInteger> MetalGraphicsContext::Impl::Resolve(DeviceAddress address) const
    {
        if (address == 0)
            return { nil, 0 };
        const auto it = buffers.find(uint32_t(address >> 32));
        if (it == buffers.end())
            return { nil, 0 };
        return { it->second, NSUInteger(address & 0xFFFFFFFFu) };
    }

    void MetalGraphicsContext::Impl::DestroyAfterGpu(std::unique_ptr<MetalImage> image, uint64_t afterValue)
    {
        if (!image)
            return;
        std::lock_guard lock(garbageMutex);
        garbage.push_back({ afterValue, std::move(image), nil });
    }

    void MetalGraphicsContext::Impl::DestroyAfterGpu(id<MTLResource> resource, uint64_t afterValue)
    {
        if (resource == nil)
            return;
        std::lock_guard lock(garbageMutex);
        garbage.push_back({ afterValue, nullptr, resource });
    }

    void MetalGraphicsContext::Impl::FlushDeletions()
    {
        const uint64_t done = Completed();
        std::lock_guard lock(garbageMutex);
        std::erase_if(garbage, [done](const Garbage& g) { return g.afterValue <= done; });
    }

    // ---- Uploads ---------------------------------------------------------------------------

    void MetalGraphicsContext::Impl::RecordPendingUploads(id<MTLCommandBuffer> commandBuffer)
    {
        // Taken in one go; the two vectors trade places, each keeping its storage.
        std::vector<ImageUpload>& images = uploadScratch;
        {
            std::lock_guard lock(uploadMutex);
            images.swap(imageUploads);
        }
        if (images.empty())
            return;
        // In the frame's command buffer, ahead of its render passes: Metal orders the copies
        // before the draws that sample the textures, and after those already committed.
        id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
        blit.label = @"Image uploads";
        for (const ImageUpload& upload : images)
        {
            [blit copyFromBuffer:upload.staging
                    sourceOffset:0
               sourceBytesPerRow:upload.rowPitch
             sourceBytesPerImage:0
                      sourceSize:MTLSizeMake(upload.image->size.x, upload.rowCount, 1)
                       toTexture:upload.image->texture
                destinationSlice:0
                destinationLevel:0
               destinationOrigin:MTLOriginMake(0, upload.firstRow, 0)];
        }
        [blit endEncoding];
        // The command buffer holds the staging buffers until it's done.
        images.clear();
    }

    // ---- Resident buffers ------------------------------------------------------------------

    RHI::ResidentBufferId MetalGraphicsContext::Impl::CreateResidentBuffer(std::span<const RHI::UploadRequest> requests,
                                                                          std::span<DeviceAddress> addresses)
    {
        std::fill(addresses.begin(), addresses.end(), DeviceAddress{ 0 });
        size_t end = 0;
        for (const RHI::UploadRequest& request : requests)
            if (request.bytes > 0)
            {
                const size_t alignment = std::max<size_t>(request.alignment, 1);
                end = (end + alignment - 1) / alignment * alignment + request.bytes;
            }
        if (end == 0)
            return 0;

        Buffer buffer = CreateBuffer(end);
        if (!buffer)
            return 0;
        size_t offset = 0;
        for (size_t i = 0; i < requests.size(); ++i)
        {
            const RHI::UploadRequest& request = requests[i];
            if (request.bytes == 0)
                continue;
            const size_t alignment = std::max<size_t>(request.alignment, 1);
            offset = (offset + alignment - 1) / alignment * alignment;
            std::memcpy(static_cast<uint8_t*>(buffer.Mapped()) + offset, request.data, request.bytes);
            addresses[i] = buffer.Address(offset);
            offset += request.bytes;
        }
        const RHI::ResidentBufferId id = nextResident++;
        residents.emplace(id, std::move(buffer));
        return id;
    }

    RHI::ResidentState MetalGraphicsContext::Impl::ResidentState(RHI::ResidentBufferId id) const
    {
        return residents.contains(id) ? RHI::ResidentState::Ready : RHI::ResidentState::Gone;
    }

    void MetalGraphicsContext::Impl::ReleaseResident(RHI::ResidentBufferId id)
    {
        const auto it = residents.find(id);
        if (it == residents.end())
            return;
        // The command buffers that drew with it hold the buffer until they're done.
        ReleaseBuffer(it->second);
        residents.erase(it);
    }

    bool MetalGraphicsContext::Impl::EnsureQuadIndices(uint32_t quads)
    {
        if (quads <= quadCapacity)
            return true;
        const uint32_t capacity = std::bit_ceil(std::max(quads, 4096u));
        const size_t   bytes    = size_t(capacity) * 6 * sizeof(uint32_t);
        // A new buffer: draws already recorded keep the old one (their command buffer holds it).
        id<MTLBuffer> buffer = [device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
        if (buffer == nil)
            return false;
        auto* indices = static_cast<uint32_t*>(buffer.contents);
        for (uint32_t p = 0; p < capacity; ++p)
        {
            const uint32_t base = p * 4;
            const uint32_t quad[6] = { base, base + 1, base + 3, base, base + 3, base + 2 };
            std::memcpy(indices + size_t(p) * 6, quad, sizeof(quad));
        }
        quadIndices  = buffer;
        quadCapacity = capacity;
        return true;
    }

    // ---- Scenes ----------------------------------------------------------------------------

    MetalImage* MetalGraphicsContext::Impl::CreateTargetImage(uint2 size, MTLPixelFormat format, MTLTextureUsage usage, Sampler sampler)
    {
        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                                                        width:size.x
                                                                                       height:size.y
                                                                                    mipmapped:NO];
        desc.usage       = usage;
        desc.storageMode = MTLStorageModePrivate;
        auto image = std::make_unique<MetalImage>();
        image->texture = [device newTextureWithDescriptor:desc];
        if (image->texture == nil)
        {
            TACHYON_LOG_ERROR(kLogMetal, "a target of {}x{} failed", size.x, size.y);
            return nullptr;
        }
        image->format  = format;
        image->sampler = sampler;
        image->size    = size;
        std::lock_guard lock(uploadMutex);
        liveImages.insert(image.get());
        return image.release();
    }

    void MetalGraphicsContext::Impl::RecordScenes(id<MTLCommandBuffer> commandBuffer, ID window)
    {
        for (const SceneRequest& request : sceneRequests)
        {
            if (request.window != window)
                continue;
            const auto it = scenes.find(request.key);
            if (it == scenes.end() || it->second.color == nullptr)
                continue;
            const Scene& scene = it->second;

            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture     = scene.color->texture;
            pass.colorAttachments[0].loadAction  = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.colorAttachments[0].clearColor  = MTLClearColorMake(request.clear.r, request.clear.g, request.clear.b, request.clear.a);
            pass.depthAttachment.texture     = scene.depth->texture;
            pass.depthAttachment.loadAction  = MTLLoadActionClear;
            pass.depthAttachment.storeAction = MTLStoreActionDontCare;
            pass.depthAttachment.clearDepth  = 1.0;
            id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];
            encoder.label = @"Scene";
            [encoder setViewport:MTLViewport{ 0.0, 0.0, double(scene.extent.x), double(scene.extent.y), 0.0, 1.0 }];
            if (request.record)
            {
                MetalSceneRender state;
                state.commandBuffer = (__bridge void*)commandBuffer;
                state.encoder       = (__bridge void*)encoder;
                state.colorTexture  = (__bridge void*)scene.color->texture;
                state.depthTexture  = (__bridge void*)scene.depth->texture;
                state.colorFormat   = uint32_t(kSceneColorFormat);
                state.depthFormat   = uint32_t(kDepthFormat);
                state.extent        = scene.extent;
                request.record(ScenePayload(state));
            }
            [encoder endEncoding];
        }
    }

    // ---- The image set ---------------------------------------------------------------------

    void MetalImageSet::Clear()
    {
        m_images.clear();
        m_samplers.clear();
        m_free.clear();
    }

    ImageIndex MetalImageSet::Allocate()
    {
        uint32_t slot;
        if (!m_free.empty())
        {
            slot = m_free.back();
            m_free.pop_back();
        }
        else
        {
            if (m_images.size() >= m_capacity)
            {
                TACHYON_LOG_ERROR(kLogMetal, "the image set is full ({} images)", m_capacity);
                return ImageIndex::Invalid;
            }
            slot = uint32_t(m_images.size());
            m_images.push_back(nullptr);
            m_samplers.push_back(0);
        }
        return ImageIndex(slot + 1);
    }

    ImageIndex MetalImageSet::Add(Image* image)
    {
        const ImageIndex index = Allocate();
        if (index != ImageIndex::Invalid)
            SetImage(index, static_cast<MetalImage*>(image));
        return index;
    }

    ImageIndex MetalImageSet::ReserveIndex()
    {
        return Allocate();
    }

    void MetalImageSet::SetImage(ImageIndex index, MetalImage* image)
    {
        const uint32_t slot = uint32_t(index) - 1;
        if (index == ImageIndex::Invalid || slot >= m_images.size())
            return;
        m_images[slot]   = image;
        m_samplers[slot] = image != nullptr ? SamplerSlot(image->sampler) : 0;
    }

    void MetalImageSet::Remove(ImageIndex index)
    {
        const uint32_t slot = uint32_t(index) - 1;
        if (index == ImageIndex::Invalid || slot >= m_images.size())
            return;
        m_images[slot]   = nullptr;
        m_samplers[slot] = 0;
        m_free.push_back(slot);
    }
}
