#include <gui_core/graphics/sdlgpu/sdlgpu_device.h>

#include <gui_core/diagnostics/gui_logging.h>

#include <algorithm>
#include <bit>
#include <cstring>

static constexpr tachyon::Diagnostics::LogCategory kLogSDLGPU = tachyon::Diagnostics::MakeLogCategory("sdlgpu");

namespace tachyon::sdlgpu
{
    // ---- Buffers a DeviceAddress names ---------------------------------------------------

    uint32_t SDLGPUGraphicsContext::Impl::AddBuffer(SDL_GPUBuffer* buffer)
    {
        if (!freeBuffers.empty())
        {
            const uint32_t entry = freeBuffers.back();
            freeBuffers.pop_back();
            buffers[entry] = buffer;
            return entry;
        }
        buffers.push_back(buffer);
        return uint32_t(buffers.size() - 1);
    }

    void SDLGPUGraphicsContext::Impl::RemoveBuffer(uint32_t entry)
    {
        if (entry == 0 || entry >= buffers.size())
            return;
        buffers[entry] = nullptr;
        freeBuffers.push_back(entry);
    }

    SDL_GPUBuffer* SDLGPUGraphicsContext::Impl::CreateBuffer(SDL_GPUBufferUsageFlags usage, const void* data, uint32_t bytes)
    {
        const uint32_t size = std::max(bytes, 4u);
        const SDL_GPUBufferCreateInfo info{ usage, size, 0 };
        SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(device, &info);
        const SDL_GPUTransferBufferCreateInfo stagingInfo{ SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, size, 0 };
        SDL_GPUTransferBuffer* staging = buffer != nullptr ? SDL_CreateGPUTransferBuffer(device, &stagingInfo) : nullptr;
        void* mapped = staging != nullptr ? SDL_MapGPUTransferBuffer(device, staging, false) : nullptr;
        if (mapped == nullptr)
        {
            TACHYON_LOG_ERROR(kLogSDLGPU, "a buffer of {} bytes failed ({})", bytes, SDL_GetError());
            if (staging != nullptr)
                SDL_ReleaseGPUTransferBuffer(device, staging);
            if (buffer != nullptr)
                SDL_ReleaseGPUBuffer(device, buffer);
            return nullptr;
        }
        std::memset(mapped, 0, size);
        if (data != nullptr && bytes > 0)
            std::memcpy(mapped, data, bytes);
        SDL_UnmapGPUTransferBuffer(device, staging);
        std::lock_guard lock(copyMutex);
        bufferCopies.push_back({ buffer, staging, size });
        return buffer;
    }

    void SDLGPUGraphicsContext::Impl::ReleaseBuffer(SDL_GPUBuffer*& buffer)
    {
        if (buffer == nullptr)
            return;
        {
            std::lock_guard lock(copyMutex);
            std::erase_if(bufferCopies, [&](const BufferCopy& copy) {
                if (copy.buffer != buffer)
                    return false;
                SDL_ReleaseGPUTransferBuffer(device, copy.staging);
                return true;
            });
        }
        SDL_ReleaseGPUBuffer(device, buffer);
        buffer = nullptr;
    }

    void SDLGPUGraphicsContext::Impl::RecordCopies(SDL_GPUCopyPass* pass)
    {
        std::vector<TextureCopy>  textures;
        std::vector<BufferCopy>   copied;
        std::vector<SDLGPUImage*> released;
        {
            std::lock_guard lock(copyMutex);
            textures.swap(textureCopies);
            copied.swap(bufferCopies);
            released.swap(releasedImages);
        }
        for (const TextureCopy& copy : textures)
        {
            // A band of rows goes over those rows alone; SDL orders it after the draws before
            // that read the texture.
            const uint32_t rows = copy.rows != 0 ? copy.rows : copy.image->size.y;
            const SDL_GPUTextureTransferInfo from{ copy.staging, 0, copy.image->size.x, rows };
            SDL_GPUTextureRegion to{};
            to.texture = copy.image->texture;
            to.y       = copy.firstRow;
            to.w       = copy.image->size.x;
            to.h       = rows;
            to.d       = 1;
            SDL_UploadToGPUTexture(pass, &from, &to, false);
            SDL_ReleaseGPUTransferBuffer(device, copy.staging);   // freed once the copy is done
        }
        for (const BufferCopy& copy : copied)
        {
            const SDL_GPUTransferBufferLocation from{ copy.staging, 0 };
            const SDL_GPUBufferRegion to{ copy.buffer, 0, copy.bytes };
            SDL_UploadToGPUBuffer(pass, &from, &to, false);
            SDL_ReleaseGPUTransferBuffer(device, copy.staging);
        }
        // The resident buffers copied now: draws from here on read them. Each one waiting had its
        // copy among those taken, as both are made on this thread, and one let go of is gone.
        for (const RHI::ResidentBufferId id : pendingResidents)
            if (const auto it = residents.find(id); it != residents.end())
                it->second.ready = true;
        pendingResidents.clear();
        // Let go of after their last copy was recorded: SDL frees each once the GPU is done.
        for (SDLGPUImage* image : released)
        {
            SDL_ReleaseGPUTexture(device, image->texture);
            delete image;
        }
    }

