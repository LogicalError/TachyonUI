// The Settings panel's page list (header, icons) and ShortcutButton, a command's shortcut as
// keycaps.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/elements/gui_element_storage.h>

#include <string>
#include <vector>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        // SettingsPanelState is protected on GUIApplication; reach it as an app does.
        class SettingsApp : public HeadlessGUIApp
        {
        public:
            using SettingsState = GUIApplication::SettingsPanelState;
            using GUIApplication::RenderSettingsPanel;
        };

        class ControlsSettingsTest : public ::testing::Test
        {
        protected:
            SettingsApp app;
            void SetUp() override { ASSERT_TRUE(app.Initialize()); }
            void TearDown() override { app.Cleanup(); }

            // The children of `id`'s element that have `style`.
            int ChildrenWithStyle(ID id, StyleRef style)
            {
                const ElementRef parent = app.elementStorage.FindElementRefByID(id);
                if (!parent.IsValid()) return -1;
                const auto children = app.elementStorage.GetChildRange(parent);
                int count = 0;
                for (uint32_t i = children.begin; i < children.end; ++i)
                    if (app.elementStorage.GetElementUnsafe(ElementRef{ parent.depth + 1, i }).styleRef == style)
                        ++count;
                return count;
            }
        };

        // The page list shows its header above the pages, and each page's icon.
        TEST_F(ControlsSettingsTest, ThePageListHasAHeaderAndThePagesIcons)
        {
            SettingsApp::SettingsState settings;
            settings.header = "Preferences";
            settings.AddPage(app.idManager.CreateStaticID(), "Shortcuts", app.theme.addIcon, [] {});
            settings.AddPage(app.idManager.CreateStaticID(), "General", [] {});
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.RenderSettingsPanel(settings);
                    app.EndRootWindow();
                });
            };
            frame();
            frame();
            ASSERT_EQ(settings.internal.rows.size(), 2u);
            EXPECT_EQ(settings.internal.rows[0].icon, app.theme.addIcon);
            EXPECT_EQ(settings.internal.rows[1].icon, IconID::Invalid);
            EXPECT_TRUE(settings.internal.tableState.internal.hasHeaders) << "the header row shows";

            settings.header.clear();
            frame();
            EXPECT_FALSE(settings.internal.tableState.internal.hasHeaders) << "no header, no header row";
        }

        // A command's shortcut as keycaps, one per key; a chord's steps apart; none, a muted word.
        TEST_F(ControlsSettingsTest, AShortcutButtonShowsAKeycapPerKey)
        {
            CommandManager& commands = app.commandManager;
            const CommandID copy  = commands.RegisterCommand(KeyCode::KeyC, KeyModifier::Control, [] {}, "Copy");
            const CommandID chord = commands.RegisterChord(CommandMode::Default, KeyCode::KeyK, KeyModifier::Control,
                                                           KeyCode::KeyS, KeyModifier::Control, [] {}, "Save All");
            const CommandID about = commands.RegisterCommand(IconID::Invalid, [] {}, "About");
            const ID copyID  = app.idManager.CreateStaticID();
            const ID chordID = app.idManager.CreateStaticID();
            const ID aboutID = app.idManager.CreateStaticID();
            int clicks = 0;
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.ShortcutButton(copyID,  *commands.GetCommand(copy),  [&clicks] { ++clicks; });
                    app.ShortcutButton(chordID, *commands.GetCommand(chord), [] {});
                    app.ShortcutButton(aboutID, *commands.GetCommand(about), [] {});
                    app.EndRootWindow();
                });
            };
            frame();
            frame();
            EXPECT_EQ(ChildrenWithStyle(copyID, app.theme.keycapStyleRef), 2) << "Ctrl, C";
            EXPECT_EQ(ChildrenWithStyle(chordID, app.theme.keycapStyleRef), 4) << "Ctrl, K, then Ctrl, S";
            EXPECT_EQ(ChildrenWithStyle(chordID, app.theme.shortcutMutedStyleRef), 1) << "the comma between the steps";
            EXPECT_EQ(ChildrenWithStyle(aboutID, app.theme.keycapStyleRef), 0);
            EXPECT_EQ(ChildrenWithStyle(aboutID, app.theme.shortcutMutedStyleRef), 1) << "Unassigned";

            const IntRect keycaps = app.GetElementRect(copyID);
            app.ClickAt({ keycaps.x + 2, keycaps.y + keycaps.h / 2 });
            frame();
            EXPECT_EQ(clicks, 1) << "a click on its keycaps is the button's";
        }
    }
}
