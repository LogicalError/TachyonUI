// Gesture and HoverPart: the shared interaction model over PointerDrag. A press takes the focus
// and the capture and keeps what the drag will change; Escape puts that back; the part of a
// drawn control under the pointer is tracked while nothing is held.

#include <gtest/gtest.h>

#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/input/gui_gesture.h>
#include <gui_core/state/gui_state_manager.h>
#include <gui_core/style/gui_gradient_manager.h>
#include <gui_core/style/gui_style_manager.h>

#include <deque>

namespace tachyon
{
    namespace
    {
        InputState Pointer(int2 pos, MouseButtons buttons, MouseButtons prevButtons)
        {
            InputState s{};
            s.screenPos   = pos;
            s.buttons     = buttons;
            s.prevButtons = prevButtons;
            return s;
        }

        class GestureTest : public ::testing::Test
        {
        protected:
            GradientManager gradientManager{ 16 };
            StyleManager    styleManager{ gradientManager };
            ElementStorage  storage{ styleManager };
            GUIStateManager gsm;
            ID              a = ID{ .value{ .integer = 21 } };
            ID              b = ID{ .value{ .integer = 22 } };

            std::deque<InputState> states;   // an event refers to its state: kept for the test's length

            ElementEvent Event(ID id, EventType type, const InputState& s)
            {
                states.push_back(s);
                return ElementEvent{ .id = id, .type = type, .state = states.back(), .rect = nullptr, .elementRef = ElementRef::Invalid() };
            }
            ElementEvent Down(ID id, int2 pos, MouseButtons button = MouseButtons::Left, MouseButtons alreadyHeld = MouseButtons::None)
            {
                return Event(id, EventType::MouseDown, Pointer(pos, button | alreadyHeld, alreadyHeld));
            }
            ElementEvent MoveTo(ID id, int2 pos, MouseButtons held = MouseButtons::Left)
            {
                return Event(id, EventType::MouseMove, Pointer(pos, held, held));
            }
            ElementEvent Up(ID id, int2 pos, MouseButtons button = MouseButtons::Left)
            {
                return Event(id, EventType::MouseUp, Pointer(pos, MouseButtons::None, button));
            }
            ElementEvent Escape(ID id)
            {
                InputState s{};
                s.lastKey       = KeyCode::Escape;
                s.lastKeyAction = KeyAction::KeyDown;
                return Event(id, EventType::KeyDown, s);
            }
        };

        // A press takes the focus and the capture and keeps the value as it was; the press
        // goes from held to dragging past the distance, and ends as a drag.
        TEST_F(GestureTest, APressTakesTheFocusAndTheCaptureAndKeepsTheValue)
        {
            gsm.TextEditorFocus().Set(b);
            Gesture<float> g;
            EXPECT_EQ(g.Phase(), GesturePhase::Idle);
            ASSERT_TRUE(g.Press(Down(a, { 10, 10 }), storage, gsm, 3.0f));
            EXPECT_EQ(gsm.Interaction().GetFocusedID(), a);
            EXPECT_EQ(gsm.Interaction().GetCaptureID(), a);
            EXPECT_EQ(gsm.TextEditorFocus().GetFocusedID(), ID::Invalid()) << "a text field loses its editor";
            EXPECT_EQ(g.before, 3.0f);
            EXPECT_EQ(g.Phase(), GesturePhase::Pressed);

            EXPECT_FALSE(g.Move(MoveTo(a, { 11, 10 }), 4));
            EXPECT_TRUE(g.Move(MoveTo(a, { 20, 10 }), 4));
            EXPECT_EQ(g.Phase(), GesturePhase::Dragging);
            EXPECT_TRUE(g.IsDragging(a));
            EXPECT_EQ(g.Release(Up(a, { 20, 10 })), PointerDrag::Ended::Drag);
            EXPECT_EQ(g.Phase(), GesturePhase::Idle);
            EXPECT_EQ(g.before, 3.0f) << "kept for the commit";
            g.Reset();
            EXPECT_EQ(g.before, 0.0f) << "a reset drops it";
        }

