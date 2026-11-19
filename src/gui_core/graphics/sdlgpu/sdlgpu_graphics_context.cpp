#include <gui_core/graphics/sdlgpu/sdlgpu_graphics_context.h>

#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/graphics/gui_image_set_text_atlas.h>
#include <gui_core/graphics/gui_primitive_renderer.h>
#include <gui_core/graphics/sdlgpu/sdlgpu_device.h>
#include <gui_core/graphics/sdlgpu/sdlgpu_rhi.h>
#include <gui_core/graphics/sdlgpu/sdlgpu_window_context.h>
#include <gui_core/style/gui_gradient_manager.h>
#include <gui_core/style/gui_style_manager.h>
#include <gui_core/windows/gui_window_flags.h>

#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <format>

static constexpr tachyon::Diagnostics::LogCategory kLogSDLGPU = tachyon::Diagnostics::MakeLogCategory("sdlgpu");

namespace tachyon::sdlgpu
{
    namespace
    {
        // Lets go of a scene's textures and image slot.
        void ReleaseScene(SDLGPUGraphicsContext& graphics, SDLGPUGraphicsContext::Impl::Scene& scene)
        {
            if (scene.index != ImageIndex::Invalid)
                graphics.UnregisterImage(scene.index);
            if (scene.color != nullptr)
                graphics.DestroyImageAtEnd(scene.color, DeviceScope::Frame);
            if (scene.depth != nullptr)
                graphics.DestroyImageAtEnd(scene.depth, DeviceScope::Frame);
            scene = {};
        }

        // SDL GPU's own messages go to the debug callback; the rest of SDL's log as before.
        void SDLCALL OnSDLLog(void* userData, int category, SDL_LogPriority priority, const char* message)
        {
            auto& d = *static_cast<SDLGPUGraphicsContext::Impl*>(userData);
            if (category == SDL_LOG_CATEGORY_GPU && d.debug.callback != nullptr)
            {
                const DebugSeverity severity = priority >= SDL_LOG_PRIORITY_ERROR ? DebugSeverity::Error
                                             : priority == SDL_LOG_PRIORITY_WARN  ? DebugSeverity::Warning
                                                                                  : DebugSeverity::Info;
                d.debug.callback(severity, message, d.debug.userData);
                return;
            }
            if (d.previousLog != nullptr)
                d.previousLog(d.previousLogData, category, priority, message);
        }

        // How an ImageFormat is stored: SDL GPU has no 3-byte format and no sRGB format of fewer
        // than four channels, so those are widened to RGBA8, the missing channels 0 and alpha 255.
        struct StoredFormat
        {
            SDL_GPUTextureFormat format;
            uint32_t             sourceBytes;   ///< per pixel, as given
            uint32_t             storedBytes;   ///< per pixel, as stored
        };

        StoredFormat Stored(ImageFormat format)
        {
            switch (format)
            {
            case ImageFormat::R8:            return { SDL_GPU_TEXTUREFORMAT_R8_UNORM, 1, 1 };
            case ImageFormat::R8_SRGB:       return { SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB, 1, 4 };
            case ImageFormat::R8G8:          return { SDL_GPU_TEXTUREFORMAT_R8G8_UNORM, 2, 2 };
            case ImageFormat::R8G8_SRGB:     return { SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB, 2, 4 };
            case ImageFormat::R8G8B8:        return { SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 3, 4 };
            case ImageFormat::R8G8B8_SRGB:   return { SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB, 3, 4 };
            case ImageFormat::R8G8B8A8_SRGB: return { SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB, 4, 4 };
            default:                         return { SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 4, 4 };
            }
        }

        // The pixels as stored, rows packed, widened where the stored format is.
        void Convert(uint8_t* to, const uint8_t* from, uint2 size, const StoredFormat& stored)
        {
            const size_t pixels = size_t(size.x) * size.y;
            if (from == nullptr)
            {
                std::memset(to, 0, pixels * stored.storedBytes);
                return;
            }
            if (stored.sourceBytes == stored.storedBytes)
            {
                std::memcpy(to, from, pixels * stored.storedBytes);
                return;
            }
            for (size_t i = 0; i < pixels; ++i, to += 4, from += stored.sourceBytes)
            {
                to[0] = from[0];
                to[1] = stored.sourceBytes > 1 ? from[1] : 0;
                to[2] = stored.sourceBytes > 2 ? from[2] : 0;
                to[3] = 255;
            }
        }

