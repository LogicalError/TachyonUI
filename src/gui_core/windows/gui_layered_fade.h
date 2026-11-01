#pragma once

#include <gui_core/gui_base_types.h>

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>

namespace tachyon
{
    /// @addtogroup windows
    /// @{

    /// Layered windows that stand in for a window while it fades in or out: each shows a
    /// fixed picture (the window's last frame) at a screen rect and an opacity. A window with
    /// a GPU surface can't be faded itself on Windows, where a fading window needs to be
    /// layered. Elsewhere there are none: Show returns 0.
    class LayeredFades
    {
    public:
        LayeredFades();
        ~LayeredFades();
        LayeredFades(const LayeredFades&) = delete;
        LayeredFades& operator=(const LayeredFades&) = delete;

        /// Shows `bgra` (`size` pixels, BGRA8, alpha 255 for an opaque window) at `screenRect`
        /// (screen pixels; the picture is drawn at its own size from its top-left) with
        /// `opacity` (0 to 1). Returns the stand-in, or 0 when it can't be shown.
        uint32_t Show(std::span<const uint8_t> bgra, int2 size, IntRect screenRect, float opacity);
        /// Moves a stand-in and sets its opacity.
        void     Move(uint32_t fade, IntRect screenRect, float opacity);
        /// Removes a stand-in; 0 and unknown ones are ignored.
        void     End(uint32_t fade);
        /// How many are shown.
        size_t   Count() const;

    private:
        struct Fade;
        std::unordered_map<uint32_t, std::unique_ptr<Fade>> m_fades;
        uint32_t m_next = 1;
    };

    /// @}
}
