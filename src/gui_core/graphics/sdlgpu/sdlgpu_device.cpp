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
        std::vector<TextureCopy> textures;
        std::vector<BufferCopy>  copied;
        {
            std::lock_guard lock(copyMutex);
            textures.swap(textureCopies);
            copied.swap(bufferCopies);
        }
        for (const TextureCopy& copy : textures)
        {
            const SDL_GPUTextureTransferInfo from{ copy.staging, 0, copy.image->size.x, copy.image->size.y };
            SDL_GPUTextureRegion to{};
            to.texture = copy.image->texture;
            to.w       = copy.image->size.x;
            to.h       = copy.image->size.y;
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
        // The resident buffers copied now: draws from here on read them.
        for (auto& [id, resident] : residents)
            if (!resident.ready)
                resident.ready = std::any_of(copied.begin(), copied.end(), [&](const BufferCopy& c) { return c.buffer == resident.buffer; });
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
}
