// Control-layer tests for the panel docking system (BeginDockingContainer /
// EndDockingContainer in controls_docking.cpp) driven through the headless
// harness: which panels draw, the enable/disable lifecycle, split layout and
// the sizer drag. (dock_node_test.cpp covers the pure tree operations.)

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_controls/gui_dock_node.h>
#include <gui_core/elements/gui_element_storage.h>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        class DockingTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            tachyon::DockingLayout layout;
            tachyon::ID containerID{};
            tachyon::ID panelA{}, panelB{}, panelC{};
            int drawsA = 0, drawsB = 0, drawsC = 0;
            int enables[3] = {}, disables[3] = {};

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                containerID = app.idManager.CreateStaticID();
                panelA = app.CreatePanel(layout.panels, "A", IconID::Invalid,
                                         [this] { drawsA++; }, {},
                                         [this] { enables[0]++; }, [this] { disables[0]++; });
                panelB = app.CreatePanel(layout.panels, "B", IconID::Invalid,
                                         [this] { drawsB++; }, {},
                                         [this] { enables[1]++; }, [this] { disables[1]++; });
                panelC = app.CreatePanel(layout.panels, "C", IconID::Invalid,
                                         [this] { drawsC++; }, {},
                                         [this] { enables[2]++; }, [this] { disables[2]++; });

                // [A] | [B, C] - left pane pinned at 250px.
                layout.root = MakeSplit(DockSplitMode::LeftPinned, 250,
                                        MakeLeaf({ panelA }),
                                        MakeLeaf({ panelB, panelC }));
            }

            void TearDown() override { app.Cleanup(); }

            void EmitDock()
            {
                app.BeginRootWindow();
                app.BeginDockingContainer(containerID, &layout,
                                          tachyon::LayoutOverride{}.Size(800, 600));
                app.EndDockingContainer();
                app.EndRootWindow();
            }

            void Frame() { app.Frame([&] { EmitDock(); }); }

            // The content element of a leaf, as registered during construction.
            tachyon::IntRect ContentRect(const DockNode* leaf) const
            {
                return app.GetElementRect(leaf->lastContentID);
            }
        };

        TEST_F(DockingTest, VisiblePanelsDrawHiddenTabsDoNot)
        {
            Frame();
            EXPECT_EQ(drawsA, 1) << "left leaf's only panel draws";
            EXPECT_EQ(drawsB, 1) << "right leaf's selected tab draws";
            EXPECT_EQ(drawsC, 0) << "the hidden tab must not draw";
        }

        TEST_F(DockingTest, EnableFiresOncePerVisibilityTransition)
        {
            Frame();
            Frame();
            EXPECT_EQ(enables[0], 1);
            EXPECT_EQ(enables[1], 1);
            EXPECT_EQ(enables[2], 0) << "hidden tab is not enabled";
            EXPECT_EQ(disables[0], 0);
        }

        // A tab whose panel the app erased is dropped, so the others keep their indices: the
        // selected one still shows, and closing tab 1 closes the panel in tab 1.
        TEST_F(DockingTest, ATabWhosePanelIsGoneIsDropped)
        {
            const ID gone = app.idManager.CreateStaticID();
            DockNode& right = *layout.root->second;
            right.panelIds = { gone, panelB, panelC };
            right.selectedTab = 2;   // C
            Frame();
            EXPECT_EQ(right.panelIds, (std::vector<ID>{ panelB, panelC }));
            EXPECT_EQ(right.selectedTab, 1) << "C still";
            EXPECT_EQ(drawsC, 1);
            EXPECT_EQ(drawsB, 0);
        }

        TEST_F(DockingTest, SplitPinsFirstPaneAndStacksHorizontally)
        {
            Frame();
            const auto a = ContentRect(layout.root->first.get());
            const auto b = ContentRect(layout.root->second.get());
            ASSERT_GT(a.w, 0);
            ASSERT_GT(b.w, 0);
            EXPECT_LE(a.x + a.w, b.x) << "left pane sits left of the right pane";
            // Pinned at 250 minus leaf/content chrome; right pane absorbs the rest.
            EXPECT_NEAR(a.w, 250, 60);
            EXPECT_GT(b.w, a.w) << "the unpinned pane absorbs the remaining width";
        }

        TEST_F(DockingTest, SelectingTabSwitchesPanelAndFiresLifecycle)
        {
            Frame();
            ASSERT_EQ(drawsB, 1);
            ASSERT_EQ(drawsC, 0);

            // Switch the right leaf to its second tab (panel C).
            layout.root->second->selectedTab = 1;
            Frame();

            EXPECT_EQ(drawsC, 1) << "newly selected tab draws";
            EXPECT_EQ(drawsB, 1) << "deselected tab stops drawing";
            EXPECT_EQ(enables[2], 1)  << "panel C transitions hidden → visible";
            EXPECT_EQ(disables[1], 1) << "panel B transitions visible → hidden";
        }

        TEST_F(DockingTest, SizerDragResizesPinnedPane)
        {
            Frame();
            const auto a0 = ContentRect(layout.root->first.get());
            const auto b0 = ContentRect(layout.root->second.get());
            ASSERT_GT(a0.w, 0);
            ASSERT_GT(b0.w, 0);

            // The 4px sizer sits in the gap between the panes; grab its middle.
            const int sizerX = (a0.x + a0.w + b0.x) / 2;
            const tachyon::int2 from{ sizerX, a0.y + a0.h / 2 };
            const tachyon::int2 to  { sizerX + 80, from.y };
            const int before = layout.root->splitSize.value;

            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            Frame();
            app.InjectMouseMove(to, tachyon::MouseButtons::Left);
            Frame();
            app.InjectMouseUp(to);
            Frame();

            EXPECT_EQ(layout.root->splitSize, before + 80)
                << "dragging the split sizer right grows the pinned pane";
        }

        // A point on a leaf's tab bar finds that leaf and its layout; elsewhere, none.
        TEST_F(DockingTest, TheTabBarUnderAPointFindsItsLeaf)
        {
            Frame();
            Frame();
            DockNode* right = layout.root->second.get();
            const IntRect bar = app.GetElementRect(right->tabBarID);
            ASSERT_GT(bar.w, 0);

            const DockLeafResult onBar = app.FindDockLeafAtScreenPos({ bar.x + bar.w / 2, bar.y + bar.h / 2 });
            EXPECT_EQ(onBar.leaf, right);
            EXPECT_EQ(onBar.layout, &layout);

            const IntRect content = ContentRect(right);
            const DockLeafResult inContent = app.FindDockLeafAtScreenPos({ content.x + content.w / 2, content.y + content.h / 2 });
            EXPECT_EQ(inContent.leaf, nullptr) << "a leaf's content isn't its tab bar";
            EXPECT_EQ(inContent.layout, nullptr);
            EXPECT_EQ(app.FindDockLeafAtScreenPos({ 5000, 5000 }).leaf, nullptr);
        }

        // A point in a leaf's content finds that leaf and the part of it: an edge to split it
        // at, or the middle. Off every leaf's content there's no target.
        TEST_F(DockingTest, TheContentUnderAPointIsADropTarget)
        {
            Frame();
            Frame();
            DockNode* left  = layout.root->first.get();
            DockNode* right = layout.root->second.get();
            const IntRect c = right->cachedContentRect;
            ASSERT_GT(c.w, 0);
            ASSERT_GT(c.h, 0);

            const DockDropResult middle = app.FindDockDropTargetAtScreenPos({ c.x + c.w / 2, c.y + c.h / 2 });
            EXPECT_EQ(middle.target.leaf, right);
            EXPECT_EQ(middle.target.zone, DockDropZone::Center);
            EXPECT_EQ(middle.layout, &layout);

            EXPECT_EQ(app.FindDockDropTargetAtScreenPos({ c.x + 2, c.y + c.h / 2 }).target.zone, DockDropZone::Left);
            EXPECT_EQ(app.FindDockDropTargetAtScreenPos({ c.x + c.w - 3, c.y + c.h / 2 }).target.zone, DockDropZone::Right);
            EXPECT_EQ(app.FindDockDropTargetAtScreenPos({ c.x + c.w / 2, c.y + 2 }).target.zone, DockDropZone::Top);
            EXPECT_EQ(app.FindDockDropTargetAtScreenPos({ c.x + c.w / 2, c.y + c.h - 3 }).target.zone, DockDropZone::Bottom);

            const IntRect l = left->cachedContentRect;
            EXPECT_EQ(app.FindDockDropTargetAtScreenPos({ l.x + l.w / 2, l.y + l.h / 2 }).target.leaf, left);

            const IntRect bar = app.GetElementRect(right->tabBarID);
            EXPECT_EQ(app.FindDockDropTargetAtScreenPos({ bar.x + bar.w / 2, bar.y + bar.h / 2 }).target.leaf, nullptr)
                << "tab bars are FindDockLeafAtScreenPos's";
            const DockDropResult outside = app.FindDockDropTargetAtScreenPos({ 5000, 5000 });
            EXPECT_EQ(outside.target.leaf, nullptr);
            EXPECT_EQ(outside.layout, nullptr);
        }

        // A panel moved out of a leaf: the leaf keeps showing the tab it showed (or the one
        // next to it), and between layouts the panel goes with it.
        TEST_F(DockingTest, TransferPanelKeepsTheSelectedTabAndMovesThePanel)
        {
            DockNode& right = *layout.root->second;
            right.selectedTab = 1;   // C
            DockingLayout other;
            GUIApplication::TransferPanel(panelB, &right, &layout, &other);
            EXPECT_EQ(right.panelIds, (std::vector<ID>{ panelC }));
            EXPECT_EQ(right.selectedTab, 0) << "C still, now first";
            EXPECT_EQ(layout.panels.count(panelB), 0u);
            ASSERT_EQ(other.panels.count(panelB), 1u);
            EXPECT_EQ(other.panels[panelB].title, "B");

            // The selected, last tab moved within the layout: the one before it shows.
            right.panelIds = { panelC, panelA };
            right.selectedTab = 1;
            GUIApplication::TransferPanel(panelA, &right, &layout, &layout);
            EXPECT_EQ(right.panelIds, (std::vector<ID>{ panelC }));
            EXPECT_EQ(right.selectedTab, 0);
            EXPECT_EQ(layout.panels.count(panelA), 1u) << "the same layout keeps it";

            // The leaf's last panel: an empty leaf.
            GUIApplication::TransferPanel(panelC, &right, &layout, nullptr);
            EXPECT_TRUE(right.panelIds.empty());
            EXPECT_EQ(right.selectedTab, 0);
            EXPECT_EQ(layout.panels.count(panelC), 1u) << "no target layout: the panel stays";

            // A panel the leaf doesn't hold leaves it as it is.
            right.panelIds = { panelC };
            GUIApplication::TransferPanel(panelA, &right, &layout, &layout);
            EXPECT_EQ(right.panelIds, (std::vector<ID>{ panelC }));
            layout.panels[panelB] = std::move(other.panels[panelB]);   // back, for the fixture to close
        }

        // Split sizes are CSS pixels: at UI scale 2 the 250 split lays out 500 px,
        // and an 80 px drag moves it by 40.
        TEST_F(DockingTest, SplitSizeIsInCssPixels)
        {
            app.SetUIScale(2.0f);
            Frame();
            Frame();
            const DockNode* pinned = layout.root->first.get();
            EXPECT_EQ(app.GetElementRect(pinned->nodeID).w, 500);

            const auto a0 = ContentRect(layout.root->first.get());
            const auto b0 = ContentRect(layout.root->second.get());
            const int sizerX = (a0.x + a0.w + b0.x) / 2;
            const tachyon::int2 from{ sizerX, a0.y + a0.h / 2 };
            const tachyon::int2 to  { sizerX + 80, from.y };
            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            Frame();
            app.InjectMouseMove(to, tachyon::MouseButtons::Left);
            Frame();
            app.InjectMouseUp(to);
            Frame();

            EXPECT_EQ(layout.root->splitSize, 290);
            EXPECT_EQ(app.GetElementRect(pinned->nodeID).w, 580);
        }

        TEST_F(DockingTest, SingleLeafFillsContainer)
        {
            layout.root = MakeLeaf({ panelA });
            Frame();
            const auto a = ContentRect(layout.root.get());
            ASSERT_GT(a.w, 0);
            // Content spans most of the 800x600 container (minus chrome/tab bar).
            EXPECT_GT(a.w, 700);
            EXPECT_GT(a.h, 450);
        }

        // ---- Floating dock windows ----

        // Static IDs a dock tree holds (node + tab-bar IDs, assigned on emit).
        static size_t CountTreeStaticIDs(const DockNode* n)
        {
            if (!n) return 0;
            size_t c = 0;
            if (n->nodeID   != ID::Invalid() && n->nodeID.GetMode()   == IDMode::Static) ++c;
            if (n->tabBarID != ID::Invalid() && n->tabBarID.GetMode() == IDMode::Static) ++c;
            if (!n->isLeaf) c += CountTreeStaticIDs(n->first.get()) + CountTreeStaticIDs(n->second.get());
            return c;
        }

        // A floating window that empties (its last tab dragged out) goes away and
        // releases every static ID it held, its dock tree's included.
        TEST_F(DockingTest, EmptiedFloatingWindowReleasesItsTreeIDs)
        {
            auto fw = std::make_unique<FloatingDockWindow>();
            fw->windowID = app.idManager.CreateStaticID();
            const ID floating = app.CreatePanel(fw->layout.panels, "F", IconID::Invalid, [] {}, {}, {}, {});
            fw->layout.root = MakeLeaf({ floating });
            FloatingDockWindow* window = fw.get();
            app.m_floatingDockWindows.push_back(std::move(fw));

            auto frame = [&] { app.Frame([&] { EmitDock(); app.EmitFloatingDockWindows(); }); };
            frame();
            frame();
            const size_t treeIDs = CountTreeStaticIDs(window->layout.root.get());
            ASSERT_GT(treeIDs, 0u) << "test setup: the floating tree was emitted";
            const size_t before = app.idManager.GetStaticIDCount();

            // Its last tab dragged out: the panel goes to another layout, as TransferPanel moves it.
            DockingLayout elsewhere;
            elsewhere.panels[floating] = std::move(window->layout.panels.at(floating));
            window->layout.panels.erase(floating);
            window->layout.root->panelIds.clear();
            frame();

            ASSERT_TRUE(app.m_floatingDockWindows.empty());
            EXPECT_EQ(app.idManager.GetStaticIDCount(), before - 1 - treeIDs) << "the window's and its tree's (its panel moved out)";
            EXPECT_TRUE(app.idManager.IsStaticIDValid(floating)) << "the panel keeps its own where it went";
        }

        // Closing a floating window closes its panels: each gets onDisable, the
        // window goes away, and all its IDs are released.
        TEST_F(DockingTest, ClosingAFloatingWindowClosesItsPanels)
        {
            int enabled = 0, disabled = 0;
            auto fw = std::make_unique<FloatingDockWindow>();
            fw->windowID = app.idManager.CreateStaticID();
            const ID floating = app.CreatePanel(fw->layout.panels, "F", IconID::Invalid, [] {}, {},
                                                [&] { enabled++; }, [&] { disabled++; });
            fw->layout.root = MakeLeaf({ floating });
            FloatingDockWindow* window = fw.get();
            app.m_floatingDockWindows.push_back(std::move(fw));

            auto frame = [&] { app.Frame([&] { EmitDock(); app.EmitFloatingDockWindows(); }); };
            frame();
            frame();
            ASSERT_EQ(enabled, 1) << "test setup: the floating panel is shown";
            const size_t treeIDs = CountTreeStaticIDs(window->layout.root.get());
            const size_t before  = app.idManager.GetStaticIDCount();

            // A user close (the OS reports the window gone) ends here; the
            // headless harness has no OS windows, so call it directly.
            app.CloseFloatingDockWindow(0);
            frame();

            EXPECT_TRUE(app.m_floatingDockWindows.empty());
            EXPECT_EQ(disabled, 1) << "the panel is closed (onDisable), like closing its tab";
            EXPECT_EQ(app.idManager.GetStaticIDCount(), before - 2 - treeIDs) << "the window's, its panel's and its tree's";
        }

        // Dock (the torn-off window's title-bar button) returns its panels as tabs to the
        // leaf they came from, and the emptied window goes away.
        TEST_F(DockingTest, DockReturnsTheTornOffPanelsToTheirLeaf)
        {
            Frame();
            DockNode* home = layout.root->second.get();
            auto fw = std::make_unique<FloatingDockWindow>();
            fw->windowID   = app.idManager.CreateStaticID();
            fw->home       = &layout;
            fw->homeLeafID = home->nodeID;
            const ID floating = app.CreatePanel(fw->layout.panels, "F", IconID::Invalid, [] {}, {}, {}, {});
            fw->layout.root = MakeLeaf({ floating });
            FloatingDockWindow* window = fw.get();
            app.m_floatingDockWindows.push_back(std::move(fw));

            auto frame = [&] { app.Frame([&] { EmitDock(); app.EmitFloatingDockWindows(); }); };
            frame();
            ASSERT_TRUE(window->layout.onDock) << "a window with a home gets a Dock button";
            window->layout.onDock();
            frame();
            frame();

            EXPECT_TRUE(app.m_floatingDockWindows.empty());
            ASSERT_EQ(home->panelIds.size(), 3u);
            EXPECT_EQ(home->panelIds.back(), floating);
            EXPECT_EQ(home->selectedTab, 2) << "the returned panel is the one shown";
            EXPECT_TRUE(layout.panels.contains(floating)) << "the panel moved with it";
        }

        // Its title bar's close button is the user's close: the window stays closed (and
        // its panels with it) though the app declares it again the next frame.
        TEST_F(DockingTest, ClosingATornOffWindowFromItsTitleBarKeepsItClosed)
        {
            auto fw = std::make_unique<FloatingDockWindow>();
            fw->windowID = app.idManager.CreateStaticID();
            const ID floating = app.CreatePanel(fw->layout.panels, "F", IconID::Invalid, [] {}, {}, {}, {});
            fw->layout.root = MakeLeaf({ floating });
            const ID windowID = fw->windowID;
            app.m_floatingDockWindows.push_back(std::move(fw));

            auto frame = [&] { app.Frame([&] { EmitDock(); app.EmitFloatingDockWindows(); }); };
            frame();
            app.Frame([&] { (void)app.windowManager->RequestUserClose(windowID); EmitDock(); app.EmitFloatingDockWindows(); });
            frame();

            EXPECT_TRUE(app.m_floatingDockWindows.empty());
        }

        // A torn-off window's tab takes the title bar's tab style (in the light theme it joins
        // the panel); docked tabs keep theirs.
        // The group holding the focus, in its panel or on its tabs, draws its tabs in the
        // focused-group style; the other groups keep the plain one.
        TEST_F(DockingTest, TheGroupHoldingTheFocusMarksItsTabs)
        {
            const ID fieldA = app.idManager.CreateStaticID(), fieldB = app.idManager.CreateStaticID();
            layout.panels[panelA].onPanelGUI = [&] { app.Button(fieldA, GUIContent{ "A" }, {}); };
            layout.panels[panelB].onPanelGUI = [&] { app.Button(fieldB, GUIContent{ "B" }, {}); };
            layout.panels[panelA].cacheSignature = [] { return std::optional<uint64_t>{}; };
            layout.panels[panelB].cacheSignature = [] { return std::optional<uint64_t>{}; };
            auto tabsWith = [&](StyleRef style) {
                int count = 0;
                const auto& storage = app.elementStorage;
                for (auto it = storage.TopDownDepthScanBegin(); it != storage.TopDownDepthScanEnd(); ++it)
                    for (const Element& e : it->GetChildren(storage))
                        if (e.styleRef.visualBoxStyleIndex == style.visualBoxStyleIndex && e.styleRef.layoutStyleIndex == style.layoutStyleIndex)
                            ++count;
                return count;
            };
            Frame();
            EXPECT_EQ(tabsWith(app.theme.tabPanelFocusedStyleRef), 0) << "no focus, no group marked";

            app.guiStateManager.Interaction().SetFocusID(fieldB);
            Frame();
            Frame();
            EXPECT_EQ(tabsWith(app.theme.tabPanelFocusedStyleRef), 2) << "B and C, the right group";
            EXPECT_EQ(tabsWith(app.theme.tabStyleRef), 1) << "A";

            app.guiStateManager.Interaction().SetFocusID(fieldA);
            Frame();
            Frame();
            EXPECT_EQ(tabsWith(app.theme.tabPanelFocusedStyleRef), 1) << "A";
            EXPECT_EQ(tabsWith(app.theme.tabStyleRef), 2) << "B and C";
        }

        TEST_F(DockingTest, TornOffTabsUseTheTitleBarTabStyle)
        {
            auto fw = std::make_unique<FloatingDockWindow>();
            fw->windowID = app.idManager.CreateStaticID();
            const ID floating = app.CreatePanel(fw->layout.panels, "F", IconID::Invalid, [] {}, {}, {}, {});
            fw->layout.root = MakeLeaf({ floating });
            app.m_floatingDockWindows.push_back(std::move(fw));

            auto frame = [&] { app.Frame([&] { EmitDock(); app.EmitFloatingDockWindows(); }); };
            frame();
            frame();

            int titleBarTabs = 0, dockedTabs = 0;
            const auto& storage = app.elementStorage;
            for (auto it = storage.TopDownDepthScanBegin(); it != storage.TopDownDepthScanEnd(); ++it)
                for (const Element& e : it->GetChildren(storage))
                {
                    if (e.styleRef.visualBoxStyleIndex == app.theme.titleBarTabStyleRef.visualBoxStyleIndex
                        && e.styleRef.layoutStyleIndex == app.theme.titleBarTabStyleRef.layoutStyleIndex) ++titleBarTabs;
                    if (e.styleRef.visualBoxStyleIndex == app.theme.tabStyleRef.visualBoxStyleIndex
                        && e.styleRef.layoutStyleIndex == app.theme.tabStyleRef.layoutStyleIndex) ++dockedTabs;
                }
            EXPECT_EQ(titleBarTabs, 1);
            EXPECT_EQ(dockedTabs, 3) << "A, B and C stay docked tabs";
        }

        // A torn-off window's tabs are its title bar: the strip between and after the tabs
        // drags the window, the tabs themselves stay the GUI's.
        TEST_F(DockingTest, TornOffTabsSitInTheTitleBar)
        {
            auto fw = std::make_unique<FloatingDockWindow>();
            fw->windowID = app.idManager.CreateStaticID();
            const ID floating = app.CreatePanel(fw->layout.panels, "F", IconID::Invalid, [] {}, {}, {}, {});
            fw->layout.root = MakeLeaf({ floating });
            FloatingDockWindow* window = fw.get();
            app.m_floatingDockWindows.push_back(std::move(fw));

            auto frame = [&] { app.Frame([&] { EmitDock(); app.EmitFloatingDockWindows(); }); };
            frame();
            frame();
            const IntRect strip = app.GetElementRect(window->layout.root->tabBarID);
            const IntRect tab   = app.GetElementRect(floating);
            ASSERT_GT(strip.w, tab.w) << "test setup: the strip is wider than its tab";

            EXPECT_EQ(app.TitleBarHitTest(window->windowID, { tab.x + tab.w / 2, tab.y + tab.h / 2 }), WindowHitArea::Client);
            EXPECT_EQ(app.TitleBarHitTest(window->windowID, { strip.x + strip.w - 4, strip.y + strip.h / 2 }), WindowHitArea::Caption);
        }

        // Closing a docked tab closes its panel for good: onDisable fires and the
        // Panel (and its state) is gone; a new panel can be created later.
        TEST_F(DockingTest, ClosingADockedTabDestroysItsPanel)
        {
            Frame();
            ASSERT_EQ(enables[1], 1) << "test setup: B is the visible tab";
            DockNode* right = layout.root->second.get();

            app.CloseDockedPanel(layout, *right, 0);   // close B
            Frame();

            EXPECT_EQ(disables[1], 1);
            EXPECT_EQ(layout.panels.count(panelB), 0u) << "the closed panel must not be kept";
            EXPECT_EQ(right->panelIds, (std::vector<ID>{ panelC }));
        }

        // A docked tab group's context menu puts its tabs on several rows; the dock node keeps
        // that, and the other group stays as it was.
        TEST_F(DockingTest, ATabGroupsMenuPutsItsTabsOnSeveralRows)
        {
            Frame();
            Frame();
            const ID tabB = app.FindByText("B");
            ASSERT_NE(tabB, ID::Invalid());
            const int2 at = app.CenterOf(tabB);
            app.InjectMouseMove(at);
            Frame();
            app.InjectMouseDown(at, MouseButtons::Right);
            app.InjectMouseUp(at, MouseButtons::Right);
            Frame();
            ASSERT_NE(app.FindByText("Multiple Rows"), ID::Invalid());
            // From the keyboard: headless, the menu over the panel's content doesn't take the
            // click from it. Up from the first item wraps to the last, "Multiple Rows".
            app.InjectKey(KeyCode::Up);
            Frame();
            app.InjectKey(KeyCode::Return);
            Frame();
            Frame();
            EXPECT_EQ(layout.root->second->tabLayout, TabBarLayout::Rows);
            EXPECT_EQ(layout.root->first->tabLayout, TabBarLayout::OneRow);
        }

        // A tab group's "+" follows its last tab, when the layout has a new-tab menu; its menu
        // adds a panel there (AddPanelToLeaf), and the new tab is the selected one.
        TEST_F(DockingTest, ATabGroupsPlusAddsATab)
        {
            // The "+" buttons in a leaf's tab bar: what draws there in the icon-menu-button style.
            auto PlusButtons = [this](const DockNode* leaf) {
                std::vector<ID> found;
                const IntRect bar = app.GetElementRect(leaf->tabBarID);
                const auto& storage = app.elementStorage;
                for (uint32_t d = 0; d < storage.NumberOfDepthLevels(); d++)
                    for (uint32_t i = 0; i < storage.DepthSizeOf(d); i++)
                    {
                        const ElementRef ref{ d, i };
                        if (!storage.IsValidRef(ref) || storage.GetElementUnsafe(ref).styleRef != app.theme.iconMenuButtonStyleRef)
                            continue;
                        const IntRect r = storage.GetElementRectUnsafe(ref).rect;
                        if (r.x >= bar.x && r.x + r.w <= bar.x + bar.w && r.y >= bar.y && r.y + r.h <= bar.y + bar.h)
                            found.push_back(storage.GetElementUnsafe(ref).id);
                    }
                return found;
            };
            Frame();
            Frame();
            DockNode* right = layout.root->second.get();
            EXPECT_TRUE(PlusButtons(right).empty()) << "no new-tab menu, no +";

            ID added = ID::Invalid();
            layout.newTabMenu = [this, &added](DockingLayout& in, ID leafNodeID) {
                app.MenuItem("Notes", [this, &in, leafNodeID, &added] {
                    added = app.CreatePanel(in.panels, "Notes", IconID::Invalid, [] {});
                    EXPECT_TRUE(AddPanelToLeaf(in.root.get(), leafNodeID, added));
                });
            };
            Frame();
            Frame();
            const std::vector<ID> plus = PlusButtons(right);
            ASSERT_EQ(plus.size(), 1u);
            const IntRect plusRect = app.GetElementRect(plus[0]);
            const IntRect tabC = app.GetElementRect(panelC);
            EXPECT_GE(plusRect.x, tabC.x + tabC.w) << "after the last tab";
            EXPECT_LT(plusRect.x, tabC.x + tabC.w + app.styleManager.Px(12)) << "right after it";

            app.ClickAt(app.CenterOf(plus[0]));
            Frame();
            Frame();
            ASSERT_TRUE(app.menuManager.IsOpen());
            const ID notes = app.FindByText("Notes");
            ASSERT_NE(notes, ID::Invalid());
            app.mouseWindow = app.menuManager.levels[0].windowID;   // the menu is a window of its own
            app.InjectMouseMove(app.CenterOf(notes));
            Frame();
            app.ClickAt(app.CenterOf(notes));
            Frame();
            app.mouseWindow = ID::Invalid();
            Frame();
            ASSERT_NE(added, ID::Invalid());
            ASSERT_EQ(right->panelIds.size(), 3u);
            EXPECT_EQ(right->panelIds.back(), added);
            EXPECT_EQ(right->selectedTab, 2) << "the new tab shows";
        }

        // A panel closed while input is handled (its tab's Ctrl+W, its menu's Close, a button
        // in it) is out of the layout at once, but what it holds lives until the next frame
        // starts: what this frame queued to draw may still point into it.
        TEST(PanelCloseTest, AClosedPanelLivesUntilTheNextFrame)
        {
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            DockingLayout layout;
            const ID container = app.idManager.CreateStaticID(), closeButton = app.idManager.CreateStaticID();
            auto held = std::make_shared<int>(0);
            const std::weak_ptr<int> watch = held;
            ID closing = ID::Invalid();
            closing = app.CreatePanel(layout.panels, "Closing", IconID::Invalid, [&app, &layout, &closing, closeButton, held] {
                app.Button(closeButton, "Close", [&app, &layout, &closing] { app.ClosePanel(layout, closing); });
            });
            layout.panels[closing].cacheSignature = [] { return std::optional<uint64_t>{}; };   // built each frame
            const ID other = app.CreatePanel(layout.panels, "Other", IconID::Invalid, [] {});
            layout.root = MakeSplit(DockSplitMode::LeftPinned, 400, MakeLeaf({ closing }), MakeLeaf({ other }));
            held.reset();
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.BeginDockingContainer(container, &layout, LayoutOverride{}.Size(800, 600));
                    app.EndDockingContainer();
                    app.EndRootWindow();
                });
            };

            frame();
            const int2 at = app.CenterOf(closeButton);
            app.InjectMouseMove(at);
            frame();
            app.ClickAt(at);
            frame();   // closed by the click, during this frame's input
            EXPECT_FALSE(layout.panels.contains(closing)) << "out of the layout at once";
            EXPECT_FALSE(app.idManager.IsStaticIDValid(closing)) << "its ID given back";
            EXPECT_FALSE(watch.expired()) << "what it holds lives through the frame it was closed in";
            frame();
            EXPECT_TRUE(watch.expired()) << "and goes when the next one starts";
            app.Cleanup();
        }
    } // namespace
}
