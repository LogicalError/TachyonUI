// Theme variables: what @property rules say about them, the check of their values, and
// !important; a variable's colors kept apart in the style manager so a new value can be
// written into them; and the app's per-theme changes, shown at once, reset and saved.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/style/gui_style_manager.h>
#include <gui_core/style/gui_style_sheet.h>
#include <gui_core/diagnostics/gui_logging.h>

#include <string>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        bool HasErrorWith(const StyleSheet& sheet, std::string_view text)
        {
            for (const ParseError& e : sheet.errors)
                if (e.message.find(text) != std::string::npos)
                    return true;
            return false;
        }

        // ---------------------------------------------------------------------
        // @property rules
        // ---------------------------------------------------------------------

        TEST(ThemeVariables, ARuleSaysWhatAVariableIs)
        {
            const StyleSheet sheet = ParseStyleSheet(R"(
                @property --radius {
                    syntax: "<length>";
                    category: "Sizes";
                    label: "Corners";
                    description: "Corner radius of controls";
                    range: 0px 16px;
                }
                @property --panel { syntax: "<color>"; category: "Surfaces"; label: "Panel"; }
                :root { --radius: 4px; --panel: #121418; }
            )");
            EXPECT_FALSE(sheet.HasErrors());
            ASSERT_EQ(sheet.propertyInfos.size(), 2u);
            const CustomPropertyInfo& radius = sheet.propertyInfos[0];
            EXPECT_EQ(radius.name, "--radius");
            EXPECT_EQ(radius.type, CustomPropertyType::Length);
            EXPECT_EQ(radius.category, "Sizes");
            EXPECT_EQ(radius.label, "Corners");
            EXPECT_EQ(radius.description, "Corner radius of controls");
            EXPECT_TRUE(radius.hasRange);
            EXPECT_EQ(radius.min, 0.0f);
            EXPECT_EQ(radius.max, 16.0f);
            EXPECT_EQ(radius.line, 2);
            EXPECT_EQ(sheet.GetPropertyInfo("--panel")->type, CustomPropertyType::Color);
            EXPECT_FALSE(sheet.GetPropertyInfo("--panel")->hasRange);
            EXPECT_EQ(sheet.GetPropertyInfo("--other"), nullptr);
        }

        TEST(ThemeVariables, EverySyntax)
        {
            const std::pair<const char*, CustomPropertyType> syntaxes[] = {
                { "*", CustomPropertyType::Any },           { "<color>", CustomPropertyType::Color },
                { "<number>", CustomPropertyType::Number }, { "<length>", CustomPropertyType::Length },
                { "<length>+", CustomPropertyType::Lengths }, { "<edges>", CustomPropertyType::Edges },
                { "<corners>", CustomPropertyType::Corners }, { "<shadow>", CustomPropertyType::Shadow },
            };
            for (const auto& [syntax, type] : syntaxes)
            {
                const StyleSheet sheet = ParseStyleSheet(std::format("@property --v {{ syntax: \"{}\"; }}", syntax));
                EXPECT_FALSE(sheet.HasErrors()) << syntax;
                ASSERT_EQ(sheet.propertyInfos.size(), 1u) << syntax;
                EXPECT_EQ(sheet.propertyInfos[0].type, type) << syntax;
            }
        }

        // A second rule for a name replaces the first, where the first was.
        TEST(ThemeVariables, ASecondRuleReplacesTheFirstInItsPlace)
        {
            const StyleSheet sheet = ParseStyleSheet(R"(
                @property --a { label: "One"; }
                @property --b { label: "Two"; }
                @property --a { label: "Three"; }
            )");
            ASSERT_EQ(sheet.propertyInfos.size(), 2u);
            EXPECT_EQ(sheet.propertyInfos[0].name, "--a");
            EXPECT_EQ(sheet.propertyInfos[0].label, "Three");
            EXPECT_EQ(sheet.propertyInfos[1].name, "--b");
        }

        // The standard descriptors with no use here are passed over; anything else is an error.
        TEST(ThemeVariables, WhatARuleCantSay)
        {
            EXPECT_FALSE(ParseStyleSheet(R"(@property --a { inherits: false; initial-value: 4px; label: "A"; })").HasErrors());
            EXPECT_EQ(ParseStyleSheet(R"(@property --a { inherits: false; initial-value: 4px; label: "A"; })").propertyInfos[0].label, "A");

            EXPECT_TRUE(HasErrorWith(ParseStyleSheet(R"(@property --a { syntax: "<angle>"; })"), "Unsupported @property syntax"));
            EXPECT_TRUE(HasErrorWith(ParseStyleSheet(R"(@property --a { syntax: color; })"), "quoted syntax"));
            EXPECT_TRUE(HasErrorWith(ParseStyleSheet(R"(@property --a { colour: "x"; })"), "Unsupported @property descriptor"));
            EXPECT_TRUE(HasErrorWith(ParseStyleSheet(R"(@property --a { label: Panel; })"), "quoted string"));
            EXPECT_TRUE(HasErrorWith(ParseStyleSheet(R"(@property --a { range: 4 1; })"), "min is above its max"));
            EXPECT_TRUE(HasErrorWith(ParseStyleSheet(R"(@property --a { range: 4; })"), "range: <min> <max>"));
            EXPECT_TRUE(HasErrorWith(ParseStyleSheet(R"(@property a { label: "A"; })"), "custom property name"));
            EXPECT_TRUE(HasErrorWith(ParseStyleSheet(R"(@property --a { label: "A" category: "B"; })"), "Expected ';'"));
            // A bad descriptor leaves the rest of the rule.
            const StyleSheet sheet = ParseStyleSheet(R"(@property --a { colour: "x"; label: "A"; })");
            ASSERT_EQ(sheet.propertyInfos.size(), 1u);
            EXPECT_EQ(sheet.propertyInfos[0].label, "A");
        }

        // A value that isn't what its rule says, or outside its range, is reported, wherever
        // the rule is.
        TEST(ThemeVariables, ValuesAreChecked)
        {
            EXPECT_TRUE(HasErrorWith(ParseStyleSheet(R"(@property --a { syntax: "<color>"; } :root { --a: 4px; })"),
                                     R"(--a is "<color>" by its @property rule, not '4px')"));
            EXPECT_TRUE(HasErrorWith(ParseStyleSheet(R"(:root { --a: 4px; } @property --a { syntax: "<color>"; })"),
                                     "by its @property rule")) << "the rule after the value";
            EXPECT_TRUE(HasErrorWith(ParseStyleSheet(R"(@property --a { syntax: "<length>"; } :root { --a: 1px 2px; })"),
                                     "by its @property rule")) << "one length, not two";
            EXPECT_TRUE(HasErrorWith(ParseStyleSheet(R"(@property --a { syntax: "<number>"; } :root { --a: red; })"),
                                     "by its @property rule"));
            EXPECT_TRUE(HasErrorWith(ParseStyleSheet(R"(@property --a { syntax: "<shadow>"; } :root { --a: red; })"),
                                     "by its @property rule"));
            EXPECT_TRUE(HasErrorWith(ParseStyleSheet(R"(@property --a { syntax: "<edges>"; } :root { --a: red; })"),
                                     "by its @property rule"));
            EXPECT_FALSE(ParseStyleSheet(R"(@property --a { syntax: "<edges>"; } :root { --a: 1px 2px 3px 4px; })").HasErrors());
            EXPECT_FALSE(ParseStyleSheet(R"(@property --a { syntax: "<corners>"; } :root { --a: 1px; })").HasErrors());
            EXPECT_FALSE(ParseStyleSheet(R"(@property --a { syntax: "<length>+"; } :root { --a: 1px 2px; })").HasErrors());
            EXPECT_FALSE(ParseStyleSheet(R"(@property --a { syntax: "<shadow>"; } :root { --a: none; })").HasErrors());
            EXPECT_FALSE(ParseStyleSheet(R"(@property --a { syntax: "*"; } :root { --a: 0 1px 2px red; })").HasErrors());
            EXPECT_FALSE(ParseStyleSheet(R"(@property --a { syntax: "<color>"; })").HasErrors()) << "a rule without a value";

            EXPECT_TRUE(HasErrorWith(ParseStyleSheet(R"(@property --a { syntax: "<number>"; range: 0 1; } :root { --a: 1.5; })"),
                                     "--a is 1.5, outside its range of 0 to 1"));
            EXPECT_FALSE(ParseStyleSheet(R"(@property --a { syntax: "<number>"; range: 0 1; } :root { --a: 1; })").HasErrors());
            EXPECT_FALSE(ParseStyleSheet(R"(@property --a { syntax: "<number>"; range: 0 1; } :root { --a: 0; })").HasErrors());
            EXPECT_TRUE(HasErrorWith(ParseStyleSheet(R"(@property --a { syntax: "<number>"; range: 2 4; } :root { --a: 1; })"),
                                     "outside its range")) << "below it";
            EXPECT_FALSE(ParseStyleSheet(R"(@property --a { syntax: "<color>"; range: 2 4; } :root { --a: #FFFFFF; })").HasErrors())
                << "a range says nothing of a color";
            EXPECT_TRUE(HasErrorWith(ParseStyleSheet(R"(@property --a { syntax: "<edges>"; range: 0 4; } :root { --a: 1px 5px; })"),
                                     "outside its range")) << "each of them";
            EXPECT_FALSE(ParseStyleSheet(R"(@property --a { syntax: "<edges>"; range: 0 4; } :root { --a: 1px 4px; })").HasErrors());
        }

        // ---------------------------------------------------------------------
        // Values as written, and !important
        // ---------------------------------------------------------------------

        TEST(ThemeVariables, AValueKeepsItsTextAndLine)
        {
            const StyleSheet sheet = ParseStyleSheet(":root {\n  --a: #121418;\n  --b: 4px 0px  0px 4px ;\n  --c: 0 8px 20px rgba(0, 0, 0, 0.35);\n  --d: none;\n}");
            EXPECT_EQ(sheet.GetCustomProperty("--a")->text, "#121418");
            EXPECT_EQ(sheet.GetCustomProperty("--a")->line, 2);
            EXPECT_EQ(sheet.GetCustomProperty("--b")->text, "4px 0px  0px 4px") << "without the space before the ';'";
            EXPECT_EQ(sheet.GetCustomProperty("--c")->text, "0 8px 20px rgba(0, 0, 0, 0.35)");
            EXPECT_EQ(sheet.GetCustomProperty("--d")->text, "none");
            EXPECT_EQ(sheet.GetCustomProperty("--d")->line, 5);
        }

        // An important value stays through later plain ones; a later important one replaces it.
        TEST(ThemeVariables, ImportantWins)
        {
            StyleSheet sheet = ParseStyleSheet(":root { --a: #FF0000 !important; } :root { --a: #0000FF; }");
            EXPECT_FALSE(sheet.HasErrors());
            const CustomProperty* a = sheet.GetCustomProperty("--a");
            EXPECT_EQ(a->color, (color4b{ 255, 0, 0, 255 }));
            EXPECT_TRUE(a->important);
            EXPECT_EQ(a->text, "#FF0000") << "the text without !important";

            sheet = ParseStyleSheet(":root { --a: 1px !important; --a: 2px !important; --a: 3px; }");
            EXPECT_EQ(sheet.GetCustomProperty("--a")->number, 2.0f);
            sheet = ParseStyleSheet(":root { --a: 0 1px 2px #000000 !important; --a: none; }");
            EXPECT_FALSE(sheet.GetCustomProperty("--a")->shadows[0].IsNone());
            sheet = ParseStyleSheet(":root { --a: none !important; --a: 0 1px 2px #000000; }");
            EXPECT_TRUE(sheet.GetCustomProperty("--a")->shadows[0].IsNone());
            sheet = ParseStyleSheet(":root { --a: 1px; }");
            EXPECT_FALSE(sheet.GetCustomProperty("--a")->important);

            EXPECT_TRUE(HasErrorWith(ParseStyleSheet(":root { --a: 1px !importent; }"), "Expected 'important' after '!'"));
            EXPECT_TRUE(HasErrorWith(ParseStyleSheet(":root { --a: #FFF red; }"), "Unexpected 'red' in --a"));
        }

        // ---------------------------------------------------------------------
        // A variable's colors
        // ---------------------------------------------------------------------

        // A color from var() is numbered after its variable; a literal one isn't.
        TEST(ThemeVariables, ColorsKnowTheirVariable)
        {
            const StyleSheet sheet = ParseStyleSheet(R"(
                :root { --panel: #121418; --text: #E8EAEF; --alias: var(--panel); }
                .a { background-color: var(--panel); color: var(--text); }
                .b { background-color: var(--panel); border-color: #FF0000; }
                .c { background-color: var(--alias); }
            )");
            ASSERT_FALSE(sheet.HasErrors());
            const auto number = [&sheet](std::string_view name) {
                const auto it = std::find(sheet.colorVariables.begin(), sheet.colorVariables.end(), name);
                return it == sheet.colorVariables.end() ? 0 : int(it - sheet.colorVariables.begin()) + 1;
            };
            const StyleState& a = sheet.GetStyle("a")->states[0];
            const StyleState& b = sheet.GetStyle("b")->states[0];
            EXPECT_EQ(a.backgroundColor.variable, number("--panel"));
            EXPECT_EQ(a.textColor.variable, number("--text"));
            EXPECT_NE(a.backgroundColor.variable, a.textColor.variable);
            EXPECT_EQ(b.backgroundColor.variable, number("--panel")) << "one number for a variable";
            EXPECT_EQ(b.borderColors[0].variable, 0) << "a color of its own";
            EXPECT_NE(number("--panel"), 0);
            EXPECT_EQ(sheet.GetStyle("c")->states[0].backgroundColor.variable, number("--alias"))
                << "a variable set from another is its own";
        }

        // A variable's fills share slots with nothing else, so a new color goes into them alone.
        TEST(ThemeVariables, AVariablesFillsAreItsOwn)
        {
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            StyleManager& styles = app.styleManager;
            ColorStyle plain = ColorStyle::Create(color4b{ 10, 20, 30, 255 });
            ColorStyle ofOne = plain;
            ofOne.variable = 901;
            ColorStyle ofTwo = plain;
            ofTwo.variable = 902;

            const VisualColorStyleIndex p  = styles.RegisterColorStyle(plain);
            const VisualColorStyleIndex o1 = styles.RegisterColorStyle(ofOne);
            const VisualColorStyleIndex o2 = styles.RegisterColorStyle(ofTwo);
            EXPECT_NE(p, o1);
            EXPECT_NE(o1, o2);
            EXPECT_EQ(styles.RegisterColorStyle(ofOne), o1) << "shared with its own variable's";
            EXPECT_EQ(styles.RegisterColorStyle(plain), p);

            const VisualColorStyle before = *styles.GetVisualColorStyle(p);
            const VisualColorStyle twoBefore = *styles.GetVisualColorStyle(o2);
            EXPECT_TRUE(styles.SetVariableColor(901, ColorStyle::Create(color4b{ 200, 0, 0, 255 })));
            EXPECT_FALSE(*styles.GetVisualColorStyle(o1) == before) << "its fill has the new color";
            EXPECT_TRUE(*styles.GetVisualColorStyle(p) == before) << "the plain one stays";
            EXPECT_TRUE(*styles.GetVisualColorStyle(o2) == twoBefore) << "another variable's stays";
            EXPECT_FALSE(styles.SetVariableColor(903, plain)) << "nothing has it";
            EXPECT_FALSE(styles.SetVariableColor(0, plain));

            styles.ClearAll();
            EXPECT_FALSE(styles.SetVariableColor(901, plain)) << "forgotten with the styles";
        }

        // ---------------------------------------------------------------------
        // The shipped theme
        // ---------------------------------------------------------------------

        // Every palette and styles.css variable has a rule with a label, a category and a
        // description, and a value that fits it, in both palettes.
        TEST(ThemeVariables, TheShippedThemeDescribesEveryVariable)
        {
            for (const char* palette : { "themes/dark.css", "themes/light.css" })
            {
                HeadlessGUIApp app(palette);
                ASSERT_TRUE(app.Initialize()) << palette;
                const auto variables = app.theme.GetVariables();
                EXPECT_GE(variables.size(), 150u) << palette;
                for (const CustomPropertyInfo& info : variables)
                {
                    EXPECT_FALSE(info.label.empty()) << info.name;
                    EXPECT_FALSE(info.category.empty()) << info.name;
                    EXPECT_FALSE(info.description.empty()) << info.name;
                    EXPECT_NE(info.type, CustomPropertyType::Any) << info.name;
                    EXPECT_NE(app.theme.GetVariable(info.name), nullptr) << palette << " sets " << info.name;
                }
                EXPECT_NE(app.theme.GetVariable("--bg-panel"), nullptr);
                EXPECT_EQ(app.theme.GetVariable("--no-such-thing"), nullptr);
            }
        }

        // ---------------------------------------------------------------------
        // Changes made in code
        // ---------------------------------------------------------------------

        class ThemeVariableChanges : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;

            void SetUp()    override { ASSERT_TRUE(app.Initialize()); Frame(); }
            void TearDown() override { app.Cleanup(); }

            void Frame() { app.Frame([&] { app.BeginRootWindow(); app.EndRootWindow(); }); }

            color4b Color(const char* name)
            {
                const color4f c = app.theme.GetColor(name, color4f{ -1.0f, -1.0f, -1.0f, -1.0f });
                auto b = [](float v) { return byte(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
                return { b(c.r), b(c.g), b(c.b), b(c.a) };
            }
        };

        // A color shows on the next frame without loading the theme; the theme loads once the
        // color has stayed the same a moment.
        TEST_F(ThemeVariableChanges, AColorShowsAtOnceThenTheThemeLoads)
        {
            const color4b own = Color("--bg-panel");
            const uint32_t generation = app.styleManager.Generation();
            app.SetThemeVariable("--bg-panel", "#204060");
            EXPECT_EQ(Color("--bg-panel"), (color4b{ 0x20, 0x40, 0x60, 0xFF })) << "at once";
            EXPECT_EQ(app.theme.GetVariable("--bg-panel")->text, "#204060");
            Frame();
            EXPECT_EQ(app.styleManager.Generation(), generation) << "not loaded again yet";
            EXPECT_EQ(app.GetThemeVariables().at("--bg-panel"), "#204060");

            app.AdvanceTime(300);
            Frame();
            EXPECT_EQ(app.styleManager.Generation(), generation) << "not yet settled";
            app.AdvanceTime(150);
            Frame();
            EXPECT_NE(app.styleManager.Generation(), generation) << "loaded again once it settled";
            EXPECT_EQ(Color("--bg-panel"), (color4b{ 0x20, 0x40, 0x60, 0xFF })) << "and kept";

            app.ClearThemeVariable("--bg-panel");
            Frame();
            EXPECT_EQ(Color("--bg-panel"), own);
            EXPECT_TRUE(app.GetThemeVariables().empty());
        }

        // What's drawn with the color changes at once: the fills of the styles that use it, and
        // the theme's own colors from it. Fills of other colors stay.
        TEST_F(ThemeVariableChanges, WhatsDrawnWithTheColorChanges)
        {
            const auto background = [this](const char* cssClass) {
                const StyleRef style = app.theme.GetStyle(cssClass);
                const VisualBoxStyle* box = app.styleManager.GetVisualBoxStyle(style.visualBoxStyleIndex, ElementState::None);
                return *app.styleManager.GetVisualColorStyle(box->backgroundColorIndex);
            };
            const VisualColorStyle panel  = background("dock-leaf");             // --bg-panel
            const VisualColorStyle header = background("table-header-section"); // --bg-panel too
            const VisualColorStyle arc    = *app.styleManager.GetVisualColorStyle(app.theme.progressArcColor);   // --accent
            const VisualColorStyle input  = *app.styleManager.GetVisualColorStyle(app.theme.alphaTrackColor);    // --alpha-track
            const uint32_t generation = app.styleManager.Generation();

            app.SetThemeVariable("--bg-panel", "#204060");
            EXPECT_FALSE(background("dock-leaf") == panel);
            EXPECT_TRUE(background("dock-leaf") == background("table-header-section")) << "one variable, one fill";
            EXPECT_FALSE(background("table-header-section") == header);
            EXPECT_TRUE(*app.styleManager.GetVisualColorStyle(app.theme.progressArcColor) == arc) << "the accent's stays";

            app.SetThemeVariable("--accent", "#FF0000");
            EXPECT_FALSE(*app.styleManager.GetVisualColorStyle(app.theme.progressArcColor) == arc) << "the theme's own from it";
            EXPECT_TRUE(*app.styleManager.GetVisualColorStyle(app.theme.alphaTrackColor) == input);
            EXPECT_EQ(app.styleManager.Generation(), generation) << "without loading the theme";
        }

        // A panel color past the middle grey makes the theme a light one, once it has loaded again.
        TEST_F(ThemeVariableChanges, ThePanelColorSaysLightOrDark)
        {
            EXPECT_FALSE(app.theme.IsLight());
            app.SetThemeVariable("--bg-panel", "#F0F0F0");
            app.AdvanceTime(500);
            Frame();
            EXPECT_TRUE(app.theme.IsLight());
        }

        // A size loads the theme again; it wins over styles.css, which sets it after the palette.
        TEST_F(ThemeVariableChanges, ASizeLoadsTheThemeAgain)
        {
            const uint32_t generation = app.styleManager.Generation();
            app.SetThemeVariable("--radius-md", "9px");
            Frame();
            EXPECT_NE(app.styleManager.Generation(), generation);
            EXPECT_EQ(app.theme.GetVariable("--radius-md")->number, 9.0f);
            app.SetThemeVariable("--radius-md", "9px");
            const uint32_t again = app.styleManager.Generation();
            Frame();
            EXPECT_EQ(app.styleManager.Generation(), again) << "the same value again changes nothing";
        }

        // Dark keeps its changes and Light its own; back on Dark, they're back.
        TEST_F(ThemeVariableChanges, EachThemeKeepsItsOwn)
        {
            app.SetThemeVariable("--bg-panel", "#204060");
            app.SetThemeVariable("--radius-md", "9px");
            Frame();
            app.RequestReloadTheme("themes/light.css", 1.0f);
            Frame();
            EXPECT_TRUE(app.GetThemeVariables().empty());
            EXPECT_EQ(Color("--bg-panel"), (color4b{ 0xFB, 0xFB, 0xFC, 0xFF })) << "Light's own";
            EXPECT_EQ(app.theme.GetVariable("--radius-md")->number, 4.0f);
            app.SetThemeVariable("--accent", "#FF0000");
            Frame();

            app.RequestReloadTheme("./themes\\dark.css", 1.0f);   // the same theme, written another way
            Frame();
            EXPECT_EQ(app.GetThemeVariables().size(), 2u);
            EXPECT_EQ(Color("--bg-panel"), (color4b{ 0x20, 0x40, 0x60, 0xFF }));
            EXPECT_EQ(app.theme.GetVariable("--radius-md")->number, 9.0f);
            EXPECT_EQ(Color("--accent"), (color4b{ 0x6B, 0x9B, 0xFF, 0xFF })) << "Light's change stays Light's";
        }

        TEST_F(ThemeVariableChanges, ResetGoesBackToTheThemesOwn)
        {
            const color4b panel = Color("--bg-panel");
            app.SetThemeVariable("--bg-panel", "#204060");
            app.SetThemeVariable("--radius-md", "9px");
            Frame();
            app.ClearThemeVariables();
            Frame();
            EXPECT_TRUE(app.GetThemeVariables().empty());
            EXPECT_EQ(Color("--bg-panel"), panel);
            EXPECT_EQ(app.theme.GetVariable("--radius-md")->number, 4.0f);
            const uint32_t generation = app.styleManager.Generation();
            app.ClearThemeVariables();
            app.ClearThemeVariable("--bg-panel");
            Frame();
            EXPECT_EQ(app.styleManager.Generation(), generation) << "nothing to clear: no reload";
        }

        // The accent set in code gives way to a change of the accent variable.
        TEST_F(ThemeVariableChanges, AChangeWinsOverTheAccent)
        {
            app.SetAccent(kAccentPresets[1].colors);
            app.SetThemeVariable("--radius-md", "5px");   // so the theme loads with both
            app.SetThemeVariable("--accent", "#FF0000");
            Frame();
            EXPECT_EQ(Color("--accent"), (color4b{ 255, 0, 0, 255 }));
        }

        // A value that would end its declaration isn't used; the theme loads without it.
        TEST_F(ThemeVariableChanges, AValueThatIsntOneIsLeftOut)
        {
            const color4b panel = Color("--bg-panel");
            app.SetThemeVariable("--bg-panel", "red; } .x { color: blue");
            Frame();
            EXPECT_EQ(Color("--bg-panel"), panel);
            app.SetThemeVariable("bg-panel", "#204060");
            Frame();
            EXPECT_EQ(Color("--bg-panel"), panel) << "a name without --";
            EXPECT_NE(app.theme.GetVariable("--text-1"), nullptr) << "the theme still loaded";
        }

        // Each theme's changes are saved with the UI state and come back with it.
        TEST_F(ThemeVariableChanges, TheyAreSavedWithTheUIState)
        {
            app.PersistThemeVariables("themeVariables");
            app.SetThemeVariable("--bg-panel", "#204060");
            Frame();
            app.RequestReloadTheme("themes/light.css", 1.0f);
            app.SetThemeVariable("--radius-md", "7px");
            Frame();
            const std::string saved = app.SaveUIState();
            EXPECT_NE(saved.find("\"themes/dark.css\""), std::string::npos) << saved;
            EXPECT_NE(saved.find("#204060"), std::string::npos);

            HeadlessGUIApp other;
            ASSERT_TRUE(other.Initialize());
            other.PersistThemeVariables("themeVariables");
            ASSERT_TRUE(other.RestoreUIState(saved));
            other.Frame([&] { other.BeginRootWindow(); other.EndRootWindow(); });
            EXPECT_EQ(other.GetThemeVariables().at("--bg-panel"), "#204060") << "Dark's";
            EXPECT_EQ(other.theme.GetVariable("--bg-panel")->text, "#204060");
            other.RequestReloadTheme("themes/light.css", 1.0f);
            other.Frame([&] { other.BeginRootWindow(); other.EndRootWindow(); });
            EXPECT_EQ(other.theme.GetVariable("--radius-md")->number, 7.0f) << "Light's";
            other.Cleanup();
        }

        // Restoring the UI state loads the theme again only when its saved changes differ from
        // the ones it was loaded with: a start with nothing changed loads it once.
        TEST_F(ThemeVariableChanges, RestoringTheSameChangesDoesntLoadTheThemeAgain)
        {
            app.PersistThemeVariables("themeVariables");
            app.SetThemeVariable("--radius-md", "7px");
            Frame();
            Frame();
            const std::string saved = app.SaveUIState();

            int loads = 0;
            auto counter = [](const Diagnostics::LogRecord& record, void* context) {
                if (record.messageTemplate.starts_with("loaded "))
                    ++*static_cast<int*>(context);
            };
            const Diagnostics::LogSeverity before = Diagnostics::GetMinimumLogSeverity();
            Diagnostics::SetMinimumLogSeverity(Diagnostics::LogSeverity::Info);   // the load is an info record
            ASSERT_TRUE(Diagnostics::AddLogSink(counter, &loads));
            ASSERT_TRUE(app.RestoreUIState(saved));
            Frame();
            EXPECT_EQ(loads, 0) << "the same changes";

            const std::string other = std::string(saved).replace(saved.find("7px"), 3, "5px");
            ASSERT_TRUE(app.RestoreUIState(other));
            Frame();
            EXPECT_EQ(loads, 1) << "another value";
            Diagnostics::RemoveLogSink(counter, &loads);
            Diagnostics::SetMinimumLogSeverity(before);
            EXPECT_EQ(app.theme.GetVariable("--radius-md")->number, 5.0f);
        }

        // Shut down right after a reset, with the reload still to come: nothing left dangling.
        TEST_F(ThemeVariableChanges, ShutsDownAfterAReset)
        {
            app.PersistThemeVariables("themeVariables");
            app.SetThemeVariable("--bg-panel", "#204060");
            Frame();
            app.ClearThemeVariables();
            EXPECT_FALSE(app.SaveUIState().empty());
        }
    }
}
