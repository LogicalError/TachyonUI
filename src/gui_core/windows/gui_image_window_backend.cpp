#include <gui_core/windows/gui_image_window_backend.h>

#include <gui_core/windows/gui_window_flags.h>
#include <gui_core/input/gui_input_enums.h>

#include <algorithm>
#include <vector>

namespace tachyon
{
    struct ImageWindowBackend::ImageWindow
    {
        IntRect          rect { 0, 0, 0, 0 };
        IWindowContext*  context = nullptr;
        ImageWindow*     parent  = nullptr;   // the image window it was opened from
        bool             focusable = true;    // not WindowFlags::NotFocusable
    };

    struct ImageWindowBackend::State
    {
        std::vector<std::unique_ptr<ImageWindow>> windows;   // the handle is the ImageWindow's address
        ImageWindow*                              pointerWindow = nullptr;
        int2                                      pointerPos{};
        MouseButtons                              pointerButtons = MouseButtons::None;
        ImageWindow*                              focusWindow   = nullptr;
        std::vector<OSWindowHandle>               openedFromImage;   // OS windows with an image window as parent
        // The OS focus changes counted so far, of those seen, and the OS's active window then.
        uint32_t                                  osFocusCounted = 0;
        uint32_t                                  osFocusSeen    = 0;
        OSWindowHandle                            osActiveSeen   = nullptr;
    };

    ImageWindowBackend::ImageWindowBackend(IWindowBackend& osBackend)
        : m_os(osBackend), m_state(std::make_unique<State>())
    {
    }

    ImageWindowBackend::~ImageWindowBackend() = default;

    ImageWindowBackend::ImageWindow* ImageWindowBackend::Find(OSWindowHandle window) const
    {
        for (const auto& w : m_state->windows)
            if (reinterpret_cast<OSWindowHandle>(w.get()) == window)
                return w.get();
        return nullptr;
    }

    bool ImageWindowBackend::IsImageWindow(OSWindowHandle window) const { return window != nullptr && Find(window) != nullptr; }

    void ImageWindowBackend::SetPointer(OSWindowHandle window, int2 screenPos, MouseButtons buttons)
    {
        m_state->pointerWindow  = Find(window);
        m_state->pointerPos     = screenPos;
        m_state->pointerButtons = buttons;
    }

    void ImageWindowBackend::SetFocus(OSWindowHandle window)
    {
        ImageWindow* focus = Find(window);
        if (focus != m_state->focusWindow)
            ++m_focusChanges;
        m_state->focusWindow = focus;
    }

    bool ImageWindowBackend::HasFocusWithin(OSWindowHandle window) const
    {
        const ImageWindow* w = Find(window);
        if (w == nullptr)
            return false;
        for (const ImageWindow* up = m_state->focusWindow; up != nullptr; up = up->parent)
            if (up == w)
                return true;
        return false;
    }

    bool ImageWindowBackend::IsOpenedFromImageWindow(OSWindowHandle window) const
    {
        const auto& opened = m_state->openedFromImage;
        return window != nullptr && std::find(opened.begin(), opened.end(), window) != opened.end();
    }

    uint32_t ImageWindowBackend::GetFocusChangeCount() const
    {
        State& s = *m_state;
        if (const uint32_t os = m_os.GetFocusChangeCount(); os != s.osFocusSeen)
        {
            const OSWindowHandle active = m_os.GetActiveWindow();
            auto menuOrNone = [this](OSWindowHandle w) { return w == nullptr || IsOpenedFromImageWindow(w); };
            if (s.focusWindow == nullptr || !menuOrNone(s.osActiveSeen) || !menuOrNone(active))
                s.osFocusCounted += os - s.osFocusSeen;
            s.osFocusSeen  = os;
            s.osActiveSeen = active;
        }
        return s.osFocusCounted + m_focusChanges;
    }


