#include <gui_core/graphics/metal/metal_graphics_context.h>

#include <gui_core/graphics/metal/metal_device.h>
#include <gui_core/graphics/metal/metal_rhi.h>
#include <gui_core/graphics/metal/metal_window_context.h>
#include <gui_core/graphics/gui_image_set_text_atlas.h>
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

static constexpr tachyon::Diagnostics::LogCategory kLogMetal = tachyon::Diagnostics::MakeLogCategory("metal");

namespace tachyon::metal
{
    namespace
    {
        // How an ImageFormat is stored: Metal has no 3-byte format and macOS no sRGB format of
        // fewer than four channels, so those are widened to RGBA8, the missing channels 0 and
        // alpha 255, which samples the same.
        struct StoredFormat
        {
            MTLPixelFormat format;
            uint32_t       sourceBytes;   ///< per pixel, as given
            uint32_t       storedBytes;   ///< per pixel, as stored
        };

        StoredFormat Stored(ImageFormat format)
        {
            switch (format)
            {
            case ImageFormat::R8:            return { MTLPixelFormatR8Unorm, 1, 1 };
            case ImageFormat::R8_SRGB:       return { MTLPixelFormatRGBA8Unorm_sRGB, 1, 4 };
            case ImageFormat::R8G8:          return { MTLPixelFormatRG8Unorm, 2, 2 };
            case ImageFormat::R8G8_SRGB:     return { MTLPixelFormatRGBA8Unorm_sRGB, 2, 4 };
            case ImageFormat::R8G8B8:        return { MTLPixelFormatRGBA8Unorm, 3, 4 };
            case ImageFormat::R8G8B8_SRGB:   return { MTLPixelFormatRGBA8Unorm_sRGB, 3, 4 };
            case ImageFormat::R8G8B8A8_SRGB: return { MTLPixelFormatRGBA8Unorm_sRGB, 4, 4 };
            default:                         return { MTLPixelFormatRGBA8Unorm, 4, 4 };
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

        // `rowCount` rows of `width` pixels, from `data` (null: zeros), into a new staging buffer.
        id<MTLBuffer> Stage(id<MTLDevice> device, const void* data, uint32_t width, uint32_t rowCount, const StoredFormat& stored,
                            NSUInteger& rowPitch)
        {
            rowPitch = NSUInteger(width) * stored.storedBytes;
            id<MTLBuffer> staging = [device newBufferWithLength:std::max<NSUInteger>(rowPitch * rowCount, 16)
                                                        options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
            if (staging == nil)
                return nil;
            const auto*  from        = static_cast<const uint8_t*>(data);
            auto*        to          = static_cast<uint8_t*>(staging.contents);
            const size_t sourcePitch = size_t(width) * stored.sourceBytes;
            for (uint32_t y = 0; y < rowCount; ++y)
                CopyRow(to + size_t(y) * rowPitch, from != nullptr ? from + y * sourcePitch : nullptr, width, stored);
            return staging;
        }

        // Lets go of a scene's textures and image slot.
        void ReleaseScene(MetalGraphicsContext& graphics, MetalGraphicsContext::Impl::Scene& scene)
        {
            if (scene.index != ImageIndex::Invalid)
                graphics.UnregisterImage(scene.index);
            if (scene.color != nullptr)
                graphics.DestroyImageAtEnd(scene.color, DeviceScope::Frame);
            if (scene.depth != nullptr)
                graphics.DestroyImageAtEnd(scene.depth, DeviceScope::Frame);
            scene = {};
        }

        GpuKind KindOf(id<MTLDevice> device)
        {
            if (device.removable)
                return GpuKind::Discrete;   // an eGPU
            if (device.lowPower || device.hasUnifiedMemory)
                return GpuKind::Integrated;  // Apple silicon, Intel graphics
            return GpuKind::Discrete;
        }
    }

    bool MetalGraphicsContext::CheckDeviceSupport(void* devicePointer, std::string* problems)
    {
        std::string missing;
        id<MTLDevice> device = (__bridge id<MTLDevice>)devicePointer;
        if (device == nil)
            missing += "a device\n";
        else
        {
            // Metal 3: bindless textures by MTLResourceID in a buffer (argument buffers tier 2).
            if (![device supportsFamily:MTLGPUFamilyMetal3])
                missing += "the Metal 3 GPU family (bindless textures)\n";
            if (device.argumentBuffersSupport < MTLArgumentBuffersTier2)
                missing += "argument buffers tier 2\n";
        }
        if (problems != nullptr)
            *problems = missing;
        return missing.empty();
    }

    void MetalGraphicsContext::SetExternalDevice(const ExternalDevice& external)
    {
        m_impl->external = external;
    }

    MetalGraphicsContext::MetalGraphicsContext() : m_impl(std::make_unique<Impl>())
    {
    }

    MetalGraphicsContext::~MetalGraphicsContext()
    {
        Cleanup();
    }

    std::string_view MetalGraphicsContext::GetDeviceName() const
    {
        return m_impl->deviceName;
    }

    bool MetalGraphicsContext::DeviceLost() const
    {
        return m_impl->removed.load(std::memory_order_relaxed);
    }

    void MetalGraphicsContext::SetGpuPreference(std::string_view preference)
    {
        m_impl->gpuPreference = std::string(preference);
    }

    void MetalGraphicsContext::SetImageSlotCount(uint32_t slots)
    {
        m_impl->imageSlots = std::clamp(slots, 1u, kMaxImageSlots);
    }

    uint32_t MetalGraphicsContext::GetImageSlotCount() const
    {
        return m_impl->imageSlots;
    }

    Result<void> MetalGraphicsContext::Initialize(IWindowBackend* windowBackend)
    {
        if (Result<void> base = NullGraphicsContext::Initialize(windowBackend); !base)
            return base;
        Impl& d = *m_impl;
        const auto fail = [this](ErrorCode code, std::string message) -> Result<void>
        {
            TACHYON_LOG_ERROR(kLogMetal, "{}", message);
            Cleanup();
            return Error{ code, std::move(message) };
        };

        @autoreleasepool
        {
            if (d.external.device != nullptr || d.external.queue != nullptr)
            {
                id<MTLDevice>       device = (__bridge id<MTLDevice>)d.external.device;
                id<MTLCommandQueue> queue  = (__bridge id<MTLCommandQueue>)d.external.queue;
                if (device == nil || queue == nil)
                    return fail(ErrorCode::InvalidArgument, "Metal: the host's device needs both its device and its queue");
                if (queue.device != device)
                    return fail(ErrorCode::InvalidArgument, "Metal: the host's queue isn't its device's");
                if (std::string problems; !CheckDeviceSupport(d.external.device, &problems))
                    return fail(ErrorCode::Unsupported, "Metal: the host's device lacks:\n" + problems);
                d.device     = device;
                d.queue      = queue;
                d.deviceName = device.name.UTF8String;
                TACHYON_LOG_INFO(kLogMetal, "Using the host's GPU: {}", d.deviceName);
            }
            else
            {
                // The GPUs that run the shaders, the system's default first unless the app or
                // TACHYON_GPU asks for another.
                id<MTLDevice> systemDefault = MTLCreateSystemDefaultDevice();
                NSArray<id<MTLDevice>>* all = MTLCopyAllDevices();
                std::vector<id<MTLDevice>> candidates;
                std::vector<GpuInfo>       infos;
                TACHYON_LOG_INFO(kLogMetal, "Detected Metal GPUs:");
                const auto consider = [&](id<MTLDevice> device)
                {
                    if (device == nil || std::find(candidates.begin(), candidates.end(), device) != candidates.end())
                        return;
                    const std::string name = device.name.UTF8String;
                    if (!CheckDeviceSupport((__bridge void*)device))
                    {
                        TACHYON_LOG_INFO(kLogMetal, "  -: {} (not of the Metal 3 GPU family)", name);
                        return;
                    }
                    const GpuKind kind = KindOf(device);
                    TACHYON_LOG_INFO(kLogMetal, "  {}: {} ({})", candidates.size(), name, GpuKindName(kind));
                    candidates.push_back(device);
                    infos.push_back({ name, kind });
                };
                consider(systemDefault);
                for (id<MTLDevice> device in all)
                    consider(device);
                if (candidates.empty())
                    return fail(ErrorCode::Unsupported, "Metal: no GPU of the Metal 3 family (an Apple silicon Mac, or an "
                                                        "Intel Mac with AMD Vega, Radeon 5000/6000 or Intel UHD 630 or newer)");
                const size_t chosen = ChooseGpu(infos, GpuPreference(d.gpuPreference)).value_or(0);
                d.device     = candidates[chosen];
                d.deviceName = infos[chosen].name;
                TACHYON_LOG_INFO(kLogMetal, "Using GPU {}: {} ({})", chosen, d.deviceName, GpuKindName(infos[chosen].kind));
                d.queue = [d.device newCommandQueue];
                if (d.queue == nil)
                    return fail(ErrorCode::Failed, "Metal: newCommandQueue failed");
                d.queue.label = @"TachyonUI";
            }
            d.submitted = 0;
            d.completed = 0;
            d.imageSet.Create(d.imageSlots);
            if (!CreatePipelines(d))
                return fail(ErrorCode::Failed, "Metal: the GUI's pipelines failed");
        }
        return {};
    }

    void MetalGraphicsContext::Cleanup()
    {
        Impl& d = *m_impl;
        @autoreleasepool
        {
            if (d.queue != nil)
                d.WaitIdle();

            d.sceneRequests.clear();
            d.scenes.clear();
            d.externalImages.clear();
            {
                std::lock_guard lock(d.uploadMutex);
                d.imageUploads.clear();
                for (MetalImage* image : d.liveImages)
                    delete image;
                d.liveImages.clear();
            }
            {
                std::lock_guard lock(d.garbageMutex);
                d.garbage.clear();
            }
            d.uploadScratch.clear();
            d.residents.clear();
            d.buffers.clear();
            d.gradientPages.clear();
            d.boxStyles     = 0;
            d.colorStyles   = 0;
            d.quadIndices   = nil;
            d.quadCapacity  = 0;
            d.emptyBuffer   = nil;
            d.pushConstants = {};
            d.lastStyles    = nullptr;
            d.lastGradients = nullptr;
            d.pipelines     = {};
            d.pipelineStates.clear();
            d.depthTest  = nil;
            d.depthWrite = nil;
            d.samplers   = {};
            d.imageSet   = {};
            {
                std::lock_guard lock(d.inFlightMutex);
                d.inFlight.clear();
            }
            d.submitted = 0;
            d.completed = 0;
            d.queue  = nil;
            d.device = nil;
            d.deviceName.clear();
        }
        NullGraphicsContext::Cleanup();
    }

    void MetalGraphicsContext::BeginFrame()
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

    ImageIndex MetalGraphicsContext::RenderScene(ID key, ID window, uint2 size, color4f clear,
                                                 std::function<void(const ScenePayload& scene)> record)
    {
        Impl& d = *m_impl;
        if (d.device == nil || size.x == 0 || size.y == 0)
            return ImageIndex::Invalid;
        MarkContentChanged();   // its pixels are drawn again, its image slot the same
        Impl::Scene& scene = d.scenes[key];
        scene.lastFrame = d.sceneFrame;
        if (scene.color == nullptr || scene.extent != size)
        {
            // Made again; the old textures go once the frames using them are done.
            ReleaseScene(*this, scene);
            scene.lastFrame = d.sceneFrame;
            scene.color = d.CreateTargetImage(size, Impl::kSceneColorFormat, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead,
                                              Sampler::LinearClampToEdge);
            scene.depth = d.CreateTargetImage(size, kDepthFormat, MTLTextureUsageRenderTarget, Sampler::NearestRepeat);
            if (scene.color == nullptr || scene.depth == nullptr)
            {
                ReleaseScene(*this, scene);
                return ImageIndex::Invalid;
            }
            scene.extent = size;
            scene.index  = RegisterImage(scene.color);
        }
        d.sceneRequests.push_back({ key, window, clear, std::move(record) });
        return scene.index;
    }

    Image* MetalGraphicsContext::CreateImage(void* data, uint2 size, ImageFormat format, Sampler sampler, bool)
    {
        Impl& d = *m_impl;
        if (d.device == nil || size.x == 0 || size.y == 0)
            return nullptr;
        @autoreleasepool
        {
            const StoredFormat stored = Stored(format);
            MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:stored.format
                                                                                            width:size.x
                                                                                           height:size.y
                                                                                        mipmapped:NO];
            desc.usage       = MTLTextureUsageShaderRead;
            desc.storageMode = MTLStorageModePrivate;

            auto image = std::make_unique<MetalImage>();
            image->texture = [d.device newTextureWithDescriptor:desc];
            if (image->texture == nil)
            {
                TACHYON_LOG_ERROR(kLogMetal, "an image of {}x{} failed", size.x, size.y);
                return nullptr;
            }
            image->format       = stored.format;
            image->sampler      = sampler;
            image->size         = size;
            image->sourceFormat = format;

            Impl::ImageUpload upload;
            upload.staging = Stage(d.device, data, size.x, size.y, stored, upload.rowPitch);
            if (upload.staging == nil)
                return nullptr;
            upload.image    = image.get();
            upload.rowCount = size.y;
            std::lock_guard lock(d.uploadMutex);
            d.imageUploads.push_back(std::move(upload));
            d.liveImages.insert(image.get());
            return image.release();
        }
    }

    bool MetalGraphicsContext::UpdateImageRows(Image* img, uint32_t firstRow, uint32_t rowCount, const void* data)
    {
        Impl& d = *m_impl;
        auto* image = static_cast<MetalImage*>(img);
        if (d.device == nil || image == nullptr || image->external || !image->sourceFormat || data == nullptr ||
            rowCount == 0 || uint64_t(firstRow) + rowCount > image->size.y)
            return false;
        @autoreleasepool
        {
            const StoredFormat stored = Stored(*image->sourceFormat);
            Impl::ImageUpload upload;
            upload.staging = Stage(d.device, data, image->size.x, rowCount, stored, upload.rowPitch);
            if (upload.staging == nil)
                return false;
            upload.image    = image;
            upload.firstRow = firstRow;
            upload.rowCount = rowCount;
            std::lock_guard lock(d.uploadMutex);
            if (!d.liveImages.contains(image))
                return false;
            d.imageUploads.push_back(std::move(upload));
            return true;
        }
    }

    void MetalGraphicsContext::DestroyImageAtEnd(Image* img, DeviceScope scope, const std::source_location&)
    {
        Impl& d = *m_impl;
        auto* image = static_cast<MetalImage*>(img);
        if (image == nullptr || scope == DeviceScope::Application)
            return;   // Cleanup frees it
        {
            std::lock_guard lock(d.uploadMutex);
            // Freed already: Cleanup ran.
            if (d.liveImages.erase(image) == 0)
                return;
            // Not copied yet: no command buffer refers to its staging buffer.
            std::erase_if(d.imageUploads, [image](const Impl::ImageUpload& upload) { return upload.image == image; });
        }
        // A window frame being recorded may draw with it still.
        d.DestroyAfterGpu(std::unique_ptr<MetalImage>(image), d.submitted + 1);
    }

    TextAtlasManager* MetalGraphicsContext::CreateTextAtlasManager()
    {
        if (m_impl->device == nil)
            return NullGraphicsContext::CreateTextAtlasManager();
        return new ImageSetTextAtlasManager<MetalImageSet, MetalImage>();
    }

    ImageSet* MetalGraphicsContext::GetImageSet() const
    {
        if (m_impl->device == nil)
            return NullGraphicsContext::GetImageSet();
        return &m_impl->imageSet;
    }

    ImageIndex MetalGraphicsContext::RegisterImage(Image* image)
    {
        MarkContentChanged();
        return GetImageSet()->Add(image);
    }

    void MetalGraphicsContext::UnregisterImage(ImageIndex index)
    {
        MarkContentChanged();
        GetImageSet()->Remove(index);
        m_impl->externalImages.erase(uint32_t(index));
    }

    ImageIndex MetalGraphicsContext::RegisterExternalImage(void* texture, Sampler sampler)
    {
        Impl& d = *m_impl;
        if (d.device == nil || texture == nullptr)
            return ImageIndex::Invalid;
        const ImageIndex index = d.imageSet.ReserveIndex();
        if (index != ImageIndex::Invalid)
        {
            MarkContentChanged();
            UpdateExternalImage(index, texture, sampler);
        }
        return index;
    }

    void MetalGraphicsContext::UpdateExternalImage(ImageIndex index, void* texturePointer, Sampler sampler)
    {
        Impl& d = *m_impl;
        id<MTLTexture> texture = (__bridge id<MTLTexture>)texturePointer;
        if (d.device == nil || index == ImageIndex::Invalid || texture == nil)
            return;
        std::unique_ptr<MetalImage>& image = d.externalImages[uint32_t(index)];
        if (!image)
            image = std::make_unique<MetalImage>();
        image->texture  = texture;
        image->external = true;
        image->format   = texture.pixelFormat;
        image->sampler  = sampler;
        image->size     = { uint32_t(texture.width), uint32_t(texture.height) };
        d.imageSet.SetImage(index, image.get());
        MarkContentChanged();
    }

    void MetalGraphicsContext::PrepareFrame(StyleManager& styleManager, GradientManager& gradientManager, float elapsedTimeSecs)
    {
        Impl& d = *m_impl;
        if (d.device == nil)
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
            auto* page = static_cast<MetalImage*>(gradientManager.GetGradientImage(this, p));
            if (p < d.gradientPages.size())
                d.imageSet.SetImage(d.gradientPages[p], page);
            else
            {
                const ImageIndex index = d.imageSet.Add(page);
                d.gradientPages.push_back(index);
                gradientManager.SetPagePhysicalImageIndex(p, int32_t(uint32_t(index)) - 1);
            }
        }

        // Into GPU memory: a resident buffer, replaced and never written in place, so the frames
        // in flight keep theirs. A live resize prepares again before its window's frame.
        const auto upload = [&d](RHI::ResidentBufferId& buffer, DeviceAddress& address, const void* data, size_t bytes)
        {
            d.ReleaseResident(buffer);
            const RHI::UploadRequest request{ data, bytes, 16 };
            buffer = d.CreateResidentBuffer(std::span(&request, 1), std::span(&address, 1));
        };
        DeviceAddress boxAddress   = d.pushConstants.boxStyleBuffer;
        DeviceAddress colorAddress = d.pushConstants.colorStyleBuffer;
        if (styleManager.AreVisualBoxStylesModified() || !d.boxStyles)
        {
            const std::span<const VisualBoxStyle> styles = styleManager.GetVisualBoxStylesSpan();
            upload(d.boxStyles, boxAddress, styles.data(), styles.size_bytes());
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
            upload(d.colorStyles, colorAddress, resolved.data(), resolved.size() * sizeof(VisualColorStyle));
            styleManager.ResetVisualColorStylesModified();
            gradientManager.ClearPageTableChanged();
        }

        d.pushConstants = {};
        d.pushConstants.boxStyleBuffer   = boxAddress;
        d.pushConstants.colorStyleBuffer = colorAddress;
        d.pushConstants.elapsedTime      = elapsedTimeSecs;
    }

