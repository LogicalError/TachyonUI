// ColorField: a swatch, the color as hex and its alpha in percent; typing a hex color sets it.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_controls/controls_color_internal.h>
#include <gui_controls/gui_application_internal.h>
#include <gui_controls/subsystems/gui_color_picker_subsystem.h>

namespace
{
    using namespace tachyon;
    using GUITest::HeadlessGUIApp;

    class ColorFieldHex : public ::testing::Test
    {
    protected:
        void SetUp() override { ASSERT_TRUE(app.Initialize()); }

        void Emit()
        {
            app.Frame([&] {
                app.BeginRootWindow({ 800, 600 });
                app.ColorField(color, false, LayoutOverride{}.Width(300));
                app.EndRootWindow();
            });
        }

        // The first element drawn in `style`.
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

        HeadlessGUIApp app;
        color4f        color{ 0.0f, 0.0f, 1.0f, 0.5f };
    };

    TEST_F(ColorFieldHex, HasASwatchAHexFieldAndAlpha)
    {
        Emit();
        EXPECT_NE(FindByStyle(app.theme.colorSwatchStyleRef), ID::Invalid());
        EXPECT_NE(FindByStyle(app.theme.colorHexStyleRef), ID::Invalid());
        EXPECT_NE(FindByStyle(app.theme.colorAlphaInputStyleRef), ID::Invalid());
        EXPECT_EQ(app.GetElementRect(FindByStyle(app.theme.colorSwatchStyleRef)).w, app.styleManager.Px(56));
    }

    TEST_F(ColorFieldHex, TypingAHexColorSetsIt)
    {
        Emit();
        const ID hexID = FindByStyle(app.theme.colorHexStyleRef);
        ASSERT_NE(hexID, ID::Invalid());
        app.ClickAt(app.CenterOf(hexID));
        Emit();
        app.InjectKey(KeyCode::End);
        for (int i = 0; i < 6; ++i)
            app.InjectKey(KeyCode::Backspace);
        Emit();
        app.InjectText("FF0000");
        Emit();
        app.InjectKey(KeyCode::Return);
        Emit();
        Emit();
        EXPECT_FLOAT_EQ(color.r, 1.0f);
        EXPECT_FLOAT_EQ(color.g, 0.0f);
        EXPECT_FLOAT_EQ(color.b, 0.0f);
        EXPECT_FLOAT_EQ(color.a, 0.5f) << "the alpha is left alone";
    }

    // The swatch opens the picker dialog; a click on its square changes the color.
    class ColorPickerDialog : public ColorFieldHex
    {
    protected:
        void Open()
        {
            Emit();
            app.ClickAt(app.CenterOf(FindByStyle(app.theme.colorSwatchStyleRef)));
            Emit();
            Emit();
        }
        void Click(StyleRef style)
        {
            const ID id = FindByStyle(style);
            ASSERT_NE(id, ID::Invalid());
            app.ClickAt(app.CenterOf(id));
            Emit();
            Emit();
        }
    };

    TEST_F(ColorPickerDialog, TheSwatchOpensIt)
    {
        Open();
        const ID square = FindByStyle(app.theme.colorPickerSquareStyleRef);
        ASSERT_NE(square, ID::Invalid()) << "the dialog is open";
        EXPECT_EQ(app.GetElementRect(square).w, app.styleManager.Px(240));
        EXPECT_NE(FindByStyle(app.theme.dialogPrimaryButtonStyleRef), ID::Invalid()) << "with an OK button";
    }

    // New over Original, each named beside its half, to the right of the swatch.
    TEST_F(ColorPickerDialog, NewIsOverOriginalNamedBeside)
    {
        Open();
        const ID preview = FindByStyle(app.theme.colorPickerPreviewStyleRef);
        const ID newText = app.FindByText("New"), originalText = app.FindByText("Original");
        ASSERT_NE(preview, ID::Invalid());
        ASSERT_NE(newText, ID::Invalid());
        ASSERT_NE(originalText, ID::Invalid());
        const IntRect p = app.GetElementRect(preview), n = app.GetElementRect(newText), o = app.GetElementRect(originalText);
        EXPECT_GE(n.x, p.x + p.w) << "beside the swatch";
        EXPECT_GE(o.x, p.x + p.w);
        const int mid = p.y + p.h / 2;
        EXPECT_LE(n.y + n.h / 2, mid) << "New by the top half";
        EXPECT_GE(o.y + o.h / 2, mid) << "Original by the bottom half";
    }

