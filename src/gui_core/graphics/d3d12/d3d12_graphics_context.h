#pragma once

// The Direct3D 12 graphics backend, on Windows (TACHYON_D3D12). It's being built (#248): so far
// it brings up the device and presents its windows, cleared. Until its own images, text atlas
// and drawing come, the null backend's stand-ins take their place, so an app runs on it and
// shows empty windows.

#include <gui_core/graphics/null/gui_null_graphics_context.h>

#include <memory>
#include <string>

namespace tachyon::d3d12
{
    /// @addtogroup graphics
    /// @{

    /// How serious a debug-layer message is.
    enum class DebugSeverity : uint8_t { Info, Warning, Error };

    /// Where the debug layer's messages go. Without a callback they're logged, and the debug
    /// layer is on in debug builds only. Tests set one, so a message fails them.
    struct DebugConfig
    {
        void (*callback)(DebugSeverity severity, const char* message, void* userData) = nullptr;
        void* userData = nullptr;
    };

    class D3D12GraphicsContext : public NullGraphicsContext
    {
    public:
        D3D12GraphicsContext();
        ~D3D12GraphicsContext() override;

        GraphicsAPI      GetAPI() const override { return GraphicsAPI::D3D12; }
        std::string_view GetDeviceName() const override;
        void             SetGpuPreference(std::string_view preference) override;
        /// Call before Initialize.
        void             SetDebugConfig(const DebugConfig& config);

        /// Picks a GPU that runs Shader Model 6.6 with resource binding tier 3 (the shaders'
        /// descriptor heap indexing) and makes the device and its queue. Fails when none can,
        /// which a Windows 10 without the newer D3D12 runtime is.
        Result<void> Initialize(IWindowBackend* windowBackend) override;
        void         Cleanup() override;

        /// An OS window gets a swapchain; a render-to-image window, for now, the null stand-in.
        IWindowContext* CreateWindowContext(void* nativeWindowHandle, OSWindowHandle osHandle, ID id, int2 size,
                                            WindowFlags windowFlags, OnGuiFunc onGui) override;
        void            DestroyWindowContext(IWindowContext* context) override;

        /// Waits until the GPU has done all work submitted so far.
        void WaitIdle();

        struct Impl;
        /// Library internal: the device, its queue and the DXGI factory.
        Impl& Internal() { return *m_impl; }

    private:
        std::unique_ptr<Impl> m_impl;
    };
    /// @}
}
