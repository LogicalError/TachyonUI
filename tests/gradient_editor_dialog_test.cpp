// The gradient editor dialog: opened by a GradientField's bar; a click on the
// gradient adds a stop; Cancel restores the gradient, OK keeps it.

#include <gtest/gtest.h>

#include "headless_gui_app.h"
#include "skribidi_test_fixture.h"

#include <gui_core/graphics/gui_custom_draw_context.h>

#include <gui_controls/gui_application_internal.h>
#include <gui_controls/subsystems/gui_gradient_editor_subsystem.h>

namespace
{
    using namespace tachyon;
    using GUITest::HeadlessGUIApp;

    class GradientEditorDialog : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ASSERT_TRUE(app.Initialize());
            gradient.type = GradientType::Linear;
            gradient.AddStop(color4f{ 1, 0, 0, 1 }, 0.0f);
            gradient.AddStop(color4f{ 0, 0, 1, 1 }, 1.0f);
        }

        void Emit()
        {
            app.Frame([&] {
                app.BeginRootWindow({ 1200, 900 });
                app.GradientField(gradient, LayoutOverride{}.Width(300));
                app.EndRootWindow();
            });
        }

        ID FindByStyle(StyleRef style)
        {
            const auto& storage = app.elementStorage;
            for (auto it = storage.TopDownDepthScanBegin(); it != storage.TopDownDepthScanEnd(); ++it)
                for (const Element& e : it->GetChildren(storage))
                    if (e.styleRef.layoutStyleIndex == style.layoutStyleIndex
                        && e.styleRef.visualBoxStyleIndex == style.visualBoxStyleIndex)
                        return e.id;
            return ID::Invalid();
        }

        void Open()
        {
            Emit();
            app.ClickAt(app.CenterOf(FindByStyle(app.theme.gradientSwatchStyleRef)));
            Emit();
            Emit();
        }

        void ClickAt(int2 pos)
        {
            app.ClickAt(pos);
            Emit();
            Emit();
        }

        // A point on the dialog's gradient bar, `t` across it (the bar is inset 24 px at the sides).
        int2 OnTheBar(float t)
        {
            const IntRect r = app.GetElementRect(FindByStyle(app.theme.gradientEditorBarStyleRef));
            const int inset = app.styleManager.Px(24);
            const int barTop = r.y + (r.h - app.styleManager.Px(44 + 6 + 18)) / 2;
            return { r.x + inset + int(t * float(r.w - 2 * inset)), barTop + app.styleManager.Px(20) };
        }

        HeadlessGUIApp app;
        Gradient       gradient;
    };

    TEST_F(GradientEditorDialog, TheFieldOpensIt)
    {
        Open();
        EXPECT_NE(FindByStyle(app.theme.gradientEditorBarStyleRef), ID::Invalid());
        EXPECT_NE(FindByStyle(app.theme.dialogPrimaryButtonStyleRef), ID::Invalid()) << "with an OK button";
    }

    // The table: each stop's color (swatch and hex), its alpha and its location; the footer
    // holds only Cancel and OK.
    TEST_F(GradientEditorDialog, TheTableListsColorAlphaAndLocation)
    {
        gradient.stops[1].SetColor(color4f{ 0, 0, 1, 0.2f });
        Open();
        EXPECT_NE(app.FindByText("Alpha"), ID::Invalid());
        EXPECT_NE(app.FindByText("Location"), ID::Invalid());
        EXPECT_EQ(app.FindByText("Type"), ID::Invalid());
        EXPECT_EQ(app.FindByText("Value"), ID::Invalid());
        EXPECT_NE(app.FindByText("#0000FF"), ID::Invalid()) << "the hex alone";
        EXPECT_NE(app.FindByText("20%"), ID::Invalid()) << "the alpha in its own column";
        EXPECT_NE(app.FindByText("100%"), ID::Invalid());
        const IntRect hex = app.GetElementRect(app.FindByText("#0000FF")), alpha = app.GetElementRect(app.FindByText("20%"));
        EXPECT_LT(hex.x, alpha.x);
        EXPECT_EQ(app.FindByText("2 stops \xC2\xB7 click the bar to add, drag a stop off to remove"), ID::Invalid()) << "no hint in the footer";
    }

    // The preset the gradient is is ringed, though an 8-bit gradient keeps its stops a little
    // off the preset's (a stop at 0.5 is kept as 32768 / 65535).
    TEST_F(GradientEditorDialog, ThePresetInUseIsRinged)
    {
        gradient = Gradient{};
        gradient.type = GradientType::Linear;
        const auto rgb = [](uint32_t hex) { return color4f{ float((hex >> 16) & 0xFF) / 255.0f, float((hex >> 8) & 0xFF) / 255.0f, float(hex & 0xFF) / 255.0f, 1.0f }; };
        gradient.AddStop(rgb(0x0B1D51), 0.0f);
        gradient.AddStop(rgb(0x3B82F6), 0.5f);
        gradient.AddStop(rgb(0xA5F3FC), 1.0f);   // the "Sky" preset
        Open();

        // Each chip's drawing: a ringed one draws its ring and the gap inside it first.
        std::vector<size_t> quads;
        const auto& storage = app.elementStorage;
        for (auto it = storage.TopDownDepthScanBegin(); it != storage.TopDownDepthScanEnd(); ++it)
            for (const Element& e : it->GetChildren(storage))
                if (e.styleRef == app.theme.gradientEditorPresetStyleRef)
                {
                    GUITest::StubGUIRenderer renderer;
                    const ElementRef ref = storage.FindElementRefByID(e.id);
                    const ElementCustomDraw* draw = storage.GetCustomDraw(ref);
                    ASSERT_TRUE(draw != nullptr && draw->callback);
                    const ElementRect* rects = storage.GetElementRectPtr(ref);
                    CustomDrawContext ctx{ renderer, app.elementStorage, ref, rects->rect, rects->innerRect,
                                           IntRect{ -4000, -4000, 8000, 8000 }, float3x2{}, int2{ 0, 0 } };
                    draw->callback(ctx);
                    quads.push_back(renderer.quads.size());
                }
        ASSERT_GE(quads.size(), 2u);
        EXPECT_EQ(quads[1], quads[0] + 2) << "Sky ringed, Grayscale not";
    }

    TEST_F(GradientEditorDialog, CancelRestoresTheGradient)
    {
        Open();
        ClickAt(OnTheBar(0.5f));
        EXPECT_EQ(gradient.stopCount, 3) << "a click on the bar added a stop";
        ClickAt(app.CenterOf(FindByStyle(app.theme.dialogButtonStyleRef)));
        EXPECT_EQ(gradient.stopCount, 2);
        EXPECT_EQ(FindByStyle(app.theme.gradientEditorBarStyleRef), ID::Invalid()) << "and closed the dialog";
    }

    TEST_F(GradientEditorDialog, OkKeepsTheGradient)
    {
        Open();
        ClickAt(OnTheBar(0.5f));
        ClickAt(app.CenterOf(FindByStyle(app.theme.dialogPrimaryButtonStyleRef)));
        EXPECT_EQ(gradient.stopCount, 3);
        EXPECT_EQ(FindByStyle(app.theme.gradientEditorBarStyleRef), ID::Invalid()) << "and closed the dialog";
    }

    // Escape in the dialog is Cancel, and the keyboard goes back to the swatch.
    TEST_F(GradientEditorDialog, EscapeCancels)
    {
        Open();
        ClickAt(OnTheBar(0.5f));
        ASSERT_EQ(gradient.stopCount, 3);
        app.activeWindow = GUIApplicationInternals::GradientEditor(app).State().windowID;   // the dialog has the keyboard
        Emit();
        app.InjectKey(KeyCode::Escape);
        Emit();
        Emit();
        app.activeWindow = ID::Invalid();
        EXPECT_EQ(gradient.stopCount, 2);
        EXPECT_EQ(FindByStyle(app.theme.gradientEditorBarStyleRef), ID::Invalid()) << "closed";
        EXPECT_EQ(app.guiStateManager.Interaction().GetFocusedID(), FindByStyle(app.theme.gradientSwatchStyleRef));
    }

    // Opened and closed with OK, untouched, the gradient is exactly as it was: no edit reported.
    TEST_F(GradientEditorDialog, OkWithoutEditsChangesNothing)
    {
        gradient.stops[1].SetOffset(0.7f);   // an offset a float round trip could move
        gradient.colorSpace = ColorSpace::Oklab;
        const Gradient before = gradient;
        bool changed = false;
        auto emit = [&] {
            app.Frame([&] {
                app.BeginRootWindow({ 1200, 900 });
                changed |= app.GradientField(gradient, LayoutOverride{}.Width(300));
                app.EndRootWindow();
            });
        };
        emit();
        app.ClickAt(app.CenterOf(FindByStyle(app.theme.gradientSwatchStyleRef)));
        emit();
        emit();
        ASSERT_NE(FindByStyle(app.theme.gradientEditorBarStyleRef), ID::Invalid());
        app.ClickAt(app.CenterOf(FindByStyle(app.theme.dialogPrimaryButtonStyleRef)));
        emit();
        emit();
        EXPECT_FALSE(changed);
        EXPECT_EQ(gradient, before);
        EXPECT_EQ(FindByStyle(app.theme.gradientEditorBarStyleRef), ID::Invalid()) << "closed";
    }

    // A Gradient field's editor stops at the Gradient's 8 stops, and has no per-stop blend.
    TEST_F(GradientEditorDialog, AGradientsEditorKeepsToItsEightStops)
    {
        for (int i = 2; i < Gradient::kMaxStops; ++i)
            gradient.AddStop(color4f{ 0, 1, 0, 1 }, float(i) / 10.0f);
        Open();
        EXPECT_EQ(app.FindByText("To next"), ID::Invalid()) << "no per-stop blend";
        ClickAt(OnTheBar(0.95f));
        EXPECT_EQ(gradient.stopCount, Gradient::kMaxStops) << "no ninth stop";
    }

    // The editor on a GradientF: more than 8 stops, colors over 1 kept, each stop's blend to
    // the next, and Reverse keeping each stretch's blend.
    class GradientFEditorDialog : public GradientEditorDialog
    {
    protected:
        void EmitF()
        {
            app.Frame([&] {
                app.BeginRootWindow({ 1200, 900 });
                app.GradientField(floatGradient, LayoutOverride{}.Width(300));
                app.EndRootWindow();
            });
        }
        void OpenF()
        {
            EmitF();
            app.ClickAt(app.CenterOf(FindByStyle(app.theme.gradientSwatchStyleRef)));
            EmitF();
            EmitF();
        }
        void ClickF(int2 pos)
        {
            app.ClickAt(pos);
            EmitF();
            EmitF();
        }
        GradientF floatGradient;
    };

    TEST_F(GradientFEditorDialog, ItTakesMoreThanEightStops)
    {
        for (int i = 0; i < Gradient::kMaxStops; ++i)
            floatGradient.AddStop(color4f{ float(i) / 8.0f, 0, 0, 1 }, float(i) / 10.0f);
        OpenF();
        ClickF(OnTheBar(0.95f));
        EXPECT_EQ(floatGradient.stops.size(), size_t(Gradient::kMaxStops + 1)) << "a ninth stop";
    }

    TEST_F(GradientFEditorDialog, ColorsOverOneAndBlendsAreKept)
    {
        floatGradient.AddStop(color4f{ 4.0f, 2.0f, 0.5f, 1.0f }, 0.0f, StopInterpolation::Constant);
        floatGradient.AddStop(color4f{ 0.0f, 0.0f, 0.0f, 1.0f }, 1.0f);
        const GradientF before = floatGradient;
        OpenF();
        EXPECT_NE(app.FindByText("To next"), ID::Invalid()) << "the selected stop's blend";
        EXPECT_NE(app.FindByText("Blend"), ID::Invalid()) << "and each stop's in the table";
        EXPECT_NE(app.FindByText("Constant"), ID::Invalid());
        ClickF(app.CenterOf(FindByStyle(app.theme.dialogPrimaryButtonStyleRef)));
        EXPECT_EQ(floatGradient, before) << "OK untouched keeps HDR and the blend";

        // A stop added on the bar blends on as the stop before it did.
        OpenF();
        ClickF(OnTheBar(0.5f));
        ASSERT_EQ(floatGradient.stops.size(), 3u);
        EXPECT_EQ(floatGradient.stops[1].interpolation, StopInterpolation::Constant);
        EXPECT_FLOAT_EQ(floatGradient.stops[1].color.r, 4.0f) << "its color: the step's, over 1";
    }

    // In a row 300 wide, as in an inspector.
    void EmitInARow(HeadlessGUIApp& app, GradientF& gradient)
    {
        for (int i = 0; i < 2; ++i)
            app.Frame([&] {
                app.BeginRootWindow({ 1200, 900 });
                app.BeginHorizontal(app.idManager.AllocateDynamicID("row"), app.theme.emptyHorzStyleRef, LayoutOverride::FixedWidth(300));
                app.GradientField(gradient);
                app.EndHorizontal();
                app.EndRootWindow();
            });
    }

    // Points at the swatch and waits past the tooltip's delay.
    void HoverTheSwatch(HeadlessGUIApp& app, GradientF& gradient, ID swatch)
    {
        app.InjectMouseMove(app.CenterOf(swatch));
        for (int i = 0; i < 8; ++i)
        {
            app.AdvanceTime(100);
            EmitInARow(app, gradient);
        }
    }

    // A gradient brighter than 1 says how bright in the swatch's tooltip; the bar stays the
    // whole field.
    TEST_F(GradientFEditorDialog, AnHdrGradientsTooltipSaysHowBrightItGets)
    {
        floatGradient.AddStop(color4f{ 0.0f, 0.0f, 0.0f, 1.0f }, 0.0f);
        floatGradient.AddStop(color4f{ 8.0f, 6.0f, 2.0f, 1.0f }, 1.0f);
        EmitInARow(app, floatGradient);
        const ID swatch = FindByStyle(app.theme.gradientSwatchStyleRef);
        EXPECT_EQ(app.GetElementRect(swatch).w, app.styleManager.Px(300)) << "the bar is the whole field";
        HoverTheSwatch(app, floatGradient, swatch);
        EXPECT_NE(app.FindByText("Up to 8.0 \xC3\x97 white"), ID::Invalid());
    }

    TEST_F(GradientFEditorDialog, AGradientUpToOneHasNoBrightnessTooltip)
    {
        floatGradient.AddStop(color4f{ 0.0f, 0.0f, 0.0f, 1.0f }, 0.0f);
        floatGradient.AddStop(color4f{ 1.0f, 1.0f, 1.0f, 1.0f }, 1.0f);
        EmitInARow(app, floatGradient);
        HoverTheSwatch(app, floatGradient, FindByStyle(app.theme.gradientSwatchStyleRef));
        EXPECT_EQ(app.FindByText("Up to 1.0 \xC3\x97 white"), ID::Invalid());
    }

    TEST_F(GradientFEditorDialog, ReverseKeepsEachStretchsBlend)
    {
        floatGradient.AddStop(color4f{ 1, 0, 0, 1 }, 0.0f, StopInterpolation::Constant);
        floatGradient.AddStop(color4f{ 0, 1, 0, 1 }, 0.5f, StopInterpolation::Smooth);
        floatGradient.AddStop(color4f{ 0, 0, 1, 1 }, 1.0f);
        OpenF();
        const ID reverse = app.FindByText("Reverse");
        ASSERT_NE(reverse, ID::Invalid());
        ClickF(app.CenterOf(reverse));
        ASSERT_EQ(floatGradient.stops.size(), 3u);
        EXPECT_EQ(floatGradient.stops[0].color, (color4f{ 0, 0, 1, 1 }));
        EXPECT_EQ(floatGradient.stops[0].interpolation, StopInterpolation::Smooth) << "blue to green was smooth";
        EXPECT_EQ(floatGradient.stops[1].interpolation, StopInterpolation::Constant) << "green to red was the step";
        EXPECT_EQ(floatGradient.stops[2].interpolation, StopInterpolation::Linear);
        EXPECT_FLOAT_EQ(floatGradient.stops[2].offset, 1.0f);
    }

    TEST_F(GradientEditorDialog, TheHeaderDragsTheDialog)
    {
        Open();
        const ID window = GUIApplicationInternals::GradientEditor(app).State().windowID;
        const IntRect header = app.GetElementRect(FindByStyle(app.theme.dialogHeaderStyleRef));
        EXPECT_EQ(app.TitleBarHitTest(window, { header.x + header.w / 2, header.y + header.h / 2 }), WindowHitArea::Caption);
        EXPECT_EQ(app.TitleBarHitTest(window, app.CenterOf(FindByStyle(app.theme.dialogCloseStyleRef))), WindowHitArea::Client);
    }

    // As for the color picker: after a theme reload it creates its styles again.
    TEST_F(GradientEditorDialog, AThemeReloadRecreatesItsStyles)
    {
        Open();
        auto& ge = GUIApplicationInternals::GradientEditor(app).State();
        ge.stopHaloColor = app.theme.accentColor;
        GUIApplicationInternals::ReloadThemeNextFrame(app);
        Emit();
        ASSERT_NE(ge.stopHaloColor, VisualColorStyleIndex::Invalid);
        EXPECT_NE(ge.stopHaloColor, app.theme.accentColor);
    }
    // A stop grabbed off its centre doesn't jump to the cursor; dragging moves it from there,
    // and Escape puts the gradient back with the dialog still open.
    TEST_F(GradientEditorDialog, AGrabbedStopMovesFromWhereItWasAndEscapePutsItBack)
    {
        Open();
        const auto& working = GUIApplicationInternals::GradientEditor(app).State().workingCopy;
        ASSERT_EQ(working.stops.size(), 2u);
        const int2 bar = OnTheBar(0.0f);
        // The stops' handles hang under the bar (44 px), past its pointer (6 px), 18 px tall.
        const int2 grab{ bar.x + app.styleManager.Px(5), bar.y - app.styleManager.Px(20) + app.styleManager.Px(44 + 6 + 9) };
        app.InjectMouseMove(grab);
        app.InjectMouseDown(grab);
        Emit();
        app.InjectMouseMove(grab, MouseButtons::Left);
        Emit();
        EXPECT_FLOAT_EQ(working.stops[0].offset, 0.0f) << "grabbed 5 px to the right, the stop stays put";

        const int2 to = { OnTheBar(0.25f).x + app.styleManager.Px(5), grab.y };
        app.InjectMouseMove(to, MouseButtons::Left);
        Emit();
        EXPECT_NEAR(working.stops[0].offset, 0.25f, 0.01f) << "and moves as far as the cursor does";

        app.InjectKey(KeyCode::Escape);
        Emit();
        EXPECT_FLOAT_EQ(working.stops[0].offset, 0.0f);
        EXPECT_EQ(working.stops.size(), 2u);
        EXPECT_NE(FindByStyle(app.theme.gradientEditorBarStyleRef), ID::Invalid()) << "only the drag was cancelled";

        app.InjectMouseMove({ to.x + 30, to.y }, MouseButtons::Left);
        app.InjectMouseUp({ to.x + 30, to.y });
        Emit();
        EXPECT_FLOAT_EQ(working.stops[0].offset, 0.0f) << "the moves after Escape are ignored";
    }
}