    // The value range is in the footer, after the format: "sRGB . 8-bit . 0-255"; a click
    // switches it to 0-1.
    TEST_F(ColorPickerDialog, TheRangeIsInTheFooter)
    {
        Open();
        const ID footer = FindByStyle(app.theme.dialogFooterStyleRef);
        const ID range  = app.FindByText("0\xE2\x80\x93" "255");
        const ID status = app.FindByText("sRGB \xC2\xB7 8-bit \xC2\xB7");
        ASSERT_NE(footer, ID::Invalid());
        ASSERT_NE(range, ID::Invalid());
        ASSERT_NE(status, ID::Invalid());
        const IntRect f = app.GetElementRect(footer), r = app.GetElementRect(range), s = app.GetElementRect(status);
        EXPECT_GE(r.y, f.y);
        EXPECT_LE(r.y + r.h, f.y + f.h) << "in the footer";
        EXPECT_GE(r.x, s.x + s.w) << "after the format";
        app.ClickAt(app.CenterOf(range));
        Emit();
        Emit();
        EXPECT_NE(app.FindByText("0\xE2\x80\x93" "1"), ID::Invalid());
    }

    // The app gives the swatches, and adds recent colors as OK does: newest first, each
    // once, at most 8.
    TEST_F(ColorPickerDialog, TheAppSetsTheSwatchesAndAddsRecentColors)
    {
        const color4f palette[] = { { 1, 0, 0, 1 }, { 0, 1, 0, 1 }, { 0, 0, 1, 0.5f } };
        app.SetColorSwatches(palette);
        for (int i = 0; i < 10; ++i)
            app.AddRecentColor(color4f{ float(i) / 10.0f, 0, 0, 1 });
        app.AddRecentColor(color4f{ 0.5f, 0, 0, 1 });   // already there: moved to the front
        ASSERT_EQ(app.ColorSwatches().size(), 3u);
        EXPECT_EQ(app.ColorSwatches()[2], palette[2]);

        Open();
        auto count = [&](StyleRef style) {
            int n = 0;
            const auto& storage = app.elementStorage;
            for (auto it = storage.TopDownDepthScanBegin(); it != storage.TopDownDepthScanEnd(); ++it)
                for (const Element& e : it->GetChildren(storage))
                    if (e.styleRef == style) ++n;
            return n;
        };
        EXPECT_EQ(count(app.theme.colorPickerSwatchStyleRef), 3);
        EXPECT_EQ(count(app.theme.colorPickerRecentStyleRef), 8) << "at most 8, the field's own color first";
    }

    // With nothing set, the swatches are the theme's palette of twelve.
    TEST_F(ColorPickerDialog, TheDefaultSwatchesAreTwelve)
    {
        EXPECT_EQ(app.ColorSwatches().size(), 12u);
    }

    TEST_F(ColorPickerDialog, CancelRestoresTheColor)
    {
        const color4f original = color;
        Open();
        Click(app.theme.colorPickerSquareStyleRef);
        EXPECT_NE(color.r, original.r) << "the square changed the color";
        Click(app.theme.dialogButtonStyleRef);
        EXPECT_EQ(color.r, original.r);
        EXPECT_EQ(color.b, original.b);
        EXPECT_EQ(FindByStyle(app.theme.colorPickerSquareStyleRef), ID::Invalid()) << "and closed the dialog";
    }

    TEST_F(ColorPickerDialog, OkKeepsTheColor)
    {
        const color4f original = color;
        Open();
        Click(app.theme.colorPickerSquareStyleRef);
        const color4f picked = color;
        EXPECT_NE(picked.r, original.r);
        Click(app.theme.dialogPrimaryButtonStyleRef);
        EXPECT_EQ(color.r, picked.r);
        EXPECT_EQ(FindByStyle(app.theme.colorPickerSquareStyleRef), ID::Invalid()) << "and closed the dialog";
    }

