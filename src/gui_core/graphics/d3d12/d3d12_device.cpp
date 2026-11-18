#include <gui_core/graphics/d3d12/d3d12_device.h>

#include <gui_core/diagnostics/gui_logging.h>

#include <algorithm>
#include <bit>
#include <cstring>

static constexpr tachyon::Diagnostics::LogCategory kLogD3D12 = tachyon::Diagnostics::MakeLogCategory("d3d12");

namespace tachyon::d3d12
{
    namespace
    {
        D3D12_RESOURCE_DESC BufferDesc(UINT64 size)
        {
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width            = std::max<UINT64>(size, 1);
            desc.Height           = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels        = 1;
            desc.SampleDesc       = { 1, 0 };
            desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            return desc;
        }

        D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource   = resource;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = from;
            barrier.Transition.StateAfter  = to;
            return barrier;
        }
    }

    uint32_t SamplerSlot(Sampler sampler)
    {
        switch (sampler)
        {
        case Sampler::LinearRepeat:      return 1;
        case Sampler::LinearClampToEdge: return 2;
        default:                         return 0;   // NearestRepeat
        }
    }

    // ---- The queue -----------------------------------------------------------------------

    UINT64 D3D12GraphicsContext::Impl::Submit(ID3D12CommandList* list)
    {
        queue->ExecuteCommandLists(1, &list);
        queue->Signal(submitFence.Get(), ++submitted);
        return submitted;
    }

    void D3D12GraphicsContext::Impl::Wait(UINT64 value)
    {
        if (!submitFence || submitFence->GetCompletedValue() >= value)
            return;
        submitFence->SetEventOnCompletion(value, waitEvent);
        WaitForSingleObject(waitEvent, INFINITE);
    }

    void D3D12GraphicsContext::Impl::WaitIdle()
    {
        if (!queue)
            return;
        queue->Signal(submitFence.Get(), ++submitted);
        Wait(submitted);
    }

    // ---- Memory --------------------------------------------------------------------------

    Buffer D3D12GraphicsContext::Impl::CreateUploadBuffer(UINT64 size)
    {
        Buffer buffer;
        D3D12MA::ALLOCATION_DESC alloc{};
        alloc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
        const D3D12_RESOURCE_DESC desc = BufferDesc(size);
        if (FAILED(allocator->CreateResource(&alloc, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, &buffer.allocation, IID_NULL, nullptr)))
        {
            TACHYON_LOG_ERROR(kLogD3D12, "an upload buffer of {} bytes failed", size);
            return {};
        }
        const D3D12_RANGE none{ 0, 0 };
        buffer.allocation->GetResource()->Map(0, &none, &buffer.mapped);
        buffer.address = buffer.allocation->GetResource()->GetGPUVirtualAddress();
        buffer.size    = desc.Width;
        return buffer;
    }

    Buffer D3D12GraphicsContext::Impl::CreateGpuBuffer(UINT64 size)
    {
        Buffer buffer;
        D3D12MA::ALLOCATION_DESC alloc{};
        alloc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
        const D3D12_RESOURCE_DESC desc = BufferDesc(size);
        // A buffer starts in COMMON whatever it's given; the copy into it promotes it to COPY_DEST.
        if (FAILED(allocator->CreateResource(&alloc, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, &buffer.allocation, IID_NULL, nullptr)))
        {
            TACHYON_LOG_ERROR(kLogD3D12, "a GPU buffer of {} bytes failed", size);
            return {};
        }
        buffer.address = buffer.allocation->GetResource()->GetGPUVirtualAddress();
        buffer.size    = desc.Width;
        return buffer;
    }

    void D3D12GraphicsContext::Impl::DestroyAfterGpu(ComPtr<D3D12MA::Allocation> allocation, UINT64 afterValue)
    {
        if (!allocation)
            return;
        std::lock_guard lock(garbageMutex);
        garbage.push_back({ afterValue, std::move(allocation), nullptr });
    }

    void D3D12GraphicsContext::Impl::DestroyAfterGpu(std::unique_ptr<D3D12Image> image, UINT64 afterValue)
    {
        if (!image)
            return;
        std::lock_guard lock(garbageMutex);
        garbage.push_back({ afterValue, nullptr, std::move(image) });
    }

    void D3D12GraphicsContext::Impl::FlushDeletions()
    {
        const UINT64 done = Completed();
        std::lock_guard lock(garbageMutex);
        std::erase_if(garbage, [done](const Garbage& g) { return g.afterValue <= done; });
    }

    // ---- Uploads -------------------------------------------------------------------------

    void D3D12GraphicsContext::Impl::RecordPendingUploads(ID3D12GraphicsCommandList* list)
    {
        std::vector<ImageUpload> images;
        {
            std::lock_guard lock(uploadMutex);
            images.swap(imageUploads);
        }
        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        const UINT64 thisSubmit = submitted + 1;
        for (ImageUpload& upload : images)
        {
            D3D12_TEXTURE_COPY_LOCATION to{};
            to.pResource        = upload.image->Resource();
            to.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            to.SubresourceIndex = 0;
            D3D12_TEXTURE_COPY_LOCATION from{};
            from.pResource       = upload.staging.Resource();
            from.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            from.PlacedFootprint = upload.footprint;
            list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
            barriers.push_back(Transition(upload.image->Resource(), D3D12_RESOURCE_STATE_COPY_DEST,
                                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
            DestroyAfterGpu(std::move(upload.staging.allocation), thisSubmit);
        }
        for (auto& [id, resident] : residents)
        {
            if (resident.ready)
                continue;
            list->CopyBufferRegion(resident.gpu.Resource(), 0, resident.staging.Resource(), 0, resident.gpu.size);
            barriers.push_back(Transition(resident.gpu.Resource(), D3D12_RESOURCE_STATE_COPY_DEST,
                                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
            DestroyAfterGpu(std::move(resident.staging.allocation), thisSubmit);
            resident.staging = {};
            resident.ready   = true;
        }
        if (!barriers.empty())
            list->ResourceBarrier(UINT(barriers.size()), barriers.data());
    }

    // ---- Resident buffers ----------------------------------------------------------------

    RHI::ResidentBufferId D3D12GraphicsContext::Impl::CreateResidentBuffer(std::span<const RHI::UploadRequest> requests,
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

        Resident resident;
        resident.gpu     = CreateGpuBuffer(end);
        resident.staging = CreateUploadBuffer(end);
        if (!resident.gpu || !resident.staging)
            return 0;
        size_t offset = 0;
        for (size_t i = 0; i < requests.size(); ++i)
        {
            const RHI::UploadRequest& request = requests[i];
            if (request.bytes == 0)
                continue;
            const size_t alignment = std::max<size_t>(request.alignment, 1);
            offset = (offset + alignment - 1) / alignment * alignment;
            std::memcpy(static_cast<uint8_t*>(resident.staging.mapped) + offset, request.data, request.bytes);
            addresses[i] = DeviceAddress(resident.gpu.address + offset);
            offset += request.bytes;
        }
        const RHI::ResidentBufferId id = nextResident++;
        residents.emplace(id, std::move(resident));
        return id;
    }

    RHI::ResidentState D3D12GraphicsContext::Impl::ResidentState(RHI::ResidentBufferId id) const
    {
        const auto it = residents.find(id);
        if (it == residents.end())
            return RHI::ResidentState::Gone;
        return it->second.ready ? RHI::ResidentState::Ready : RHI::ResidentState::Pending;
    }

    void D3D12GraphicsContext::Impl::ReleaseResident(RHI::ResidentBufferId id)
    {
        const auto it = residents.find(id);
        if (it == residents.end())
            return;
        // Not copied yet: no command list refers to it. Else the frames that drew with it finish
        // first, and the one being recorded too, which may hold the copy into it.
        DestroyAfterGpu(std::move(it->second.gpu.allocation), it->second.ready ? submitted + 1 : 0);
        DestroyAfterGpu(std::move(it->second.staging.allocation), 0);
        residents.erase(it);
    }

    bool D3D12GraphicsContext::Impl::EnsureQuadIndices(uint32_t quads)
    {
        if (quads <= quadCapacity)
            return true;
        const uint32_t capacity = std::bit_ceil(std::max(quads, 4096u));
        Buffer buffer = CreateUploadBuffer(UINT64(capacity) * 6 * sizeof(uint32_t));
        if (!buffer)
            return false;
        auto* indices = static_cast<uint32_t*>(buffer.mapped);
        for (uint32_t p = 0; p < capacity; ++p)
        {
            const uint32_t base = p * 4;
            const uint32_t quad[6] = { base, base + 1, base + 3, base, base + 3, base + 2 };
            std::memcpy(indices + size_t(p) * 6, quad, sizeof(quad));
        }
        // Draws already recorded this frame may use the old one.
        DestroyAfterGpu(std::move(quadIndices.allocation), submitted + 1);
        quadIndices  = std::move(buffer);
        quadCapacity = capacity;
        return true;
    }

    // ---- The image set -------------------------------------------------------------------

    bool D3D12ImageSet::Create(ID3D12Device* device, uint32_t capacity)
    {
        m_device   = device;
        m_capacity = capacity;
        const D3D12_DESCRIPTOR_HEAP_DESC desc{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, capacity, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
        if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap))))
            return false;
        m_descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        return true;
    }

    void D3D12ImageSet::Clear()
    {
        m_images.clear();
        m_samplers.clear();
        m_free.clear();
    }

    ImageIndex D3D12ImageSet::Allocate()
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
                TACHYON_LOG_ERROR(kLogD3D12, "the image set is full ({} images)", m_capacity);
                return ImageIndex::Invalid;
            }
            slot = uint32_t(m_images.size());
            m_images.push_back(nullptr);
            m_samplers.push_back(0);
        }
        return ImageIndex(slot + 1);
    }

    void D3D12ImageSet::WriteSlot(uint32_t slot)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = m_heap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += SIZE_T(slot) * m_descriptorSize;
        const D3D12Image* image = m_images[slot];
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format                  = image != nullptr ? image->format : DXGI_FORMAT_R8G8B8A8_UNORM;
        view.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels     = 1;
        // An empty slot gets a null view: the shaders never sample one, and a frame copies it.
        m_device->CreateShaderResourceView(image != nullptr ? image->Resource() : nullptr, &view, handle);
        m_samplers[slot] = image != nullptr ? SamplerSlot(image->sampler) : 0;
    }

    ImageIndex D3D12ImageSet::Add(Image* image)
    {
        const ImageIndex index = Allocate();
        if (index != ImageIndex::Invalid)
            SetImage(index, static_cast<D3D12Image*>(image));
        return index;
    }

    ImageIndex D3D12ImageSet::ReserveIndex()
    {
        const ImageIndex index = Allocate();
        if (index != ImageIndex::Invalid)
            WriteSlot(uint32_t(index) - 1);
        return index;
    }

    void D3D12ImageSet::SetImage(ImageIndex index, D3D12Image* image)
    {
        const uint32_t slot = uint32_t(index) - 1;
        if (index == ImageIndex::Invalid || slot >= m_images.size())
            return;
        m_images[slot] = image;
        WriteSlot(slot);
    }

    void D3D12ImageSet::Remove(ImageIndex index)
    {
        const uint32_t slot = uint32_t(index) - 1;
        if (index == ImageIndex::Invalid || slot >= m_images.size())
            return;
        m_images[slot] = nullptr;
        WriteSlot(slot);
        m_free.push_back(slot);
    }
}