    // ---- Resident buffers ----------------------------------------------------------------

    RHI::ResidentBufferId SDLGPUGraphicsContext::Impl::CreateResident(std::span<const RHI::UploadRequest> requests,
                                                                      std::span<DeviceAddress> addresses)
    {
        std::fill(addresses.begin(), addresses.end(), DeviceAddress{ 0 });
        size_t end = 0;
        for (const RHI::UploadRequest& request : requests)
            if (request.bytes > 0)
            {
                const size_t alignment = std::max<size_t>(request.alignment, 4);
                end = (end + alignment - 1) / alignment * alignment + request.bytes;
            }
        if (end == 0)
            return 0;
        std::vector<std::byte> bytes(end);
        std::vector<uint32_t>  offsets(requests.size(), 0);
        size_t offset = 0;
        for (size_t i = 0; i < requests.size(); ++i)
        {
            if (requests[i].bytes == 0)
                continue;
            const size_t alignment = std::max<size_t>(requests[i].alignment, 4);
            offset = (offset + alignment - 1) / alignment * alignment;
            std::memcpy(bytes.data() + offset, requests[i].data, requests[i].bytes);
            offsets[i] = uint32_t(offset);
            offset += requests[i].bytes;
        }
        Resident resident;
        resident.buffer = CreateBuffer(SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, bytes.data(), uint32_t(end));
        if (resident.buffer == nullptr)
            return 0;
        resident.entry = AddBuffer(resident.buffer);
        for (size_t i = 0; i < requests.size(); ++i)
            if (requests[i].bytes > 0)
                addresses[i] = MakeAddress(resident.entry, offsets[i]);
        const RHI::ResidentBufferId id = nextResident++;
        residents.emplace(id, resident);
        pendingResidents.push_back(id);
        return id;
    }

    RHI::ResidentState SDLGPUGraphicsContext::Impl::ResidentState(RHI::ResidentBufferId id) const
    {
        const auto it = residents.find(id);
        if (it == residents.end())
            return RHI::ResidentState::Gone;
        return it->second.ready ? RHI::ResidentState::Ready : RHI::ResidentState::Pending;
    }

    void SDLGPUGraphicsContext::Impl::ReleaseResident(RHI::ResidentBufferId id)
    {
        const auto it = residents.find(id);
        if (it == residents.end())
            return;
        RemoveBuffer(it->second.entry);
        ReleaseBuffer(it->second.buffer);   // SDL frees it once the frames that read it are done
        residents.erase(it);
    }

    // ---- What every draw uses ------------------------------------------------------------

    SDL_GPUSampler* SDLGPUGraphicsContext::Impl::SamplerFor(tachyon::Sampler sampler) const
    {
        switch (sampler)
        {
        case tachyon::Sampler::LinearRepeat:      return samplers[1];
        case tachyon::Sampler::LinearClampToEdge: return samplers[2];
        default:                                  return samplers[0];
        }
    }

    bool SDLGPUGraphicsContext::Impl::EnsureQuadIndices(uint32_t quads)
    {
        if (quads <= quadCapacity)
            return true;
        const uint32_t capacity = std::bit_ceil(std::max(quads, 4096u));
        std::vector<uint32_t> indices(size_t(capacity) * 6);
        for (uint32_t p = 0; p < capacity; ++p)
        {
            const uint32_t base = p * 4;
            const uint32_t quad[6] = { base, base + 1, base + 3, base, base + 3, base + 2 };
            std::memcpy(indices.data() + size_t(p) * 6, quad, sizeof(quad));
        }
        SDL_GPUBuffer* buffer = CreateBuffer(SDL_GPU_BUFFERUSAGE_INDEX, indices.data(), uint32_t(indices.size() * sizeof(uint32_t)));
        if (buffer == nullptr)
            return false;
        ReleaseBuffer(quadIndices);
        quadIndices  = buffer;
        quadCapacity = capacity;
        return true;
    }