        SDL_GPUSampler* MakeSampler(SDL_GPUDevice* device, SDL_GPUFilter filter, SDL_GPUSamplerAddressMode address)
        {
            SDL_GPUSamplerCreateInfo info{};
            info.min_filter     = filter;
            info.mag_filter     = filter;
            info.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
            info.address_mode_u = address;
            info.address_mode_v = address;
            info.address_mode_w = address;
            return SDL_CreateGPUSampler(device, &info);
        }
    }

    SDLGPUGraphicsContext::SDLGPUGraphicsContext() : m_impl(std::make_unique<Impl>())
    {
    }

    SDLGPUGraphicsContext::~SDLGPUGraphicsContext()
    {
        Cleanup();
    }

    std::string_view SDLGPUGraphicsContext::GetDeviceName() const
    {
        return m_impl->deviceName;
    }

    void SDLGPUGraphicsContext::SetGpuPreference(std::string_view preference)
    {
        m_impl->lowPower = preference == "integrated" || preference == "cpu";
    }

    void SDLGPUGraphicsContext::SetDriver(std::string_view driver)
    {
        m_impl->driverAsked = std::string(driver);
    }

    std::string_view SDLGPUGraphicsContext::GetDriver() const
    {
        return m_impl->driver;
    }

    void SDLGPUGraphicsContext::SetDebugConfig(const DebugConfig& config)
    {
        m_impl->debug = config;
    }

    void SDLGPUGraphicsContext::SetWindowResolver(WindowResolver resolver)
    {
        m_impl->resolveWindow = resolver;
    }

    void SDLGPUGraphicsContext::SetImageSlotCount(uint32_t slots)
    {
        m_impl->imageSlots = std::max(1u, slots);
    }

    uint32_t SDLGPUGraphicsContext::GetImageSlotCount() const
    {
        return m_impl->imageSlots;
    }

    void SDLGPUGraphicsContext::ApplyDeviceRequirements(SDL_PropertiesID props)
    {
        // On Vulkan, what the shaders use that SDL's device doesn't enable by itself: the 16-bit
        // types, and the draw parameters SV_VertexID is built from (VertexIndex - BaseVertex).
        // SDL reads them when the device is made; they live as long as the program.
        struct Vulkan
        {
            VkPhysicalDeviceVulkan12Features features12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
            VkPhysicalDeviceVulkan11Features features11{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
            VkPhysicalDeviceFeatures         features10{};
            SDL_GPUVulkanOptions             options{};

            Vulkan()
            {
                features12.shaderFloat16                      = VK_TRUE;
                features11.pNext                              = &features12;
                features11.shaderDrawParameters               = VK_TRUE;
                features11.storageBuffer16BitAccess           = VK_TRUE;
                features11.uniformAndStorageBuffer16BitAccess = VK_TRUE;
                features10.shaderInt16                        = VK_TRUE;
                options.vulkan_api_version                 = VK_API_VERSION_1_2;
                options.feature_list                       = &features11;
                options.vulkan_10_physical_device_features = &features10;
            }
        };
        static Vulkan vulkan;
        SDL_SetPointerProperty(props, SDL_PROP_GPU_DEVICE_CREATE_VULKAN_OPTIONS_POINTER, &vulkan.options);
        // The shader formats the GUI's shaders are built in.
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
#if defined(_WIN32)
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true);
#endif
    }

    bool SDLGPUGraphicsContext::CheckDeviceSupport(SDL_GPUDevice* device, std::string* problems)
    {
        std::string found;
        if (device == nullptr)
            found += "no device\n";
        else
        {
            const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
#if defined(_WIN32)
            constexpr SDL_GPUShaderFormat built = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL;
            constexpr const char* builtNames = "SPIR-V or DXIL";
#else
            constexpr SDL_GPUShaderFormat built = SDL_GPU_SHADERFORMAT_SPIRV;
            constexpr const char* builtNames = "SPIR-V";
#endif
            if ((formats & built) == 0)
                found += std::format("its driver ({}) takes none of the shader formats the GUI's shaders are built in ({})\n",
                                     SDL_GetGPUDeviceDriver(device), builtNames);
        }
        if (problems != nullptr)
            *problems = found;
        return found.empty();
    }

    void SDLGPUGraphicsContext::SetExternalDevice(SDL_GPUDevice* device)
    {
        m_impl->externalDevice = device;
    }