    // Escape in the dialog is Cancel, and the keyboard goes back to the swatch.
    TEST_F(ColorPickerDialog, EscapeCancels)
    {
        const color4f original = color;
        Open();
        Click(app.theme.colorPickerSquareStyleRef);
        ASSERT_NE(color.r, original.r);
        app.activeWindow = app.m_colorPickerSub->State().windowID;   // the dialog has the keyboard
        Emit();
        app.InjectKey(KeyCode::Escape);
        Emit();
        Emit();
        app.activeWindow = ID::Invalid();
        EXPECT_EQ(color.r, original.r);
        EXPECT_EQ(color.b, original.b);
        EXPECT_EQ(FindByStyle(app.theme.colorPickerSquareStyleRef), ID::Invalid()) << "closed";
        EXPECT_EQ(app.guiStateManager.Interaction().GetFocusedID(), FindByStyle(app.theme.colorSwatchStyleRef));
    }

    // The square and the strips work from the keyboard: arrows move their point.
    TEST_F(ColorPickerDialog, TheArrowsMoveTheSquareAndStrips)
    {
        Open();
        app.activeWindow = app.m_colorPickerSub->State().windowID;
        Emit();
        auto press = [&](ID id, KeyCode key, KeyModifier mods = KeyModifier::None) {
            app.guiStateManager.Interaction().SetFocusID(id);
            Emit();
            app.InjectKey(key, KeyAction::KeyDown, mods);
            Emit();
            Emit();
        };
        const ID square = FindByStyle(app.theme.colorPickerSquareStyleRef);
        ASSERT_NE(square, ID::Invalid());
        const color4f before = color;
        press(square, KeyCode::Down, KeyModifier::Shift);
        EXPECT_NE(color, before) << "Down on the square changes the color";

        const color4f mid = color;
        press(square, KeyCode::Left, KeyModifier::Shift);
        EXPECT_NE(color, mid) << "and so does Left";

        // The strips: hue first, then alpha.
        std::vector<ID> strips;
        const auto& storage = app.elementStorage;
        for (auto it = storage.TopDownDepthScanBegin(); it != storage.TopDownDepthScanEnd(); ++it)
            for (const Element& e : it->GetChildren(storage))
                if (e.styleRef.layoutStyleIndex == app.theme.colorPickerStripStyleRef.layoutStyleIndex
                    && e.styleRef.visualBoxStyleIndex == app.theme.colorPickerStripStyleRef.visualBoxStyleIndex)
                    strips.push_back(e.id);
        ASSERT_EQ(strips.size(), 2u);
        press(strips[1], KeyCode::End);
        EXPECT_NEAR(color.a, 0.0f, 1e-4f) << "End takes the alpha strip to its bottom";
        press(strips[1], KeyCode::Up, KeyModifier::Shift);
        EXPECT_NEAR(color.a, 0.1f, 1e-3f);
        app.activeWindow = ID::Invalid();
    }

    // An Escape meant for another window leaves the dialog open.
    TEST_F(ColorPickerDialog, EscapeElsewhereLeavesItOpen)
    {
        Open();
        app.guiStateManager.Interaction().SetFocusID(ID::Invalid());
        app.InjectKey(KeyCode::Escape);
        Emit();
        Emit();
        EXPECT_NE(FindByStyle(app.theme.colorPickerSquareStyleRef), ID::Invalid());
    }

    // The header is the dialog's title bar: it drags the window, its close button doesn't.
    TEST_F(ColorPickerDialog, TheHeaderDragsTheDialog)
    {
        Open();
        const ID window = GUIApplicationInternals::ColorPicker(app).State().windowID;
        const IntRect header = app.GetElementRect(FindByStyle(app.theme.dialogHeaderStyleRef));
        EXPECT_EQ(app.TitleBarHitTest(window, { header.x + header.w / 2, header.y + header.h / 2 }), WindowHitArea::Caption);
        EXPECT_EQ(app.TitleBarHitTest(window, app.CenterOf(FindByStyle(app.theme.dialogCloseStyleRef))), WindowHitArea::Client);
    }

    // A theme reload frees every style; the open picker must create its own again rather
    // than keep writing through indices the new theme now uses (here: the accent's).
    TEST_F(ColorPickerDialog, AThemeReloadRecreatesItsStyles)
    {
        Open();
        auto& cp = GUIApplicationInternals::ColorPicker(app).State();
        cp.hueStrip = app.theme.accentColor;
        GUIApplicationInternals::ReloadThemeNextFrame(app);
        Emit();
        ASSERT_NE(cp.hueStrip, VisualColorStyleIndex::Invalid);
        EXPECT_NE(cp.hueStrip, app.theme.accentColor);
    }