    OSWindowHandle ImageWindowBackend::CreateWindow(std::string_view title, int2 size, WindowFlags windowFlags, OSWindowHandle parentOSHandle)
    {
        return CreateWindow(title, IntRect{ 0, 0, size.x, size.y }, windowFlags, parentOSHandle);
    }

    OSWindowHandle ImageWindowBackend::CreateWindow(std::string_view title, IntRect rect, WindowFlags windowFlags, OSWindowHandle parentOSHandle)
    {
        if ((windowFlags & WindowFlags::RenderToImage) == WindowFlags::None)
        {
            if (!IsImageWindow(parentOSHandle))
                return m_os.CreateWindow(title, rect, windowFlags, parentOSHandle);
            const OSWindowHandle handle = m_os.CreateWindow(title, rect, windowFlags, nullptr);
            if (handle != nullptr)
                m_state->openedFromImage.push_back(handle);
            return handle;
        }
        auto window = std::make_unique<ImageWindow>();
        window->rect   = rect;
        window->parent = Find(parentOSHandle);
        window->focusable = (windowFlags & WindowFlags::NotFocusable) == WindowFlags::None;
        const auto handle = reinterpret_cast<OSWindowHandle>(window.get());
        // Opened from the window with the focus: it takes it, as an OS popup or dialog does.
        const bool takesFocus = window->parent != nullptr && window->parent == m_state->focusWindow && window->focusable;
        m_state->windows.push_back(std::move(window));
        if (takesFocus)
            SetFocus(handle);
        return handle;
    }

    bool ImageWindowBackend::DestroyWindow(OSWindowHandle window)
    {
        ImageWindow* w = Find(window);
        if (w == nullptr)
        {
            std::erase(m_state->openedFromImage, window);
            if (m_state->osActiveSeen == window)
                m_state->osActiveSeen = nullptr;
            return m_os.DestroyWindow(window);
        }
        if (m_state->pointerWindow == w)
            m_state->pointerWindow = nullptr;
        // The focus goes back to the window it was opened from, as the OS does.
        if (m_state->focusWindow == w)
            m_state->focusWindow = w->parent;
        for (const auto& other : m_state->windows)
            if (other->parent == w)
                other->parent = w->parent;
        std::erase_if(m_state->windows, [w](const auto& p) { return p.get() == w; });
        return true;
    }

    void ImageWindowBackend::AttachInputHandlerToWindow(OSWindowHandle window, IPlatformInputHandler* inputHandler)
    {
        // An image window's input comes from the host, not from OS events.
        if (!IsImageWindow(window)) m_os.AttachInputHandlerToWindow(window, inputHandler);
    }

    void ImageWindowBackend::AttachContextToWindow(OSWindowHandle window, IWindowContext* context)
    {
        if (ImageWindow* w = Find(window)) w->context = context;
        else m_os.AttachContextToWindow(window, context);
    }

    void ImageWindowBackend::DetachWindowContext(OSWindowHandle window)
    {
        if (ImageWindow* w = Find(window)) w->context = nullptr;
        else m_os.DetachWindowContext(window);
    }

    bool ImageWindowBackend::IsWindow(OSWindowHandle window) { return IsImageWindow(window) || m_os.IsWindow(window); }

    IWindowContext* ImageWindowBackend::GetWindowContext(OSWindowHandle window)
    {
        if (ImageWindow* w = Find(window)) return w->context;
        return m_os.GetWindowContext(window);
    }

    void ImageWindowBackend::BringToFront(OSWindowHandle window) { if (!IsImageWindow(window)) m_os.BringToFront(window); }

    OSWindowHandle ImageWindowBackend::GetWindowUnderMouse()
    {
        if (m_state->pointerWindow != nullptr)
            return reinterpret_cast<OSWindowHandle>(m_state->pointerWindow);
        return m_os.GetWindowUnderMouse();
    }

    OSWindowHandle ImageWindowBackend::GetActiveWindow()
    {
        if (m_state->focusWindow != nullptr)
            return reinterpret_cast<OSWindowHandle>(m_state->focusWindow);
        return m_os.GetActiveWindow();
    }