        // The options: another element takes the focus, or none does; no capture; another button,
        // a drag from the start.
        TEST_F(GestureTest, ThePressOptions)
        {
            Gesture<int> g;
            ASSERT_TRUE(g.Press(Down(a, { 0, 0 }), storage, gsm, 1, { .focusID = b }));
            EXPECT_EQ(gsm.Interaction().GetFocusedID(), b);
            g.drag.Reset();

            gsm.Interaction().SetFocusID(ID::Invalid());
            gsm.Interaction().ReleaseCaptureID();
            ASSERT_TRUE(g.Press(Down(a, { 0, 0 }, MouseButtons::Right), storage, gsm, 2,
                                { .button = MouseButtons::Right, .atOnce = true, .focus = false, .capture = false }));
            EXPECT_EQ(gsm.Interaction().GetFocusedID(), ID::Invalid());
            EXPECT_EQ(gsm.Interaction().GetCaptureID(), ID::Invalid());
            EXPECT_EQ(g.Phase(), GesturePhase::Dragging) << "at once";

            Gesture<int> other;
            EXPECT_FALSE(other.Press(Down(a, { 0, 0 }, MouseButtons::Right, MouseButtons::Left), storage, gsm, 3))
                << "the left button was already down: not its press";
            EXPECT_EQ(other.before, 0) << "nothing kept without a press";
        }

        // Escape on the pressed element puts the value back, drops the press and the capture;
        // on another element, or without a press, it does nothing.
        TEST_F(GestureTest, EscapePutsTheValueBack)
        {
            Gesture<float> g;
            float value = 3.0f;
            ASSERT_TRUE(g.Press(Down(a, { 0, 0 }), storage, gsm, value));
            value = 7.0f;
            EXPECT_FALSE(g.Cancel(Escape(b), gsm, value));
            EXPECT_EQ(value, 7.0f);
            EXPECT_TRUE(g.Cancel(Escape(a), gsm, value));
            EXPECT_EQ(value, 3.0f);
            EXPECT_FALSE(g.IsHeld());
            EXPECT_EQ(gsm.Interaction().GetCaptureID(), ID::Invalid());
            value = 9.0f;
            EXPECT_FALSE(g.Cancel(Escape(a), gsm, value)) << "no press left";
            EXPECT_EQ(value, 9.0f);
        }

        // A cancel leaves another element's capture alone.
        TEST_F(GestureTest, CancelKeepsAnotherCapture)
        {
            Gesture<int> g;
            int value = 0;
            ASSERT_TRUE(g.Press(Down(a, { 0, 0 }), storage, gsm, value, { .capture = false }));
            gsm.Interaction().SetCaptureID(b);
            EXPECT_TRUE(g.Cancel(Escape(a), gsm, value));
            EXPECT_EQ(gsm.Interaction().GetCaptureID(), b);
        }

        // The part under the pointer follows moves while nothing is held, and is shown only while
        // the element is hovered.
        TEST(HoverPartTest, TracksThePartWhileNothingIsHeld)
        {
            enum class Part : uint8_t { None, Body, Edge };
            HoverPart<Part> hover;
            int hitTests = 0;
            hover.Track(/*held*/ false, [&] { ++hitTests; return Part::Body; });
            EXPECT_EQ(hover.part, Part::Body);
            hover.Track(/*held*/ true, [&] { ++hitTests; return Part::Edge; });
            EXPECT_EQ(hover.part, Part::Body) << "kept while a drag holds";
            EXPECT_EQ(hitTests, 1) << "no hit test while held";
            EXPECT_EQ(hover.Shown(true), Part::Body);
            EXPECT_EQ(hover.Shown(false), Part::None) << "not over the element";
            hover.Clear();
            EXPECT_EQ(hover.part, Part::None);
        }

        TEST(HoverPartTest, AnyHeldLooksAtEveryPress)
        {
            PointerDrag x, y;
            Gesture<int> z;
            EXPECT_FALSE(AnyHeld(x, y, z));
            y.id = ID{ .value{ .integer = 5 } };
            EXPECT_TRUE(AnyHeld(x, y, z));
        }
    }
}