    // ---- The image set -------------------------------------------------------------------

    void SDLGPUImageSet::Clear()
    {
        m_images.clear();
        m_free.clear();
    }

    ImageIndex SDLGPUImageSet::Allocate()
    {
        if (!m_free.empty())
        {
            const uint32_t slot = m_free.back();
            m_free.pop_back();
            return ImageIndex(slot + 1);
        }
        if (m_images.size() >= m_capacity)
        {
            TACHYON_LOG_ERROR(kLogSDLGPU, "the image set is full ({} images)", m_capacity);
            return ImageIndex::Invalid;
        }
        m_images.push_back(nullptr);
        return ImageIndex(uint32_t(m_images.size()));
    }

    ImageIndex SDLGPUImageSet::Add(Image* image)
    {
        const ImageIndex index = Allocate();
        SetImage(index, static_cast<SDLGPUImage*>(image));
        return index;
    }

    ImageIndex SDLGPUImageSet::ReserveIndex()
    {
        return Allocate();
    }

    void SDLGPUImageSet::SetImage(ImageIndex index, SDLGPUImage* image)
    {
        const uint32_t slot = uint32_t(index) - 1;
        if (index != ImageIndex::Invalid && slot < m_images.size())
            m_images[slot] = image;
    }

    void SDLGPUImageSet::Remove(ImageIndex index)
    {
        const uint32_t slot = uint32_t(index) - 1;
        if (index == ImageIndex::Invalid || slot >= m_images.size())
            return;
        m_images[slot] = nullptr;
        m_free.push_back(slot);
    }

    // ---- Scenes ---------------------------------------------------------------------------

    SDLGPUImage* SDLGPUGraphicsContext::Impl::CreateTargetImage(uint2 size, SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage,
                                                                Sampler sampler)
    {
        SDL_GPUTextureCreateInfo info{};
        info.type                 = SDL_GPU_TEXTURETYPE_2D;
        info.format               = format;
        info.usage                = usage;
        info.width                = size.x;
        info.height               = size.y;
        info.layer_count_or_depth = 1;
        info.num_levels           = 1;
        SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &info);
        if (texture == nullptr)
        {
            TACHYON_LOG_ERROR(kLogSDLGPU, "a target of {}x{} failed ({})", size.x, size.y, SDL_GetError());
            return nullptr;
        }
        auto* image    = new SDLGPUImage();
        image->texture = texture;
        image->sampler = sampler;
        image->size    = size;
        std::lock_guard lock(copyMutex);
        liveImages.insert(image);
        return image;
    }

    void SDLGPUGraphicsContext::Impl::RecordScenes(SDL_GPUCommandBuffer* cmd, ID window)
    {
        for (const SceneRequest& request : sceneRequests)
        {
            if (request.window != window)
                continue;
            const auto it = scenes.find(request.key);
            if (it == scenes.end() || it->second.color == nullptr)
                continue;
            const Scene& scene = it->second;

            // Cleared in a pass of its own, so the app's passes (and copies) come after it.
            SDL_GPUColorTargetInfo color{};
            color.texture     = scene.color->texture;
            color.clear_color = SDL_FColor{ request.clear.r, request.clear.g, request.clear.b, request.clear.a };
            color.load_op     = SDL_GPU_LOADOP_CLEAR;
            color.store_op    = SDL_GPU_STOREOP_STORE;
            SDL_GPUDepthStencilTargetInfo depth{};
            depth.texture          = scene.depth->texture;
            depth.clear_depth      = 1.0f;
            depth.load_op          = SDL_GPU_LOADOP_CLEAR;
            depth.store_op         = SDL_GPU_STOREOP_STORE;
            depth.stencil_load_op  = SDL_GPU_LOADOP_DONT_CARE;
            depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
            SDL_EndGPURenderPass(SDL_BeginGPURenderPass(cmd, &color, 1, &depth));
            if (!request.record)
                continue;

            SDLGPUSceneRender state;
            state.device        = device;
            state.commandBuffer = cmd;
            state.colorTexture  = scene.color->texture;
            state.depthTexture  = scene.depth->texture;
            state.colorFormat   = kSceneColorFormat;
            state.depthFormat   = kDepthFormat;
            state.extent        = scene.extent;
            state.colorTarget   = color;
            state.colorTarget.load_op = SDL_GPU_LOADOP_LOAD;
            state.depthTarget   = depth;
            state.depthTarget.load_op = SDL_GPU_LOADOP_LOAD;
            request.record(ScenePayload(state));
        }
    }
}
