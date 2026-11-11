#pragma once

#include <gui_core/input/gui_pointer_drag.h>

#include <stdint.h>
#include <utility>

namespace tachyon
{
    /// @addtogroup input
    /// @{

    class GUIStateManager;

    /// Where a press is, nested in the element's hover: nothing held, held, or dragging.
    enum class GesturePhase : uint8_t
    {
        Idle,       ///< no press
        Pressed,    ///< held, not yet moved the drag distance: may still be a click
        Dragging    ///< held and moved the drag distance (or a drag from the press on)
    };

    /// A press focuses `element`: the keyboard goes to it, and a text editor that had it lets go.
    void TakeFocus(GUIStateManager& gsm, ID element);

    /// What a press does besides being tracked.
    struct PressOptions
    {
        MouseButtons button  = MouseButtons::Left;  ///< the button that starts it
        bool         atOnce  = false;               ///< a drag from the press on, never a click
        bool         focus   = true;                ///< take the keyboard focus (and leave any text editor)
        ID           focusID = ID::Invalid();       ///< the element to focus; Invalid: the one pressed
        bool         capture = true;                ///< take the mouse capture, so the moves and release come here
    };

    /// No value to keep: a press that changes nothing to put back.
    struct NoSnapshot {};

    namespace detail
    {
        /// The focus and the capture a press of `pressed` takes, as `options` say.
        void TakePress(GUIStateManager& gsm, ID pressed, const PressOptions& options);
        /// Lets go of the capture when `element` has it.
        void ReleaseCaptureOf(GUIStateManager& gsm, ID element);
    }

    /// One press that may become a drag, with what every control does around it: the focus and
    /// the capture on the press, the value the drag changes kept as it was, put back on Escape
    /// and left for the commit after the release. The phases nest in the element's hover
    /// (InteractionState): hovered, then pressed, then dragging, ending as a click, a drag or a
    /// cancel.
    ///
    ///     Gesture<float> drag;                       // in the control's state
    ///     case MouseDown: return drag.Press(e, storage, gsm, value);
    ///     case MouseMove: if (drag.Move(e, distance)) value = ...; return true;
    ///     case MouseUp:   if (drag.Release(e) == PointerDrag::Ended::Drag) Commit(drag.before, value);
    ///     case KeyDown:   return drag.Cancel(e, gsm, value);
    template <class Snapshot = NoSnapshot>
    struct Gesture
    {
        PointerDrag drag;       ///< the press itself
        Snapshot    before{};   ///< what the press changes, as it was at the press

        bool IsHeld() const noexcept               { return drag.IsHeld(); }
        bool IsPressed(ID element) const noexcept  { return drag.IsPressed(element); }
        bool IsDragging(ID element) const noexcept { return drag.IsDragging(element); }
        GesturePhase Phase() const noexcept
        {
            return !drag.IsHeld() ? GesturePhase::Idle : drag.dragging ? GesturePhase::Dragging : GesturePhase::Pressed;
        }

        /// Starts on `e` when it is the options' button going down: keeps `snapshot`, takes the
        /// focus and the capture as the options say. True then.
        bool Press(const ElementEvent& e, const ElementStorage& storage, GUIStateManager& gsm,
                   Snapshot snapshot = {}, const PressOptions& options = {})
        {
            if (!drag.Press(e, storage, options.button, options.atOnce))
                return false;
            before = std::move(snapshot);
            detail::TakePress(gsm, e.id, options);
            return true;
        }

        /// See PointerDrag::Move.
        bool Move(const ElementEvent& e, int32_t distance) { return drag.Move(e, distance); }
        /// See PointerDrag::Release. `before` stays for the commit.
        PointerDrag::Ended Release(const ElementEvent& e) { return drag.Release(e); }
        /// Drops any press and what it kept (the control was disabled under it, say).
        void Reset() { *this = Gesture{}; }

        /// Escape on the pressed element: drops the press and its capture. True then; the caller
        /// puts `before` back itself (several fields, say).
        bool Cancel(const ElementEvent& e, GUIStateManager& gsm)
        {
            if (!IsDragCancelKey(e.state) || !drag.Cancel(e.id))
                return false;
            detail::ReleaseCaptureOf(gsm, e.id);
            return true;
        }
        /// Escape on the pressed element: as Cancel, and `value` is put back as it was.
        bool Cancel(const ElementEvent& e, GUIStateManager& gsm, Snapshot& value)
        {
            if (!Cancel(e, gsm))
                return false;
            value = before;
            return true;
        }
    };

    /// The part of a drawn control under the pointer (a key, a handle, a clip's end), for its
    /// hover look. It follows the pointer's moves while no press is held, so the part being
    /// dragged stays lit; it's drawn only while the control's element is hovered.
    template <class Part>
    struct HoverPart
    {
        Part part{};    ///< under the pointer at the last move with nothing held

        /// On a move: unless a press is held, the part `hitTest()` finds under the pointer
        /// becomes the hovered one (the hit test isn't run while one is).
        template <class HitTest>
        void Track(bool held, HitTest&& hitTest) { if (!held) part = hitTest(); }
        /// The part to draw hovered: none while the element isn't.
        Part Shown(bool elementHovered) const { return elementHovered ? part : Part{}; }
        void Clear() { part = Part{}; }
    };

    namespace detail
    {
        inline bool HeldOf(const PointerDrag& d) noexcept { return d.IsHeld(); }
        template <class S> bool HeldOf(const Gesture<S>& g) noexcept { return g.IsHeld(); }
    }

    /// Whether any of a control's presses is held: its hover parts stay as they are then.
    template <class... Presses>
    bool AnyHeld(const Presses&... presses) noexcept { return (detail::HeldOf(presses) || ...); }

    /// @}
}
