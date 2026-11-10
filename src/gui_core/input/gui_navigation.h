#pragma once

// How the pointer moves around a canvas that pans and zooms (the node view, the curve and
// timeline editors, a plot): the same buttons and wheel everywhere.

#include <gui_core/input/gui_input_state.h>

#include <cmath>

namespace tachyon
{
    /// @addtogroup input
    /// @{

    /// What the wheel, and a trackpad's two-finger scroll, does over such a canvas. A trackpad's
    /// scroll reaches the app as wheel turns, so with Pan a trackpad scrolls the view as it
    /// would a page.
    enum class WheelMode : uint8_t
    {
        Zoom,   ///< the wheel zooms round the pointer; Shift+wheel scrolls sideways
        Pan     ///< the wheel scrolls the view (Shift: sideways); Ctrl+wheel zooms
    };

    /// The mode a platform's users expect: Pan on macOS, where most pointers are trackpads;
    /// Zoom elsewhere.
    constexpr WheelMode DefaultWheelMode()
    {
#if defined(__APPLE__)
        return WheelMode::Pan;
#else
        return WheelMode::Zoom;
#endif
    }

    /// What a wheel or pinch event asks of a canvas.
    struct WheelNavigation
    {
        float  zoom = 1.0f;   ///< by how much to zoom round the pointer; above 1 is in, 1 is none
        float2 pan {};        ///< how far to scroll, in wheel notches: x right, y down
        bool Any() const { return zoom != 1.0f || pan.x != 0.0f || pan.y != 0.0f; }
    };

    /// A wheel (or pinch) event's zoom or scroll by `mode`; `zoomStep` is the zoom of one notch.
    /// A pinch zooms in either mode. A sideways scroll (a tilt wheel, a trackpad) always
    /// scrolls sideways.
    inline WheelNavigation NavigateByWheel(const InputState& s, WheelMode mode, float zoomStep)
    {
        WheelNavigation nav;
        if (s.pinch != 1.0f && std::isfinite(s.pinch) && s.pinch > 0.0f)
        {
            nav.zoom = s.pinch;
            return nav;
        }
        const bool ctrl  = (s.modifiers & KeyModifier::Control) != KeyModifier::None;
        const bool shift = (s.modifiers & KeyModifier::Shift) != KeyModifier::None;
        const bool zooms = mode == WheelMode::Zoom ? !shift : ctrl;
        if (zooms)
        {
            if (s.scroll.y != 0.0f)
                nav.zoom = std::pow(zoomStep, s.scroll.y);
            nav.pan.x = s.scroll.x;
            return nav;
        }
        // Away from the user scrolls up; with Shift, the turn scrolls sideways.
        nav.pan = shift ? float2{ s.scroll.x - s.scroll.y, 0.0f } : float2{ s.scroll.x, -s.scroll.y };
        return nav;
    }

    /// The button a press drags the view with: the middle one, or the left with Alt for a
    /// mouse or trackpad without a middle button. None when the press isn't one.
    inline MouseButtons PanDragButton(const InputState& s)
    {
        if (s.MiddlePressed())
            return MouseButtons::Middle;
        if (s.LeftPressed() && (s.modifiers & KeyModifier::Alt) != KeyModifier::None)
            return MouseButtons::Left;
        return MouseButtons::None;
    }

    /// @}
}