    Result<void> SDLGPUGraphicsContext::Initialize(IWindowBackend* windowBackend)
    {
        if (Result<void> base = NullGraphicsContext::Initialize(windowBackend); !base)
            return base;
        Impl& d = *m_impl;
        const auto fail = [this](ErrorCode code, std::string message) -> Result<void>
        {
            TACHYON_LOG_ERROR(kLogSDLGPU, "{}", message);
            Cleanup();
            return Error{ code, std::move(message) };
        };

        // The debug callback takes SDL GPU's messages from the start, the device's making included.
        if (d.debug.callback != nullptr && !d.logHooked)
        {
            SDL_GetLogOutputFunction(&d.previousLog, &d.previousLogData);
            SDL_SetLogOutputFunction(&OnSDLLog, &d);
            d.logHooked = true;
        }

        if (d.externalDevice != nullptr)
        {
            // The host's: checked, not made.
            if (std::string problems; !CheckDeviceSupport(d.externalDevice, &problems))
                return fail(ErrorCode::Unsupported, std::format("SDL GPU: the host's device can't run the GUI:\n{}", problems));
            d.device = d.externalDevice;
        }
        else
        {
            // Debug mode (the driver's validation): for a test's callback, and in debug builds.
#if defined(NDEBUG)
            const bool debugMode = d.debug.callback != nullptr;
#else
            const bool debugMode = true;
#endif
            const SDL_PropertiesID props = SDL_CreateProperties();
            ApplyDeviceRequirements(props);
            SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, debugMode);
            SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN, d.lowPower);
            if (!d.driverAsked.empty())
                SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, d.driverAsked.c_str());
            d.device = SDL_CreateGPUDeviceWithProperties(props);
            SDL_DestroyProperties(props);
            if (d.device == nullptr)
                return fail(ErrorCode::Unsupported, std::format("SDL GPU: no device ({})", SDL_GetError()));
        }

        d.driver = SDL_GetGPUDeviceDriver(d.device);
        const char* name = SDL_GetStringProperty(SDL_GetGPUDeviceProperties(d.device), SDL_PROP_GPU_DEVICE_NAME_STRING, nullptr);
        d.deviceName = std::format("{} ({})", name != nullptr ? name : "unknown GPU", d.driver);
        TACHYON_LOG_INFO(kLogSDLGPU, "Using GPU: {}", d.deviceName);
        const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(d.device);
        d.shaderFormat = (formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0 ? SDL_GPU_SHADERFORMAT_SPIRV : SDL_GPU_SHADERFORMAT_DXIL;

        // The samplers, as SamplerFor picks them; what's bound where a draw has nothing.
        d.samplers = { MakeSampler(d.device, SDL_GPU_FILTER_NEAREST, SDL_GPU_SAMPLERADDRESSMODE_REPEAT),
                       MakeSampler(d.device, SDL_GPU_FILTER_LINEAR, SDL_GPU_SAMPLERADDRESSMODE_REPEAT),
                       MakeSampler(d.device, SDL_GPU_FILTER_LINEAR, SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE) };
        SDL_GPUTextureCreateInfo blank{};
        blank.type                 = SDL_GPU_TEXTURETYPE_2D;
        blank.format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        blank.usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        blank.width                = 1;
        blank.height               = 1;
        blank.layer_count_or_depth = 1;
        blank.num_levels           = 1;
        d.blankImage  = SDL_CreateGPUTexture(d.device, &blank);
        d.blankBuffer = d.CreateBuffer(SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, nullptr, 256);
        if (std::find(d.samplers.begin(), d.samplers.end(), nullptr) != d.samplers.end() || d.blankImage == nullptr || d.blankBuffer == nullptr)
            return fail(ErrorCode::Failed, std::format("SDL GPU: the samplers or blanks failed ({})", SDL_GetError()));
        if (!CreatePipelines(d))
            return fail(ErrorCode::Failed, "SDL GPU: the GUI's pipelines failed");
        d.imageSet.SetCapacity(d.imageSlots);
        return {};
    }

    void SDLGPUGraphicsContext::Cleanup()
    {
        Impl& d = *m_impl;
        // Scenes' textures are among the live images.
        d.sceneRequests.clear();
        d.scenes.clear();
        if (d.device != nullptr)
        {
            SDL_WaitForGPUIdle(d.device);
            {
                std::lock_guard lock(d.copyMutex);
                for (const Impl::TextureCopy& copy : d.textureCopies)
                    SDL_ReleaseGPUTransferBuffer(d.device, copy.staging);
                for (const Impl::BufferCopy& copy : d.bufferCopies)
                    SDL_ReleaseGPUTransferBuffer(d.device, copy.staging);
                d.textureCopies.clear();
                d.bufferCopies.clear();
                for (SDLGPUImage* image : d.releasedImages)
                {
                    SDL_ReleaseGPUTexture(d.device, image->texture);
                    delete image;
                }
                d.releasedImages.clear();
                for (SDLGPUImage* image : d.liveImages)
                {
                    SDL_ReleaseGPUTexture(d.device, image->texture);
                    delete image;
                }
                d.liveImages.clear();
            }
            for (auto& [id, resident] : d.residents)
                SDL_ReleaseGPUBuffer(d.device, resident.buffer);
            d.residents.clear();
            for (SDL_GPUBuffer** buffer : { &d.boxStyles, &d.colorStyles, &d.quadIndices, &d.blankBuffer })
                d.ReleaseBuffer(*buffer);
            d.quadCapacity = 0;
            ReleasePipelines(d);
            for (SDL_GPUSampler*& sampler : d.samplers)
            {
                if (sampler != nullptr)
                    SDL_ReleaseGPUSampler(d.device, sampler);
                sampler = nullptr;
            }
            if (d.blankImage != nullptr)
                SDL_ReleaseGPUTexture(d.device, d.blankImage);
            d.blankImage = nullptr;
            // The host's device stays: SDL frees what was released with its next work.
            if (d.device != d.externalDevice)
                SDL_DestroyGPUDevice(d.device);
            d.device = nullptr;
        }
        d.externalImages.clear();
        d.imageTargets.clear();
        d.buffers = { nullptr };
        d.freeBuffers.clear();
        d.imageSet.Clear();
        d.gradientPages.clear();
        d.lastStyles    = nullptr;
        d.lastGradients = nullptr;
        if (d.logHooked)
        {
            SDL_SetLogOutputFunction(d.previousLog, d.previousLogData);
            d.logHooked = false;
        }
        d.driver.clear();
        d.deviceName.clear();
        NullGraphicsContext::Cleanup();
    }

    Image* SDLGPUGraphicsContext::CreateImage(void* data, uint2 size, ImageFormat format, Sampler sampler, bool)
    {
        Impl& d = *m_impl;
        if (d.device == nullptr || size.x == 0 || size.y == 0)
            return nullptr;
        const StoredFormat stored = Stored(format);
        SDL_GPUTextureCreateInfo info{};
        info.type                 = SDL_GPU_TEXTURETYPE_2D;
        info.format               = stored.format;
        info.usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width                = size.x;
        info.height               = size.y;
        info.layer_count_or_depth = 1;
        info.num_levels           = 1;
        auto image = std::make_unique<SDLGPUImage>();
        image->texture = SDL_CreateGPUTexture(d.device, &info);
        image->sampler = sampler;
        image->size    = size;
        const uint32_t bytes = size.x * size.y * stored.storedBytes;
        const SDL_GPUTransferBufferCreateInfo stagingInfo{ SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, bytes, 0 };
        SDL_GPUTransferBuffer* staging = image->texture != nullptr ? SDL_CreateGPUTransferBuffer(d.device, &stagingInfo) : nullptr;
        void* mapped = staging != nullptr ? SDL_MapGPUTransferBuffer(d.device, staging, false) : nullptr;
        if (mapped == nullptr)
        {
            TACHYON_LOG_ERROR(kLogSDLGPU, "an image of {}x{} failed ({})", size.x, size.y, SDL_GetError());
            if (staging != nullptr)
                SDL_ReleaseGPUTransferBuffer(d.device, staging);
            if (image->texture != nullptr)
                SDL_ReleaseGPUTexture(d.device, image->texture);
            return nullptr;
        }
        Convert(static_cast<uint8_t*>(mapped), static_cast<const uint8_t*>(data), size, stored);
        SDL_UnmapGPUTransferBuffer(d.device, staging);

        std::lock_guard lock(d.copyMutex);
        d.textureCopies.push_back({ image.get(), staging });
        d.liveImages.insert(image.get());
        return image.release();
    }

    void SDLGPUGraphicsContext::DestroyImageAtEnd(Image* img, DeviceScope scope, const std::source_location&)
    {
        Impl& d = *m_impl;
        auto* image = static_cast<SDLGPUImage*>(img);
        if (image == nullptr || scope == DeviceScope::Application)
            return;   // Cleanup frees it
        {
            std::lock_guard lock(d.copyMutex);
            // Freed already: Cleanup ran.
            if (d.liveImages.erase(image) == 0)
                return;
            // Not copied yet: no command buffer refers to it.
            std::erase_if(d.textureCopies, [&](const Impl::TextureCopy& copy) {
                if (copy.image != image)
                    return false;
                SDL_ReleaseGPUTransferBuffer(d.device, copy.staging);
                return true;
            });
            // Freed on the frame's thread (RecordCopies): a copy it took before this may
            // still be recording into it.
            d.releasedImages.push_back(image);
        }
    }

    TextAtlasManager* SDLGPUGraphicsContext::CreateTextAtlasManager()
    {
        if (m_impl->device == nullptr)
            return NullGraphicsContext::CreateTextAtlasManager();
        return new ImageSetTextAtlasManager<SDLGPUImageSet, SDLGPUImage>();
    }

    ImageSet* SDLGPUGraphicsContext::GetImageSet() const
    {
        if (m_impl->device == nullptr)
            return NullGraphicsContext::GetImageSet();
        return &m_impl->imageSet;
    }

    ImageIndex SDLGPUGraphicsContext::RegisterImage(Image* image)
    {
        MarkContentChanged();
        return GetImageSet()->Add(image);
    }

    void SDLGPUGraphicsContext::UnregisterImage(ImageIndex index)
    {
        MarkContentChanged();
        GetImageSet()->Remove(index);
        // A host's texture: the frames in flight bound the texture itself, not this.
        m_impl->externalImages.erase(uint32_t(index));
    }

    ImageIndex SDLGPUGraphicsContext::RegisterExternalImage(SDL_GPUTexture* texture, Sampler sampler)
    {
        Impl& d = *m_impl;
        if (d.device == nullptr || texture == nullptr)
            return ImageIndex::Invalid;
        const ImageIndex index = d.imageSet.ReserveIndex();
        if (index != ImageIndex::Invalid)
            UpdateExternalImage(index, texture, sampler);
        return index;
    }

    void SDLGPUGraphicsContext::UpdateExternalImage(ImageIndex index, SDL_GPUTexture* texture, Sampler sampler)
    {
        Impl& d = *m_impl;
        if (d.device == nullptr || index == ImageIndex::Invalid || texture == nullptr)
            return;
        std::unique_ptr<SDLGPUImage>& image = d.externalImages[uint32_t(index)];
        if (!image)
            image = std::make_unique<SDLGPUImage>();
        image->texture = texture;
        image->sampler = sampler;
        d.imageSet.SetImage(index, image.get());
        MarkContentChanged();
    }

    void SDLGPUGraphicsContext::SetWindowImageTarget(ID window, SDL_GPUTexture* texture, uint2 size)
    {
        if (texture == nullptr || size.x == 0 || size.y == 0)
            m_impl->imageTargets.erase(window);
        else
            m_impl->imageTargets[window] = { texture, size };
    }

    void SDLGPUGraphicsContext::PrepareFrame(StyleManager& styleManager, GradientManager& gradientManager, float elapsedTimeSecs)
    {
        Impl& d = *m_impl;
        if (d.device == nullptr)
        {
            NullGraphicsContext::PrepareFrame(styleManager, gradientManager, elapsedTimeSecs);
            return;
        }
        d.lastStyles    = &styleManager;
        d.lastGradients = &gradientManager;
        d.elapsedTime   = elapsedTimeSecs;

        // Every gradient page in the image set first: the color styles name their slots.
        const uint32_t pageCount = gradientManager.PageCount();
        for (uint32_t p = 0; p < pageCount; ++p)
        {
            auto* page = static_cast<SDLGPUImage*>(gradientManager.GetGradientImage(this, p));
            if (p < d.gradientPages.size())
                d.imageSet.SetImage(d.gradientPages[p], page);
            else
            {
                const ImageIndex index = d.imageSet.Add(page);
                d.gradientPages.push_back(index);
                gradientManager.SetPagePhysicalImageIndex(p, int32_t(uint32_t(index)) - 1);
            }
        }

        // A buffer is replaced, never written in place, so the frames in flight keep theirs.
        const std::span<const VisualBoxStyle> boxStyles = styleManager.GetVisualBoxStylesSpan();
        const bool boxChanged = styleManager.AreVisualBoxStylesModified() || d.boxStyles == nullptr;
        if (boxChanged)
        {
            d.ReleaseBuffer(d.boxStyles);
            d.boxStyles = d.CreateBuffer(SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, boxStyles.data(), uint32_t(boxStyles.size_bytes()));
            styleManager.ResetVisualBoxStylesModified();
        }
        const bool colorChanged = styleManager.AreVisualColorStylesModified() || gradientManager.HasPageTableChanged() || d.colorStyles == nullptr;
        if (!boxChanged && !colorChanged)
            return;
        // Styles name a gradient page; the shaders sample its slot.
        const std::span<const VisualColorStyle> source = styleManager.GetVisualColorStylesSpan();
        std::vector<VisualColorStyle> resolved(source.begin(), source.end());
        for (VisualColorStyle& style : resolved)
        {
            const int32_t slot = gradientManager.GetPagePhysicalImageIndex(style.gradientPage);
            assert(slot >= 0);
            style.gradientPage = uint16_t(slot);
        }
        if (colorChanged)
        {
            d.ReleaseBuffer(d.colorStyles);
            d.colorStyles = d.CreateBuffer(SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, resolved.data(),
                                           uint32_t(resolved.size() * sizeof(VisualColorStyle)));
            styleManager.ResetVisualColorStylesModified();
            gradientManager.ClearPageTableChanged();
        }
        // What each style reads, for cutting the frame's draws.
        d.batcher.SetStyles(resolved, boxStyles);
    }

    void SDLGPUGraphicsContext::RePrepareFrame()
    {
        Impl& d = *m_impl;
        if (d.lastStyles != nullptr && d.lastGradients != nullptr)
            PrepareFrame(*d.lastStyles, *d.lastGradients, d.elapsedTime);
    }

    void SDLGPUGraphicsContext::BeginFrame()
    {
        NullGraphicsContext::BeginFrame();
        Impl& d = *m_impl;
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

    ImageIndex SDLGPUGraphicsContext::RenderScene(ID key, ID window, uint2 size, color4f clear,
                                                  std::function<void(const ScenePayload& scene)> record)
    {
        Impl& d = *m_impl;
        if (d.device == nullptr || size.x == 0 || size.y == 0)
            return ImageIndex::Invalid;
        MarkContentChanged();   // its pixels are drawn again, its image slot the same
        Impl::Scene& scene = d.scenes[key];
        scene.lastFrame = d.sceneFrame;
        if (scene.color == nullptr || scene.extent != size)
        {
            // Made again at the new size; SDL frees the old textures once the frames using them are done.
            ReleaseScene(*this, scene);
            scene.lastFrame = d.sceneFrame;
            scene.color = d.CreateTargetImage(size, Impl::kSceneColorFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                              Sampler::LinearClampToEdge);
            scene.depth = d.CreateTargetImage(size, kDepthFormat, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, Sampler::NearestRepeat);
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

    IWindowContext* SDLGPUGraphicsContext::CreateWindowContext(void* nativeWindowHandle, OSWindowHandle osHandle, ID id, int2 size,
                                                               WindowFlags windowFlags, OnGuiFunc onGui)
    {
        Impl& d = *m_impl;
        if (d.device != nullptr && (windowFlags & WindowFlags::RenderToImage) != WindowFlags::None)
            return SDLGPUWindowContext::Create(*this, nullptr, nullptr, osHandle, id, size, windowFlags, std::move(onGui));
        SDL_Window* window = d.device != nullptr && nativeWindowHandle != nullptr && d.resolveWindow != nullptr
                           ? d.resolveWindow(osHandle) : nullptr;
        if (window == nullptr)
        {
            if (d.device != nullptr && nativeWindowHandle != nullptr)
                TACHYON_LOG_ERROR(kLogSDLGPU, "window: not an SDL window (no window resolver)");
            return NullGraphicsContext::CreateWindowContext(nativeWindowHandle, osHandle, id, size, windowFlags, std::move(onGui));
        }
        return SDLGPUWindowContext::Create(*this, window, nativeWindowHandle, osHandle, id, size, windowFlags, std::move(onGui));
    }

    void SDLGPUGraphicsContext::DestroyWindowContext(IWindowContext* context)
    {
        if (auto* window = dynamic_cast<SDLGPUWindowContext*>(context))
        {
            if (guiRenderer != nullptr)
                static_cast<PrimitiveRenderer*>(guiRenderer)->ReleaseTarget(window->GetID());
            delete window;
            return;
        }
        NullGraphicsContext::DestroyWindowContext(context);
    }

    void SDLGPUGraphicsContext::WaitIdle()
    {
        if (m_impl->device != nullptr)
            SDL_WaitForGPUIdle(m_impl->device);
    }
}
