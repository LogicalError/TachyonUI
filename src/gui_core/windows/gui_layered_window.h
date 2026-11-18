#pragma once

// Library internal: a layered window (WS_EX_LAYERED, Windows) shows a picture given to it rather
// than a GPU surface. The graphics backends draw a transparent window offscreen, read it back,
// and hand it over here. Elsewhere no window is layered and these do nothing.

#include <gui_core/gui_base_types.h>

#include <cstdint>
#include <span>

namespace tachyon
{
    /// Whether the native window is layered: it gets no swapchain, and shows what
    /// ShowLayeredPixels gives it.
    bool IsLayeredWindow(void* nativeWindow);

    /// Shows `bgra` (`size` pixels, BGRA8, premultiplied alpha) in the layered window. Its size
    /// and content only: the window stays where the window manager put it.
    void ShowLayeredPixels(void* nativeWindow, std::span<const uint8_t> bgra, uint2 size);
}
