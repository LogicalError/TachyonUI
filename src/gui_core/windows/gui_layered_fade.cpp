#include <gui_core/windows/gui_layered_fade.h>

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace tachyon
{
#ifdef _WIN32
    namespace
    {
        constexpr wchar_t kFadeClass[] = L"TachyonLayeredFade";

        // Registered once; the windows only ever show their picture.
        bool RegisterFadeClass()
        {
            static const bool registered = []
            {
                WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
                wc.lpfnWndProc   = DefWindowProcW;
                wc.hInstance     = GetModuleHandleW(nullptr);
                wc.lpszClassName = kFadeClass;
                return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
            }();
            return registered;
        }

        BYTE Alpha(float opacity) { return BYTE(std::lround(std::clamp(opacity, 0.0f, 1.0f) * 255.0f)); }
    }

    struct LayeredFades::Fade
    {
        HWND    hwnd   = nullptr;
        HDC     dc     = nullptr;
        HBITMAP bitmap = nullptr;
        HGDIOBJ old    = nullptr;
        SIZE    size{};

        ~Fade()
        {
            if (hwnd)   DestroyWindow(hwnd);
            if (old)    SelectObject(dc, old);
            if (bitmap) DeleteObject(bitmap);
            if (dc)     DeleteDC(dc);
        }

        void Update(IntRect screenRect, float opacity) const
        {
            POINT position{ screenRect.x, screenRect.y };
            POINT origin{ 0, 0 };
            SIZE  extent = size;
            BLENDFUNCTION blend{ AC_SRC_OVER, 0, Alpha(opacity), AC_SRC_ALPHA };
            HDC screen = GetDC(nullptr);
            UpdateLayeredWindow(hwnd, screen, &position, &extent, dc, &origin, 0, &blend, ULW_ALPHA);
            ReleaseDC(nullptr, screen);
        }
    };

    LayeredFades::LayeredFades() = default;
    LayeredFades::~LayeredFades() = default;

    uint32_t LayeredFades::Show(std::span<const uint8_t> bgra, int2 size, IntRect screenRect, float opacity)
    {
        if (size.x <= 0 || size.y <= 0 || bgra.size() < size_t(size.x) * size_t(size.y) * 4 || !RegisterFadeClass())
            return 0;

        auto fade = std::make_unique<Fade>();
        fade->size = SIZE{ size.x, size.y };
        BITMAPINFO info{};
        info.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth       = size.x;
        info.bmiHeader.biHeight      = -size.y;   // top-down, as the picture
        info.bmiHeader.biPlanes      = 1;
        info.bmiHeader.biBitCount    = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        fade->dc     = CreateCompatibleDC(nullptr);
        fade->bitmap = fade->dc ? CreateDIBSection(fade->dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
        if (fade->bitmap == nullptr || bits == nullptr)
            return 0;
        fade->old = SelectObject(fade->dc, fade->bitmap);

        // UpdateLayeredWindow takes premultiplied pixels.
        auto* out = static_cast<uint8_t*>(bits);
        const size_t count = size_t(size.x) * size_t(size.y);
        for (size_t i = 0; i < count; ++i)
        {
            const uint8_t a = bgra[i * 4 + 3];
            out[i * 4 + 0] = uint8_t((bgra[i * 4 + 0] * a + 127) / 255);
            out[i * 4 + 1] = uint8_t((bgra[i * 4 + 1] * a + 127) / 255);
            out[i * 4 + 2] = uint8_t((bgra[i * 4 + 2] * a + 127) / 255);
            out[i * 4 + 3] = a;
        }

        // Click-through, never activated, above other windows and out of the taskbar.
        fade->hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                                     kFadeClass, L"", WS_POPUP, screenRect.x, screenRect.y, size.x, size.y,
                                     nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (fade->hwnd == nullptr)
            return 0;
        fade->Update(screenRect, opacity);
        ShowWindow(fade->hwnd, SW_SHOWNOACTIVATE);

        const uint32_t id = m_next++;
        m_fades[id] = std::move(fade);
        return id;
    }

    void LayeredFades::Move(uint32_t fade, IntRect screenRect, float opacity)
    {
        if (auto it = m_fades.find(fade); it != m_fades.end())
            it->second->Update(screenRect, opacity);
    }
#else
    struct LayeredFades::Fade {};

    LayeredFades::LayeredFades() = default;
    LayeredFades::~LayeredFades() = default;

    uint32_t LayeredFades::Show(std::span<const uint8_t>, int2, IntRect, float) { return 0; }
    void     LayeredFades::Move(uint32_t, IntRect, float) {}
#endif

    void LayeredFades::End(uint32_t fade)
    {
        m_fades.erase(fade);
    }

    size_t LayeredFades::Count() const
    {
        return m_fades.size();
    }
}
