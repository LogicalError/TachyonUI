#pragma once

#include <stdint.h>
#include <string_view>
#include <gui_core/gui_base_types.h>
#include <gui_core/gui_id.h>   // OnFileDrop's default body takes it by value

namespace tachyon
{
    /// @addtogroup input
    /// @{

    enum class KeyCode : uint32_t;
    enum class KeyModifier : uint8_t;
    enum class KeyAction : uint8_t;

    enum class MouseButtons : uint8_t;
    enum class MouseAction : uint8_t;
    enum class FileDropAction : uint8_t;
    struct PenState;

    /// Platform-level input interface: receives raw OS events (keyboard, mouse)
    /// and forwards them into the GUI input pipeline.
    class IPlatformInputHandler
    {
    public:
        virtual ~IPlatformInputHandler() = default;
        /// A key went down or up (`key` is its KeyCode, `repeat` nonzero for an auto-repeat), or
        /// a character was typed (KeyAction::KeyChar: `key` holds its Unicode codepoint).
        virtual void OnKeyboard(tachyon::ID id, KeyCode key, int repeat, KeyModifier modifiers, KeyAction action) = 0;
        /// The mouse moved over window `id`; screenPos in screen pixels, `buttons` those held down.
        virtual void OnMouseMove(tachyon::ID id, tachyon::int2 screenPos, tachyon::MouseButtons buttons, KeyModifier modifiers) = 0;
        /// A mouse button went down or up; `buttons` are those held down after the change.
        virtual void OnMouseButton(tachyon::ID id, tachyon::int2 screenPos, tachyon::MouseButtons buttons, KeyModifier modifiers, tachyon::MouseAction action) = 0;
        /// The wheel turned, by scrollOffset notches; positive y is away from the user (scrolls up).
        virtual void OnMouseScroll(tachyon::ID id, tachyon::int2 screenPos, tachyon::float2 scrollOffset, KeyModifier modifiers) = 0;
        /// IME composition (pre-edit) text, UTF-8, shown but not yet committed; empty
        /// when composing ends. `cursor` is the caret's codepoint offset within it.
        /// Committed text still arrives as KeyChar.
        virtual void OnTextComposition(tachyon::ID id, std::string_view text, int32_t cursor) = 0;
        /// A drag from another application over window `id`: it came, moved to screenPos (screen
        /// pixels), dropped a file or text (`data`), or ended (dropped, or left the window).
        virtual void OnFileDrop(tachyon::ID /*id*/, tachyon::int2 /*screenPos*/, FileDropAction /*action*/, std::string_view /*data*/) {}
        /// The pen changed (came near, touched, pressed harder, tilted, left). It comes before
        /// the pointer event it goes with; a default PenState means the mouse again.
        virtual void OnPen(tachyon::ID /*id*/, const PenState& /*pen*/) {}
        /// Two fingers pinched on a trackpad over window `id`: `scale` is how far they spread
        /// since the last call, above 1 apart.
        virtual void OnPinch(tachyon::ID /*id*/, float /*scale*/, KeyModifier /*modifiers*/) {}
    };
    /// @}
};
