#pragma once

#include <gui_core/gui_base_types.h>
#include <gui_core/gui_id.h>
#include <gui_core/input/gui_input_enums.h>

#include <string>
#include <vector>

namespace tachyon
{
    /// @addtogroup input
    /// @{

    /// What moves the pointer.
    enum class PointerType : uint8_t
    {
        Mouse,
        Pen      ///< a pen on a tablet or a pen display
    };

    /// A pen's axes, in the order PenState::SetAxis takes them.
    enum class PenAxis : uint8_t
    {
        Pressure,
        TiltX,
        TiltY,
        Distance,
        Rotation,
        Slider,
        TangentialPressure,
        Count
    };

    /// The pen, as it was at a pointer event. A pen also drives the pointer as a mouse: its tip
    /// is the left button. A mouse reads as pressure 1 while a button is held, 0 otherwise, so a
    /// brush works with either.
    struct PenState
    {
        PointerType pointer = PointerType::Mouse;
        bool    inProximity = false;   ///< near enough to the tablet to be tracked
        bool    touching = false;      ///< the tip (or eraser) on the tablet
        bool    eraser = false;        ///< the pen's eraser end is the one in use
        uint8_t buttons = 0;           ///< barrel buttons held: bit 0 is the first

        float  pressure = 0.0f;            ///< 0 to 1
        float2 tilt{};                     ///< degrees, -90 to 90: x leaning right, y leaning down
        float  distance = 0.0f;            ///< to the tablet, 0 to 1
        float  rotation = 0.0f;            ///< of the barrel, degrees -180 to 180, clockwise, 0 up
        float  slider = 0.0f;              ///< a finger wheel or slider (an airbrush pen), 0 to 1
        float  tangentialPressure = 0.0f;  ///< from squeezing the barrel, 0 to 1

        bool IsPen() const { return pointer == PointerType::Pen; }

        /// Sets one axis; PenAxis::Count changes nothing.
        void SetAxis(PenAxis axis, float value)
        {
            switch (axis)
            {
                case PenAxis::Pressure:           pressure = value; break;
                case PenAxis::TiltX:              tilt.x = value; break;
                case PenAxis::TiltY:              tilt.y = value; break;
                case PenAxis::Distance:           distance = value; break;
                case PenAxis::Rotation:           rotation = value; break;
                case PenAxis::Slider:             slider = value; break;
                case PenAxis::TangentialPressure: tangentialPressure = value; break;
                case PenAxis::Count:              break;
            }
        }
    };

    /// Snapshot of input state for a single recorded OS event.
    struct InputState
    {
        tachyon::int2 screenPos{0,0};
        tachyon::int2 prevScreenPos{0,0};
        tachyon::int2 screenDelta{0,0};

        tachyon::MouseButtons buttons = tachyon::MouseButtons::None;
        tachyon::MouseButtons prevButtons = tachyon::MouseButtons::None;

        tachyon::KeyModifier modifiers = tachyon::KeyModifier::None;

        tachyon::float2 scroll{}; ///< wheel notches (fractional on hi-res devices); label drags: pixels

        tachyon::KeyCode lastKey = tachyon::KeyCode::None;
        tachyon::KeyAction lastKeyAction = tachyon::KeyAction::KeyDown;

        /// Drag-and-drop source and payload; set for drag events.
        ///@{
        tachyon::ID dragSourceID = tachyon::ID::Invalid();
        uint32_t dragPayloadType = 0;
        uintptr_t dragPayloadValue = 0;
        ///@}

        /// Text typed with this event, or dropped (EventType::FileDrop); UTF-8.
        std::string textInput;
        /// EventType::FileDrop: the paths of the files dropped (UTF-8).
        std::vector<std::string> droppedFiles;

        /// Pointer events: the pen, or the mouse's pressure when a mouse moved the pointer.
        PenState pen;

        /// True when the event was already handled (e.g. Tab by tab navigation).
        bool consumed = false;

        bool IsPen() const { return pen.IsPen(); }

        bool LeftDown() const { return (buttons & tachyon::MouseButtons::Left) != tachyon::MouseButtons::None; }
        bool LeftPressed() const { return LeftDown() && ((prevButtons & tachyon::MouseButtons::Left) == tachyon::MouseButtons::None); }
        bool LeftReleased() const { return !LeftDown() && ((prevButtons & tachyon::MouseButtons::Left) != tachyon::MouseButtons::None); }

        bool RightDown() const { return (buttons & tachyon::MouseButtons::Right) != tachyon::MouseButtons::None; }
        bool RightPressed() const { return RightDown() && ((prevButtons & tachyon::MouseButtons::Right) == tachyon::MouseButtons::None); }
        bool RightReleased() const { return !RightDown() && ((prevButtons & tachyon::MouseButtons::Right) != tachyon::MouseButtons::None); }

        bool MiddleDown() const { return (buttons & tachyon::MouseButtons::Middle) != tachyon::MouseButtons::None; }
        bool MiddlePressed() const { return MiddleDown() && ((prevButtons & tachyon::MouseButtons::Middle) == tachyon::MouseButtons::None); }
        bool MiddleReleased() const { return !MiddleDown() && ((prevButtons & tachyon::MouseButtons::Middle) != tachyon::MouseButtons::None); }
    };

    /// A drag from another application, of files or text, over one of the app's windows.
    struct FileDrag
    {
        tachyon::ID   windowID    = tachyon::ID::Invalid();
        tachyon::int2 screenPos   {};
        bool          hasPosition = false;   ///< where it is is known from its first move on
    };

    /// Files or text dropped from another application.
    struct FileDrop
    {
        tachyon::ID              windowID = tachyon::ID::Invalid();
        tachyon::int2            screenPos {};
        std::vector<std::string> files;   ///< paths, UTF-8
        std::string              text;    ///< UTF-8; lines joined with '\n'
    };
    /// @}
}
