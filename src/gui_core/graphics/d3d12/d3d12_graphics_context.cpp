#include <gui_core/graphics/d3d12/d3d12_graphics_context.h>

#include <gui_core/graphics/d3d12/d3d12_device.h>
#include <gui_core/graphics/d3d12/d3d12_rhi.h>
#include <gui_core/graphics/gui_image_set_text_atlas.h>
#include <gui_core/graphics/d3d12/d3d12_window_context.h>
#include <gui_core/graphics/gui_gpu_choice.h>
#include <gui_core/graphics/gui_primitive_renderer.h>
#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/style/gui_gradient_manager.h>
#include <gui_core/style/gui_style_manager.h>
#include <gui_core/windows/gui_window_flags.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <format>
#include <vector>

static constexpr tachyon::Diagnostics::LogCategory kLogD3D12 = tachyon::Diagnostics::MakeLogCategory("d3d12");

namespace tachyon::d3d12
{
    std::string HResultText(HRESULT hr)
    {
        return std::format("0x{:08X}", static_cast<uint32_t>(hr));
    }

    std::string FromWide(const wchar_t* text)
    {
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
        if (bytes <= 1)
            return {};
        std::string out(size_t(bytes - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), bytes, nullptr, nullptr);
        return out;
    }

    namespace
    {
        // How an ImageFormat is stored: Direct3D 12 has no 3-byte format and no sRGB format of
        // fewer than four channels, so those are widened to RGBA8, the missing channels 0 and
        // alpha 255, which samples the same.
        struct StoredFormat
        {
            DXGI_FORMAT format;
            uint32_t    sourceBytes;   ///< per pixel, as given
            uint32_t    storedBytes;   ///< per pixel, as stored
        };

        StoredFormat Stored(ImageFormat format)
        {
            switch (format)
            {
            case ImageFormat::R8:            return { DXGI_FORMAT_R8_UNORM, 1, 1 };
            case ImageFormat::R8_SRGB:       return { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 1, 4 };
            case ImageFormat::R8G8:          return { DXGI_FORMAT_R8G8_UNORM, 2, 2 };
            case ImageFormat::R8G8_SRGB:     return { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 2, 4 };
            case ImageFormat::R8G8B8:        return { DXGI_FORMAT_R8G8B8A8_UNORM, 3, 4 };
            case ImageFormat::R8G8B8_SRGB:   return { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 3, 4 };
            case ImageFormat::R8G8B8A8_SRGB: return { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 4, 4 };
            default:                         return { DXGI_FORMAT_R8G8B8A8_UNORM, 4, 4 };
            }
        }

        // One row of pixels into the staging buffer, widened where the stored format is.
        void CopyRow(uint8_t* to, const uint8_t* from, uint32_t width, const StoredFormat& stored)
        {
            if (from == nullptr)
            {
                std::memset(to, 0, size_t(width) * stored.storedBytes);
                return;
            }
            if (stored.sourceBytes == stored.storedBytes)
            {
                std::memcpy(to, from, size_t(width) * stored.storedBytes);
                return;
            }
            for (uint32_t x = 0; x < width; ++x, to += 4, from += stored.sourceBytes)
            {
                to[0] = from[0];
                to[1] = stored.sourceBytes > 1 ? from[1] : 0;
                to[2] = stored.sourceBytes > 2 ? from[2] : 0;
                to[3] = 255;
            }
        }

        // Lets go of a scene's textures and image slot.
        void ReleaseScene(D3D12GraphicsContext& graphics, D3D12GraphicsContext::Impl::Scene& scene)
        {
            if (scene.index != ImageIndex::Invalid)
                graphics.UnregisterImage(scene.index);
            if (scene.color != nullptr)
                graphics.DestroyImageAtEnd(scene.color, DeviceScope::Frame);
            if (scene.depth != nullptr)
                graphics.DestroyImageAtEnd(scene.depth, DeviceScope::Frame);
            scene = {};
        }

