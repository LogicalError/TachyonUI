// An accent set in code: it replaces the palette's, with the colors derived from it, in the
// dark or the light form as the palette is, and stays through theme changes.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_controls/gui_theme.h>
#include <gui_core/gui_resources.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        class AccentTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;

            void SetUp()    override { ASSERT_TRUE(app.Initialize()); }
            void TearDown() override { app.Cleanup(); }

            void Frame() { app.Frame([&] { app.BeginRootWindow(); app.EndRootWindow(); }); }

            // A palette color as 0 to 255 bytes.
            color4b Color(const char* name)
            {
                const color4f c = app.theme.GetColor(name, color4f{ -1.0f, -1.0f, -1.0f, -1.0f });
                auto b = [](float v) { return byte(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
                return { b(c.r), b(c.g), b(c.b), b(c.a) };
            }
        };

        // Each channel within `slack` steps of the other's.
        ::testing::AssertionResult Near(color4b a, color4b b, int slack = 0)
        {
            for (int i = 0; i < 4; ++i)
                if (std::abs(int(a[i]) - int(b[i])) > slack)
                    return ::testing::AssertionFailure() << "channel " << i << ": " << int(a[i]) << " vs " << int(b[i]);
            return ::testing::AssertionSuccess();
        }

        constexpr color4b Rgba(uint32_t hex)
        {
            return { byte(hex >> 24), byte(hex >> 16), byte(hex >> 8), byte(hex) };
        }

        TEST_F(AccentTest, ThePalettesOwnUntilOneIsSet)
        {
            Frame();
            EXPECT_FALSE(app.GetAccent().has_value());
            EXPECT_FALSE(app.theme.IsLight());
            EXPECT_TRUE(Near(Color("--accent"), Rgba(0x6B9BFFFF)));
        }

        // A dark palette takes the dark color: the accent, the caret, a hovered divider and the
        // selection (35%) are it; the soft fill and the menu highlight are it at 16% over the
        // panel and the popup.
        TEST_F(AccentTest, ADarkPaletteTakesTheDarkColorAndWhatFollowsIt)
        {
            app.SetAccent(kAccentPresets[1].colors);   // teal
            Frame();
            ASSERT_EQ(app.GetAccent(), kAccentPresets[1].colors);
            const color4b teal = Rgba(0x34D1BFFF);
            EXPECT_TRUE(Near(Color("--accent"), teal));
            EXPECT_TRUE(Near(Color("--caret"), teal));
            EXPECT_TRUE(Near(Color("--divider-hover"), teal));
            EXPECT_TRUE(Near(Color("--selection"), Rgba(0x34D1BF59)));
            const color4b panel = Color("--bg-panel"), popup = Color("--bg-popup");
            auto over = [&](color4b base, float amount) {
                auto mix = [amount](byte from, byte to) { return byte(std::lround(float(from) + (float(to) - float(from)) * amount)); };
                return color4b{ mix(base.r, teal.r), mix(base.g, teal.g), mix(base.b, teal.b), 255 };
            };
            EXPECT_TRUE(Near(Color("--accent-soft"), over(panel, 0.16f)));
            EXPECT_TRUE(Near(Color("--bg-menu-hover"), over(popup, 0.16f)));

            // What doesn't follow it stays: info, links, document colors.
            EXPECT_TRUE(Near(Color("--info"), Rgba(0x6B9BFFFF)));
            EXPECT_TRUE(Near(Color("--text-link"), Rgba(0x8FB3FFFF)));
            EXPECT_TRUE(Near(Color("--doc-level-solid"), Rgba(0x6B9BFFFF)));
        }

        // A light palette takes the light color, laid on more lightly: 12% and a 25% selection.
        TEST_F(AccentTest, ALightPaletteTakesTheLightColor)
        {
            app.SetAccent(kAccentPresets[2].colors);   // violet
            app.RequestReloadTheme("themes/light.css", 1.0f);
            Frame();
            ASSERT_TRUE(app.theme.IsLight());
            const color4b violet = Rgba(0x6D4FD6FF);
            EXPECT_TRUE(Near(Color("--accent"), violet));
            EXPECT_TRUE(Near(Color("--selection"), Rgba(0x6D4FD640)));
            const color4b panel = Color("--bg-panel");
            const color4b soft{ byte(std::lround(panel.r + (violet.r - panel.r) * 0.12f)),
                                byte(std::lround(panel.g + (violet.g - panel.g) * 0.12f)),
                                byte(std::lround(panel.b + (violet.b - panel.b) * 0.12f)), 255 };
            EXPECT_TRUE(Near(Color("--accent-soft"), soft));
        }

        // The palettes' own blue, set as an accent, gives the palettes' own colors back (to a
        // step of rounding): the amounts are theirs.
        TEST_F(AccentTest, ThePalettesBlueGivesThePalettesColors)
        {
            for (const char* palette : { "themes/dark.css", "themes/light.css" })
            {
                app.SetAccent(std::nullopt);
                app.RequestReloadTheme(palette, 1.0f);
                Frame();
                const char* names[] = { "--accent", "--caret", "--divider-hover", "--selection", "--accent-soft", "--bg-menu-hover" };
                color4b own[std::size(names)];
                for (size_t i = 0; i < std::size(names); ++i)
                    own[i] = Color(names[i]);

                app.SetAccent(kAccentPresets[0].colors);
                Frame();
                for (size_t i = 0; i < std::size(names); ++i)
                    EXPECT_TRUE(Near(Color(names[i]), own[i], 1)) << palette << " " << names[i];
            }
        }

        // It stays through a change of palette, and none goes back to the palette's own.
        TEST_F(AccentTest, ItStaysThroughThemeChangesUntilCleared)
        {
            app.SetAccent(kAccentPresets[3].colors);   // orange
            Frame();
            EXPECT_TRUE(Near(Color("--accent"), Rgba(0xF59E4BFF)));
            app.RequestReloadTheme("themes/light.css", 1.0f);
            Frame();
            EXPECT_TRUE(Near(Color("--accent"), Rgba(0xB85C10FF)));
            app.RequestReloadTheme("themes/light.css", 1.25f);
            Frame();
            EXPECT_TRUE(Near(Color("--accent"), Rgba(0xB85C10FF))) << "and through a font size change";

            app.SetAccent(std::nullopt);
            Frame();
            EXPECT_FALSE(app.GetAccent().has_value());
            EXPECT_TRUE(Near(Color("--accent"), Rgba(0x2F6FEBFF)));
        }

        // A color of its own, the same in both palettes.
        TEST_F(AccentTest, AnyColorWillDo)
        {
            const color4b pink = Rgba(0xF472B6FF);
            app.SetAccent(AccentColors{ pink, pink });
            Frame();
            EXPECT_TRUE(Near(Color("--accent"), pink));
            app.RequestReloadTheme("themes/light.css", 1.0f);
            Frame();
            EXPECT_TRUE(Near(Color("--accent"), pink));
        }

        // Light or dark is how bright the palette's panel is: past the middle grey, light.
        TEST_F(AccentTest, APaletteIsLightWhenItsPanelIsBrighterThanMiddleGrey)
        {
            Result<std::string> light = ReadResource("themes/light.css");
            ASSERT_TRUE(light);
            const std::string from = "--bg-panel: #FBFBFC;";
            const size_t at = light->find(from);
            ASSERT_NE(at, std::string::npos);
            // In a themes folder of its own, where the built-in styles.css is found next to it.
            const std::filesystem::path folder = std::filesystem::temp_directory_path() / "tachyon-accent-test" / "themes";
            std::filesystem::create_directories(folder);
            const std::string path = (folder / "palette.css").string();
            app.SetAccent(kAccentPresets[1].colors);   // teal
            for (const auto& [panel, isLight] : { std::pair{ "#8C8C8C", true }, std::pair{ "#737373", false } })
            {
                {
                    std::string text = *light;
                    text.replace(at, from.size(), std::string("--bg-panel: ") + panel + ";");
                    std::ofstream out(path, std::ios::binary);
                    out << text;
                }
                app.RequestReloadTheme(path, isLight ? 1.0f : 1.1f);
                Frame();
                std::filesystem::remove(path);
                EXPECT_EQ(app.theme.IsLight(), isLight) << panel;
                EXPECT_TRUE(Near(Color("--accent"), isLight ? Rgba(0x0E8277FF) : Rgba(0x34D1BFFF))) << panel;
            }
            std::error_code ignored;   // the folders it made, now empty
            std::filesystem::remove(folder, ignored);
            std::filesystem::remove(folder.parent_path(), ignored);
        }

        TEST(AccentPresetsTest, TheDesignsFour)
        {
            ASSERT_EQ(std::size(kAccentPresets), 4u);
            EXPECT_EQ(kAccentPresets[0].name, "Blue");
            EXPECT_EQ(kAccentPresets[1].colors.dark,  Rgba(0x34D1BFFF));
            EXPECT_EQ(kAccentPresets[1].colors.light, Rgba(0x0E8277FF));
            EXPECT_EQ(kAccentPresets[2].colors.dark,  Rgba(0xA98BFFFF));
            EXPECT_EQ(kAccentPresets[2].colors.light, Rgba(0x6D4FD6FF));
            EXPECT_EQ(kAccentPresets[3].colors.dark,  Rgba(0xF59E4BFF));
            EXPECT_EQ(kAccentPresets[3].colors.light, Rgba(0xB85C10FF));
        }
    }
}
