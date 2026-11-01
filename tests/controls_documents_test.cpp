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

            struct PanelLog { int draws = 0, enables = 0, disables = 0; };
            std::vector<std::unique_ptr<PanelLog>> logs;

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                app.runPostLayoutCallbacks = true;   // the tab bar measures what fits
                documentsID = app.idManager.CreateStaticID();
            }

            void TearDown() override { app.Cleanup(); }

            // A document with one panel, whose draws and visibility are logged.
            Document& Add(std::string name, std::string extension, std::string kind)
            {
                Document& document = app.CreateDocument(set, name, std::move(extension), std::move(kind), IconID::Invalid);
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
                    app.Documents(documentsID, set);
                    app.EndRootWindow();
                    app.EmitFloatingDockWindows();
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