    void ImageWindowBackend::PollEvents(std::function<void(OSWindowHandle, IWindowContext*)> cleanup) { m_os.PollEvents(std::move(cleanup)); }

    int2 ImageWindowBackend::GetScreenMousePos()
    {
        return m_state->pointerWindow != nullptr ? m_state->pointerPos : m_os.GetScreenMousePos();
    }

    MouseButtons ImageWindowBackend::GetMouseButtons()
    {
        return m_state->pointerWindow != nullptr ? m_state->pointerButtons : m_os.GetMouseButtons();
    }
    void ImageWindowBackend::SetCursor(MouseCursor cursor) { m_os.SetCursor(cursor); }

    void ImageWindowBackend::SetTextInputArea(OSWindowHandle window, IntRect rect)
    {
        if (!IsImageWindow(window)) m_os.SetTextInputArea(window, rect);
    }

    float ImageWindowBackend::GetWindowDisplayScale(OSWindowHandle window)
    {
        return IsImageWindow(window) ? 1.0f : m_os.GetWindowDisplayScale(window);
    }

    IntRect ImageWindowBackend::GetWindowPixelRect(OSWindowHandle window)
    {
        if (ImageWindow* w = Find(window)) return w->rect;
        return m_os.GetWindowPixelRect(window);
    }

    void ImageWindowBackend::SwapBuffers(OSWindowHandle window) { if (!IsImageWindow(window)) m_os.SwapBuffers(window); }

    IntRect ImageWindowBackend::GetWindowRect(OSWindowHandle window)
    {
        if (ImageWindow* w = Find(window)) return w->rect;
        return m_os.GetWindowRect(window);
    }

    int2 ImageWindowBackend::GetWindowPos(OSWindowHandle window)
    {
        if (ImageWindow* w = Find(window)) return w->rect.position;
        return m_os.GetWindowPos(window);
    }

    int2 ImageWindowBackend::GetWindowSize(OSWindowHandle window)
    {
        if (ImageWindow* w = Find(window)) return w->rect.size;
        return m_os.GetWindowSize(window);
    }

    void* ImageWindowBackend::GetNativeHandle(OSWindowHandle window)
    {
        return IsImageWindow(window) ? nullptr : m_os.GetNativeHandle(window);
    }

    bool ImageWindowBackend::SetWindowRect(OSWindowHandle window, const IntRect& clientRect)
    {
        if (ImageWindow* w = Find(window))
        {
            w->rect = clientRect;
            return true;
        }
        return m_os.SetWindowRect(window, clientRect);
    }

    void ImageWindowBackend::SetWindowVisible(OSWindowHandle window, bool visible)
    {
        ImageWindow* w = Find(window);
        if (w == nullptr)
        {
            m_os.SetWindowVisible(window, visible);
            return;
        }
        // Hidden with the focus in it (a menu that closed): the focus goes back to the
        // window it was opened from. Shown again from there: it takes it again.
        if (!visible && w->parent != nullptr && HasFocusWithin(window))
            SetFocus(reinterpret_cast<OSWindowHandle>(w->parent));
        if (visible && w->parent != nullptr && w->parent == m_state->focusWindow && w->focusable)
            SetFocus(window);
    }

    std::vector<MonitorInfo> ImageWindowBackend::GetMonitors() const { return m_os.GetMonitors(); }
    int ImageWindowBackend::GetMonitorAt(int2 pos) const { return m_os.GetMonitorAt(pos); }

    int ImageWindowBackend::GetMonitorFromWindow(OSWindowHandle window) const
    {
        return IsImageWindow(window) ? 0 : m_os.GetMonitorFromWindow(window);
    }

    uint32_t ImageWindowBackend::GetTicks() const { return m_os.GetTicks(); }
    std::string ImageWindowBackend::GetPreferredLanguage() const { return m_os.GetPreferredLanguage(); }
    int32_t     ImageWindowBackend::GetCursorSize() const        { return m_os.GetCursorSize(); }
}
