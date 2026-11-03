// Tests for the TooltipManager hover delay and SetTooltip / BeginTooltip /
// BeginContextMenu. AdvanceTime() steps the virtual clock past delaySeconds.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/gui_id.h>
#include <gui_core/gui_id_manager.h>
#include <gui_core/state/gui_state_manager.h>
#include <gui_controls/gui_tooltip_manager.h>

#include <functional>
#include <optional>
#include <string>

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
        // shows: the tooltip's box and the button.
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
            // The box: the text's parent (the window around it is see-through room for its shadow).
            const ElementRef textRef = app.elementStorage.FindElementRefByID(app.FindByText(text));
            if (!textRef.IsValid())
                return {};
            const ElementRef boxRef = app.elementStorage.GetParentRef(textRef);
            return { app.elementStorage.GetElementRectPtr(boxRef)->rect, app.GetElementRect(button) };
        }

        TEST_F(ControlsTooltipTest, BelowATargetWithAGap)
        {
            const auto [tip, target] = TooltipAndTarget(app, 0);
            ASSERT_GT(tip.h, 0);
            EXPECT_EQ(tip.y - target.Max(1), 9);
        }

        // At the bottom of the screen it goes above, the same gap away.
        TEST_F(ControlsTooltipTest, AboveATargetItKeepsTheSameGap)
        {
            const auto [tip, target] = TooltipAndTarget(app, 1040);
            ASSERT_GT(tip.h, 0);
            EXPECT_EQ(target.y - tip.Max(1), 9);
        }

        // A label given property help is underlined, dotted; its help shows a title, a
        // description and a footer, in that order.
        TEST_F(ControlsTooltipTest, PropertyHelpUnderlinesItsLabelAndShowsItsParts)
        {
            ID label = ID::Invalid();
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.Label("Cast shadows");
                    label = app.elementStorage.GetLastItemID();
                    app.SetTooltip(PropertyHelp{ "Shadow casting", "Whether this mesh casts shadows.", "MeshRenderer.castShadows" });
                    app.EndRootWindow();
                });
            };
            frame();
            const ElementRef ref = app.elementStorage.FindElementRefByID(label);
            ASSERT_TRUE(ref.IsValid());
            EXPECT_EQ(app.elementStorage.GetElementUnsafe(ref).styleRef, app.theme.labelHelpStyleRef);
            EXPECT_EQ(app.ownedTextSystem.StyleOf(app.elementStorage.GetElementUnsafe(ref).textID), app.theme.labelHelpStyleRef)
                << "its text is drawn in the underlined style too";
            const VisualTextStyleStates* text = app.styleManager.GetVisualTextStyles(app.theme.labelHelpStyleRef.visualTextStyleIndex);
            ASSERT_NE(text, nullptr);
            for (int s = 0; s < Style::kStateCount; ++s)   // hovered and focused too
            {
                EXPECT_GT((*text)[s].textStyle.underLine.thickness, 0) << "state " << s;
                EXPECT_EQ((*text)[s].textStyle.underLine.style, DecorationStyle::Dotted) << "state " << s;
            }

            app.InjectMouseMove(app.CenterOf(label));
            for (int i = 0; i < 8; ++i) { app.AdvanceTime(100); frame(); }
            const ID title = app.FindByText("Shadow casting");
            const ID description = app.FindByText("Whether this mesh casts shadows.");
            const ID footer = app.FindByText("MeshRenderer.castShadows");
            ASSERT_NE(title, ID::Invalid());
            ASSERT_NE(description, ID::Invalid());
            ASSERT_NE(footer, ID::Invalid());
            EXPECT_LT(app.GetElementRect(description).y, app.GetElementRect(footer).y);
            EXPECT_LT(app.GetElementRect(title).y, app.GetElementRect(description).y);
        }

        // At the left edge of the screen, the box centered on a button's text is kept on the
        // screen, right at its edge: the room for its shadow may reach past it.
        TEST_F(ControlsTooltipTest, AtTheLeftEdgeTheBoxIsRightAtTheScreensEdge)
        {
            const auto [tip, target] = TooltipAndTarget(app, 0, 0, "A tooltip wider than its button");
            ASSERT_GT(tip.w, 0);
            ASSERT_LT(target.x + target.w / 2 - tip.w / 2, 0) << "centered, it would reach past the edge";
            EXPECT_EQ(tip.x, 0);
        }

        // ... and at the right edge.
        TEST_F(ControlsTooltipTest, AtTheRightEdgeItStaysOnScreen)
        {
            const auto [tip, target] = TooltipAndTarget(app, 0, 1840, "A tooltip far too long to fit beside the right edge");
            ASSERT_GT(tip.w, 0);
            EXPECT_EQ(tip.Max(0), 1920);
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

        // ---------------------------------------------------------------------
        // Text cut short: shown whole, right over itself
        // ---------------------------------------------------------------------

        // The cut text, and the tooltip's box and text when it shows (empty when not).
        struct Expansion
        {
            IntRect cut{}, box{}, text{};
            std::string shown;
            StyleRef style{};
            IconID icon = IconID::Invalid;
        };

        // Builds the frame with `build` (which says what to point at, and the text that may be
        // cut), points `at` that far into what to point at (its middle when unset), and waits
        // past the delay.
        // `inRootWindow` false: `build` makes windows of its own, after the root window.
        Expansion PointAtCutText(HeadlessGUIApp& app, const std::function<void(ID& pointAt, ID& cut)>& build,
                                 std::optional<int2> at = std::nullopt, bool inRootWindow = true)
        {
            ID pointAt = ID::Invalid(), cut = ID::Invalid();
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    if (inRootWindow)
                        build(pointAt, cut);
                    app.EndRootWindow();
                    if (!inRootWindow)
                        build(pointAt, cut);
                });
            };
            frame();
            app.InjectMouseMove(at ? app.GetElementRect(pointAt).position + *at : app.CenterOf(pointAt));
            for (int i = 0; i < 8; ++i) { app.AdvanceTime(100); frame(); }

            Expansion e;
            e.cut = app.GetElementRect(cut);
            const ElementRef window = app.elementStorage.FindElementRefByID(app.tooltipWindowID);
            if (!window.IsValid() || app.elementStorage.GetChildrenSpan(window).empty())
                return e;
            const ElementRef box = app.elementStorage.FindElementRefByID(app.elementStorage.GetChildrenSpan(window)[0].id);
            if (app.elementStorage.GetChildrenSpan(box).empty())
                return e;
            const Element& text = app.elementStorage.GetChildrenSpan(box)[0];
            e.box   = app.elementStorage.GetElementRectPtr(box)->rect;
            e.text  = app.GetElementRect(text.id);
            e.shown = std::string(app.TextOf(text.id));
            e.style = text.styleRef;
            e.icon  = text.iconID;
            return e;
        }

        // A label of `style` (the theme's when unset), `width` wide (and `height` high, when
        // set), `indent` from the left.
        void CutLabel(HeadlessGUIApp& app, std::string_view text, int width, ID& id, StyleRef style = {}, int indent = 0,
                      int height = 0)
        {
            app.BeginHorizontal(app.idManager.AllocateDynamicID("line"), app.theme.emptyHorzStyleRef);
            app.BeginHorizontal(app.idManager.AllocateDynamicID("indent"), app.theme.emptyHorzStyleRef, LayoutOverride{}.Width(Pixels{ indent }));
            app.EndHorizontal();
            id = app.idManager.AllocateDynamicID("cut");
            app.Label(id, text, style == StyleRef{} ? app.theme.labelStyleRef : style,
                      height > 0 ? LayoutOverride{}.Size(width, height) : LayoutOverride{}.Width(width));
            app.EndHorizontal();
        }

        // A one-line style padded 8 px each side, its text cut short at its box, aligned as `align`.
        StyleRef AlignedStyle(HeadlessGUIApp& app, TextAlign align)
        {
            Style s{};
            s.padding.left = 8;
            s.padding.right = 8;
            s.SetOverflow(Overflow::Hidden);
            s.textLayoutStyle.textWrapping = TextWrap::NoWrap;
            s.textLayoutStyle.textOverflow = TextOverflow::Ellipsis;
            s.textLayoutStyle.horizontalAlignment = align;
            return app.styleManager.Register(s);
        }

        constexpr std::string_view kLongName = "UpdateLayoutWhenDirty";   // 21 characters, 168 px

        // An ellipsis: the whole text, its first letter where the cut one's is, as high and on
        // its row, in its font and the tooltip's colors, in a box 1 px border and 6 px padding
        // around it.
        TEST_F(ControlsTooltipTest, TextCutShortShowsWholeRightOverItself)
        {
            const Expansion e = PointAtCutText(app, [&](ID& at, ID& cut) {
                CutLabel(app, kLongName, 80, cut, {}, 0, 24);   // taller than its text
                at = cut;
            });
            ASSERT_EQ(e.shown, kLongName);
            ASSERT_EQ(e.cut.h, 24);
            EXPECT_EQ(e.text.x, e.cut.x + 8) << "where the label's padding puts its text";
            EXPECT_EQ(e.text.y, e.cut.y);
            EXPECT_EQ(e.text.h, e.cut.h) << "its row's height, so the text sits as high";
            EXPECT_EQ(e.text.w, int(kLongName.size()) * 8) << "all of it";
            EXPECT_EQ(e.box.x, e.text.x - 7);
            EXPECT_EQ(e.box.y, e.text.y - 1);
            EXPECT_EQ(e.box.h, e.text.h + 2);

            const VisualTextStyle* shown = app.styleManager.GetVisualTextStyle(e.style.visualTextStyleIndex, ElementState::None);
            const VisualTextStyle* label = app.styleManager.GetVisualTextStyle(app.theme.labelStyleRef.visualTextStyleIndex, ElementState::Hover);
            const VisualTextStyle* tip   = app.styleManager.GetVisualTextStyle(app.theme.tooltipExpandTextStyleRef.visualTextStyleIndex, ElementState::None);
            ASSERT_TRUE(shown && label && tip);
            EXPECT_EQ(shown->textStyle, label->textStyle) << "the label's font, as it is hovered";
            EXPECT_EQ(shown->textColorIndex, tip->textColorIndex) << "the tooltip's text color";
            const LayoutStyle* layout = app.styleManager.GetLayoutStyle(e.style.layoutStyleIndex);
            ASSERT_NE(layout, nullptr);
            EXPECT_EQ(layout->textLayoutStyle.textOverflow, TextOverflow::Clip) << "not cut short again";
        }

        // Text that fits shows nothing.
        TEST_F(ControlsTooltipTest, TextThatFitsShowsNothing)
        {
            const Expansion e = PointAtCutText(app, [&](ID& at, ID& cut) {
                CutLabel(app, kLongName, 200, cut);
                at = cut;
            });
            EXPECT_TRUE(e.shown.empty());
            EXPECT_EQ(app.CountByText(kLongName), 1);
        }

        // A tooltip of its own shows instead.
        TEST_F(ControlsTooltipTest, ATooltipOfItsOwnShowsInstead)
        {
            const Expansion e = PointAtCutText(app, [&](ID& at, ID& cut) {
                CutLabel(app, kLongName, 80, cut);
                app.SetTooltip(cut, "Lays out what changed");
                at = cut;
            });
            EXPECT_NE(app.FindByText("Lays out what changed"), ID::Invalid());
            EXPECT_EQ(app.CountByText(kLongName), 1);
        }

        // Cut by a box around it, not its own: a row in a narrow panel.
        TEST_F(ControlsTooltipTest, TextCutByABoxAroundItShowsToo)
        {
            Style clip{};
            clip.SetOverflow(Overflow::Hidden);
            const StyleRef panel = app.styleManager.Register(clip);
            const Expansion e = PointAtCutText(app, [&](ID& at, ID& cut) {
                app.BeginHorizontal(app.idManager.AllocateDynamicID("panel"), panel, LayoutOverride{}.Width(90));
                CutLabel(app, kLongName, 200, cut);
                app.EndHorizontal();
                at = cut;
            }, int2{ 30, 8 });
            ASSERT_EQ(e.shown, kLongName);
            EXPECT_EQ(e.text.x, e.cut.x + 8);
        }

        // A part of what is pointed at, the one under the pointer: a row's name, not its type.
        TEST_F(ControlsTooltipTest, ThePartUnderThePointerShows)
        {
            ID name = ID::Invalid(), type = ID::Invalid();
            const Expansion e = PointAtCutText(app, [&](ID& at, ID& cut) {
                at = app.idManager.AllocateDynamicID("row");
                app.BeginHorizontal(at, app.theme.emptyHorzStyleRef);
                name = app.idManager.AllocateDynamicID("name");
                type = app.idManager.AllocateDynamicID("type");
                app.RegisterElement(Element{ .id = name, .styleRef = app.theme.labelStyleRef, .flags = ElementFlags::NoInput },
                                    kLongName, LayoutOverride{}.Width(80));
                app.RegisterElement(Element{ .id = type, .styleRef = app.theme.labelStyleRef, .flags = ElementFlags::NoInput },
                                    "MeshRendererComponent", LayoutOverride{}.Width(80));
                app.EndHorizontal();
                cut = type;
            }, int2{ 120, 8 });   // into the second
            EXPECT_EQ(e.shown, "MeshRendererComponent");
            EXPECT_EQ(e.text.x, e.cut.x + 8);
        }

        // Aligned to its end, the ends line up; centered, the middles. (Away from the screen's
        // left edge, which the whole text would reach past.)
        TEST_F(ControlsTooltipTest, CutTextLinesUpOnTheSideItsAlignedTo)
        {
            const StyleRef end = AlignedStyle(app, TextAlign::End);
            Expansion e = PointAtCutText(app, [&](ID& at, ID& cut) { CutLabel(app, kLongName, 80, cut, end, 300); at = cut; });
            ASSERT_EQ(e.shown, kLongName);
            EXPECT_EQ(e.text.Max(0), e.cut.Max(0) - 8);

            HeadlessGUIApp centered;
            ASSERT_TRUE(centered.Initialize());
            const StyleRef middle = AlignedStyle(centered, TextAlign::Center);
            e = PointAtCutText(centered, [&](ID& at, ID& cut) { CutLabel(centered, kLongName, 80, cut, middle, 300); at = cut; });
            ASSERT_EQ(e.shown, kLongName);
            EXPECT_EQ(e.text.x + e.text.w / 2, e.cut.x + e.cut.w / 2);
            centered.Cleanup();
        }

        // Near the screen's right edge the box stays on it, the text moved in as far as it must.
        TEST_F(ControlsTooltipTest, CutTextNearTheScreensEdgeStaysOnIt)
        {
            const IntRect screen = app.windowManager->GetMonitorArea(0);
            const Expansion e = PointAtCutText(app, [&](ID& at, ID& cut) {
                CutLabel(app, "UpdateLayoutWhenDirtyAndRedrawTheWindow", 80, cut, {}, 1600);   // 312 px, past 1920
                at = cut;
            });
            ASSERT_EQ(e.shown, "UpdateLayoutWhenDirtyAndRedrawTheWindow");
            ASSERT_GT(e.cut.x + 8 + 312, screen.Max(0)) << "it would reach past the edge";
            EXPECT_EQ(e.box.Max(0), screen.Max(0));
            EXPECT_EQ(e.text.y, e.cut.y) << "still on its row";
        }

        // ... and at the left and bottom edges.
        TEST_F(ControlsTooltipTest, CutTextNearTheLeftAndBottomEdgesStaysOnTheScreen)
        {
            const IntRect screen = app.windowManager->GetMonitorArea(0);
            const StyleRef end = AlignedStyle(app, TextAlign::End);
            const Expansion e = PointAtCutText(app, [&](ID& at, ID& cut) {
                app.BeginVertical(app.idManager.AllocateDynamicID("column"), app.theme.emptyVertStyleRef);
                app.BeginVertical(app.idManager.AllocateDynamicID("spacer"), app.theme.emptyVertStyleRef,
                                  LayoutOverride{}.Height(Pixels{ screen.h - 26 }));   // its foot under the window's edge
                app.EndVertical();
                CutLabel(app, kLongName, 80, cut, end);
                app.EndVertical();
                at = cut;
            }, int2{ 40, 3 });
            ASSERT_EQ(e.shown, kLongName);
            ASSERT_LT(e.cut.Max(0) - 8 - 168, screen.x) << "it would reach past the left";
            ASSERT_GT(e.cut.Max(1) + 1, screen.Max(1)) << "... and the bottom";
            EXPECT_EQ(e.box.x, screen.x);
            EXPECT_EQ(e.box.Max(1), screen.Max(1));
        }

        // An icon before the text counts: the text alone would fit. The icon shows with it.
        // Above the text, it doesn't make it wider.
        TEST_F(ControlsTooltipTest, AnIconBesideTheTextCountsAndShowsWithIt)
        {
            const IconID icon = app.textSystem->RegisterIconFromSVG("mesh", "", 0);
            const LayoutStyle* label = app.styleManager.GetLayoutStyle(app.theme.labelStyleRef.layoutStyleIndex);
            ASSERT_NE(label, nullptr);
            const int beside = 16 + 8 + label->iconTextGap + 32 + 4;   // 4 px short of the icon, the gap and "Mesh"
            const Expansion e = PointAtCutText(app, [&](ID& at, ID& cut) {
                at = cut = app.idManager.AllocateDynamicID("cut");
                app.RegisterElement(Element{ .id = cut, .iconID = icon, .styleRef = app.theme.labelStyleRef }, "Mesh",
                                    LayoutOverride{}.Width(beside));
            });
            ASSERT_EQ(e.shown, "Mesh");
            EXPECT_EQ(e.icon, icon);
            EXPECT_EQ(e.text.w, 16 + label->iconTextGap + 32);

            HeadlessGUIApp tiles;
            ASSERT_TRUE(tiles.Initialize());
            Style s{};
            s.padding.left = 8;
            s.padding.right = 8;
            s.SetOverflow(Overflow::Hidden);
            s.iconSide = IconSide::Top;
            s.textLayoutStyle.textOverflow = TextOverflow::Ellipsis;
            const StyleRef tile = tiles.styleManager.Register(s);
            const IconID tileIcon = tiles.textSystem->RegisterIconFromSVG("mesh", "", 0);
            const Expansion t = PointAtCutText(tiles, [&](ID& at, ID& cut) {
                at = cut = tiles.idManager.AllocateDynamicID("tile");
                tiles.RegisterElement(Element{ .id = cut, .iconID = tileIcon, .styleRef = tile }, "Mesh",
                                      LayoutOverride{}.Width(16 + 32));
            });
            EXPECT_TRUE(t.shown.empty()) << "the icon above: as wide as the text";
            tiles.Cleanup();
        }

        // Focused with the keyboard, a control's cut text shows at once, the pointer elsewhere:
        // a button's, in its middle, inside its border and at its padding.
        TEST_F(ControlsTooltipTest, FocusedByTheKeyboardItShowsAtOnce)
        {
            ID button = ID::Invalid();
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.BeginHorizontal(app.idManager.AllocateDynamicID("holder"), app.theme.emptyHorzStyleRef, LayoutOverride{}.Width(80));
                    button = app.idManager.AllocateDynamicID("button");
                    app.Button(button, std::string(kLongName), [] {});   // grows to fill the holder
                    app.EndHorizontal();
                    app.EndRootWindow();
                });
            };
            app.InjectMouseMove({ 1500, 900 });
            frame();
            app.InjectKey(KeyCode::Tab);
            frame();
            frame();
            ASSERT_EQ(app.guiStateManager.Interaction().GetFocusedID(), button);
            ASSERT_EQ(app.CountByText(kLongName), 2) << "the button's, and shown whole";

            const ElementRef window = app.elementStorage.FindElementRefByID(app.tooltipWindowID);
            ASSERT_TRUE(window.IsValid());
            const ElementRef box = app.elementStorage.FindElementRefByID(app.elementStorage.GetChildrenSpan(window)[0].id);
            const Element& text = app.elementStorage.GetChildrenSpan(box)[0];
            const IntRect shown = app.GetElementRect(text.id), cut = app.GetElementRect(button);
            const LayoutStyle* buttonLayout = app.styleManager.GetLayoutStyle(app.elementStorage.GetElementUnsafe(app.elementStorage.FindElementRefByID(button)).styleRef.layoutStyleIndex);
            const LayoutStyle* shownLayout  = app.styleManager.GetLayoutStyle(text.styleRef.layoutStyleIndex);
            ASSERT_TRUE(buttonLayout && shownLayout);
            const int border = buttonLayout->borderWidths.top;
            ASSERT_GT(border, 0);
            EXPECT_EQ(shown.x + shown.w / 2, cut.x + cut.w / 2);
            EXPECT_EQ(shown.y, cut.y + border);
            EXPECT_EQ(shown.h, cut.h - buttonLayout->borderWidths.Vertical());
            EXPECT_EQ(shownLayout->padding.top, buttonLayout->padding.top) << "its text as high in it";
            EXPECT_EQ(shownLayout->padding.bottom, buttonLayout->padding.bottom);
        }

        // Focused but out of sight (scrolled away), nothing shows.
        TEST_F(ControlsTooltipTest, FocusedOutOfSightNothingShows)
        {
            Style clip{};
            clip.SetOverflow(Overflow::Hidden);
            const StyleRef panel = app.styleManager.Register(clip);
            ID button = ID::Invalid();
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.BeginHorizontal(app.idManager.AllocateDynamicID("panel"), panel, LayoutOverride{}.Width(60));
                    app.BeginHorizontal(app.idManager.AllocateDynamicID("pad"), app.theme.emptyHorzStyleRef,
                                        LayoutOverride{}.Width(100).FlexShrink(0));
                    app.EndHorizontal();
                    app.BeginHorizontal(app.idManager.AllocateDynamicID("holder"), app.theme.emptyHorzStyleRef,
                                        LayoutOverride{}.Width(80).FlexShrink(0));
                    button = app.idManager.AllocateDynamicID("button");
                    app.Button(button, std::string(kLongName), [] {});
                    app.EndHorizontal();
                    app.EndHorizontal();
                    app.EndRootWindow();
                });
            };
            app.InjectMouseMove({ 1500, 900 });
            frame();
            app.InjectKey(KeyCode::Tab);
            frame();
            frame();
            ASSERT_EQ(app.guiStateManager.Interaction().GetFocusedID(), button);
            EXPECT_EQ(app.CountByText(kLongName), 1);
        }

        // Under a canvas's pan or zoom the text is drawn other than as laid out: nothing shows.
        TEST_F(ControlsTooltipTest, UnderAPanOrZoomNothingShows)
        {
            const Expansion e = PointAtCutText(app, [&](ID& at, ID& cut) {
                const ElementRef canvas = app.RegisterElement(Element{ .id = app.idManager.AllocateDynamicID("canvas"),
                                                                       .styleRef = app.theme.emptyVertStyleRef },
                                                              LayoutOverride{}.Size(400, 100));
                app.elementStorage.SetContentTransform(canvas, float3x2::Translate(3, 0));
                ASSERT_EQ(app.elementStorage.BeginChildren(), ElementStorageResult::Success);
                CutLabel(app, kLongName, 80, cut);
                ASSERT_EQ(app.elementStorage.EndChildren(), ElementStorageResult::Success);
                at = cut;
            });
            ASSERT_NE(e.cut.w, 0);
            EXPECT_TRUE(e.shown.empty());
        }

        // In a style whose hovered state has another font, it's that font.
        TEST_F(ControlsTooltipTest, CutTextShowsInItsFontAsItIsNow)
        {
            Style s{};
            s.padding.left = 8;
            s.padding.right = 8;
            s.SetOverflow(Overflow::Hidden);
            s.textLayoutStyle.textOverflow = TextOverflow::Ellipsis;
            for (StyleState& state : s.states)
                state.textStyle.fontSize = 13.0f;
            s.states[ElementState::Hover].textStyle.fontSize = 17.0f;
            const StyleRef style = app.styleManager.Register(s);
            const Expansion e = PointAtCutText(app, [&](ID& at, ID& cut) { CutLabel(app, kLongName, 80, cut, style); at = cut; });
            ASSERT_EQ(e.shown, kLongName);
            const VisualTextStyle* shown = app.styleManager.GetVisualTextStyle(e.style.visualTextStyleIndex, ElementState::None);
            ASSERT_NE(shown, nullptr);
            EXPECT_EQ(shown->textStyle.fontSize,
                      app.styleManager.GetVisualTextStyle(style.visualTextStyleIndex, ElementState::Hover)->textStyle.fontSize);
        }

        // In a window of its own, away from the screen's corner.
        TEST_F(ControlsTooltipTest, CutTextInAnotherWindowShowsOverItself)
        {
            const ID window = app.idManager.CreateStaticID();
            app.mouseWindow = window;   // the pointer over it
            const Expansion e = PointAtCutText(app, [&](ID& at, ID& cut) {
                if (app.BeginWindow(window, "Panel", app.theme.windowStyleRef, LayoutOverride{}.Position(400, 300).Size(300, 100)))
                    CutLabel(app, kLongName, 80, cut);
                app.EndWindow();
                at = cut;
            }, std::nullopt, false);
            ASSERT_GT(e.cut.x, 300);
            ASSERT_GT(e.cut.y, 250);
            ASSERT_EQ(e.shown, kLongName);
            EXPECT_EQ(e.text.x, e.cut.x + 8);
            EXPECT_EQ(e.text.y, e.cut.y);
        }

        // A field's text scrolls in it, and text that wraps isn't cut: neither shows.
        TEST_F(ControlsTooltipTest, FieldsAndWrappingTextShowNothing)
        {
            std::string value{ kLongName };
            Expansion e = PointAtCutText(app, [&](ID& at, ID& cut) {
                app.BeginHorizontal(app.idManager.AllocateDynamicID("holder"), app.theme.emptyHorzStyleRef, LayoutOverride{}.Width(80));
                app.Field(value);
                cut = at = app.elementStorage.GetLastItemID();
                app.EndHorizontal();
            });
            ASSERT_GT(int(kLongName.size()) * 8, e.cut.w) << "its text cut";
            EXPECT_TRUE(e.shown.empty()) << "a field";

            e = PointAtCutText(app, [&](ID& at, ID& cut) {
                CutLabel(app, "Lays out what changed since the last frame", 80, cut, app.theme.tooltipDescriptionStyleRef);
                at = cut;
            });
            EXPECT_TRUE(e.shown.empty()) << "wrapping text";
        }

        // ---------------------------------------------------------------------
        // A value that isn't accepted, a disabled control
        // ---------------------------------------------------------------------

        // The children of `box`: the tooltip's lines.
        std::vector<ID> ChildrenOf(HeadlessGUIApp& app, ID box)
        {
            std::vector<ID> out;
            for (const Element& e : app.elementStorage.GetChildrenSpan(app.elementStorage.FindElementRefByID(box)))
                out.push_back(e.id);
            return out;
        }

        // The box of the tooltip showing `text`.
        ID BoxOf(HeadlessGUIApp& app, ID text)
        {
            ElementRef ref = app.elementStorage.FindElementRefByID(text);
            while (ref.IsValid() && app.elementStorage.GetParentRef(ref).IsValid() &&
                   app.elementStorage.GetElementUnsafe(app.elementStorage.GetParentRef(ref)).id != app.tooltipWindowID)
                ref = app.elementStorage.GetParentRef(ref);
            return ref.IsValid() ? app.elementStorage.GetElementUnsafe(ref).id : ID::Invalid();
        }

        StyleRef StyleOf(HeadlessGUIApp& app, ID id)
        {
            const ElementRef ref = app.elementStorage.FindElementRefByID(id);
            return ref.IsValid() ? app.elementStorage.GetElementUnsafe(ref).styleRef : StyleRef{};
        }

        // Builds the frame with `build`, which gives what to point at, and waits past the delay.
        void PointAt(HeadlessGUIApp& app, const std::function<ID()>& build)
        {
            ID at = ID::Invalid();
            auto frame = [&] { app.Frame([&] { app.BeginRootWindow(); at = build(); app.EndRootWindow(); }); };
            frame();
            app.InjectMouseMove(app.CenterOf(at));
            for (int i = 0; i < 8; ++i) { app.AdvanceTime(100); frame(); }
        }

        // Why a value isn't accepted: on the popup surface, with an error border. (In the light
        // theme, where its text's color isn't the plain tooltip's.)
        TEST_F(ControlsTooltipTest, AnErrorTooltipSaysWhyAValueIsntAccepted)
        {
            HeadlessGUIApp light("themes/light.css");
            ASSERT_TRUE(light.Initialize());
            float opacity = 1.8f;
            PointAt(light, [&] {
                light.BeginInvalid();
                light.Field(opacity);
                const ID field = light.elementStorage.GetLastItemID();
                light.SetErrorTooltip("Value must be between 0 and 1");
                light.EndInvalid();
                return field;
            });
            const ID message = light.FindByText("Value must be between 0 and 1");
            ASSERT_NE(message, ID::Invalid());
            EXPECT_EQ(StyleOf(light, message), light.theme.tooltipErrorTextStyleRef);
            EXPECT_NE(light.theme.tooltipErrorTextStyleRef, light.theme.tooltipTextStyleRef);
            EXPECT_EQ(StyleOf(light, BoxOf(light, message)), light.theme.tooltipErrorStyleRef);
            light.Cleanup();
        }

        // A disabled control shows its tooltip, the reason on a second, muted line.
        TEST_F(ControlsTooltipTest, ADisabledControlSaysWhy)
        {
            int clicks = 0;
            ID button = ID::Invalid(), next = ID::Invalid();
            PointAt(app, [&] {
                app.BeginDisabled();
                app.Button("Publish", [&] { ++clicks; });
                button = app.elementStorage.GetLastItemID();
                app.SetDisabledTooltip("Publish to asset store", "Sign in to enable publishing");
                app.Button("Share", [] {});
                next = app.elementStorage.GetLastItemID();
                app.EndDisabled();
                return button;
            });
            EXPECT_NE(app.elementStorage.GetElementUnsafe(app.elementStorage.FindElementRefByID(next)).state & ElementState::Disabled,
                      ElementState::None) << "the next control is disabled still";
            EXPECT_EQ(app.guiStateManager.Interaction().GetHoveredID(), ID::Invalid()) << "it takes no input";
            EXPECT_EQ(app.guiStateManager.Interaction().GetHoveredDisabledID(), button);
            const ID text = app.FindByText("Publish to asset store"), reason = app.FindByText("Sign in to enable publishing");
            ASSERT_NE(text, ID::Invalid());
            ASSERT_NE(reason, ID::Invalid());
            const ID box = BoxOf(app, text);
            EXPECT_EQ(StyleOf(app, box), app.theme.tooltipLinesStyleRef);
            EXPECT_EQ(ChildrenOf(app, box), (std::vector<ID>{ text, reason })) << "the reason under the text";
            EXPECT_EQ(StyleOf(app, reason), app.theme.tooltipNoteStyleRef);
            for (const ID line : { box, text, reason })   // not dimmed with the button
                EXPECT_EQ(app.elementStorage.GetElementUnsafe(app.elementStorage.FindElementRefByID(line)).state & ElementState::Disabled,
                          ElementState::None);
            EXPECT_TRUE(app.guiStateManager.Construction().IsBalanced());
            app.ClickAt(app.CenterOf(button));
            EXPECT_EQ(clicks, 0);
        }

        // Disabled with no reason given: a plain tooltip.
        TEST_F(ControlsTooltipTest, ADisabledControlWithoutAReasonHasAPlainTooltip)
        {
            PointAt(app, [&] {
                app.BeginDisabled();
                app.Button("Publish", [] {});
                const ID button = app.elementStorage.GetLastItemID();
                app.SetDisabledTooltip("Publish to asset store", "");
                app.EndDisabled();
                return button;
            });
            const ID text = app.FindByText("Publish to asset store");
            ASSERT_NE(text, ID::Invalid());
            EXPECT_EQ(StyleOf(app, BoxOf(app, text)), app.theme.tooltipStyleRef);
        }

        // A press held on another control: a disabled one it's dragged over isn't pointed at.
        TEST_F(ControlsTooltipTest, ADisabledControlIsntPointedAtDuringAPress)
        {
            ID enabled = ID::Invalid(), disabled = ID::Invalid();
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.Button("Save", [] {});
                    enabled = app.elementStorage.GetLastItemID();
                    app.BeginDisabled();
                    app.Button("Publish", [] {});
                    disabled = app.elementStorage.GetLastItemID();
                    app.EndDisabled();
                    app.EndRootWindow();
                });
            };
            frame();
            app.InjectMouseMove(app.CenterOf(disabled));
            frame();
            EXPECT_EQ(app.guiStateManager.Interaction().GetHoveredDisabledID(), disabled);
            app.InjectMouseMove(app.CenterOf(enabled));
            app.InjectMouseDown(app.CenterOf(enabled));
            frame();
            app.InjectMouseMove(app.CenterOf(disabled), MouseButtons::Left);
            frame();
            EXPECT_EQ(app.guiStateManager.Interaction().GetHoveredDisabledID(), ID::Invalid());
            app.InjectMouseUp(app.CenterOf(disabled));
            frame();
        }

        // A disabled control in an enabled row: the row's tooltip isn't its, as for an enabled
        // control in it.
        TEST_F(ControlsTooltipTest, AnEnabledRowsTooltipIsntADisabledControlsInIt)
        {
            PointAt(app, [&] {
                const ID row = app.idManager.AllocateDynamicID("row");
                app.BeginHorizontal(row, app.theme.emptyHorzStyleRef);
                app.BeginDisabled();
                app.Button("Publish", [] {});
                const ID button = app.elementStorage.GetLastItemID();
                app.EndDisabled();
                app.EndHorizontal();
                app.SetTooltip(row, "The row's");
                return button;
            });
            EXPECT_EQ(app.FindByText("The row's"), ID::Invalid());
        }

        // Inside a panned canvas, an element anchored to another stays in the canvas's space:
        // it's drawn through the same pan.
        TEST_F(ControlsTooltipTest, AnElementAnchoredInsideACanvasStaysInItsSpace)
        {
            const ID a = app.idManager.CreateStaticID(), b = app.idManager.CreateStaticID();
            for (int i = 0; i < 2; ++i)
                app.Frame([&] {
                    app.BeginRootWindow();
                    const ElementRef canvas = app.RegisterElement(Element{ .id = app.idManager.AllocateDynamicID("canvas"),
                                                                           .styleRef = app.theme.emptyVertStyleRef },
                                                                  LayoutOverride{}.Size(400, 200));
                    app.elementStorage.SetContentTransform(canvas, float3x2::Translate(30, 20));
                    ASSERT_EQ(app.elementStorage.BeginChildren(), ElementStorageResult::Success);
                    app.RegisterElement(Element{ .id = a, .styleRef = app.theme.emptyVertStyleRef }, LayoutOverride{}.Size(50, 20));
                    app.RegisterElement(Element{ .id = b, .styleRef = app.theme.emptyVertStyleRef },
                                        LayoutOverride{}.Anchored(a, AnchorPlacement::OutsideBottom | AnchorPlacement::InsideLeft).Size(10, 10));
                    ASSERT_EQ(app.elementStorage.EndChildren(), ElementStorageResult::Success);
                    app.EndRootWindow();
                });
            EXPECT_EQ(app.GetElementRect(b).x, app.GetElementRect(a).x);
            EXPECT_EQ(app.GetElementRect(b).y, app.GetElementRect(a).Max(1));
        }

        // Enabled, it's a plain tooltip: no reason.
        TEST_F(ControlsTooltipTest, AnEnabledControlLeavesTheReasonOut)
        {
            PointAt(app, [&] {
                app.Button("Publish", [] {});
                const ID button = app.elementStorage.GetLastItemID();
                app.SetDisabledTooltip("Publish to asset store", "Sign in to enable publishing");
                return button;
            });
            const ID text = app.FindByText("Publish to asset store");
            ASSERT_NE(text, ID::Invalid());
            EXPECT_EQ(StyleOf(app, BoxOf(app, text)), app.theme.tooltipStyleRef);
            EXPECT_EQ(app.FindByText("Sign in to enable publishing"), ID::Invalid());
        }

        // A control in a disabled row: its own tooltip, though the row took the hit. Without
        // one, the row's.
        TEST_F(ControlsTooltipTest, AControlInADisabledRowShowsItsTooltip)
        {
            for (bool own : { true, false })
            {
                HeadlessGUIApp rows;
                ASSERT_TRUE(rows.Initialize());
                PointAt(rows, [&] {
                    rows.BeginDisabled();
                    const ID row = rows.idManager.AllocateDynamicID("row");
                    rows.BeginHorizontal(row, rows.theme.emptyHorzStyleRef);
                    rows.Button("Publish", [] {});
                    const ID button = rows.elementStorage.GetLastItemID();
                    if (own)
                        rows.SetTooltip("The button's");
                    rows.EndHorizontal();
                    rows.SetTooltip(row, "The row's");
                    rows.EndDisabled();
                    return button;
                });
                EXPECT_NE(rows.FindByText(own ? "The button's" : "The row's"), ID::Invalid()) << own;
                EXPECT_EQ(rows.FindByText(own ? "The row's" : "The button's"), ID::Invalid()) << own;
                rows.Cleanup();
            }
        }

        // Placed by where the target's text is: its text starts where a label's starts, is
        // centered on a button's, ends where text aligned to its end ends.
        TEST_F(ControlsTooltipTest, ATooltipIsPlacedByItsTargetsText)
        {
            struct Case { const char* name; int align; };   // 0 start, 1 end, 2 middle
            for (const Case c : { Case{ "start", 0 }, Case{ "end", 1 }, Case{ "middle", 2 } })
            {
                HeadlessGUIApp a;
                ASSERT_TRUE(a.Initialize());
                Style s{};
                s.padding.left = 8;
                s.padding.right = 12;
                s.textLayoutStyle.horizontalAlignment = c.align == 0 ? TextAlign::Start : c.align == 1 ? TextAlign::End : TextAlign::Center;
                const StyleRef style = a.styleManager.Register(s);
                ID target = ID::Invalid();
                PointAt(a, [&] {
                    a.BeginHorizontal(a.idManager.AllocateDynamicID("indent"), a.theme.emptyHorzStyleRef, LayoutOverride{}.Width(Pixels{ 400 }));
                    a.EndHorizontal();
                    target = a.idManager.AllocateDynamicID("target");
                    a.RegisterElement(Element{ .id = target, .styleRef = style }, "Opacity", LayoutOverride{}.Width(200));
                    a.SetTooltip("Its tooltip");
                    return target;
                });
                const IntRect t = a.GetElementRect(target), shown = a.GetElementRect(a.FindByText("Its tooltip"));
                ASSERT_GT(shown.w, 0) << c.name;
                const int textW = 7 * 8;   // "Opacity" in the stub's 8 px a character
                if (c.align == 0)
                    EXPECT_EQ(shown.x, t.x + 8) << "its text starts where the label's does";
                else if (c.align == 1)
                    EXPECT_EQ(shown.Max(0), t.Max(0) - 12) << "its text ends where the label's does";
                else
                {
                    const IntRect box = a.GetElementRect(BoxOf(a, a.FindByText("Its tooltip")));
                    const int textMid = t.x + 8 + (t.w - 20 - textW) / 2 + textW / 2;
                    EXPECT_NEAR(box.x + box.w / 2, textMid, 1) << "centered on the text";
                }
                a.Cleanup();
            }
        }

    } // namespace
} // namespace tachyon
