// Every static ID the library makes is given back: when what holds it goes (a closed panel,
// a layout or documents released), and at Cleanup for the ones the application keeps itself.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_controls/gui_dock_node.h>
#include <gui_controls/gui_menu_manager.h>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        class StaticIDReleaseTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            void SetUp() override { ASSERT_TRUE(app.Initialize()); }
            void TearDown() override { app.Cleanup(); }
        };

        // A panel CreatePanel made an ID for goes with that ID when its tab is closed; one given
        // an ID of the app's keeps it, for the app to release.
        TEST_F(StaticIDReleaseTest, AClosedPanelsIDGoesBackUnlessItWasTheApps)
        {
            DockingLayout layout;
            const ID made = app.CreatePanel(layout.panels, "Made", IconID::Invalid, [] {});
            const ID given = app.idManager.CreateStaticID();
            app.CreatePanel(layout.panels, given, "Given", IconID::Invalid, [] {});
            layout.root = MakeLeaf({ made, given });

            app.CloseDockedPanel(layout, *layout.root, 0);
            EXPECT_FALSE(app.idManager.IsStaticIDValid(made));
            app.CloseDockedPanel(layout, *layout.root, 0);
            EXPECT_TRUE(app.idManager.IsStaticIDValid(given)) << "the app's own";
            EXPECT_TRUE(layout.panels.empty());
            EXPECT_TRUE(app.idManager.ReleaseStaticID(given));
        }

        // Releasing a layout gives back its panels' IDs and its tree's, and empties it.
        TEST_F(StaticIDReleaseTest, ReleasingALayoutGivesBackItsPanelsAndTree)
        {
            const size_t before = app.idManager.GetStaticIDCount();
            const ID containerID = app.idManager.CreateStaticID();
            DockingLayout layout;
            int disables = 0;
            const ID a = app.CreatePanel(layout.panels, "A", IconID::Invalid, [] {}, {}, {}, [&] { disables++; });
            const ID b = app.CreatePanel(layout.panels, "B", IconID::Invalid, [] {});
            layout.root = MakeSplit(DockSplitMode::LeftPinned, 200, MakeLeaf({ a }), MakeLeaf({ b }));
            app.Frame([&] {
                app.BeginRootWindow();
                app.BeginDockingContainer(containerID, &layout, LayoutOverride{}.Size(800, 600));
                app.EndDockingContainer();
                app.EndRootWindow();
            });
            ASSERT_GT(app.idManager.GetStaticIDCount(), before + 3) << "test setup: the tree has IDs";

            app.ReleaseDockingLayout(layout);
            EXPECT_EQ(disables, 1) << "a shown panel is hidden first";
            EXPECT_TRUE(layout.panels.empty());
            EXPECT_EQ(layout.root, nullptr);
            app.idManager.ReleaseStaticID(containerID);
            EXPECT_EQ(app.idManager.GetStaticIDCount(), before);
        }

        // Closing every document gives back each one's IDs and its panels'.
        TEST_F(StaticIDReleaseTest, ClosingAllDocumentsGivesBackTheirIDs)
        {
            const size_t before = app.idManager.GetStaticIDCount();
            DocumentSet set;
            for (const char* name : { "Forest", "Desert" })
            {
                Document& document = app.CreateDocument(set, name, ".level", "level", IconID::Invalid);
                document.layout.root = MakeLeaf({ app.CreatePanel(document.layout.panels, "Scene", IconID::Invalid, [] {}) });
            }
            app.CloseAllDocuments(set);
            EXPECT_TRUE(set.documents.empty());
            EXPECT_EQ(app.idManager.GetStaticIDCount(), before);
        }

        // What the application keeps for itself (its popup and drag windows, menus, toasts,
        // torn-off windows) goes back at Cleanup: none is left.
        TEST_F(StaticIDReleaseTest, CleanupGivesBackTheApplicationsOwnIDs)
        {
            app.ShowToast({ .title = "Saved" });
            app.Frame([&] {
                app.BeginRootWindow();
                app.Toasts();
                app.EndRootWindow();
            });
            const ID trigger = app.idManager.CreateStaticID();
            (void)app.menuManager.PopupWindowFor(trigger, app.idManager);
            (void)app.menuManager.GetOrCreateNextLevel(app.idManager);
            app.idManager.ReleaseStaticID(trigger);
            app.m_colorPickerSub->State().windowID    = app.idManager.CreateStaticID();
            app.m_gradientEditorSub->State().windowID = app.idManager.CreateStaticID();

            DockingLayout home;
            auto fw = std::make_unique<FloatingDockWindow>();
            fw->windowID = app.idManager.CreateStaticID();
            fw->layout.root = MakeLeaf({ app.CreatePanel(fw->layout.panels, "Torn off", IconID::Invalid, [] {}) });
            fw->home = &home;
            app.m_floatingDockWindows.push_back(std::move(fw));

            ASSERT_GT(app.idManager.GetStaticIDCount(), 0u);
            app.Cleanup();
            EXPECT_EQ(app.idManager.GetStaticIDCount(), 0u);
            EXPECT_TRUE(app.m_floatingDockWindows.empty());
        }
    }
}
