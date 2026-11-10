// The pointer's way round a canvas that pans and zooms: the drag button, the wheel and a pinch.

#include <gtest/gtest.h>

#include <gui_core/input/gui_input_manager.h>
#include <gui_core/input/gui_navigation.h>

namespace tachyon
{
    namespace
    {
        InputState Wheel(float2 scroll, KeyModifier mods = KeyModifier::None)
        {
            InputState s;
            s.scroll    = scroll;
            s.modifiers = mods;
            return s;
        }

        TEST(Navigation, WithZoomTheWheelZooms)
        {
            const WheelNavigation in = NavigateByWheel(Wheel({ 0, 1 }), WheelMode::Zoom, 1.25f);
            EXPECT_FLOAT_EQ(in.zoom, 1.25f) << "away from the user zooms in";
            EXPECT_EQ(in.pan, (float2{ 0, 0 }));
            EXPECT_FLOAT_EQ(NavigateByWheel(Wheel({ 0, -2 }), WheelMode::Zoom, 1.25f).zoom, 1.0f / (1.25f * 1.25f));
            EXPECT_FLOAT_EQ(NavigateByWheel(Wheel({ 0, 1 }, KeyModifier::Control), WheelMode::Zoom, 1.25f).zoom, 1.25f)
                << "Ctrl changes nothing";

            const WheelNavigation side = NavigateByWheel(Wheel({ 0, -1 }, KeyModifier::Shift), WheelMode::Zoom, 1.25f);
            EXPECT_EQ(side.zoom, 1.0f);
            EXPECT_EQ(side.pan, (float2{ 1, 0 })) << "Shift scrolls sideways: toward the user is right";
            EXPECT_EQ(NavigateByWheel(Wheel({ 2, 0 }), WheelMode::Zoom, 1.25f).pan, (float2{ 2, 0 })) << "a sideways scroll scrolls";
        }

        TEST(Navigation, WithPanTheWheelScrolls)
        {
            const WheelNavigation up = NavigateByWheel(Wheel({ 0, 1 }), WheelMode::Pan, 1.25f);
            EXPECT_EQ(up.zoom, 1.0f);
            EXPECT_EQ(up.pan, (float2{ 0, -1 })) << "away from the user scrolls up";
            EXPECT_EQ(NavigateByWheel(Wheel({ 1.5f, -0.5f }), WheelMode::Pan, 1.25f).pan, (float2{ 1.5f, 0.5f })) << "a trackpad both ways";
            EXPECT_EQ(NavigateByWheel(Wheel({ 0, 1 }, KeyModifier::Shift), WheelMode::Pan, 1.25f).pan, (float2{ -1, 0 }));

            const WheelNavigation zoom = NavigateByWheel(Wheel({ 0, 1 }, KeyModifier::Control), WheelMode::Pan, 1.25f);
            EXPECT_FLOAT_EQ(zoom.zoom, 1.25f) << "Ctrl zooms";
            EXPECT_EQ(zoom.pan, (float2{ 0, 0 }));
            EXPECT_FALSE(NavigateByWheel(Wheel({ 0, 0 }), WheelMode::Pan, 1.25f).Any());
        }

        TEST(Navigation, APinchZoomsInEitherMode)
        {
            InputState s;
            s.pinch = 1.1f;
            for (const WheelMode mode : { WheelMode::Zoom, WheelMode::Pan })
            {
                const WheelNavigation nav = NavigateByWheel(s, mode, 1.25f);
                EXPECT_FLOAT_EQ(nav.zoom, 1.1f);
                EXPECT_EQ(nav.pan, (float2{ 0, 0 }));
            }
            s.pinch = 0.0f;
            EXPECT_FALSE(NavigateByWheel(s, WheelMode::Zoom, 1.25f).Any()) << "a broken pinch does nothing";
        }

        TEST(Navigation, TheMiddleButtonOrAltLeftDragsTheView)
        {
            InputState s;
            s.buttons = MouseButtons::Middle;
            EXPECT_EQ(PanDragButton(s), MouseButtons::Middle);
            s.buttons = MouseButtons::Left;
            EXPECT_EQ(PanDragButton(s), MouseButtons::None);
            s.modifiers = KeyModifier::Alt;
            EXPECT_EQ(PanDragButton(s), MouseButtons::Left) << "Alt + left, without a middle button";
            s.buttons = MouseButtons::Right;
            EXPECT_EQ(PanDragButton(s), MouseButtons::None) << "not the right button";
            s.buttons = MouseButtons::Left;
            s.prevButtons = MouseButtons::Left;
            EXPECT_EQ(PanDragButton(s), MouseButtons::None) << "only as it goes down";
        }

        TEST(Navigation, APinchIsRecordedAsAZoom)
        {
            InputManager input;
            input.OnMouseMove(ID::Invalid(), { 30, 40 }, MouseButtons::None, KeyModifier::None);
            input.OnPinch(ID::Invalid(), 1.2f, KeyModifier::None);
            const auto& events = input.GetRecordedEvents();
            ASSERT_EQ(events.size(), 2u);
            EXPECT_EQ(events[1].type, EventType::MouseScroll);
            EXPECT_FLOAT_EQ(events[1].state.pinch, 1.2f);
            EXPECT_EQ(events[1].state.scroll, (float2{ 0, 0 }));
            EXPECT_EQ(events[1].state.screenPos, (int2{ 30, 40 })) << "where the pointer is";
            EXPECT_EQ(events[0].state.pinch, 1.0f) << "other events don't pinch";
        }
    }
}
