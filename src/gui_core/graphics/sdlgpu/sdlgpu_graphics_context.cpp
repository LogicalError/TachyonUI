#include <gui_core/graphics/sdlgpu/sdlgpu_graphics_context.h>

#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/graphics/gui_primitive_renderer.h>
#include <gui_core/graphics/sdlgpu/sdlgpu_device.h>
#include <gui_core/graphics/sdlgpu/sdlgpu_window_context.h>

#include <format>

static constexpr tachyon::Diagnostics::LogCategory kLogSDLGPU = tachyon::Diagnostics::MakeLogCategory("sdlgpu");

namespace tachyon::sdlgpu
{
    namespace
    {
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

    Result<void> SDLGPUGraphicsContext::Initialize(IWindowBackend* windowBackend)
    {
        if (Result<void> base = NullGraphicsContext::Initialize(windowBackend); !base)
            return base;
        Impl& d = *m_impl;

        // The debug callback takes SDL GPU's messages from the start, the device's making included.
        if (d.debug.callback != nullptr && !d.logHooked)
        {
            SDL_GetLogOutputFunction(&d.previousLog, &d.previousLogData);
            SDL_SetLogOutputFunction(&OnSDLLog, &d);
            d.logHooked = true;
        }

        // Debug mode (the driver's validation): for a test's callback, and in debug builds.
#if defined(NDEBUG)
        const bool debugMode = d.debug.callback != nullptr;
#else
        const bool debugMode = true;
#endif
        const SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, debugMode);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN, d.lowPower);
        if (!d.driverAsked.empty())
            SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, d.driverAsked.c_str());
        // The shader formats the GUI's shaders are built in.
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, true);
        d.device = SDL_CreateGPUDeviceWithProperties(props);
        SDL_DestroyProperties(props);
        if (d.device == nullptr)
        {
            const std::string message = std::format("SDL GPU: no device ({})", SDL_GetError());
            TACHYON_LOG_ERROR(kLogSDLGPU, "{}", message);
            Cleanup();
            return Error{ ErrorCode::Unsupported, message };
        }

        d.driver = SDL_GetGPUDeviceDriver(d.device);
        const char* name = SDL_GetStringProperty(SDL_GetGPUDeviceProperties(d.device), SDL_PROP_GPU_DEVICE_NAME_STRING, nullptr);
        d.deviceName = std::format("{} ({})", name != nullptr ? name : "unknown GPU", d.driver);
        TACHYON_LOG_INFO(kLogSDLGPU, "Using GPU: {}", d.deviceName);
        return {};
    }

    void SDLGPUGraphicsContext::Cleanup()
    {
        Impl& d = *m_impl;
        if (d.device != nullptr)
        {
            SDL_WaitForGPUIdle(d.device);
            SDL_DestroyGPUDevice(d.device);
            d.device = nullptr;
        }
        if (d.logHooked)
        {
            SDL_SetLogOutputFunction(d.previousLog, d.previousLogData);
            d.logHooked = false;
        }
        d.driver.clear();
        d.deviceName.clear();
        NullGraphicsContext::Cleanup();
    }

    IWindowContext* SDLGPUGraphicsContext::CreateWindowContext(void* nativeWindowHandle, OSWindowHandle osHandle, ID id, int2 size,
                                                               WindowFlags windowFlags, OnGuiFunc onGui)
    {
        Impl& d = *m_impl;
        SDL_Window* window = d.device != nullptr && nativeWindowHandle != nullptr && d.resolveWindow != nullptr
                           ? d.resolveWindow(osHandle) : nullptr;
        if (window == nullptr)
        {
            if (d.device != nullptr && nativeWindowHandle != nullptr)
                TACHYON_LOG_ERROR(kLogSDLGPU, "window: not an SDL window (no window resolver)");
            return NullGraphicsContext::CreateWindowContext(nativeWindowHandle, osHandle, id, size, windowFlags, std::move(onGui));
        }
        return SDLGPUWindowContext::Create(d, window, osHandle, id, size, windowFlags);
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
