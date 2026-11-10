// Pen input: a pen drives the pointer as a mouse does, and its pressure, tilt, eraser end and
// the rest come with each pointer event in InputState::pen.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/input/gui_input_manager.h>

#include <vector>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        PenState Pen(float pressure, float2 tilt = {}, bool eraser = false)
        {
            PenState pen{ .pointer = PointerType::Pen, .inProximity = true, .touching = pressure > 0.0f,
                          .eraser = eraser };
            pen.pressure = pressure;
            pen.tilt     = tilt;
            return pen;
        }

        TEST(PenState, AxesSetTheirOwnValue)
        {
            PenState pen;
            pen.SetAxis(PenAxis::Pressure, 0.25f);
            pen.SetAxis(PenAxis::TiltX, -30.0f);
            pen.SetAxis(PenAxis::TiltY, 45.0f);
            pen.SetAxis(PenAxis::Distance, 0.5f);
            pen.SetAxis(PenAxis::Rotation, 90.0f);
            pen.SetAxis(PenAxis::Slider, 0.75f);
            pen.SetAxis(PenAxis::TangentialPressure, 0.125f);
            EXPECT_EQ(pen.pressure, 0.25f);
            EXPECT_EQ(pen.tilt, (float2{ -30.0f, 45.0f }));
            EXPECT_EQ(pen.distance, 0.5f);
            EXPECT_EQ(pen.rotation, 90.0f);
            EXPECT_EQ(pen.slider, 0.75f);
            EXPECT_EQ(pen.tangentialPressure, 0.125f);

            pen.SetAxis(PenAxis::Count, 9.0f);
            EXPECT_EQ(pen.pressure, 0.25f) << "an unknown axis changes nothing";
            EXPECT_EQ(pen.slider, 0.75f);
        }

        TEST(PenInput, AMouseIsFullPressureWhileAButtonIsHeld)
        {
            InputManager input;
            input.OnMouseMove(ID::Invalid(), { 1, 1 }, MouseButtons::None, KeyModifier::None);
            input.OnMouseButton(ID::Invalid(), { 1, 1 }, MouseButtons::Right, KeyModifier::None, MouseAction::MouseDown);
            input.OnMouseMove(ID::Invalid(), { 2, 1 }, MouseButtons::Right, KeyModifier::None);
            input.OnMouseButton(ID::Invalid(), { 2, 1 }, MouseButtons::None, KeyModifier::None, MouseAction::MouseUp);

            const auto& events = input.GetRecordedEvents();
            ASSERT_EQ(events.size(), 4u);
            for (const auto& e : events)
            {
                EXPECT_EQ(e.state.pen.pointer, PointerType::Mouse);
                EXPECT_FALSE(e.state.IsPen());
                EXPECT_FALSE(e.state.pen.eraser);
            }
            EXPECT_EQ(events[0].state.pen.pressure, 0.0f);
            EXPECT_EQ(events[1].state.pen.pressure, 1.0f);
            EXPECT_EQ(events[2].state.pen.pressure, 1.0f);
            EXPECT_EQ(events[3].state.pen.pressure, 0.0f) << "released";
        }

        TEST(PenInput, PointerEventsCarryThePenAsItWas)
        {
            InputManager input;
            input.OnPen(ID::Invalid(), Pen(0.0f, { 10.0f, -5.0f }));
            input.OnMouseMove(ID::Invalid(), { 5, 5 }, MouseButtons::None, KeyModifier::None);
            input.OnPen(ID::Invalid(), Pen(0.3f, { 12.0f, -4.0f }));
            input.OnMouseButton(ID::Invalid(), { 5, 5 }, MouseButtons::Left, KeyModifier::None, MouseAction::MouseDown);
            input.OnPen(ID::Invalid(), Pen(0.8f, { 15.0f, 0.0f }));
            input.OnMouseMove(ID::Invalid(), { 9, 5 }, MouseButtons::Left, KeyModifier::None);
            input.OnMouseScroll(ID::Invalid(), { 9, 5 }, { 0.0f, 1.0f }, KeyModifier::None);

            const auto& events = input.GetRecordedEvents();
            ASSERT_EQ(events.size(), 4u) << "pen changes alone record no event";
            EXPECT_TRUE(events[0].state.IsPen());
            EXPECT_TRUE(events[0].state.pen.inProximity);
            EXPECT_FALSE(events[0].state.pen.touching) << "hovering";
            EXPECT_EQ(events[0].state.pen.pressure, 0.0f) << "hovering";
            EXPECT_EQ(events[0].state.pen.tilt, (float2{ 10.0f, -5.0f }));
            EXPECT_EQ(events[1].state.pen.pressure, 0.3f);
            EXPECT_TRUE(events[1].state.pen.touching);
            EXPECT_EQ(events[2].state.pen.pressure, 0.8f) << "its own pressure, not a mouse's 1";
            EXPECT_EQ(events[2].state.pen.tilt, (float2{ 15.0f, 0.0f }));
            EXPECT_TRUE(events[3].state.IsPen()) << "a scroll too";

            EXPECT_EQ(input.GetPen().pressure, 0.8f) << "the latest, between events";
        }

        TEST(PenInput, BackToTheMouseWhenThePenLeaves)
        {
            InputManager input;
            input.OnPen(ID::Invalid(), Pen(0.5f, {}, /*eraser*/ true));
            input.OnMouseMove(ID::Invalid(), { 5, 5 }, MouseButtons::Left, KeyModifier::None);
            input.OnPen(ID::Invalid(), PenState{});
            input.OnMouseMove(ID::Invalid(), { 6, 5 }, MouseButtons::None, KeyModifier::None);

            const auto& events = input.GetRecordedEvents();
            ASSERT_EQ(events.size(), 2u);
            EXPECT_TRUE(events[0].state.pen.eraser);
            EXPECT_FALSE(events[1].state.IsPen());
            EXPECT_FALSE(events[1].state.pen.eraser);
            EXPECT_FALSE(events[1].state.pen.inProximity);
        }

        TEST(PenInput, APenStrokeIsNotEndedByTheMouseButtonsBeingUp)
        {
            // The OS may not report a pen's tip as a held mouse button.
            InputManager input;
            input.OnPen(ID::Invalid(), Pen(0.5f));
            input.OnMouseButton(ID::Invalid(), { 5, 5 }, MouseButtons::Left, KeyModifier::None, MouseAction::MouseDown);
            input.SynthesizeMissingMouseEvents({ 5, 5 }, MouseButtons::None, MouseButtons::None);
            ASSERT_EQ(input.GetRecordedEvents().size(), 1u) << "no made-up release";
            EXPECT_EQ(input.GetCurrentButtons(), MouseButtons::Left);

            input.OnPen(ID::Invalid(), PenState{});
            input.SynthesizeMissingMouseEvents({ 5, 5 }, MouseButtons::None, MouseButtons::None);
            ASSERT_EQ(input.GetRecordedEvents().size(), 2u) << "the mouse's lost release is still made up";
            EXPECT_EQ(input.GetRecordedEvents()[1].type, EventType::MouseUp);
        }

        TEST(PenInput, AnElementSeesThePenWhileDragging)
        {
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            const ID padID = app.idManager.CreateStaticID();
            std::vector<PenState> seen;
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    Element pad{ .id = padID, .styleRef = app.theme.emptyVertStyleRef };
                    pad.eventHandler = [&](ElementEvent&& event) -> bool {
                        if (event.type == EventType::MouseDown || event.type == EventType::MouseMove)
                            seen.push_back(event.state.pen);
                        return event.type == EventType::MouseDown || event.type == EventType::MouseMove;
                    };
                    app.RegisterElement(std::move(pad), LayoutOverride{}.Size(300, 200));
                    app.EndRootWindow();
                });
            };
            frame();
            const int2 at = app.CenterOf(padID);

            app.InjectPen(Pen(0.0f));
            app.InjectMouseMove(at);
            app.InjectPen(Pen(0.2f, { 0.0f, 30.0f }));
            app.InjectMouseDown(at);
            app.InjectPen(Pen(0.6f, { 0.0f, 35.0f }, /*eraser*/ true));
            app.InjectMouseMove(at + int2{ 20, 0 }, MouseButtons::Left);
            frame();

            ASSERT_GE(seen.size(), 3u);
            EXPECT_EQ(seen[seen.size() - 2].pressure, 0.2f);
            EXPECT_EQ(seen.back().pressure, 0.6f);
            EXPECT_EQ(seen.back().tilt, (float2{ 0.0f, 35.0f }));
            EXPECT_TRUE(seen.back().eraser);
            EXPECT_EQ(seen.back().pointer, PointerType::Pen);

            app.InjectMouseUp(at + int2{ 20, 0 });
            frame();
        }
    }
}
