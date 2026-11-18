#pragma once

// The graphics APIs a backend can render with, and what a backend hands an app that draws a
// scene (GraphicsContext::RenderScene). Light, for headers that only name them.

#include <cstdint>
#include <string_view>

namespace tachyon
{
    /// The graphics API a backend presents with; window backends create their
    /// windows to match (SDL: SDL_WINDOW_VULKAN).
    enum class GraphicsAPI : uint8_t
    {
        None,       ///< nothing is presented (NullGraphicsContext)
        Vulkan,
        D3D12,      ///< Direct3D 12, on Windows
        SDLGPU,     ///< SDL3's GPU API, over Vulkan, Direct3D 12 or Metal as SDL picks
    };

    /// The API's name as shown to people: "Vulkan", "Direct3D 12", "SDL GPU", or "No GPU" for
    /// None.
    constexpr std::string_view GraphicsAPIName(GraphicsAPI api)
    {
        switch (api)
        {
        case GraphicsAPI::Vulkan: return "Vulkan";
        case GraphicsAPI::D3D12:  return "Direct3D 12";
        case GraphicsAPI::SDLGPU: return "SDL GPU";
        default:                  return "No GPU";
        }
    }

    /// What GraphicsContext::RenderScene's `record` gets: the backend's recording state, as the
    /// struct of the API it renders with (Vulkan: a VulkanSceneRender). Ask for the one you draw
    /// with: As<T> returns it, or null when the scene is another API's. A state struct names
    /// its API as `static constexpr GraphicsAPI kAPI`.
    class ScenePayload
    {
    public:
        ScenePayload() = default;
        template <class T>
        explicit ScenePayload(const T& state) : m_api(T::kAPI), m_state(&state) {}

        GraphicsAPI API() const { return m_api; }

        template <class T>
        const T* As() const { return m_api == T::kAPI ? static_cast<const T*>(m_state) : nullptr; }

    private:
        GraphicsAPI m_api   = GraphicsAPI::None;
        const void* m_state = nullptr;
    };
}
