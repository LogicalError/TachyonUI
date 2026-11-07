// Documents (controls_documents.cpp): a bar of document tabs, each in its type's colors,
// over the active document's panels; closing, the unsaved dot, "+", the "N open" list,
// tabs that don't fit, reordering, and a document's torn-off panels.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_controls/gui_application_internal.h>
#include <gui_controls/gui_dock_node.h>
#include <gui_core/elements/gui_element_storage.h>

#include <string>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        class DocumentsTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            DocumentSet    set;
            ID             documentsID{};
            int2           windowSize{ 1200, 700 };
            // A second bar under the first, as another main window's would be.
            DocumentSet    other;
            ID             otherID{};
            bool           twoBars = false;

            struct PanelLog { int draws = 0, enables = 0, disables = 0; };
            std::vector<std::unique_ptr<PanelLog>> logs;

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                app.runPostLayoutCallbacks = true;   // the tab bar measures what fits
                documentsID = app.idManager.CreateStaticID();
                otherID     = app.idManager.CreateStaticID();
            }

            void TearDown() override { app.Cleanup(); }

            // A document with one panel, whose draws and visibility are logged.
            Document& Add(std::string name, std::string extension, std::string kind) { return AddTo(set, std::move(name), std::move(extension), std::move(kind)); }
            Document& AddTo(DocumentSet& to, std::string name, std::string extension, std::string kind)
            {
                Document& document = app.CreateDocument(to, name, std::move(extension), std::move(kind), IconID::Invalid);
                PanelLog* log = logs.emplace_back(std::make_unique<PanelLog>()).get();
                const ID panel = app.CreatePanel(document.layout.panels, name + " panel", IconID::Invalid,
                                                 [log] { log->draws++; }, {},
                                                 [log] { log->enables++; }, [log] { log->disables++; });
                document.layout.root = MakeLeaf({ panel });
                return document;
            }

            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow(windowSize, ElementFlags::None, app.theme.mainWindowStyleRef);
                    if (!twoBars)
                    {
                        app.Documents(documentsID, set);
                        app.EndRootWindow();
                        app.EmitFloatingDockWindows();
                        return;
                    }
                    app.Documents(documentsID, set, LayoutOverride{}.Height(windowSize.y / 2));
                    app.Documents(otherID, other, LayoutOverride{}.Height(windowSize.y / 2));
                    app.EndRootWindow();
                    app.EmitFloatingDockWindows(set);
                    app.EmitFloatingDockWindows(other);
                });
            }

            // Two frames: the bar measures its tabs in the first.
            void Settle() { Frame(); Frame(); }

            // The elements drawn in `style`, in tree order.
            std::vector<ID> FindAllByStyle(StyleRef style) const
            {
                std::vector<ID> found;
                const auto& storage = app.elementStorage;
                for (auto it = storage.TopDownDepthScanBegin(); it != storage.TopDownDepthScanEnd(); ++it)
                    for (const Element& e : it->GetChildren(storage))
                        if (e.styleRef == style)
                            found.push_back(e.id);
                return found;
            }
            ID FindByStyle(StyleRef style) const
            {
                const std::vector<ID> found = FindAllByStyle(style);
                return found.empty() ? ID::Invalid() : found.front();
            }
            StyleRef Class(std::string_view cssClass) const { return *app.theme.FindStyle(cssClass); }

            // The tab of `document`, by its name.
            int2 TabCenter(const Document& document) const { return app.CenterOf(app.FindByText(document.name)); }

            // Presses a tab, drags it to `to` (stopping there for `during`) and drops it.
            void DragTab(int2 from, int2 to, const std::function<void()>& during = {})
            {
                app.InjectMouseMove(from);
                app.InjectMouseDown(from);
                Frame();
                app.InjectMouseMove({ from.x + 12, from.y + 2 }, MouseButtons::Left);
                Frame();
                app.InjectMouseMove(to, MouseButtons::Left);
                Frame();
                Frame();
                if (during)
                    during();
                app.InjectMouseUp(to);
                Frame();
                Frame();
            }
            IntRect BarOf(const DocumentSet& documents) const { return app.GetElementRect(documents.internal.barID); }
            static std::vector<std::string> Names(const DocumentSet& documents)
            {
                std::vector<std::string> names;
                for (const auto& document : documents.documents)
                    names.push_back(document->name);
                return names;
            }
        };

        TEST_F(DocumentsTest, OnlyTheActiveDocumentsPanelsAreBuilt)
        {
            Add("Forest", ".level", "level");
            Add("Terrain", ".mat", "material");
            Settle();
            EXPECT_EQ(set.active, 0) << "the first document added is the active one";
            EXPECT_GE(logs[0]->draws, 1);
            EXPECT_EQ(logs[0]->enables, 1);
            EXPECT_EQ(logs[1]->draws, 0) << "a document that isn't shown builds nothing";
            EXPECT_EQ(logs[1]->enables, 0);

            set.active = 1;
            const int forestDraws = logs[0]->draws;
            Frame();
            EXPECT_EQ(logs[1]->draws, 1);
            EXPECT_EQ(logs[1]->enables, 1);
            EXPECT_EQ(logs[0]->disables, 1) << "the panels of the document left are hidden";
            EXPECT_EQ(logs[0]->draws, forestDraws);
        }

        // The bar and the panels apart: the tabs in the window's title bar, a menu bar between
        // them and the panels. A tab is clicked as before, the bar's empty space drags the
        // window, and "N open" stays a dropdown, not one of the title bar's menus.
        TEST_F(DocumentsTest, TheBarCanBeInTheTitleBar)
        {
            Add("Forest", ".level", "level");
            Add("Terrain", ".mat", "material");
            const ID titleBarID = app.idManager.CreateStaticID(), menuBarID = app.idManager.CreateStaticID();
            const ID fileID = app.idManager.CreateStaticID();
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow(windowSize, ElementFlags::WindowCustomTitleBar, app.theme.mainWindowStyleRef);
                    app.BeginTitleBar(titleBarID);
                    app.DocumentBar(documentsID, set, app.theme.titleBarDocumentsStyleRef);
                    app.EndTitleBar();
                    EXPECT_TRUE(app.menuManager.menuBarTriggers.empty()) << "\"N open\" isn't one of the title bar's menus";
                    app.BeginMenuBar(menuBarID);
                    if (app.BeginMenu(fileID, "File", app.theme.menuBarTriggerStyleRef, app.theme.menuStyleRef, app.theme.menuItemRowStyleRef))
                        app.MenuItem("New", nullptr);
                    app.EndMenu();
                    app.EndMenuBar();
                    app.DocumentPanels(set);
                    app.EndRootWindow();
                });
            };
            frame();
            frame();
            EXPECT_GE(logs[0]->draws, 1) << "the active document's panels";
            EXPECT_EQ(logs[1]->draws, 0);

            const IntRect titleBar = app.GetElementRect(titleBarID);
            const IntRect bar = BarOf(set);
            EXPECT_GE(bar.y, titleBar.y);
            EXPECT_LE(bar.y + bar.h, titleBar.y + titleBar.h) << "the tabs fit in the title bar";
            EXPECT_GE(app.GetElementRect(menuBarID).y, titleBar.y + titleBar.h) << "the menus under it";
            const int2 tab = TabCenter(*set.documents[1]);
            EXPECT_EQ(app.TitleBarHitTest(app.RootWindowID(), tab), WindowHitArea::Client) << "a tab takes clicks";
            EXPECT_EQ(app.TitleBarHitTest(app.RootWindowID(), { bar.x + bar.w * 3 / 4, bar.y + 4 }), WindowHitArea::Caption)
                << "the bar's empty space drags the window";

            app.ClickAt(tab);
            frame();
            frame();
            EXPECT_EQ(set.active, 1);
            EXPECT_GE(logs[1]->draws, 1);
        }

        TEST_F(DocumentsTest, ClickingATabShowsItsDocument)
        {
            Add("Forest", ".level", "level");
            Document& desert = Add("Desert", ".level", "level");
            Settle();
            app.ClickAt(TabCenter(desert));
            Frame();
            Frame();
            EXPECT_EQ(set.active, 1);
            EXPECT_EQ(logs[1]->draws, 1);
        }

        TEST_F(DocumentsTest, TabsAndTheLineTakeTheirTypesColors)
        {
            Add("Forest", ".level", "level");
            Add("Terrain", ".mat", "material");
            Add("Notes", ".txt", "text");   // a type the theme has no classes for
            Settle();

            EXPECT_EQ(FindAllByStyle(Class("document-tab-level")).size(), 1u);
            EXPECT_EQ(FindAllByStyle(Class("document-tab-material")).size(), 1u);
            EXPECT_EQ(FindAllByStyle(app.theme.documentTabStyleRef).size(), 1u) << "an unknown type gets the plain tab";
            EXPECT_EQ(FindAllByStyle(Class("document-name-material")).size(), 1u) << "the icon in the type's color";
            EXPECT_NE(FindByStyle(Class("document-line-level")), ID::Invalid()) << "the line is the active document's";
            EXPECT_EQ(FindByStyle(Class("document-line-material")), ID::Invalid());

            set.active = 1;
            Frame();
            EXPECT_NE(FindByStyle(Class("document-line-material")), ID::Invalid());
            EXPECT_EQ(FindByStyle(Class("document-line-level")), ID::Invalid());
            set.active = 2;
            Frame();
            EXPECT_NE(FindByStyle(app.theme.documentLineStyleRef), ID::Invalid());
        }

        TEST_F(DocumentsTest, TheNameIsFollowedByItsExtension)
        {
            Document& forest = Add("Forest", ".level", "level");
            Settle();
            const ID name = app.FindByText("Forest");
            const ID extension = app.FindByText(".level");
            ASSERT_NE(name, ID::Invalid());
            ASSERT_NE(extension, ID::Invalid());
            const IntRect n = app.GetElementRect(name), e = app.GetElementRect(extension);
            EXPECT_EQ(n.x + n.w, e.x) << "no gap between the name and its extension";
            const IntRect tab = app.GetElementRect(forest.id);
            EXPECT_EQ(tab.h, app.styleManager.Px(30));
            EXPECT_GE(tab.w, app.styleManager.Px(150)) << "min-width";
        }

        TEST_F(DocumentsTest, ATypeWithoutClassesLooksLikeTheOthers)
        {
            Document& forest = Add("Forest", ".level", "level");
            Document& notes = Add("Notes", ".txt", "text");
            Settle();
            const IntRect n = app.GetElementRect(app.FindByText("Notes"));
            const IntRect e = app.GetElementRect(app.FindByText(".txt"));
            EXPECT_EQ(n.x + n.w, e.x);
            EXPECT_EQ(app.GetElementRect(notes.id).h, app.GetElementRect(forest.id).h);
            EXPECT_EQ(n.h, app.GetElementRect(app.FindByText("Forest")).h);
            EXPECT_EQ(n.y, app.GetElementRect(app.FindByText("Forest")).y) << "the plain name sits as a typed one";
        }

        TEST_F(DocumentsTest, TheBarIsItsOwnRowOverThePanels)
        {
            Add("Forest", ".level", "level");
            Settle();
            const IntRect bar = app.GetElementRect(FindByStyle(app.theme.documentBarStyleRef));
            const IntRect line = app.GetElementRect(FindByStyle(Class("document-line-level")));
            EXPECT_EQ(bar.h, app.styleManager.Px(36));
            EXPECT_EQ(line.h, app.styleManager.Px(2));
            EXPECT_EQ(line.y + line.h, bar.y + bar.h) << "the line is the bar's bottom edge";
            EXPECT_EQ(line.w, bar.w);
            const IntRect panels = app.GetElementRect(set.documents[0]->internal.dockID);
            EXPECT_GE(panels.y, bar.y + bar.h) << "the panels start below the bar";
            EXPECT_GT(panels.h, windowSize.y / 2) << "and fill the rest";
        }

        TEST_F(DocumentsTest, TheCloseButtonAsksOnCloseAndClosesTheNextFrame)
        {
            Add("Forest", ".level", "level");
            Add("Desert", ".level", "level");
            int asked = 0;
            bool allow = false;
            set.onClose = [&](Document& document) { ++asked; EXPECT_EQ(document.name, "Forest"); return allow; };
            Settle();

            const std::vector<ID> closes = FindAllByStyle(app.theme.documentCloseStyleRef);
            ASSERT_EQ(closes.size(), 1u) << "only the active tab shows its close button";
            app.ClickAt(app.CenterOf(closes[0]));
            Frame();
            EXPECT_EQ(asked, 0) << "not from the handler";
            Frame();
            EXPECT_EQ(asked, 1);
            EXPECT_EQ(set.documents.size(), 2u) << "onClose said no";
            EXPECT_EQ(set.active, 0) << "a click on the close button doesn't select";

            allow = true;
            app.ClickAt(app.CenterOf(FindByStyle(app.theme.documentCloseStyleRef)));
            Frame();
            Frame();
            EXPECT_EQ(asked, 2);
            ASSERT_EQ(set.documents.size(), 1u);
            EXPECT_EQ(set.documents[0]->name, "Desert");
            EXPECT_EQ(set.active, 0);
            EXPECT_EQ(logs[0]->disables, 1) << "its panels are closed";
            EXPECT_EQ(logs[1]->draws, 1) << "the next document is shown";
        }

        TEST_F(DocumentsTest, CtrlWClosesTheFocusedDocument)
        {
            Add("Forest", ".level", "level");
            Document& desert = Add("Desert", ".level", "level");
            Settle();
            app.ClickAt(TabCenter(desert));
            Frame();
            app.InjectKey(KeyCode::KeyW, KeyAction::KeyDown, KeyModifier::Control);
            Frame();
            Frame();
            ASSERT_EQ(set.documents.size(), 1u);
            EXPECT_EQ(set.documents[0]->name, "Forest");
            EXPECT_EQ(set.active, 0) << "the one before takes over from the last";
        }

        TEST_F(DocumentsTest, ClosingKeepsTheActiveDocument)
        {
            Add("A", ".level", "level");
            Add("B", ".level", "level");
            Add("C", ".level", "level");
            Add("D", ".level", "level");
            set.active = 2;
            Settle();
            app.CloseDocument(set, 0);
            EXPECT_EQ(set.active, 1);
            EXPECT_EQ(set.Active()->name, "C");
            app.CloseDocument(set, 2);
            EXPECT_EQ(set.Active()->name, "C") << "one after it closed";
            app.CloseDocument(set, 1);
            EXPECT_EQ(set.Active()->name, "B") << "the active one closed, the last: the one before";
            app.CloseDocument(set, 5);   // no such document
            EXPECT_EQ(set.documents.size(), 1u);
            app.CloseDocument(set, 0);
            EXPECT_EQ(set.Active(), nullptr);
            Frame();   // a set without documents still builds its bar
            EXPECT_NE(FindByStyle(app.theme.documentLineStyleRef), ID::Invalid());
        }

        TEST_F(DocumentsTest, ClosingReleasesTheDocumentsIDs)
        {
            Add("Forest", ".level", "level");
            Add("Desert", ".level", "level");
            Settle();
            const size_t before = app.idManager.GetStaticIDCount();
            const DockNode* root = set.documents[0]->layout.root.get();
            ASSERT_NE(root->nodeID, ID::Invalid()) << "test setup: the tree was built";
            const size_t treeIDs = (root->nodeID.GetMode() == IDMode::Static) + (root->tabBarID.GetMode() == IDMode::Static);
            app.CloseDocument(set, 0);
            EXPECT_EQ(app.idManager.GetStaticIDCount(), before - 2 - treeIDs) << "its own two, and its tree's";
        }

        TEST_F(DocumentsTest, AClosedDocumentsLayoutIsNoLongerSaved)
        {
            Document& forest = Add("Forest", ".level", "level");
            Add("Desert", ".level", "level");
            app.PersistDocking("forest", forest.layout);
            Settle();
            ASSERT_NE(app.SaveUIState().find("\"forest\""), std::string::npos) << "test setup: it's saved";
            app.CloseDocument(set, 0);
            EXPECT_EQ(app.SaveUIState().find("\"forest\""), std::string::npos);
        }

        TEST_F(DocumentsTest, AnUnsavedDocumentShowsADotUntilHovered)
        {
            Add("Forest", ".level", "level");
            Document& desert = Add("Desert", ".level", "level");
            desert.dirty = true;
            Settle();
            EXPECT_EQ(FindAllByStyle(app.theme.documentDirtyStyleRef).size(), 1u);
            EXPECT_EQ(FindAllByStyle(app.theme.documentCloseStyleRef).size(), 1u) << "the active tab's only";

            app.InjectMouseMove(TabCenter(desert));
            Frame();
            Frame();
            EXPECT_EQ(FindAllByStyle(app.theme.documentDirtyStyleRef).size(), 0u) << "hovered: its close button instead";
            EXPECT_EQ(FindAllByStyle(app.theme.documentCloseStyleRef).size(), 2u);

            const IntRect tabBefore = app.GetElementRect(desert.id);
            app.InjectMouseMove({ 5, windowSize.y - 5 });
            Frame();
            Frame();
            EXPECT_EQ(FindAllByStyle(app.theme.documentDirtyStyleRef).size(), 1u);
            EXPECT_EQ(app.GetElementRect(desert.id).w, tabBefore.w) << "the mark's slot keeps the tab's width";
        }

        TEST_F(DocumentsTest, ThePlusButtonCallsOnNew)
        {
            Add("Forest", ".level", "level");
            Settle();
            EXPECT_EQ(FindByStyle(app.theme.documentNewStyleRef), ID::Invalid()) << "none without onNew";

            int created = 0;
            set.onNew = [&] { ++created; app.CreateDocument(set, "Untitled", ".level", "level", IconID::Invalid); };
            Settle();
            const ID plus = FindByStyle(app.theme.documentNewStyleRef);
            ASSERT_NE(plus, ID::Invalid());
            const IntRect last = app.GetElementRect(set.documents[0]->id);
            EXPECT_GT(app.GetElementRect(plus).x, last.x + last.w) << "after the last tab";
            app.ClickAt(app.CenterOf(plus));
            Frame();
            Frame();
            EXPECT_EQ(created, 1);
            EXPECT_EQ(set.documents.size(), 2u);
            EXPECT_NE(app.FindByText("Untitled"), ID::Invalid());
        }

        TEST_F(DocumentsTest, TheListNamesEveryDocumentAndShowsThePickedOne)
        {
            Add("Forest", ".level", "level");
            Add("Terrain", ".mat", "material");
            Add("Hud", ".layout", "layout");
            Settle();
            const ID list = app.FindByText("3 open");
            ASSERT_NE(list, ID::Invalid());
            const IntRect bar = app.GetElementRect(FindByStyle(app.theme.documentBarStyleRef));
            const IntRect at = app.GetElementRect(list);
            EXPECT_GT(at.x, bar.x + bar.w / 2) << "on the right";

            app.ClickAt(app.CenterOf(list));
            for (int i = 0; i < 4; ++i)   // the menu's window settles
                Frame();
            const ID item = app.FindByText("Hud.layout");
            ASSERT_NE(item, ID::Invalid()) << "the menu lists name and extension";
            app.mouseWindow = app.menuManager.levels[0].windowID;   // the panels are built after it
            app.ClickAt(app.CenterOf(item));
            Frame();
            app.mouseWindow = ID::Invalid();
            Frame();
            EXPECT_EQ(set.active, 2);
        }

        TEST_F(DocumentsTest, TabsThatDontFitAreLeftToTheList)
        {
            windowSize = { 700, 500 };
            for (int i = 0; i < 8; ++i)
                Add("Doc" + std::to_string(i), ".level", "level");
            set.onNew = [] {};
            set.active = 6;
            Settle();
            Frame();

            int shown = 0;
            for (const auto& document : set.documents)
                shown += app.HasElement(document->id) ? 1 : 0;
            EXPECT_LT(shown, 8);
            EXPECT_GT(shown, 1);
            EXPECT_TRUE(app.HasElement(set.documents[6]->id)) << "the active tab is always shown";
            EXPECT_EQ(FindByStyle(app.theme.tabOverflowStyleRef), ID::Invalid()) << "no \"+N\": the list has them";
            auto noOverlap = [&](const char* when)
            {
                const IntRect plus = app.GetElementRect(FindByStyle(app.theme.documentNewStyleRef));
                const IntRect list = app.GetElementRect(app.FindByText(std::to_string(set.documents.size()) + " open"));
                EXPECT_LE(plus.x + plus.w, list.x) << "\"+\" still fits before the list " << when;
                for (const auto& document : set.documents)
                    if (app.HasElement(document->id))
                    {
                        const IntRect tab = app.GetElementRect(document->id);
                        EXPECT_LE(tab.x + tab.w, plus.x) << document->name << " overlaps \"+\" " << when;
                    }
            };
            noOverlap("settled");

            // A document added out of view, never drawn, then shown: its width until it's
            // drawn is its tab's minimum, so the others make room for it at once.
            Add("X", ".level", "level");
            Frame();
            set.active = int(set.documents.size()) - 1;
            Frame();
            ASSERT_TRUE(app.HasElement(set.documents.back()->id));
            noOverlap("the frame it's shown");
        }

        TEST_F(DocumentsTest, DraggingATabReordersTheDocuments)
        {
            Document& a = Add("A", ".level", "level");
            Document& b = Add("B", ".level", "level");
            Add("C", ".level", "level");
            Settle();
            ASSERT_EQ(set.active, 0);

            const int2 from = app.CenterOf(a.id);
            const IntRect target = app.GetElementRect(b.id);
            const int2 to{ target.x + target.w * 3 / 4, target.y + target.h / 2 };
            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            Frame();
            app.InjectMouseMove({ from.x + 10, from.y }, MouseButtons::Left);
            Frame();
            app.InjectMouseMove(to, MouseButtons::Left);
            Frame();
            app.InjectMouseUp(to);
            Frame();
            Frame();
            ASSERT_EQ(set.documents.size(), 3u);
            EXPECT_EQ(set.documents[0]->name, "B");
            EXPECT_EQ(set.documents[1]->name, "A");
            EXPECT_EQ(set.Active()->name, "A") << "the dragged (active) document stays active";
        }

        // A press selects the tab it drags, so another document stays active only when it's
        // made active again during the drag (as code may); it then moves with what passes it.
        TEST_F(DocumentsTest, ADocumentDraggedPastTheActiveOneKeepsItActive)
        {
            Add("A", ".level", "level");
            Add("B", ".level", "level");
            Add("C", ".level", "level");
            Settle();

            auto drag = [&](int fromIndex, int ontoIndex, float atFraction, int activeDuring)
            {
                const int2 from = app.CenterOf(set.documents[size_t(fromIndex)]->id);
                const IntRect target = app.GetElementRect(set.documents[size_t(ontoIndex)]->id);
                const int2 to{ target.x + int32_t(float(target.w) * atFraction), target.y + target.h / 2 };
                app.InjectMouseMove(from);
                app.InjectMouseDown(from);
                Frame();
                set.active = activeDuring;
                app.InjectMouseMove({ from.x + (to.x > from.x ? 10 : -10), from.y }, MouseButtons::Left);
                Frame();
                app.InjectMouseMove(to, MouseButtons::Left);
                Frame();
                app.InjectMouseUp(to);
                Frame();
                Frame();
            };

            drag(0, 1, 0.75f, 1);   // A past B, B active
            ASSERT_EQ(set.documents[0]->name, "B");
            EXPECT_EQ(set.Active()->name, "B") << "B moved back one place";

            drag(2, 0, 0.25f, 0);   // C before B, B active
            ASSERT_EQ(set.documents[0]->name, "C");
            EXPECT_EQ(set.Active()->name, "B") << "B moved on one place";
        }

        TEST_F(DocumentsTest, ATabsMenuClosesItAndHasNoLayouts)
        {
            Document& forest = Add("Forest", ".level", "level");
            Settle();
            const int2 at = TabCenter(forest);
            app.InjectMouseMove(at);
            Frame();
            app.InjectMouseDown(at, MouseButtons::Right);
            app.InjectMouseUp(at, MouseButtons::Right);
            Frame();
            EXPECT_NE(app.FindByText("Close Document"), ID::Invalid());
            EXPECT_EQ(app.FindByText("Multiple Rows"), ID::Invalid()) << "the bar keeps one row";
            app.menuManager.CloseAll(app.guiStateManager);

            // The empty part of the bar, after the tabs, has no layouts either.
            const IntRect bar = app.GetElementRect(FindByStyle(app.theme.documentTabBarStyleRef));
            const int2 strip{ bar.x + bar.w - 10, bar.y + bar.h / 2 };
            app.InjectMouseMove(strip);
            Frame();
            app.InjectMouseDown(strip, MouseButtons::Right);
            app.InjectMouseUp(strip, MouseButtons::Right);
            Frame();
            EXPECT_EQ(app.FindByText("Multiple Rows"), ID::Invalid());
        }

        // ---- Moving documents between bars and tearing them off ----

        TEST_F(DocumentsTest, ATabDroppedOnAnotherBarMovesItsDocumentThere)
        {
            twoBars = true;
            Document& forest = Add("Forest", ".level", "level");
            Add("Desert", ".level", "level");
            Add("Dunes", ".level", "level");
            AddTo(other, "Terrain", ".mat", "material");
            AddTo(other, "Hud", ".layout", "layout");
            set.onTearOff = [](std::unique_ptr<Document>, int2) {};   // a drop off the bars would make a window
            Settle();

            const IntRect terrain = app.GetElementRect(other.documents[0]->id);
            const int2 between{ terrain.x + terrain.w * 3 / 4, terrain.y + terrain.h / 2 };
            int shownDuring = 0;
            DragTab(TabCenter(forest), between, [&] {
                shownDuring = app.CountByText("Forest");
                EXPECT_GT(app.GetElementRect(other.documents[1]->id).x, terrain.x + terrain.w + app.styleManager.Px(100))
                    << "Hud moves apart for it";
                set.active = 2;   // Dunes shown meanwhile: it stays shown
            });
            EXPECT_EQ(shownDuring, 2) << "its tab, and a phantom in the bar it would go to; no window preview";
            EXPECT_EQ(Names(set), (std::vector<std::string>{ "Desert", "Dunes" }));
            EXPECT_EQ(Names(other), (std::vector<std::string>{ "Terrain", "Forest", "Hud" })) << "after the tab it passed the middle of";
            EXPECT_EQ(other.Active(), &forest) << "the moved document is shown";
            EXPECT_EQ(set.Active()->name, "Dunes");
            EXPECT_EQ(logs[0]->enables - logs[0]->disables, 1) << "its panels move with it, shown there";
            EXPECT_EQ(app.CountByText("Forest"), 1);
        }

        // Over another bar the phantom follows the cursor, held where the tab was grabbed, as a
        // tab dragged within its own bar does.
        TEST_F(DocumentsTest, ThePhantomFollowsTheCursor)
        {
            twoBars = true;
            Document& forest = Add("Forest", ".level", "level");
            AddTo(other, "Terrain", ".mat", "material");
            AddTo(other, "Hud", ".layout", "layout");
            Settle();

            const IntRect tab = app.GetElementRect(forest.id);
            const int2 from{ tab.x + 40, tab.y + tab.h / 2 };   // grabbed 40 px into the tab
            const IntRect bar = BarOf(other);
            const IntRect hud = app.GetElementRect(other.documents[1]->id);
            auto phantomX = [&]
            {
                for (const ID id : FindAllByStyle(Class("document-tab-level")))
                {
                    const IntRect r = app.GetElementRect(id);
                    if (r.y >= bar.y && r.y < bar.y + bar.h)
                        return r.x;
                }
                return INT32_MIN;
            };

            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            Frame();
            app.InjectMouseMove({ from.x + 12, from.y + 2 }, MouseButtons::Left);
            Frame();
            const int32_t y = bar.y + bar.h / 2;
            // Two places past Hud's middle: the phantom goes after Hud both times.
            const int32_t first = hud.x + hud.w * 3 / 4, second = first + 30;
            app.InjectMouseMove({ first, y }, MouseButtons::Left);
            Frame();
            Frame();
            EXPECT_EQ(phantomX(), first - 40);
            app.InjectMouseMove({ second, y }, MouseButtons::Left);
            Frame();
            Frame();
            EXPECT_EQ(phantomX(), second - 40) << "it moves with the cursor, not by slots";
            app.InjectMouseUp({ second, y });
            Frame();
            Frame();
            EXPECT_EQ(Names(other), (std::vector<std::string>{ "Terrain", "Hud", "Forest" }));
        }

        // Dropped right of every tab, a tab goes last: in its own bar and in another.
        TEST_F(DocumentsTest, ATabDroppedPastTheLastOneGoesLast)
        {
            twoBars = true;
            Add("A", ".level", "level");
            Add("B", ".level", "level");
            Add("C", ".level", "level");
            AddTo(other, "Terrain", ".mat", "material");
            AddTo(other, "Hud", ".layout", "layout");
            set.onNew = [] {};
            Settle();

            // Just past the last tab, past "+", into the empty bar, up to "N open", on it and
            // past it; at the tabs' height, in the gap above them and on the line under them.
            const IntRect bar = BarOf(set);
            for (const int y : { bar.y + bar.h / 2, bar.y + 1, bar.y + bar.h - 1 })
                for (const int x : { 0, 1, 2, 3, 4, 5 })
                {
                    const IntRect last = app.GetElementRect(set.documents.back()->id);
                    const IntRect plus = app.GetElementRect(FindByStyle(app.theme.documentNewStyleRef));
                    const IntRect list = app.GetElementRect(app.FindByText(std::to_string(set.documents.size()) + " open"));
                    const int32_t xs[] = { last.x + last.w + 2, plus.x + plus.w + 4, (plus.x + plus.w + list.x) / 2, list.x - 4,
                                           list.x + list.w / 2, bar.x + bar.w - 2 };
                    const ID dragged = set.documents[0]->id;
                    const std::string first = set.documents[0]->name;
                    int32_t draggedX = 0;
                    DragTab(app.CenterOf(dragged), { xs[x], y }, [&] { draggedX = app.GetElementRect(dragged).x; });
                    EXPECT_GT(draggedX, last.x) << "it follows the cursor, at spot " << x << " y " << y;
                    EXPECT_EQ(set.documents.back()->name, first) << "own bar, spot " << x << " y " << y;
                }

            // The padding before the first tab: first.
            const std::string lastName = set.documents.back()->name;
            DragTab(app.CenterOf(set.documents.back()->id), { bar.x + 2, bar.y + 1 });
            EXPECT_EQ(set.documents.front()->name, lastName);

            const IntRect hud = app.GetElementRect(other.documents[1]->id);
            const IntRect otherBar = BarOf(other);
            DragTab(app.CenterOf(set.documents[0]->id), { (hud.x + hud.w + otherBar.x + otherBar.w) / 2, hud.y + hud.h / 2 });
            ASSERT_EQ(other.documents.size(), 3u);
            EXPECT_NE(other.documents.back()->name, "Hud") << "other bar: last";
        }

        // Tabs left out of the bar don't count when the one dragged is found.
        TEST_F(DocumentsTest, ATabAfterOnesLeftOutMovesItsOwnDocument)
        {
            twoBars = true;
            windowSize = { 700, 700 };
            for (int i = 0; i < 8; ++i)
                Add("Doc" + std::to_string(i), ".level", "level");
            AddTo(other, "Terrain", ".mat", "material");
            set.active = 7;
            Settle();
            Frame();
            ASSERT_FALSE(app.HasElement(set.documents[6]->id)) << "test setup: tabs before it are left out";
            ASSERT_TRUE(app.HasElement(set.documents[7]->id));

            const IntRect bar = BarOf(other);
            int phantoms = 0;
            DragTab(app.CenterOf(set.documents[7]->id), { bar.x + bar.w / 2, bar.y + bar.h / 2 },
                    [&] { phantoms = app.CountByText("Doc7"); });
            EXPECT_EQ(phantoms, 2) << "the phantom is the dragged document's";
            ASSERT_EQ(other.documents.size(), 2u);
            EXPECT_EQ(other.documents[1]->name, "Doc7");
            EXPECT_EQ(set.documents.size(), 7u);
        }

        TEST_F(DocumentsTest, ATabDroppedOffEveryBarIsTornOff)
        {
            twoBars = true;
            Document& forest = Add("Forest", ".level", "level");
            Add("Desert", ".level", "level");
            AddTo(other, "Terrain", ".mat", "material");
            std::vector<std::pair<std::string, int2>> tornOff;
            set.onTearOff = [&](std::unique_ptr<Document> document, int2 at) { tornOff.push_back({ document->name, at }); };
            Settle();

            // Over the other bar's panels: documents don't drop on panels.
            const IntRect panels = app.GetElementRect(other.documents[0]->internal.dockID);
            const int2 onPanels{ panels.x + panels.w / 2, panels.y + panels.h / 2 };
            bool ghostDuring = false;
            DragTab(TabCenter(forest), onPanels, [&] { ghostDuring = app.CountByText("Forest") == 2; });
            EXPECT_TRUE(ghostDuring) << "the window a drop would make follows the cursor";
            ASSERT_EQ(tornOff.size(), 1u);
            EXPECT_EQ(tornOff[0].first, "Forest");
            EXPECT_EQ(tornOff[0].second, onPanels);
            EXPECT_EQ(Names(set), (std::vector<std::string>{ "Desert" }));
            EXPECT_EQ(Names(other), (std::vector<std::string>{ "Terrain" }));
            EXPECT_EQ(other.documents[0]->layout.panels.size(), 1u) << "nor did it dock";
        }

        TEST_F(DocumentsTest, WithoutOnTearOffATabDroppedOffTheBarsGoesBack)
        {
            Document& forest = Add("Forest", ".level", "level");
            Add("Desert", ".level", "level");
            Settle();
            bool ghostDuring = true;
            DragTab(TabCenter(forest), { 600, 500 }, [&] { ghostDuring = app.CountByText("Forest") == 2; });
            EXPECT_FALSE(ghostDuring) << "no window to preview";
            EXPECT_EQ(Names(set), (std::vector<std::string>{ "Forest", "Desert" }));
        }

        TEST_F(DocumentsTest, OffTheTabsButOnItsOwnBarATabMovesAlongIt)
        {
            Add("Forest", ".level", "level");
            Add("Desert", ".level", "level");
            Add("Dunes", ".level", "level");
            int tornOff = 0;
            set.onTearOff = [&](std::unique_ptr<Document>, int2) { ++tornOff; };
            Settle();
            // The bar past "N open", outside the tabs but still the bar: last.
            const IntRect bar = BarOf(set);
            DragTab(TabCenter(*set.documents[0]), { bar.x + bar.w - 3, bar.y + bar.h / 2 });
            EXPECT_EQ(Names(set), (std::vector<std::string>{ "Desert", "Dunes", "Forest" }));
            EXPECT_EQ(set.Active()->name, "Forest");
            EXPECT_EQ(tornOff, 0);
        }

        TEST_F(DocumentsTest, APanelTabDroppedOnADocumentBarIsNotADocument)
        {
            twoBars = true;
            Add("Forest", ".level", "level");
            Add("Desert", ".level", "level");
            AddTo(other, "Terrain", ".mat", "material");
            Settle();
            const IntRect bar = BarOf(other);
            // A document dragged before: what's left of that drag shows nothing now.
            DragTab(TabCenter(*set.documents[1]), { bar.x + bar.w / 3, bar.y + bar.h / 2 });
            ASSERT_EQ(Names(other), (std::vector<std::string>{ "Terrain", "Desert" }));
            app.CloseDocument(other, 1);
            Frame();

            const ID panel = set.documents[0]->layout.root->panelIds[0];
            int forestDuring = 0;
            DragTab(app.CenterOf(panel), { bar.x + bar.w / 3, bar.y + bar.h / 2 },
                    [&] { forestDuring = app.CountByText("Forest"); });
            EXPECT_EQ(forestDuring, 1) << "no document phantom for a panel";
            EXPECT_EQ(Names(other), (std::vector<std::string>{ "Terrain" }));
            EXPECT_EQ(other.documents[0]->layout.panels.size(), 1u);
            ASSERT_EQ(app.m_floatingDockWindows.size(), 1u) << "it floats, as off every dock";
            EXPECT_EQ(app.m_floatingDockWindows[0]->home, &set.documents[0]->layout);
        }

        TEST_F(DocumentsTest, TornOffPanelsBelongToTheirDocumentsWindow)
        {
            twoBars = true;
            Document& forest = Add("Forest", ".level", "level");
            AddTo(other, "Terrain", ".mat", "material");
            const ID floated = app.CreatePanel(forest.layout.panels, "Floated", IconID::Invalid, [] {}, {});
            forest.layout.root = MakeSplit(DockSplitMode::LeftPinned, 200, std::move(forest.layout.root), MakeLeaf({ floated }));
            Settle();
            ASSERT_TRUE(app.FloatPanel(forest.layout, floated, { 100, 100 }));
            Frame();
            ASSERT_EQ(app.m_floatingDockWindows.size(), 1u);
            const ID window = app.m_floatingDockWindows[0]->windowID;

            // Only the set holding its document builds it.
            int built = 0;
            auto emitOnly = [&](DocumentSet& documents)
            {
                app.Frame([&] {
                    app.BeginRootWindow(windowSize, ElementFlags::None, app.theme.mainWindowStyleRef);
                    app.Documents(documentsID, set, LayoutOverride{}.Height(windowSize.y / 2));
                    app.Documents(otherID, other, LayoutOverride{}.Height(windowSize.y / 2));
                    app.EndRootWindow();
                    app.EmitFloatingDockWindows(documents);
                });
                built = app.HasElement(window) ? 1 : 0;
            };
            emitOnly(other);
            EXPECT_EQ(built, 0);
            emitOnly(set);
            EXPECT_EQ(built, 1);

            // Its document moved to the other bar, the other set builds it.
            const IntRect bar = BarOf(other);
            DragTab(TabCenter(forest), { bar.x + bar.w / 2, bar.y + bar.h / 2 });
            ASSERT_EQ(other.documents.size(), 2u);
            emitOnly(set);
            EXPECT_EQ(built, 0);
            emitOnly(other);
            EXPECT_EQ(built, 1);
            EXPECT_EQ(app.m_floatingDockWindows.size(), 1u);
        }

        TEST_F(DocumentsTest, ADocumentClosedBeforeItsDropIsDoneStaysClosed)
        {
            twoBars = true;
            Document& forest = Add("Forest", ".level", "level");
            Add("Desert", ".level", "level");
            AddTo(other, "Terrain", ".mat", "material");
            Settle();
            const IntRect bar = BarOf(other);
            const int2 from = TabCenter(forest);
            const int2 to{ bar.x + bar.w / 2, bar.y + bar.h / 2 };
            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            Frame();
            app.InjectMouseMove({ from.x + 12, from.y + 2 }, MouseButtons::Left);
            Frame();
            app.InjectMouseMove(to, MouseButtons::Left);
            Frame();
            app.InjectMouseUp(to);
            Frame();   // dropped: the move waits for the next frame
            app.CloseDocument(set, 0);
            Frame();
            EXPECT_EQ(Names(set), (std::vector<std::string>{ "Desert" }));
            EXPECT_EQ(Names(other), (std::vector<std::string>{ "Terrain" }));
        }

        TEST_F(DocumentsTest, ClosingADocumentClosesItsTornOffPanels)
        {
            Document& forest = Add("Forest", ".level", "level");
            Add("Desert", ".level", "level");
            const ID floated = app.CreatePanel(forest.layout.panels, "Floated", IconID::Invalid, [] {}, {});
            forest.layout.root = MakeSplit(DockSplitMode::LeftPinned, 200, std::move(forest.layout.root), MakeLeaf({ floated }));
            Settle();
            ASSERT_TRUE(app.FloatPanel(forest.layout, floated, { 100, 100 }));
            Frame();
            ASSERT_EQ(app.m_floatingDockWindows.size(), 1u);

            app.CloseDocument(set, 0);
            EXPECT_TRUE(app.m_floatingDockWindows.empty()) << "they would dock back into a layout that's gone";
            Frame();
        }

        TEST_F(DocumentsTest, DockingIntoAHiddenDocumentShowsItFirst)
        {
            Document& forest = Add("Forest", ".level", "level");
            Add("Desert", ".level", "level");
            const ID floated = app.CreatePanel(forest.layout.panels, "Floated", IconID::Invalid, [] {}, {});
            forest.layout.root = MakeSplit(DockSplitMode::LeftPinned, 200, std::move(forest.layout.root), MakeLeaf({ floated }));
            Settle();
            ASSERT_TRUE(app.FloatPanel(forest.layout, floated, { 100, 100 }));
            Frame();
            set.active = 1;
            Frame();
            ASSERT_EQ(app.m_floatingDockWindows.size(), 1u);

            FloatingDockWindow& window = *app.m_floatingDockWindows[0];
            ASSERT_TRUE(window.layout.onDock);
            window.layout.onDock();
            Frame();
            EXPECT_EQ(set.active, 0) << "its document is shown";
            Frame();
            Frame();
            EXPECT_TRUE(app.m_floatingDockWindows.empty()) << "and the panel docked back into it";
            EXPECT_TRUE(forest.layout.panels.contains(floated));
        }
    }
}
