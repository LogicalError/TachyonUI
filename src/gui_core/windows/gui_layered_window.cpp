#include <gui_core/windows/gui_layered_window.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <cstring>

namespace tachyon
{
#ifdef _WIN32
    bool IsLayeredWindow(void* nativeWindow)
    {
        return nativeWindow != nullptr && (GetWindowLongW(static_cast<HWND>(nativeWindow), GWL_EXSTYLE) & WS_EX_LAYERED) != 0;
    }

    void ShowLayeredPixels(void* nativeWindow, std::span<const uint8_t> bgra, uint2 size)
    {
        const size_t bytes = size_t(size.x) * size.y * 4;
        if (nativeWindow == nullptr || size.x == 0 || size.y == 0 || bgra.size() < bytes)
            return;
        HDC screen = GetDC(nullptr);
        if (screen == nullptr)
            return;
        HDC dc = CreateCompatibleDC(screen);
        BITMAPINFO info{};
        info.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth       = LONG(size.x);
        info.bmiHeader.biHeight      = -LONG(size.y);   // top-down
        info.bmiHeader.biPlanes      = 1;
        info.bmiHeader.biBitCount    = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void*   bits   = nullptr;
        HBITMAP bitmap = dc != nullptr ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
        if (bitmap != nullptr && bits != nullptr)
        {
            // UpdateLayeredWindow takes premultiplied BGRA, as the shaders write it.
            std::memcpy(bits, bgra.data(), bytes);
            const HGDIOBJ previous = SelectObject(dc, bitmap);
            if (previous != nullptr && previous != HGDI_ERROR)
            {
                POINT origin{ 0, 0 };
                SIZE  extent{ LONG(size.x), LONG(size.y) };
                BLENDFUNCTION blend{};
                blend.BlendOp             = AC_SRC_OVER;
                blend.SourceConstantAlpha = 255;
                blend.AlphaFormat         = AC_SRC_ALPHA;
                // Only the content and size, fully opaque (the window backend clears a hidden or
                // resized window until this). No position: the window stays where the window
                // manager put it. The last recorded position is stale once the window moves
                // without its picture changing (a drag ghost following the cursor), and setting
                // it would move the window back each frame. The window backend shows and hides
                // the window: shown here, SDL takes it for a window the OS showed and restores
                // it, which activates it.
                UpdateLayeredWindow(static_cast<HWND>(nativeWindow), screen, nullptr, &extent, dc, &origin, 0, &blend, ULW_ALPHA);
                SelectObject(dc, previous);
            }
        }
        if (bitmap != nullptr)
            DeleteObject(bitmap);
        if (dc != nullptr)
            DeleteDC(dc);
        ReleaseDC(nullptr, screen);
    }
#else
    bool IsLayeredWindow(void*) { return false; }
    void ShowLayeredPixels(void*, std::span<const uint8_t>, uint2) {}
#endif
}
