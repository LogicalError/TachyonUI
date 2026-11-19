#pragma once

#include <chrono>
#include <string>
#include <string_view>
#include <unordered_map>

#include <gui_core/gui_id.h>
#include <gui_core/gui_recent_id_map.h>
#include <gui_core/gui_base_types.h>
#include <gui_core/input/gui_gesture.h>
#include <gui_core/style/gui_style_text.h>   // IconID

// Plain state holders for several controls (no frame callbacks or logic).

namespace tachyon
{
    /// @addtogroup controls
    /// @{

    /// In-place rename of a tree / list / grid row (F2, Ctrl+Shift+Click or a slow re-click);
    /// the target row emits a text field instead of its label. One rename at a time.
    class RenameSubsystem
    {
    public:
        tachyon::ID      targetID   = tachyon::ID::Invalid();   ///< the row being renamed
        bool         isActive   = false;
        /// Set on the frame the rename starts, when a commit left over from an earlier edit
        /// must not end it; cleared once the field is built.
        bool         firstFrame = false;
        std::string* editBuffer = nullptr;   ///< the caller's buffer the field edits

        /// Starts renaming row `id`: `buffer` is set to `text` and edited in place.
        void Start(tachyon::ID id, std::string* buffer, std::string_view text)
        {
            targetID = id; isActive = true; firstFrame = true;
            editBuffer = buffer; *buffer = text;
        }
        /// Ends the rename without touching the buffer.
        void Cancel()
        {
            targetID = tachyon::ID::Invalid(); isActive = false; firstFrame = false; editBuffer = nullptr;
        }
    };

    /// Slider track widths written by the sliders' custom draws and read next frame
    /// (keyboard step sizes, drag mapping).
    class SliderFieldTrackSubsystem
    {
    public:
        /// 0 until the slider has drawn once. Asked while the slider is built, which keeps
        /// its width (a cached panel doesn't draw it again).
        int GetWidth(tachyon::ID id)
        {
            m_widths.Touch(id);
            const int* width = m_widths.Find(id);
            return width ? *width : 0;
        }

        /// For the custom-draw lambdas to write into.
        RecentIDMap<int>& Widths() { return m_widths; }

        /// Once per frame: the widths of sliders no longer shown go after a while.
        void EndFrame() { m_widths.EndFrame(); }

    private:
        RecentIDMap<int> m_widths;
    };

    /// Set when a dock tab drag starts, so any target leaf can emit a phantom tab during
    /// construction. Cleared at frame start when no drag is active.
    struct DockTabDragInfo
    {
        tachyon::ID    panelID  = tachyon::ID::Invalid();
        std::string title;
        IconID     icon     = IconID::Invalid;
        int        tabWidth = 0; ///< the dragged tab's width at drag start, pixels
    };

    /// The drop-zone overlay shown while a dock tab is dragged; it slides between zones.
    struct DockDropZoneAnim
    {
        bool    hasTarget = false;  ///< the previous frame showed the overlay
        IntRect currentRect{};      ///< interpolated rect, the one drawn
        IntRect targetRect{};       ///< destination rect
        std::chrono::steady_clock::time_point lastTime{};
    };

    /// Dock tab drag state: the dragged tab and the drop-zone overlay animation.
    class DockTabDragSubsystem
    {
    public:
        DockTabDragInfo&  Info()         { return m_info; }
        DockDropZoneAnim& DropZoneAnim() { return m_dropZoneAnim; }

    private:
        DockTabDragInfo  m_info{};
        DockDropZoneAnim m_dropZoneAnim{};
    };

    struct ToolbarDockState;

    /// A floating toolbar window dragged by its thumb; one drag at a time.
    /// currentMousePos is read by the construction-time drop visualization.
    class ToolbarWindowDragSubsystem
    {
    public:
        Gesture<IntRect>  press;                           ///< the thumb's press, a drag from the start; the window before it, put back by Escape
        ID                windowID        = ID::Invalid();
        int2              currentMousePos {};              ///< screen pixels
        ToolbarDockState* sourceDockState = nullptr;

        bool Active() const { return press.IsHeld(); }
    };

    /// Last frame's screen rects of menu triggers and popups, written by AddPostLayout
    /// callbacks. BeginMenu sizes the popup from them (trigger width when it opens
    /// above/below, content width when flipped to the side).
    class MenuRectsSubsystem
    {
    public:
        /// Last frame's rect of menu trigger `id`; nullptr if absent.
        const tachyon::IntRect* FindTriggerRect(tachyon::ID id) const { return m_triggerRects.Find(id); }
        /// Last frame's rect of menu popup window `id`; nullptr if absent.
        const tachyon::IntRect* FindWindowRect(tachyon::ID id) const { return m_windowRects.Find(id); }

        /// For the AddPostLayout lambdas to write into.
        RecentIDMap<tachyon::IntRect>& TriggerRects() { return m_triggerRects; }
        RecentIDMap<tachyon::IntRect>& WindowRects()  { return m_windowRects; }

        /// Once per frame: the rects of menus no longer built go after a while.
        void EndFrame() { m_triggerRects.EndFrame(); m_windowRects.EndFrame(); }

        size_t Size() const { return m_triggerRects.Size() + m_windowRects.Size(); }

    private:
        RecentIDMap<tachyon::IntRect> m_triggerRects;
        RecentIDMap<tachyon::IntRect> m_windowRects;
    };
    /// @}
}
