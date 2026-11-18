#pragma once

// The SDL GPU graphics backend (TACHYON_SDLGPU): SDL3's GPU API, which renders with Vulkan,
// Direct3D 12 or Metal, whichever SDL picks. It's on its way: so far it starts its device and
// presents its windows, without drawing the UI.

#include <gui_core/graphics/null/gui_null_graphics_context.h>

#include <memory>
#include <string>

struct SDL_Window;

namespace tachyon::sdlgpu
{
    /// @addtogroup graphics
    /// @{

    /// How serious a message from SDL GPU's debug mode is.
    enum class DebugSeverity : uint8_t { Info, Warning, Error };

    /// Where SDL GPU's messages go. Without a callback they're logged as SDL logs them, and debug
    /// mode is on in debug builds only. Tests set one, so a message fails them.
    struct DebugConfig
    {
        void (*callback)(DebugSeverity severity, const char* message, void* userData) = nullptr;
        void* userData = nullptr;
    };

    class SDLGPUGraphicsContext : public NullGraphicsContext
    {
    public:
        /// The SDL window of a window backend's window (OSWindowHandle): SDLWindowBackend sets it.
        using WindowResolver = SDL_Window* (*)(OSWindowHandle window);

        SDLGPUGraphicsContext();
        ~SDLGPUGraphicsContext() override;

        GraphicsAPI      GetAPI() const override { return GraphicsAPI::SDLGPU; }
        /// The GPU, and the driver SDL renders with: "NVIDIA GeForce RTX 4070 (vulkan)".
        std::string_view GetDeviceName() const override;
        /// "integrated" or "cpu" ask SDL for its low-power GPU; SDL picks among the others.
        void             SetGpuPreference(std::string_view preference) override;
        /// The driver SDL renders with: "vulkan", "direct3d12" or "metal"; empty, SDL's choice.
        /// Call before Initialize.
        void             SetDriver(std::string_view driver);
        /// The driver in use, after Initialize.
        std::string_view GetDriver() const;
        /// Call before Initialize.
        void             SetDebugConfig(const DebugConfig& config);
        void             SetWindowResolver(WindowResolver resolver);

        /// Makes the device, on a driver that takes the shaders the GUI has. Fails when SDL finds
        /// none.
        Result<void> Initialize(IWindowBackend* windowBackend) override;
        void         Cleanup() override;

        /// An OS window is claimed for the device: SDL makes its swapchain. A render-to-image
        /// window, for now, gets the null stand-in.
        IWindowContext* CreateWindowContext(void* nativeWindowHandle, OSWindowHandle osHandle, ID id, int2 size,
                                            WindowFlags windowFlags, OnGuiFunc onGui) override;
        void            DestroyWindowContext(IWindowContext* context) override;

        /// Waits until the GPU has done all work submitted so far.
        void WaitIdle();

        struct Impl;
        /// Library internal: the device and what the window contexts share.
        Impl& Internal() { return *m_impl; }

    private:
        std::unique_ptr<Impl> m_impl;
    };
    /// @}
}