    void MetalGraphicsContext::RePrepareFrame()
    {
        Impl& d = *m_impl;
        if (d.lastStyles != nullptr && d.lastGradients != nullptr)
            PrepareFrame(*d.lastStyles, *d.lastGradients, d.lastTime);
    }

    IWindowContext* MetalGraphicsContext::CreateWindowContext(void* nativeWindowHandle, OSWindowHandle osHandle, ID id, int2 size,
                                                              WindowFlags windowFlags, OnGuiFunc onGui)
    {
        const bool toImage = (windowFlags & WindowFlags::RenderToImage) != WindowFlags::None;
        if (m_impl->device == nil || (nativeWindowHandle == nullptr && !toImage))
            return NullGraphicsContext::CreateWindowContext(nativeWindowHandle, osHandle, id, size, windowFlags, std::move(onGui));
        return MetalWindowContext::Create(*this, nativeWindowHandle, osHandle, id, size, windowFlags, std::move(onGui));
    }

    void MetalGraphicsContext::DestroyWindowContext(IWindowContext* context)
    {
        if (auto* window = dynamic_cast<MetalWindowContext*>(context))
        {
            if (guiRenderer != nullptr)
                static_cast<PrimitiveRenderer*>(guiRenderer)->ReleaseTarget(window->GetID());
            delete window;
            return;
        }
        NullGraphicsContext::DestroyWindowContext(context);
    }

    void MetalGraphicsContext::WaitIdle()
    {
        m_impl->WaitIdle();
    }
}
