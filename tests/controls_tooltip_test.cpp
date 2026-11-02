// Tests for the TooltipManager hover delay and SetTooltip / BeginTooltip /
// BeginContextMenu. AdvanceTime() steps the virtual clock past delaySeconds.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/gui_id.h>
#include <gui_core/gui_id_manager.h>
#include <gui_core/state/gui_state_manager.h>
#include <gui_controls/gui_tooltip_manager.h>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {

        class ControlsTooltipTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            void SetUp()    override { ASSERT_TRUE(app.Initialize()); }
            void TearDown() override { app.Cleanup(); }
        };

        // ---------------------------------------------------------------------
        // TooltipManager direct unit tests (no widget construction)
        // ---------------------------------------------------------------------

        TEST_F(ControlsTooltipTest, ShouldShowFalseUntilDelayElapses)
        {
            TooltipManager tm;
            tm.delaySeconds = 0.5f;

            ID target{}; target.value.dynamicParts.mode = IDMode::Static;
            target.value.dynamicParts.hashValue = 7;

            tm.BeginFrame(target, ID::Invalid(), 0.0f, true, false, false, false, false);
            EXPECT_FALSE(tm.ShouldShow(target));

            // 0.4s - still under threshold.
            tm.BeginFrame(target, ID::Invalid(), 0.4f, false, false, false, false, false);
            EXPECT_FALSE(tm.ShouldShow(target));

            // 0.6s - past threshold, should show now.
            tm.BeginFrame(target, ID::Invalid(), 0.6f, false, false, false, false, false);
            EXPECT_TRUE(tm.ShouldShow(target));
        }

        // Moving on from a shown tooltip to another target shows the next one at once.
        TEST_F(ControlsTooltipTest, MovingOnFromAShownTooltipShowsTheNextAtOnce)
        {
            TooltipManager tm;
            ID first{}; first.value.dynamicParts.hashValue = 1;
            ID second{}; second.value.dynamicParts.hashValue = 2;

            tm.BeginFrame(first, ID::Invalid(), 0.0f, true, false, false, false, false);
            tm.BeginFrame(first, ID::Invalid(), 0.6f, false, false, false, false, false);
            ASSERT_TRUE(tm.ShouldShow(first));
            tm.EndFrame();

            tm.BeginFrame(second, ID::Invalid(), 0.7f, true, false, false, false, false);
            EXPECT_TRUE(tm.ShouldShow(second)) << "no new delay";
        }

        // Some time after the last tooltip, the next waits the delay again.
        TEST_F(ControlsTooltipTest, AfterAPauseTheDelayStartsAgain)
        {
            TooltipManager tm;
            ID first{}; first.value.dynamicParts.hashValue = 1;
            ID second{}; second.value.dynamicParts.hashValue = 2;

            tm.BeginFrame(first, ID::Invalid(), 0.0f, true, false, false, false, false);
            tm.BeginFrame(first, ID::Invalid(), 0.6f, false, false, false, false, false);
            ASSERT_TRUE(tm.ShouldShow(first));
            tm.EndFrame();
            tm.BeginFrame(ID::Invalid(), ID::Invalid(), 0.7f, true, false, false, false, false);
            tm.EndFrame();

            tm.BeginFrame(second, ID::Invalid(), 1.5f, true, false, false, false, false);
            EXPECT_FALSE(tm.ShouldShow(second)) << "0.8 s since the last: the delay again";
            tm.BeginFrame(second, ID::Invalid(), 2.1f, false, false, false, false, false);
            EXPECT_TRUE(tm.ShouldShow(second));
        }

        // A tooltip stays 100 ms after its target is left, then goes.
        TEST_F(ControlsTooltipTest, ATooltipStaysAMomentAfterItsTargetIsLeft)
        {
            TooltipManager tm;
            ID first{}; first.value.dynamicParts.hashValue = 1;
            tm.BeginFrame(first, ID::Invalid(), 0.0f, true, false, false, false, false);
            tm.BeginFrame(first, ID::Invalid(), 0.6f, false, false, false, false, false);
            ASSERT_TRUE(tm.ShouldShow(first));
            tm.EndFrame();

            tm.BeginFrame(ID::Invalid(), ID::Invalid(), 0.65f, true, false, false, false, false);
            EXPECT_TRUE(tm.ShouldShow(first)) << "50 ms after";
            tm.EndFrame();
            tm.BeginFrame(ID::Invalid(), ID::Invalid(), 0.8f, false, false, false, false, false);
            EXPECT_FALSE(tm.ShouldShow(first)) << "past 100 ms";
        }

        // The one left gives way to the next target's own tooltip, whatever order they're
        // declared in.
        TEST_F(ControlsTooltipTest, TheOneLeftGivesWayToTheNext)
        {
            TooltipManager tm;
            ID first{}; first.value.dynamicParts.hashValue = 1;
            ID second{}; second.value.dynamicParts.hashValue = 2;
            tm.BeginFrame(first, ID::Invalid(), 0.0f, true, false, false, false, false);
            tm.BeginFrame(first, ID::Invalid(), 0.6f, false, false, false, false, false);
            ASSERT_TRUE(tm.ShouldShow(first));
            tm.EndFrame();

            // The first's declared before the second's: it keeps this frame, the next is the second's.
            tm.BeginFrame(second, ID::Invalid(), 0.62f, true, false, false, false, false);
            EXPECT_TRUE(tm.ShouldShow(first));
            EXPECT_FALSE(tm.ShouldShow(second));
            tm.EndFrame();
            tm.BeginFrame(second, ID::Invalid(), 0.64f, false, false, false, false, false);
            EXPECT_FALSE(tm.ShouldShow(first));
            EXPECT_TRUE(tm.ShouldShow(second));
        }

        // The same over the gap between two buttons: the next one's shows, not the first's.
        TEST_F(ControlsTooltipTest, TheOneLeftGivesWayAcrossAGap)
        {
            TooltipManager tm;
            ID first{}; first.value.dynamicParts.hashValue = 1;
            ID gap{}; gap.value.dynamicParts.hashValue = 3;
            ID second{}; second.value.dynamicParts.hashValue = 2;
            tm.BeginFrame(first, ID::Invalid(), 0.0f, true, false, false, false, false);
            tm.BeginFrame(first, ID::Invalid(), 0.6f, false, false, false, false, false);
            ASSERT_TRUE(tm.ShouldShow(first));
            tm.EndFrame();

            tm.BeginFrame(gap, ID::Invalid(), 0.63f, true, false, false, false, false);
            EXPECT_TRUE(tm.ShouldShow(first)) << "a moment more";
            tm.EndFrame();
            float t = 0.66f;
            tm.BeginFrame(second, ID::Invalid(), t, true, false, false, false, false);
            (void)tm.ShouldShow(first);
            (void)tm.ShouldShow(second);
            tm.EndFrame();
            for (int i = 0; i < 10; ++i)
            {
                t += 0.03f;
                tm.BeginFrame(second, ID::Invalid(), t, false, false, false, false, false);
                EXPECT_FALSE(tm.ShouldShow(first)) << t;
                EXPECT_TRUE(tm.ShouldShow(second)) << t;
                tm.EndFrame();
            }
        }

        // None while a mouse button is held: a drag over targets shows nothing.
        TEST_F(ControlsTooltipTest, NoneWhileAButtonIsHeld)
        {
            TooltipManager tm;
            ID target{}; target.value.dynamicParts.hashValue = 5;
            tm.BeginFrame(target, ID::Invalid(), 0.0f, true, false, false, false, false, true);
            tm.BeginFrame(target, ID::Invalid(), 0.6f, true, false, false, false, false, true);
            EXPECT_FALSE(tm.ShouldShow(target));
            tm.BeginFrame(target, ID::Invalid(), 1.2f, true, false, false, false, false, true);
            EXPECT_FALSE(tm.ShouldShow(target));

            // Released: the delay from then.
            tm.BeginFrame(target, ID::Invalid(), 1.3f, true, false, false, false, false, false);
            EXPECT_FALSE(tm.ShouldShow(target));
            tm.BeginFrame(target, ID::Invalid(), 1.9f, false, false, false, false, false, false);
            EXPECT_TRUE(tm.ShouldShow(target));
        }

        // A command's tooltip: its description, and its shortcut in a chip.
        TEST_F(ControlsTooltipTest, ACommandsShortcutIsInAChip)
        {
            int ran = 0;
            const CommandID save = app.commandManager.RegisterCommand(KeyCode::KeyS, KeyModifier::Control, [&] { ++ran; }, "Save scene");
            ID button = ID::Invalid();
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.CommandButton(save);
                    button = app.elementStorage.GetLastItemID();
                    app.EndRootWindow();
                });
            };
            frame();
            app.InjectMouseMove(app.CenterOf(button));
            for (int i = 0; i < 8; ++i) { app.AdvanceTime(100); frame(); }
            EXPECT_NE(app.FindByText("Save scene"), ID::Invalid());
            const ID chip = app.FindByText(app.commandManager.GetCommand(save)->displayString);
            ASSERT_NE(chip, ID::Invalid()) << "the shortcut, on its own";
            EXPECT_GT(app.GetElementRect(chip).x, app.GetElementRect(app.FindByText("Save scene")).x) << "after the text";
        }

        // A button `top` px down the screen (and `left` px across), hovered until its tooltip
        // shows: the two rects.
        std::pair<IntRect, IntRect> TooltipAndTarget(HeadlessGUIApp& app, int top, int left = 0,
                                                     std::string_view text = "Save scene")
        {
            ID button = ID::Invalid();
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.BeginVertical(app.idManager.AllocateDynamicID("column"), app.theme.emptyVertStyleRef);
                    app.BeginVertical(app.idManager.AllocateDynamicID("spacer"), app.theme.emptyVertStyleRef,
                                      LayoutOverride{}.Height(top));
                    app.EndVertical();
                    app.BeginHorizontal(app.idManager.AllocateDynamicID("row"), app.theme.emptyHorzStyleRef);
                    app.BeginHorizontal(app.idManager.AllocateDynamicID("indent"), app.theme.emptyHorzStyleRef,
                                        LayoutOverride{}.Width(left));
                    app.EndHorizontal();
                    app.Button("Save", [] {});
                    button = app.elementStorage.GetLastItemID();
                    app.SetTooltip(text);
                    app.EndHorizontal();
                    app.EndVertical();
                    app.EndRootWindow();
                });
            };
            frame();
            app.InjectMouseMove(app.CenterOf(button));
            for (int i = 0; i < 8; ++i) { app.AdvanceTime(100); frame(); }
            if (!app.HasElement(app.tooltipWindowID))
                return {};
            return { app.GetElementRect(app.tooltipWindowID), app.GetElementRect(button) };
        }

        TEST_F(ControlsTooltipTest, BelowATargetWithAGap)
        {
            const auto [tip, target] = TooltipAndTarget(app, 0);
            ASSERT_GT(tip.h, 0);
            EXPECT_EQ(tip.y - target.Max(1), 2);
        }

        // At the bottom of the screen it goes above, the same gap away.
        TEST_F(ControlsTooltipTest, AboveATargetItKeepsTheSameGap)
        {
            const auto [tip, target] = TooltipAndTarget(app, 1040);
            ASSERT_GT(tip.h, 0);
            EXPECT_EQ(target.y - tip.Max(1), 2);
        }

        // At the right edge of the screen it lines up with its target's right edge instead.
        TEST_F(ControlsTooltipTest, AtTheRightEdgeItStaysOnScreen)
        {
            const auto [tip, target] = TooltipAndTarget(app, 0, 1840, "A tooltip far too long to fit beside the right edge");
            ASSERT_GT(tip.w, 0);
            EXPECT_LE(tip.Max(0), 1920);
            EXPECT_EQ(tip.Max(0), target.Max(0));
        }

        TEST_F(ControlsTooltipTest, MousePressDuringHoverDismissesTooltip)
        {
            TooltipManager tm;
            tm.delaySeconds = 0.5f;
            ID target{}; target.value.dynamicParts.hashValue = 3;

            tm.BeginFrame(target, ID::Invalid(), 0.0f, true, false, false, false, false);
            tm.BeginFrame(target, ID::Invalid(), 0.6f, false, false, false, false, false);
            ASSERT_TRUE(tm.ShouldShow(target));

            // hasMousePress = true -> dismiss.
            tm.BeginFrame(target, ID::Invalid(), 0.7f, false, true, false, false, false);
            EXPECT_FALSE(tm.ShouldShow(target));
        }

        TEST_F(ControlsTooltipTest, KeyboardActivityDismissesTooltip)
        {
            TooltipManager tm;
            tm.delaySeconds = 0.5f;
            ID target{}; target.value.dynamicParts.hashValue = 4;

            tm.BeginFrame(target, ID::Invalid(), 0.0f, true, false, false, false, false);
            tm.BeginFrame(target, ID::Invalid(), 0.6f, false, false, false, false, false);
            ASSERT_TRUE(tm.ShouldShow(target));

            tm.BeginFrame(target, ID::Invalid(), 0.7f, false, false, true, false, false);
            EXPECT_FALSE(tm.ShouldShow(target));
        }

        TEST_F(ControlsTooltipTest, OpenMenuKillsTooltip)
        {
            TooltipManager tm;
            tm.delaySeconds = 0.5f;
            ID target{}; target.value.dynamicParts.hashValue = 5;

            tm.BeginFrame(target, ID::Invalid(), 0.0f, true, false, false, false, false);
            tm.BeginFrame(target, ID::Invalid(), 0.6f, false, false, false, false, false);
            ASSERT_TRUE(tm.ShouldShow(target));

            // menuIsOpen = true.
            tm.BeginFrame(target, ID::Invalid(), 0.7f, false, false, false, false, true);
            EXPECT_FALSE(tm.ShouldShow(target));
        }

        TEST_F(ControlsTooltipTest, KeyboardFocusChangeShowsTooltipImmediately)
        {
            // When focus moves to a new element via keyboard (no mouse press),
            // the tooltip for that element shows immediately - no hover delay.
            TooltipManager tm;
            tm.delaySeconds = 0.5f;
            ID focused{}; focused.value.dynamicParts.hashValue = 9;

            tm.BeginFrame(ID::Invalid(), focused, 0.0f, false, false, false, false, false);
            EXPECT_TRUE(tm.ShouldShow(focused));
        }

        // A click moves focus during that frame's dispatch, which runs after
        // BeginFrame; the next frame sees the focus change with no press. That
        // is still a mouse focus change and must not pop the tooltip instantly.
        TEST_F(ControlsTooltipTest, FocusChangeFromAClickDoesNotShowTooltipInstantly)
        {
            TooltipManager tm;
            tm.delaySeconds = 0.5f;
            ID a{}; a.value.dynamicParts.hashValue = 21;
            ID b{}; b.value.dynamicParts.hashValue = 22;

            tm.BeginFrame(b, a, 0.00f, true,  false, false, false, false);   // hover B, A focused
            tm.BeginFrame(b, a, 0.02f, false, true,  false, false, false);   // press on B
            tm.BeginFrame(b, b, 0.04f, false, false, false, false, false);   // focus moved to B
            EXPECT_FALSE(tm.ShouldShow(b));
        }

        TEST_F(ControlsTooltipTest, EndFrameHidesTooltipNotDeclaredThisFrame)
        {
            TooltipManager tm;
            tm.delaySeconds = 0.0f;
            ID target{}; target.value.dynamicParts.hashValue = 6;

            // Two BeginFrames: the first sets the target (resetting isReady), the
            // second sees the elapsed delay.
            tm.BeginFrame(target, ID::Invalid(), 0.0f, true, false, false, false, false);
            tm.BeginFrame(target, ID::Invalid(), 0.0f, false, false, false, false, false);
            ASSERT_TRUE(tm.ShouldShow(target));
            tm.EndFrame();
            EXPECT_TRUE(tm.IsVisible());

            // Next frame: hover persists but caller never calls ShouldShow.
            tm.BeginFrame(target, ID::Invalid(), 0.1f, false, false, false, false, false);
            tm.EndFrame();
            EXPECT_FALSE(tm.IsVisible());
        }

        // ---------------------------------------------------------------------
        // Integration: hover an element with SetTooltip across frames.
        // ---------------------------------------------------------------------

        TEST_F(ControlsTooltipTest, SetTooltipDoesNothingBeforeDelayElapses)
        {
            ID buttonID = app.idManager.CreateStaticID();
            auto draw = [&] {
                app.BeginRootWindow();
                app.Button(buttonID, "B", [] {});
                app.SetTooltip(buttonID, "Help text");
                app.EndRootWindow();
            };
            app.Frame(draw);

            // Hover the button but advance only 0.1s.
            app.InjectMouseMove(app.CenterOf(buttonID));
            app.AdvanceTime(100);
            app.Frame(draw);

            EXPECT_FALSE(app.tooltipManager.IsVisible());
        }

        TEST_F(ControlsTooltipTest, SetTooltipShowsAfterHoverDelayElapses)
        {
            ID buttonID = app.idManager.CreateStaticID();
            auto draw = [&] {
                app.BeginRootWindow();
                app.Button(buttonID, "B", [] {});
                app.SetTooltip(buttonID, "Help text");
                app.EndRootWindow();
            };
            // Frame 1: layout the button.
            app.Frame(draw);

            // Frame 2: cursor moves over the button; dispatch sets hoveredID.
            app.InjectMouseMove(app.CenterOf(buttonID));
            app.Frame(draw);

            // Frame 3: tooltipManager sees the new hoveredID and seeds startTime.
            app.Frame(draw);

            // Advance past delaySeconds and run a frame with no input.
            app.AdvanceTime(700);
            app.Frame(draw);

            EXPECT_TRUE(app.tooltipManager.IsVisible());
        }

        // A control's content carries its tooltip (#19); content without one shows none.
        TEST_F(ControlsTooltipTest, AContentTooltipShowsAfterHoverDelay)
        {
            ID withID    = app.idManager.CreateStaticID();
            ID withoutID = app.idManager.CreateStaticID();
            auto draw = [&] {
                app.BeginRootWindow();
                app.Button(withID, { IconID::Invalid, "B", "Help text" }, [] {});
                app.Button(withoutID, "C", [] {});
                app.EndRootWindow();
            };
            auto hover = [&](ID id) {
                app.InjectMouseMove(app.CenterOf(id));
                app.Frame(draw);
                app.Frame(draw);
                app.AdvanceTime(700);
                app.Frame(draw);
            };
            app.Frame(draw);
            hover(withID);
            EXPECT_TRUE(app.tooltipManager.IsVisible());
            hover(withoutID);
            EXPECT_FALSE(app.tooltipManager.IsVisible());
        }

        // GUIContent converts from a string or an icon, and from both with braces.
        TEST_F(ControlsTooltipTest, ContentConvertsFromTextAndIcon)
        {
            const GUIContent fromText = "Save";
            const GUIContent fromIcon = IconID(7);
            const GUIContent both     = { IconID(7), "Save", "Write the file" };
            EXPECT_EQ(fromText.text, "Save");
            EXPECT_EQ(fromText.icon, IconID::Invalid);
            EXPECT_EQ(fromIcon.icon, IconID(7));
            EXPECT_TRUE(fromIcon.text.empty());
            EXPECT_EQ(both.tooltip, "Write the file");
        }

        TEST_F(ControlsTooltipTest, MovingMouseAwayHidesTooltip)
        {
            ID buttonID = app.idManager.CreateStaticID();
            auto draw = [&] {
                app.BeginRootWindow();
                app.Button(buttonID, "B", [] {});
                app.SetTooltip(buttonID, "Help text");
                app.EndRootWindow();
            };
            app.Frame(draw);
            app.InjectMouseMove(app.CenterOf(buttonID));
            app.Frame(draw);
            app.Frame(draw);  // seed startTime
            app.AdvanceTime(700);
            app.Frame(draw);
            ASSERT_TRUE(app.tooltipManager.IsVisible());

            // Moving off the button resets the tooltip.
            app.InjectMouseMove({ 4, 4 });
            app.Frame(draw);   // settles hoveredID
            app.Frame(draw);   // tooltipManager observes new hovered

            EXPECT_FALSE(app.tooltipManager.IsVisible());
        }

        // ---------------------------------------------------------------------
        // A tooltip or context menu set right after a control is on the whole control.
        // ---------------------------------------------------------------------

        // Hovers `pos` long enough for a tooltip.
        void HoverFor(HeadlessGUIApp& app, int2 pos, const std::function<void()>& draw)
        {
            app.InjectMouseMove(pos);
            app.Frame(draw);
            app.Frame(draw);
            app.AdvanceTime(700);
            app.Frame(draw);
        }

        // The checkbox's label is drawn last; the tooltip still goes on the checkbox.
        TEST_F(ControlsTooltipTest, SetTooltipAfterACheckboxIsOnTheCheckbox)
        {
            const ID boxID = app.idManager.CreateStaticID();
            ID item = ID::Invalid();
            auto draw = [&] {
                app.BeginRootWindow();
                app.Checkbox(boxID, false, "Label", [](bool) {});
                item = app.elementStorage.GetLastItemID();
                app.SetTooltip("Help text");
                app.EndRootWindow();
            };
            app.Frame(draw);
            EXPECT_EQ(item, boxID);
            HoverFor(app, app.CenterOf(boxID), draw);
            EXPECT_TRUE(app.tooltipManager.IsVisible());
        }

        // A color field is a swatch, a hex field and an alpha field: its tooltip shows over each.
        TEST_F(ControlsTooltipTest, AColorFieldsTooltipShowsOverEachPart)
        {
            const ID fieldID = app.idManager.CreateStaticID();
            color4f color{ 0.2f, 0.4f, 0.6f, 1.0f };
            auto draw = [&] {
                app.BeginRootWindow();
                app.ColorField(fieldID, color);
                app.SetTooltip("Help text");
                app.EndRootWindow();
            };
            app.Frame(draw);
            const IntRect r = app.GetElementRect(fieldID);
            for (const int2 at : { int2{ r.x + 4, r.y + r.h / 2 }, int2{ r.x + r.w / 2, r.y + r.h / 2 }, int2{ r.x + r.w - 4, r.y + r.h / 2 } })
            {
                app.InjectMouseMove({ 2, 2 });
                app.Frame(draw);
                app.Frame(draw);
                HoverFor(app, at, draw);
                EXPECT_TRUE(app.tooltipManager.IsVisible()) << "at " << at.x;
            }
        }

        // A popup a control opens isn't the control: the item stays the control.
        TEST_F(ControlsTooltipTest, APopupDoesntBecomeTheLastItem)
        {
            const ID buttonID = app.idManager.CreateStaticID();
            const ID popupID  = app.idManager.CreateStaticID();
            ID item = ID::Invalid();
            app.Frame([&] {
                app.BeginRootWindow();
                app.Button(buttonID, "B", [] {});
                if (app.BeginWindow(popupID, LayoutOverride{}.Position(10, 10),
                                    ElementFlags::WindowIsPopup | ElementFlags::WindowNoCaption))
                    app.Text("In the popup");
                app.EndWindow();
                item = app.elementStorage.GetLastItemID();
                app.EndRootWindow();
            });
            EXPECT_EQ(item, buttonID);
        }

        // Of two targets holding the hovered element, the first declared shows: the inner
        // control's, declared right after it, not its box's.
        TEST_F(ControlsTooltipTest, TheFirstMatchingTooltipWins)
        {
            TooltipManager tm;
            tm.delaySeconds = 0.0f;
            ID inner{}; inner.value.dynamicParts.hashValue = 11;
            ID outer{}; outer.value.dynamicParts.hashValue = 12;
            ID other{}; other.value.dynamicParts.hashValue = 13;
            tm.BeginFrame(inner, ID::Invalid(), 0.0f, true, false, false, false, false);
            tm.BeginFrame(inner, ID::Invalid(), 0.0f, false, false, false, false, false);
            tm.SetTargetAncestors({ outer });
            EXPECT_FALSE(tm.ShouldShow(other)) << "not around the hovered element";
            EXPECT_TRUE(tm.ShouldShow(inner));
            EXPECT_FALSE(tm.ShouldShow(outer)) << "one tooltip a frame";

            tm.BeginFrame(inner, ID::Invalid(), 0.1f, false, false, false, false, false);
            tm.SetTargetAncestors({ outer });
            EXPECT_TRUE(tm.ShouldShow(outer)) << "the box's, when the control has none";
        }

        // Right-clicking any part of a color field opens the context menu set after it.
        TEST_F(ControlsTooltipTest, RightClickingAPartOpensTheControlsContextMenu)
        {
            const ID fieldID = app.idManager.CreateStaticID();
            color4f color{ 0.2f, 0.4f, 0.6f, 1.0f };
            auto draw = [&] {
                app.BeginRootWindow();
                app.ColorField(fieldID, color);
                if (app.BeginContextMenu())
                    app.MenuItem("Reset", [] {});
                app.EndContextMenu();
                app.EndRootWindow();
            };
            app.Frame(draw);
            const IntRect r = app.GetElementRect(fieldID);
            const int2 swatch{ r.x + 4, r.y + r.h / 2 };
            app.InjectMouseMove(swatch);
            app.Frame(draw);
            app.ClickAt(swatch, MouseButtons::Right);
            app.Frame(draw);
            EXPECT_TRUE(app.menuManager.IsOpen());
            EXPECT_NE(app.FindByText("Reset"), ID::Invalid());
        }

        // Shift+F10 and the Menu key open the focused control's context menu.
        TEST_F(ControlsTooltipTest, TheKeyboardOpensTheFocusedControlsContextMenu)
        {
            for (const auto& [key, mods] : { std::pair{ KeyCode::F10, KeyModifier::Shift },
                                             std::pair{ KeyCode::Application, KeyModifier::None },
                                             std::pair{ KeyCode::Menu, KeyModifier::None } })
            {
                HeadlessGUIApp keys;
                ASSERT_TRUE(keys.Initialize());
                const ID buttonID = keys.idManager.CreateStaticID();
                auto draw = [&] {
                    keys.BeginRootWindow();
                    keys.Button(buttonID, "B", [] {});
                    if (keys.BeginContextMenu())
                        keys.MenuItem("Reset", [] {});
                    keys.EndContextMenu();
                    keys.EndRootWindow();
                };
                keys.Frame(draw);
                keys.InjectKey(KeyCode::Tab);
                keys.Frame(draw);
                keys.Frame(draw);
                ASSERT_EQ(keys.guiStateManager.Interaction().GetFocusedID(), buttonID);
                EXPECT_FALSE(keys.menuManager.IsOpen());
                keys.InjectKey(key, KeyAction::KeyDown, mods);
                keys.Frame(draw);
                keys.Frame(draw);
                EXPECT_TRUE(keys.menuManager.IsOpen()) << int(key);
                keys.Cleanup();
            }
            // F10 alone isn't it.
            const ID buttonID = app.idManager.CreateStaticID();
            auto draw = [&] {
                app.BeginRootWindow();
                app.Button(buttonID, "B", [] {});
                if (app.BeginContextMenu())
                    app.MenuItem("Reset", [] {});
                app.EndContextMenu();
                app.EndRootWindow();
            };
            app.Frame(draw);
            app.InjectKey(KeyCode::Tab);
            app.Frame(draw);
            app.InjectKey(KeyCode::F10);
            app.Frame(draw);
            app.Frame(draw);
            EXPECT_FALSE(app.menuManager.IsOpen());
        }

    } // namespace
} // namespace tachyon