        // The debug layer's messages, to the test's callback or the log.
        void CALLBACK OnDebugMessage(D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID,
                                     LPCSTR description, void* context)
        {
            const DebugConfig& debug = static_cast<D3D12GraphicsContext::Impl*>(context)->debug;
            const DebugSeverity level = severity <= D3D12_MESSAGE_SEVERITY_ERROR ? DebugSeverity::Error
                                      : severity == D3D12_MESSAGE_SEVERITY_WARNING ? DebugSeverity::Warning
                                                                                   : DebugSeverity::Info;
            if (debug.callback != nullptr)
                debug.callback(level, description, debug.userData);
            else if (level == DebugSeverity::Error)
                TACHYON_LOG_ERROR(kLogD3D12, "{}", description);
            else if (level == DebugSeverity::Warning)
                TACHYON_LOG_WARNING(kLogD3D12, "{}", description);
        }

        // A GPU the backend can render with: its adapter, and what kind it is.
        struct Candidate
        {
            ComPtr<IDXGIAdapter1> adapter;
            GpuInfo               info;
        };

        // Whether the adapter runs the shaders: Shader Model 6.6 and resource binding tier 3,
        // for their descriptor heap indexing. Its kind comes from the device too.
        bool Suitable(IDXGIAdapter1* adapter, const DXGI_ADAPTER_DESC1& desc, GpuKind& kind)
        {
            ComPtr<ID3D12Device> device;
            if (FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))
                || !D3D12GraphicsContext::CheckDeviceSupport(device.Get()))
                return false;
            D3D12_FEATURE_DATA_ARCHITECTURE architecture{};
            device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &architecture, sizeof(architecture));
            kind = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 ? GpuKind::Cpu
                 : architecture.UMA                                ? GpuKind::Integrated
                                                                   : GpuKind::Discrete;
            return true;
        }
    }

    bool D3D12GraphicsContext::CheckDeviceSupport(ID3D12Device* device, std::string* problems)
    {
        std::string missing;
        D3D12_FEATURE_DATA_SHADER_MODEL model{ D3D_SHADER_MODEL_6_6 };
        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model)))
            || model.HighestShaderModel < D3D_SHADER_MODEL_6_6)
            missing += "Shader Model 6.6 (descriptor heap indexing)\n";
        D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)))
            || options.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_3)
            missing += "resource binding tier 3\n";
        if (problems != nullptr)
            *problems = missing;
        return missing.empty();
    }

    void D3D12GraphicsContext::SetExternalDevice(const ExternalDevice& external)
    {
        m_impl->external = external;
    }

    Result<void> D3D12GraphicsContext::AdoptExternalDevice()
    {
        Impl& d = *m_impl;
        const ExternalDevice& external = d.external;
        if (external.device == nullptr || external.queue == nullptr)
            return Error{ ErrorCode::InvalidArgument, "Direct3D 12: the host's device needs both its device and its queue" };
        if (external.queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
            return Error{ ErrorCode::InvalidArgument, "Direct3D 12: the host's queue isn't a direct (graphics) queue" };
        ComPtr<ID3D12Device> queueDevice;
        if (FAILED(external.queue->GetDevice(IID_PPV_ARGS(&queueDevice))) || queueDevice.Get() != external.device)
            return Error{ ErrorCode::InvalidArgument, "Direct3D 12: the host's queue isn't its device's" };
        if (std::string problems; !CheckDeviceSupport(external.device, &problems))
            return Error{ ErrorCode::Unsupported, "Direct3D 12: the host's device lacks:\n" + problems };

        d.device = external.device;
        d.queue  = external.queue;
        // Its adapter, for the memory allocator and the GPU's name.
        if (HRESULT hr = d.factory->EnumAdapterByLuid(d.device->GetAdapterLuid(), IID_PPV_ARGS(&d.adapter)); FAILED(hr))
            return Error{ ErrorCode::Failed, std::format("Direct3D 12: the host's device's adapter wasn't found ({})", HResultText(hr)) };
        DXGI_ADAPTER_DESC1 desc{};
        d.adapter->GetDesc1(&desc);
        d.deviceName = FromWide(desc.Description);
        TACHYON_LOG_INFO(kLogD3D12, "Using the host's GPU: {}", d.deviceName);
        return {};
    }

    D3D12GraphicsContext::D3D12GraphicsContext() : m_impl(std::make_unique<Impl>())
    {
    }

    D3D12GraphicsContext::~D3D12GraphicsContext()
    {
        Cleanup();
    }

    std::string_view D3D12GraphicsContext::GetDeviceName() const
    {
        return m_impl->deviceName;
    }

    bool D3D12GraphicsContext::DeviceLost() const
    {
        return m_impl->removed.load(std::memory_order_relaxed);
    }

    void D3D12GraphicsContext::SetGpuPreference(std::string_view preference)
    {
        m_impl->gpuPreference = std::string(preference);
    }

    void D3D12GraphicsContext::SetDebugConfig(const DebugConfig& config)
    {
        m_impl->debug = config;
    }

    Result<void> D3D12GraphicsContext::Initialize(IWindowBackend* windowBackend)
    {
        if (Result<void> base = NullGraphicsContext::Initialize(windowBackend); !base)
            return base;
        Impl& d = *m_impl;
        const auto fail = [this](ErrorCode code, std::string message) -> Result<void>
        {
            TACHYON_LOG_ERROR(kLogD3D12, "{}", message);
            Cleanup();
            return Error{ code, std::move(message) };
        };

        // The debug layer: for a test's callback, and in debug builds.
#if defined(NDEBUG)
        const bool wantDebug = d.debug.callback != nullptr;
#else
        const bool wantDebug = true;
#endif
        // A host's device is made already: enabling the debug layer now would remove it. Its
        // messages come through if the host turned it on.
        const bool external = d.external.device != nullptr || d.external.queue != nullptr;
        if (wantDebug && !external)
        {
            ComPtr<ID3D12Debug> debug;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
            {
                debug->EnableDebugLayer();
                d.debugLayer = true;
            }
            else
                TACHYON_LOG_WARNING(kLogD3D12, "no debug layer (Windows' Graphics Tools aren't installed)");
        }

        if (HRESULT hr = CreateDXGIFactory2(d.debugLayer ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS(&d.factory)); FAILED(hr))
            return fail(ErrorCode::Unsupported, std::format("Direct3D 12: no DXGI factory ({})", HResultText(hr)));
        BOOL tearing = FALSE;
        d.tearing = SUCCEEDED(d.factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))) && tearing;

        if (external)
        {
            if (Result<void> adopted = AdoptExternalDevice(); !adopted)
                return fail(adopted.GetError().code, adopted.GetError().message);
        }
        else
        {
            // The GPUs, the fastest first, and the first that runs the shaders unless the app or
            // TACHYON_GPU asks for another.
            std::vector<Candidate> candidates;
            TACHYON_LOG_INFO(kLogD3D12, "Detected Direct3D 12 GPUs:");
            for (UINT i = 0;; ++i)
            {
                ComPtr<IDXGIAdapter1> adapter;
                if (d.factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND)
                    break;
                DXGI_ADAPTER_DESC1 desc{};
                adapter->GetDesc1(&desc);
                GpuKind kind = GpuKind::Other;
                const std::string name = FromWide(desc.Description);
                if (!Suitable(adapter.Get(), desc, kind))
                {
                    TACHYON_LOG_INFO(kLogD3D12, "  -: {} (no Shader Model 6.6 with resource binding tier 3)", name);
                    continue;
                }
                TACHYON_LOG_INFO(kLogD3D12, "  {}: {} ({})", candidates.size(), name, GpuKindName(kind));
                candidates.push_back({ adapter, { name, kind } });
            }
            if (candidates.empty())
                return fail(ErrorCode::Unsupported, "Direct3D 12: no GPU runs Shader Model 6.6 with resource binding tier 3 "
                                                    "(Windows 11's D3D12 runtime has it)");
            std::vector<GpuInfo> infos;
            for (const Candidate& c : candidates)
                infos.push_back(c.info);
            const size_t chosen = ChooseGpu(infos, GpuPreference(d.gpuPreference)).value_or(0);
            d.adapter    = candidates[chosen].adapter;
            d.deviceName = candidates[chosen].info.name;
            TACHYON_LOG_INFO(kLogD3D12, "Using GPU {}: {} ({})", chosen, d.deviceName, GpuKindName(candidates[chosen].info.kind));

            if (HRESULT hr = D3D12CreateDevice(d.adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&d.device)); FAILED(hr))
                return fail(ErrorCode::Failed, std::format("Direct3D 12: D3D12CreateDevice failed ({})", HResultText(hr)));

            const D3D12_COMMAND_QUEUE_DESC queueDesc{ D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
                                                      D3D12_COMMAND_QUEUE_FLAG_NONE, 0 };
            if (HRESULT hr = d.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&d.queue)); FAILED(hr))
                return fail(ErrorCode::Failed, std::format("Direct3D 12: CreateCommandQueue failed ({})", HResultText(hr)));
        }
        if (d.debugLayer || external)
            if (SUCCEEDED(d.device.As(&d.infoQueue)))
                d.infoQueue->RegisterMessageCallback(&OnDebugMessage, D3D12_MESSAGE_CALLBACK_FLAG_NONE, &d, &d.messageCallback);

        if (HRESULT hr = d.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&d.submitFence)); FAILED(hr))
            return fail(ErrorCode::Failed, std::format("Direct3D 12: CreateFence failed ({})", HResultText(hr)));
        d.submitted = 0;
        d.waitEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        D3D12MA::ALLOCATOR_DESC allocatorDesc{};
        allocatorDesc.pDevice  = d.device.Get();
        allocatorDesc.pAdapter = d.adapter.Get();
        if (HRESULT hr = D3D12MA::CreateAllocator(&allocatorDesc, &d.allocator); FAILED(hr))
            return fail(ErrorCode::Failed, std::format("Direct3D 12: the memory allocator failed ({})", HResultText(hr)));
        if (!d.imageSet.Create(d.device.Get(), d.imageSlots))
            return fail(ErrorCode::Failed, std::format("Direct3D 12: a descriptor heap of {} images failed", d.imageSlots));
        if (!CreatePipelines(d))
            return fail(ErrorCode::Failed, "Direct3D 12: the GUI's pipelines failed");
        return {};
    }

    void D3D12GraphicsContext::Cleanup()
    {
        Impl& d = *m_impl;
        d.WaitIdle();

        // What's left of the GPU memory, before the allocator and the device go. Scenes' textures
        // are among the live images.
        d.sceneRequests.clear();
        d.scenes.clear();
        d.externalImages.clear();
        d.imageTargets.clear();
        d.frameWaits.clear();
        {
            std::lock_guard lock(d.uploadMutex);
            d.imageUploads.clear();
            for (D3D12Image* image : d.liveImages)
                delete image;
            d.liveImages.clear();
        }
        {
            std::lock_guard lock(d.garbageMutex);
            d.garbage.clear();
        }
        d.residents.clear();
        d.gradientPages.clear();
        d.boxStyles   = {};
        d.colorStyles = {};
        d.quadIndices = {};
        d.quadCapacity  = 0;
        d.pushConstants = {};
        d.lastStyles    = nullptr;
        d.lastGradients = nullptr;
        d.pipelines     = {};
        d.pipelineStates.clear();
        d.rootSignature.Reset();
        d.imageSet = {};

        if (d.infoQueue && d.messageCallback != 0)
            d.infoQueue->UnregisterMessageCallback(d.messageCallback);
        d.messageCallback = 0;
        d.infoQueue.Reset();
        d.allocator.Reset();
        d.submitFence.Reset();
        d.submitted = 0;
        d.queue.Reset();
        // Everything made from the device holds it: a reference left to one the GUI made is an
        // object leaked. A host's keeps the host's own.
        if (d.device)
            if (const ULONG left = d.device.Detach()->Release(); left != 0 && d.external.device == nullptr)
            {
                const std::string message = std::format("Direct3D 12: the device is still referenced {} time(s): objects made from it leaked", left);
                if (d.debug.callback != nullptr)
                    d.debug.callback(DebugSeverity::Error, message.c_str(), d.debug.userData);
                else
                    TACHYON_LOG_ERROR(kLogD3D12, "{}", message);
            }
        d.adapter.Reset();
        d.factory.Reset();
        if (d.waitEvent != nullptr)
            CloseHandle(d.waitEvent);
        d.waitEvent = nullptr;
        d.deviceName.clear();
        NullGraphicsContext::Cleanup();
    }

    void D3D12GraphicsContext::SetImageSlotCount(uint32_t slots)
    {
        // A shader-visible heap holds no more (resource binding tier 3); each window frame has one.
        m_impl->imageSlots = std::clamp(slots, 1u, uint32_t(D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_2));
    }

    uint32_t D3D12GraphicsContext::GetImageSlotCount() const
    {
        return m_impl->imageSlots;
    }

    uint64_t D3D12GraphicsContext::ImageViewWrites() const
    {
        return m_impl->imageSet.Writes();
    }

    void D3D12GraphicsContext::BeginFrame()
    {
        Impl& d = *m_impl;
        d.FlushDeletions();
        NullGraphicsContext::BeginFrame();

        // Last frame's scenes are recorded; the ones no longer asked for go.
        d.sceneRequests.clear();
        ++d.sceneFrame;
        for (auto it = d.scenes.begin(); it != d.scenes.end();)
        {
            if (d.sceneFrame - it->second.lastFrame > Impl::kSceneKeepFrames)
            {
                ReleaseScene(*this, it->second);
                it = d.scenes.erase(it);
            }
            else
                ++it;
        }
    }

    ImageIndex D3D12GraphicsContext::RenderScene(ID key, ID window, uint2 size, color4f clear,
                                                 std::function<void(const ScenePayload& scene)> record)
    {
        Impl& d = *m_impl;
        if (!d.device || size.x == 0 || size.y == 0)
            return ImageIndex::Invalid;
        MarkContentChanged();   // its pixels are drawn again, its image slot the same
        Impl::Scene& scene = d.scenes[key];
        scene.lastFrame = d.sceneFrame;
        if (scene.color == nullptr || scene.extent != size)
        {
            // Made again at the new size; the old textures go once the frames using them are done.
            ReleaseScene(*this, scene);
            scene.lastFrame = d.sceneFrame;
            // Cleared to the app's clear color each frame, and depth to 1: their optimized clears.
            D3D12_CLEAR_VALUE colorClear{};
            colorClear.Format = Impl::kSceneColorFormat;
            std::memcpy(colorClear.Color, &clear, sizeof(colorClear.Color));
            D3D12_CLEAR_VALUE depthClear{};
            depthClear.Format             = kDepthFormat;
            depthClear.DepthStencil.Depth = 1.0f;
            scene.color = d.CreateTargetImage(size, Impl::kSceneColorFormat, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                              &colorClear, Sampler::LinearClampToEdge);
            scene.depth = d.CreateTargetImage(size, kDepthFormat, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE,
                                              D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear, Sampler::NearestRepeat);
            const D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{ D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
            const D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{ D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
            if (scene.color == nullptr || scene.depth == nullptr || FAILED(d.device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&scene.rtv)))
                || FAILED(d.device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&scene.dsv))))
            {
                ReleaseScene(*this, scene);
                return ImageIndex::Invalid;
            }
            d.device->CreateRenderTargetView(scene.color->Resource(), nullptr, scene.rtv->GetCPUDescriptorHandleForHeapStart());
            d.device->CreateDepthStencilView(scene.depth->Resource(), nullptr, scene.dsv->GetCPUDescriptorHandleForHeapStart());
            scene.extent = size;
            scene.index  = RegisterImage(scene.color);
        }
        d.sceneRequests.push_back({ key, window, clear, std::move(record) });
        return scene.index;
    }

    Image* D3D12GraphicsContext::CreateImage(void* data, uint2 size, ImageFormat format, Sampler sampler, bool)
    {
        Impl& d = *m_impl;
        if (!d.device || size.x == 0 || size.y == 0)
            return nullptr;
        const StoredFormat stored = Stored(format);

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width            = size.x;
        desc.Height           = size.y;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.Format           = stored.format;
        desc.SampleDesc       = { 1, 0 };

        auto image = std::make_unique<D3D12Image>();
        image->format       = stored.format;
        image->sampler      = sampler;
        image->size         = size;
        image->sourceFormat = format;
        D3D12MA::ALLOCATION_DESC alloc{};
        alloc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
        if (HRESULT hr = d.allocator->CreateResource(&alloc, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, &image->allocation, IID_NULL, nullptr);
            FAILED(hr))
        {
            TACHYON_LOG_ERROR(kLogD3D12, "an image of {}x{} failed ({})", size.x, size.y, HResultText(hr));
            return nullptr;
        }

        Impl::ImageUpload upload;
        UINT64 bytes = 0;
        d.device->GetCopyableFootprints(&desc, 0, 1, 0, &upload.footprint, nullptr, nullptr, &bytes);
        upload.staging = d.CreateUploadBuffer(bytes);
        if (!upload.staging)
            return nullptr;
        const auto* from = static_cast<const uint8_t*>(data);
        auto*       to   = static_cast<uint8_t*>(upload.staging.mapped);
        const size_t sourcePitch = size_t(size.x) * stored.sourceBytes;
        for (uint32_t y = 0; y < size.y; ++y)
            CopyRow(to + size_t(y) * upload.footprint.Footprint.RowPitch, from != nullptr ? from + y * sourcePitch : nullptr, size.x, stored);

        upload.image = image.get();
        std::lock_guard lock(d.uploadMutex);
        d.imageUploads.push_back(std::move(upload));
        d.liveImages.insert(image.get());
        return image.release();
    }

    bool D3D12GraphicsContext::UpdateImageRows(Image* img, uint32_t firstRow, uint32_t rowCount, const void* data)
    {
        Impl& d = *m_impl;
        auto* image = static_cast<D3D12Image*>(img);
        if (!d.device || image == nullptr || !image->allocation || !image->sourceFormat || data == nullptr ||
            rowCount == 0 || uint64_t(firstRow) + rowCount > image->size.y)
            return false;
        const StoredFormat stored = Stored(*image->sourceFormat);

        // The band's own footprint: its rows, the image's width.
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width            = image->size.x;
        desc.Height           = rowCount;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.Format           = stored.format;
        desc.SampleDesc       = { 1, 0 };

        Impl::ImageUpload upload;
        UINT64 bytes = 0;
        d.device->GetCopyableFootprints(&desc, 0, 1, 0, &upload.footprint, nullptr, nullptr, &bytes);
        upload.staging = d.CreateUploadBuffer(bytes);
        if (!upload.staging)
            return false;
        const auto* from = static_cast<const uint8_t*>(data);
        auto*       to   = static_cast<uint8_t*>(upload.staging.mapped);
        const size_t sourcePitch = size_t(image->size.x) * stored.sourceBytes;
        for (uint32_t y = 0; y < rowCount; ++y)
            CopyRow(to + size_t(y) * upload.footprint.Footprint.RowPitch, from + y * sourcePitch, image->size.x, stored);

        upload.image    = image;
        upload.firstRow = firstRow;
        upload.partial  = true;
        std::lock_guard lock(d.uploadMutex);
        if (!d.liveImages.contains(image))
            return false;
        d.imageUploads.push_back(std::move(upload));
        return true;
    }

    void D3D12GraphicsContext::DestroyImageAtEnd(Image* img, DeviceScope scope, const std::source_location&)
    {
        Impl& d = *m_impl;
        auto* image = static_cast<D3D12Image*>(img);
        if (image == nullptr || scope == DeviceScope::Application)
            return;   // Cleanup frees it
        {
            std::lock_guard lock(d.uploadMutex);
            // Freed already: Cleanup ran.
            if (d.liveImages.erase(image) == 0)
                return;
            // Not copied yet: no command list refers to its staging buffer.
            std::erase_if(d.imageUploads, [image](const Impl::ImageUpload& upload) { return upload.image == image; });
        }
        // A window frame being recorded may draw with it still.
        d.DestroyAfterGpu(std::unique_ptr<D3D12Image>(image), d.submitted + 1);
    }

    TextAtlasManager* D3D12GraphicsContext::CreateTextAtlasManager()
    {
        if (!m_impl->device)
            return NullGraphicsContext::CreateTextAtlasManager();
        return new ImageSetTextAtlasManager<D3D12ImageSet, D3D12Image>();
    }

    ImageSet* D3D12GraphicsContext::GetImageSet() const
    {
        if (!m_impl->device)
            return NullGraphicsContext::GetImageSet();
        return &m_impl->imageSet;
    }

    ImageIndex D3D12GraphicsContext::RegisterImage(Image* image)
    {
        MarkContentChanged();
        return GetImageSet()->Add(image);
    }

    void D3D12GraphicsContext::UnregisterImage(ImageIndex index)
    {
        MarkContentChanged();
        GetImageSet()->Remove(index);
        // A host's texture: its slot's view is gone, and the frames in flight have their copy.
        m_impl->externalImages.erase(uint32_t(index));
    }

    ImageIndex D3D12GraphicsContext::RegisterExternalImage(ID3D12Resource* texture, Sampler sampler)
    {
        Impl& d = *m_impl;
        if (!d.device || texture == nullptr)
            return ImageIndex::Invalid;
        const ImageIndex index = d.imageSet.ReserveIndex();
        if (index != ImageIndex::Invalid)
        {
            MarkContentChanged();
            UpdateExternalImage(index, texture, sampler);
        }
        return index;
    }

    void D3D12GraphicsContext::UpdateExternalImage(ImageIndex index, ID3D12Resource* texture, Sampler sampler)
    {
        Impl& d = *m_impl;
        if (!d.device || index == ImageIndex::Invalid || texture == nullptr)
            return;
        const D3D12_RESOURCE_DESC desc = texture->GetDesc();
        std::unique_ptr<D3D12Image>& image = d.externalImages[uint32_t(index)];
        if (!image)
            image = std::make_unique<D3D12Image>();
        image->external = texture;
        image->format   = desc.Format;
        image->sampler  = sampler;
        image->size     = { uint32_t(desc.Width), desc.Height };
        d.imageSet.SetImage(index, image.get());
        MarkContentChanged();
    }

    void D3D12GraphicsContext::SetWindowImageTarget(ID window, ID3D12Resource* texture)
    {
        if (texture == nullptr)
            m_impl->imageTargets.erase(window);
        else
            m_impl->imageTargets[window] = texture;
    }

    void D3D12GraphicsContext::AddFrameWait(ID3D12Fence* fence, uint64_t value)
    {
        if (fence == nullptr)
            return;
        for (auto& [waited, at] : m_impl->frameWaits)
            if (waited.Get() == fence)
            {
                at = std::max<UINT64>(at, value);
                return;
            }
        m_impl->frameWaits.emplace_back(fence, value);
    }

    void D3D12GraphicsContext::PrepareFrame(StyleManager& styleManager, GradientManager& gradientManager, float elapsedTimeSecs)
    {
        Impl& d = *m_impl;
        if (!d.device)
        {
            NullGraphicsContext::PrepareFrame(styleManager, gradientManager, elapsedTimeSecs);
            return;
        }
        d.lastStyles    = &styleManager;
        d.lastGradients = &gradientManager;
        d.lastTime      = elapsedTimeSecs;

        // Every gradient page in the image set first: the color styles name their slots.
        const uint32_t pageCount = gradientManager.PageCount();
        for (uint32_t p = 0; p < pageCount; ++p)
        {
            auto* page = static_cast<D3D12Image*>(gradientManager.GetGradientImage(this, p));
            if (p < d.gradientPages.size())
                d.imageSet.SetImage(d.gradientPages[p], page);
            else
            {
                const ImageIndex index = d.imageSet.Add(page);
                d.gradientPages.push_back(index);
                gradientManager.SetPagePhysicalImageIndex(p, int32_t(uint32_t(index)) - 1);
            }
        }

        // A buffer is replaced, never written in place, so the frames in flight keep theirs. A
        // live resize prepares again between windows, while one may be recording.
        const auto upload = [&d](Buffer& buffer, const void* data, size_t bytes)
        {
            d.DestroyAfterGpu(std::move(buffer.allocation), d.submitted + 1);
            buffer = d.CreateUploadBuffer(bytes);
            if (buffer && bytes > 0)
                std::memcpy(buffer.mapped, data, bytes);
        };
        if (styleManager.AreVisualBoxStylesModified() || !d.boxStyles)
        {
            const std::span<const VisualBoxStyle> styles = styleManager.GetVisualBoxStylesSpan();
            upload(d.boxStyles, styles.data(), styles.size_bytes());
            styleManager.ResetVisualBoxStylesModified();
        }
        if (styleManager.AreVisualColorStylesModified() || gradientManager.HasPageTableChanged() || !d.colorStyles)
        {
            // Styles name a gradient page; the shaders sample its slot.
            const std::span<const VisualColorStyle> source = styleManager.GetVisualColorStylesSpan();
            std::vector<VisualColorStyle> resolved(source.begin(), source.end());
            for (VisualColorStyle& style : resolved)
            {
                const int32_t slot = gradientManager.GetPagePhysicalImageIndex(style.gradientPage);
                assert(slot >= 0);
                style.gradientPage = uint16_t(slot);
            }
            upload(d.colorStyles, resolved.data(), resolved.size() * sizeof(VisualColorStyle));
            styleManager.ResetVisualColorStylesModified();
            gradientManager.ClearPageTableChanged();
        }

        d.pushConstants = {};
        d.pushConstants.boxStyleBuffer   = DeviceAddress(d.boxStyles.address);
        d.pushConstants.colorStyleBuffer = DeviceAddress(d.colorStyles.address);
        d.pushConstants.elapsedTime      = elapsedTimeSecs;
    }

    void D3D12GraphicsContext::RePrepareFrame()
    {
        Impl& d = *m_impl;
        if (d.lastStyles != nullptr && d.lastGradients != nullptr)
            PrepareFrame(*d.lastStyles, *d.lastGradients, d.lastTime);
    }

    IWindowContext* D3D12GraphicsContext::CreateWindowContext(void* nativeWindowHandle, OSWindowHandle osHandle, ID id, int2 size,
                                                              WindowFlags windowFlags, OnGuiFunc onGui)
    {
        const bool toImage = (windowFlags & WindowFlags::RenderToImage) != WindowFlags::None;
        if (!m_impl->device || (nativeWindowHandle == nullptr && !toImage))
            return NullGraphicsContext::CreateWindowContext(nativeWindowHandle, osHandle, id, size, windowFlags, std::move(onGui));
        return D3D12WindowContext::Create(*this, static_cast<HWND>(nativeWindowHandle), osHandle, id, size, windowFlags, std::move(onGui));
    }

    void D3D12GraphicsContext::DestroyWindowContext(IWindowContext* context)
    {
        if (auto* window = dynamic_cast<D3D12WindowContext*>(context))
        {
            if (guiRenderer != nullptr)
                static_cast<PrimitiveRenderer*>(guiRenderer)->ReleaseTarget(window->GetID());
            delete window;
            return;
        }
        NullGraphicsContext::DestroyWindowContext(context);
    }

    void D3D12GraphicsContext::WaitIdle()
    {
        m_impl->WaitIdle();
    }
}
