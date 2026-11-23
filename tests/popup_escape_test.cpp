// TakeEscapeFor: a popup's trigger takes this frame's Escape when the keyboard is in the
// popup's window or on the trigger, and only once.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/state/gui_state_manager.h>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        class PopupEscapeTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            ID trigger{}, popup{};
            int taken = 0;

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                trigger = app.idManager.CreateStaticID();
                popup   = app.idManager.CreateStaticID();
            }
            void TearDown() override { app.Cleanup(); }

            // A frame that asks twice, as two popups on the same trigger would.
            void Frame()
            {
                taken = 0;
                app.Frame([&] {
                    app.BeginRootWindow();
                    taken += app.TakeEscapeFor(popup, trigger) ? 1 : 0;
                    taken += app.TakeEscapeFor(popup, trigger) ? 1 : 0;
                    app.EndRootWindow();
                });
            }
        };

        TEST_F(PopupEscapeTest, TheTriggerWithTheFocusTakesEscapeOnce)
        {
            app.guiStateManager.Interaction().SetFocusID(trigger);
            app.InjectKey(KeyCode::Escape);
            Frame();
            EXPECT_EQ(taken, 1) << "taken by the first ask; the key is gone after";
        }

        TEST_F(PopupEscapeTest, ThePopupsWindowWithTheKeyboardTakesEscape)
        {
            app.activeWindow = popup;
            Frame();   // the window with the keyboard is known from the end of a frame on
            app.InjectKey(KeyCode::Escape);
            Frame();
            EXPECT_EQ(taken, 1);
        }

        TEST_F(PopupEscapeTest, AnEscapeMeantForElsewhereIsLeft)
        {
            app.guiStateManager.Interaction().SetFocusID(ID::Invalid());
            app.InjectKey(KeyCode::Escape);
            Frame();
            EXPECT_EQ(taken, 0);
        }

        TEST_F(PopupEscapeTest, NoEscapeNothingTaken)
        {
            app.guiStateManager.Interaction().SetFocusID(trigger);
            app.InjectKey(KeyCode::Return);
            Frame();
            EXPECT_EQ(taken, 0);
        }
    }
}