    // The hue strip goes around the whole hue circle, red back to red, within the
    // gradient's stop limit (more stops used to be dropped, cutting it off at cyan).
    TEST(ColorPickerHueStrip, GoesAllTheWayAround)
    {
        const Gradient g = HueStripGradient(ColorSpace::HSV);
        ASSERT_GE(g.stopCount, 2);
        const color4f first = g.stops[0].GetColor4f();
        const color4f last  = g.stops[size_t(g.stopCount - 1)].GetColor4f();
        EXPECT_FLOAT_EQ(g.stops[size_t(g.stopCount - 1)].GetOffset(), 1.0f);
        EXPECT_EQ(first.r, 1.0f); EXPECT_EQ(first.g, 0.0f); EXPECT_EQ(first.b, 0.0f);
        EXPECT_EQ(last.r,  1.0f); EXPECT_EQ(last.g,  0.0f); EXPECT_EQ(last.b,  0.0f);
    }

    // A dialog's header names the edited field after its title, unless it only repeats it.
    TEST(DialogHeader, ShowsTheFieldUnlessItRepeatsTheTitle)
    {
        EXPECT_EQ(DialogContext("Base color", "Color"), "Base color");
        EXPECT_EQ(DialogContext("Color", "Color"), "");
        EXPECT_EQ(DialogContext("color", "Color"), "") << "regardless of case";
        EXPECT_EQ(DialogContext("", "Color"), "");
    }

    // The eyedropper: pressed and released, the color is the screen's under the cursor, opaque.
    TEST_F(ColorPickerDialog, TheEyedropperPicksTheScreensColor)
    {
        using ::testing::_;
        using ::testing::Return;
        ON_CALL(app.mockBackend, ReadScreenPixel(_)).WillByDefault(Return(std::optional<color4b>(color4b{ 255, 0, 0, 255 })));
        Open();
        const ID eyedropper = FindByStyle(app.theme.dialogSquareButtonStyleRef);
        ASSERT_NE(eyedropper, ID::Invalid()) << "shown where the screen can be read";
        Click(app.theme.dialogSquareButtonStyleRef);
        EXPECT_FLOAT_EQ(color.r, 1.0f);
        EXPECT_FLOAT_EQ(color.g, 0.0f);
        EXPECT_FLOAT_EQ(color.b, 0.0f);
        EXPECT_FLOAT_EQ(color.a, 1.0f) << "the field was half transparent; the screen isn't";
    }

    TEST_F(ColorPickerDialog, NoEyedropperWhereTheScreenCantBeRead)
    {
        Open();
        EXPECT_EQ(FindByStyle(app.theme.dialogSquareButtonStyleRef), ID::Invalid());
    }
    // Escape while dragging on the square puts the color back and leaves the dialog open.
    TEST_F(ColorPickerDialog, EscapeDuringADragPutsTheColorBack)
    {
        const color4f original = color;
        Open();
        const ID square = FindByStyle(app.theme.colorPickerSquareStyleRef);
        ASSERT_NE(square, ID::Invalid());
        const IntRect r = app.GetElementRect(square);
        const int2 from{ r.x + r.w / 4, r.y + r.h / 4 };
        app.InjectMouseMove(from);
        app.InjectMouseDown(from);
        Emit();
        app.InjectMouseMove({ r.x + r.w * 3 / 4, r.y + r.h * 3 / 4 }, MouseButtons::Left);
        Emit();
        Emit();
        ASSERT_NE(color.r, original.r) << "test setup: the drag changes the color";

        app.InjectKey(KeyCode::Escape);
        Emit();
        Emit();
        EXPECT_NEAR(color.r, original.r, 1e-4f);
        EXPECT_NEAR(color.g, original.g, 1e-4f);
        EXPECT_NEAR(color.b, original.b, 1e-4f);
        EXPECT_NE(FindByStyle(app.theme.colorPickerSquareStyleRef), ID::Invalid()) << "only the drag was cancelled";

        app.InjectMouseMove({ r.x + r.w / 2, r.y + r.h / 2 }, MouseButtons::Left);
        app.InjectMouseUp({ r.x + r.w / 2, r.y + r.h / 2 });
        Emit();
        Emit();
        EXPECT_NEAR(color.r, original.r, 1e-4f) << "the moves after Escape are ignored";
    }
}
