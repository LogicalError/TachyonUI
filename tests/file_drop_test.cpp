// Drags from other applications: files and text dragged over a window are offered to the
// elements under them, a drop to the element that takes it or else the application's handler.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/input/gui_input_manager.h>

#include <optional>
#include <string>
#include <vector>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        // A window's ID, as the window backend would give.
        ID WindowID()
        {
            ID id;
            id.value.dynamicParts.mode      = IDMode::Static;
            id.value.dynamicParts.hashValue = 0x51;
            return id;
        }

        TEST(FileDropInput, ADropGathersItsFilesAtWhereItWas)
        {
            InputManager input;
            const ID window = WindowID();
            EXPECT_EQ(input.GetFileDrag(), nullptr);

            input.OnFileDrop(window, { 0, 0 }, FileDropAction::Begin, "");
            ASSERT_NE(input.GetFileDrag(), nullptr);
            EXPECT_FALSE(input.GetFileDrag()->hasPosition) << "not known before it moves";
            input.OnFileDrop(window, { 40, 50 }, FileDropAction::Move, "");
            input.OnFileDrop(window, { 60, 70 }, FileDropAction::Move, "");
            ASSERT_NE(input.GetFileDrag(), nullptr);
            EXPECT_EQ(input.GetFileDrag()->windowID, window);
            EXPECT_EQ(input.GetFileDrag()->screenPos, (int2{ 60, 70 }));
            EXPECT_TRUE(input.GetFileDrag()->hasPosition);
            EXPECT_TRUE(input.TakeFileDrops().empty()) << "nothing dropped yet";

            input.OnFileDrop(window, { 60, 70 }, FileDropAction::File, "C:/a.png");
            input.OnFileDrop(window, { 60, 70 }, FileDropAction::File, "C:/b c.obj");
            input.OnFileDrop(window, { 0, 0 }, FileDropAction::Complete, "");
            EXPECT_EQ(input.GetFileDrag(), nullptr) << "over";
            const std::vector<FileDrop> drops = input.TakeFileDrops();
            ASSERT_EQ(drops.size(), 1u);
            EXPECT_EQ(drops[0].windowID, window);
            EXPECT_EQ(drops[0].screenPos, (int2{ 60, 70 })) << "where it was last, not the end's position";
            EXPECT_EQ(drops[0].files, (std::vector<std::string>{ "C:/a.png", "C:/b c.obj" }));
            EXPECT_TRUE(drops[0].text.empty());
            EXPECT_TRUE(input.TakeFileDrops().empty()) << "taken once";
        }

        TEST(FileDropInput, ADragThatLeavesDropsNothing)
        {
            InputManager input;
            const ID window = WindowID();
            input.OnFileDrop(window, { 0, 0 }, FileDropAction::Begin, "");
            input.OnFileDrop(window, { 10, 10 }, FileDropAction::Move, "");
            input.OnFileDrop(window, { 10, 10 }, FileDropAction::Complete, "");
            EXPECT_EQ(input.GetFileDrag(), nullptr);
            EXPECT_TRUE(input.TakeFileDrops().empty());

            // A new drag starts clean: nothing left of one that was cut short.
            input.OnFileDrop(window, { 0, 0 }, FileDropAction::Begin, "");
            input.OnFileDrop(window, { 0, 0 }, FileDropAction::File, "C:/stale.txt");
            input.OnFileDrop(window, { 0, 0 }, FileDropAction::Begin, "");
            input.OnFileDrop(window, { 5, 6 }, FileDropAction::Move, "");
            input.OnFileDrop(window, { 5, 6 }, FileDropAction::File, "C:/new.txt");
            input.OnFileDrop(window, { 5, 6 }, FileDropAction::Complete, "");
            const std::vector<FileDrop> drops = input.TakeFileDrops();
            ASSERT_EQ(drops.size(), 1u);
            EXPECT_EQ(drops[0].files, (std::vector<std::string>{ "C:/new.txt" }));
        }

        TEST(FileDropInput, TextIsJoinedByLines)
        {
            InputManager input;
            const ID window = WindowID();
            input.OnFileDrop(window, { 0, 0 }, FileDropAction::Begin, "");
            input.OnFileDrop(window, { 0, 0 }, FileDropAction::Text, "first");
            input.OnFileDrop(window, { 0, 0 }, FileDropAction::Text, "second");
            // Never moved: where it ended.
            input.OnFileDrop(window, { 7, 8 }, FileDropAction::Complete, "");
            const std::vector<FileDrop> drops = input.TakeFileDrops();
            ASSERT_EQ(drops.size(), 1u);
            EXPECT_EQ(drops[0].text, "first\nsecond");
            EXPECT_TRUE(drops[0].files.empty());
            EXPECT_EQ(drops[0].screenPos, (int2{ 7, 8 }));
        }

        // A box that takes files dropped on it, with a plain child, beside one that doesn't.
        class FileDropTest : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                takerID = app.idManager.CreateStaticID();
                childID = app.idManager.CreateStaticID();
                otherID = app.idManager.CreateStaticID();
                disabledID = app.idManager.CreateStaticID();
                cornerID = app.idManager.CreateStaticID();
                app.SetFileDropHandler([this](const FileDrop& drop) { appDrops.push_back(drop); });
            }
            void TearDown() override { app.Cleanup(); }

            EventHandler Taker(bool takes)
            {
                return [this, takes](ElementEvent&& event) -> bool
                {
                    if (event.type == EventType::FileDragOver)
                        return takes;
                    if (event.type == EventType::FileDrop)
                    {
                        seen.push_back(event.id);
                        files = event.state.droppedFiles;
                        text  = event.state.textInput;
                        return takes;
                    }
                    return false;
                };
            }

            void Frame()
            {
                app.Frame([&] {
                    // Unpadded, so its corner is an element's (a canvas filling a maximized window).
                    app.BeginRootWindow({ 1920, 1080 }, ElementFlags::None, app.theme.emptyVertStyleRef);
                    {
                        Element taker{ .id = takerID, .styleRef = app.theme.emptyVertStyleRef };
                        taker.eventHandler = Taker(true);
                        app.RegisterElement(std::move(taker), LayoutOverride{}.Size(200, 200));
                        VERIFY(app.elementStorage.BeginChildren());
                        app.RegisterElement(Element{ .id = childID, .styleRef = app.theme.emptyVertStyleRef }, LayoutOverride{}.Size(50, 50));
                        VERIFY(app.elementStorage.EndChildren());

                        Element other{ .id = otherID, .styleRef = app.theme.emptyVertStyleRef };
                        other.eventHandler = Taker(takesOther);
                        app.RegisterElement(std::move(other), LayoutOverride{}.Size(200, 200));

                        app.BeginDisabled();
                        Element disabled{ .id = disabledID, .styleRef = app.theme.emptyVertStyleRef };
                        disabled.eventHandler = Taker(true);
                        app.RegisterElement(std::move(disabled), LayoutOverride{}.Size(200, 200));
                        app.EndDisabled();

                        // At the window's corner: where a drag would be taken to be before it moves.
                        Element corner{ .id = cornerID, .styleRef = app.theme.emptyVertStyleRef };
                        corner.eventHandler = Taker(true);
                        app.RegisterElement(std::move(corner), LayoutOverride{}.Size(20, 20).Position(0, 0));
                    }
                    app.EndRootWindow();
                });
            }

            void DragTo(int2 pos)
            {
                app.SetMousePos(pos);
                app.inputManager->OnFileDrop(app.RootWindowID(), pos, FileDropAction::Move, "");
                Frame();
            }
            void Drop(int2 pos, std::vector<std::string> paths, std::string dropped = {})
            {
                app.SetMousePos(pos);
                app.inputManager->OnFileDrop(app.RootWindowID(), {}, FileDropAction::Begin, "");
                app.inputManager->OnFileDrop(app.RootWindowID(), pos, FileDropAction::Move, "");
                for (const std::string& path : paths)
                    app.inputManager->OnFileDrop(app.RootWindowID(), pos, FileDropAction::File, path);
                if (!dropped.empty())
                    app.inputManager->OnFileDrop(app.RootWindowID(), pos, FileDropAction::Text, dropped);
                app.inputManager->OnFileDrop(app.RootWindowID(), pos, FileDropAction::Complete, "");
                Frame();
            }

            HeadlessGUIApp app;
            ID takerID, childID, otherID, disabledID, cornerID;
            bool takesOther = false;
            std::vector<ID> seen;
            std::vector<std::string> files;
            std::string text;
            std::vector<FileDrop> appDrops;
        };

        TEST_F(FileDropTest, TheElementThatTakesTheDragIsItsTarget)
        {
            Frame();
            Frame();
            const int2 taker = app.CenterOf(takerID), child = app.CenterOf(childID), other = app.CenterOf(otherID);
            ASSERT_NE(taker, child) << "test setup: the child is a part of the box";
            EXPECT_FALSE(app.IsFileDragging());

            app.inputManager->OnFileDrop(app.RootWindowID(), {}, FileDropAction::Begin, "");
            Frame();
            EXPECT_TRUE(app.IsFileDragging());
            ASSERT_EQ(app.CenterOf(cornerID), (int2{ 10, 10 })) << "test setup: an element at the corner";
            EXPECT_EQ(app.FileDragTargetID(), ID::Invalid()) << "not known where it is yet";

            DragTo(int2{ 0, 0 });
            EXPECT_EQ(app.FileDragTargetID(), cornerID) << "the corner's, once it's there";
            DragTo(taker);
            EXPECT_EQ(app.FileDragTargetID(), takerID);
            DragTo(child);
            EXPECT_EQ(app.FileDragTargetID(), takerID) << "over a part of it, which doesn't take it itself";
            DragTo(other);
            EXPECT_EQ(app.FileDragTargetID(), ID::Invalid()) << "one that doesn't take it";
            takesOther = true;
            Frame();
            Frame();
            EXPECT_EQ(app.FileDragTargetID(), otherID) << "asked again each frame";
            DragTo(app.CenterOf(disabledID));
            EXPECT_EQ(app.FileDragTargetID(), ID::Invalid()) << "a disabled element takes nothing";

            DragTo(taker);
            ASSERT_EQ(app.FileDragTargetID(), takerID);
            app.inputManager->OnFileDrop(app.RootWindowID(), {}, FileDropAction::Complete, "");
            Frame();
            EXPECT_FALSE(app.IsFileDragging());
            EXPECT_EQ(app.FileDragTargetID(), ID::Invalid());
            EXPECT_TRUE(appDrops.empty()) << "it left: no drop";
            EXPECT_TRUE(seen.empty());
        }

        TEST_F(FileDropTest, ADropGoesToTheElementThatTakesItElseTheApplication)
        {
            Frame();
            Frame();
            Drop(app.CenterOf(childID), { "C:/a.png", "C:/b.png" });
            ASSERT_EQ(seen, (std::vector<ID>{ takerID })) << "up from the part it's dropped on";
            EXPECT_EQ(files, (std::vector<std::string>{ "C:/a.png", "C:/b.png" }));
            EXPECT_TRUE(appDrops.empty()) << "taken";
            EXPECT_FALSE(app.IsFileDragging());

            seen.clear();
            const int2 other = app.CenterOf(otherID);
            Drop(other, {}, "some text");
            EXPECT_EQ(seen, (std::vector<ID>{ otherID })) << "offered";
            EXPECT_EQ(text, "some text");
            ASSERT_EQ(appDrops.size(), 1u) << "not taken: the application's";
            EXPECT_EQ(appDrops[0].text, "some text");
            EXPECT_EQ(appDrops[0].screenPos, other);
            EXPECT_EQ(appDrops[0].windowID, app.RootWindowID());

            seen.clear();
            Drop(app.CenterOf(disabledID), { "C:/c.png" });
            EXPECT_TRUE(seen.empty()) << "not to a disabled element";
            ASSERT_EQ(appDrops.size(), 2u);
            EXPECT_EQ(appDrops[1].files, (std::vector<std::string>{ "C:/c.png" }));

            Drop(int2{ 5000, 5000 }, { "C:/d.png" });
            ASSERT_EQ(appDrops.size(), 3u) << "outside every element";
        }

        // While another application drags, the OS sends the window no mouse moves and gives it
        // no focus: hover knows neither the window under the pointer nor has the focus it needs.
        // The drag and the drop go by the window they came to and the pointer's position.
        TEST_F(FileDropTest, ADropNeedsNeitherHoverNorFocus)
        {
            Frame();
            Frame();
            ID elsewhere;
            elsewhere.value.dynamicParts.mode      = IDMode::Static;
            elsewhere.value.dynamicParts.hashValue = 0x77;
            app.activeWindow = elsewhere;   // another application has the focus
            app.mouseWindow  = elsewhere;   // and the OS reports no window of ours under the mouse
            const int2 taker = app.CenterOf(takerID);
            app.inputManager->OnFileDrop(app.RootWindowID(), {}, FileDropAction::Begin, "");
            DragTo(taker);
            EXPECT_EQ(app.FileDragTargetID(), takerID);
            EXPECT_EQ(app.guiStateManager.Interaction().GetHoveredID(), ID::Invalid()) << "test setup: no hover";
            app.inputManager->OnFileDrop(app.RootWindowID(), taker, FileDropAction::File, "C:/a.png");
            app.inputManager->OnFileDrop(app.RootWindowID(), taker, FileDropAction::Complete, "");
            Frame();
            EXPECT_EQ(seen, (std::vector<ID>{ takerID }));
            EXPECT_EQ(files, (std::vector<std::string>{ "C:/a.png" }));
            EXPECT_TRUE(appDrops.empty());
        }

        TEST_F(FileDropTest, WithoutAHandlerADropNoElementTakesIsLeft)
        {
            app.SetFileDropHandler({});
            Frame();
            Frame();
            Drop(app.CenterOf(otherID), { "C:/a.png" });
            EXPECT_EQ(seen, (std::vector<ID>{ otherID }));
            EXPECT_TRUE(appDrops.empty());
        }

        // A pane that takes drops, holding a tree view and a grid view (the showcase's Files
        // panel): a drop on a row, an item or the background is the pane's.
        TEST(FileDrop, AGridOrTreeInAPaneLetsItTakeTheDrop)
        {
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            const ID paneID = app.idManager.CreateStaticID(), gridID = app.idManager.CreateStaticID(),
                     treeID = app.idManager.CreateStaticID();
            std::vector<std::string> taken;
            std::vector<FileDrop> appDrops;
            app.SetFileDropHandler([&](const FileDrop& drop) { appDrops.push_back(drop); });
            GridViewVirtualState vstate;
            MultiSelectState multi;
            std::vector<ID> itemIDs(3);
            std::vector<TreeViewRow> rows(2);
            rows[0].name = "Root"; rows[0].isOpen = true;
            rows[1].name = "Child"; rows[1].depth = 1;
            GUIApplication::RebuildTreeViewIndex(rows);
            std::vector<int> visibleIdx;
            TableState treeState;
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    Element pane{ .id = paneID, .styleRef = app.theme.emptyVertStyleRef };
                    pane.eventHandler = [&](ElementEvent&& event) -> bool {
                        if (event.type == EventType::FileDragOver) return true;
                        if (event.type != EventType::FileDrop) return false;
                        taken = event.state.droppedFiles;
                        return true;
                    };
                    app.RegisterElement(std::move(pane), LayoutOverride{}.Size(600, 500));
                    VERIFY(app.elementStorage.BeginChildren());
                    app.VirtualTreeView(treeID, rows, visibleIdx, treeState, StyleRef{}, LayoutOverride{}.Size(200, 100));
                    app.VirtualGridView(gridID, vstate, 3, int2{ 100, 50 },
                                        [&](int i, ID itemID) {
                                            itemIDs[size_t(i)] = itemID;
                                            app.GridViewItem(itemID, "item", ElementState::None, nullptr, StyleRef{}, LayoutOverride{}.Size(100, 50));
                                        },
                                        nullptr, &multi, nullptr, {}, {}, LayoutOverride{}.Size(400, 300));
                    VERIFY(app.elementStorage.EndChildren());
                    app.EndRootWindow();
                });
            };
            frame();
            frame();
            auto drop = [&](int2 at) {
                taken.clear();
                app.SetMousePos(at);
                app.inputManager->OnFileDrop(app.RootWindowID(), {}, FileDropAction::Begin, "");
                app.inputManager->OnFileDrop(app.RootWindowID(), at, FileDropAction::Move, "");
                frame();
                const ID target = app.FileDragTargetID();
                app.inputManager->OnFileDrop(app.RootWindowID(), at, FileDropAction::File, "C:/a.png");
                app.inputManager->OnFileDrop(app.RootWindowID(), at, FileDropAction::Complete, "");
                frame();
                return target;
            };
            const IntRect grid = app.GetElementRect(gridID);
            for (const auto& [where, at] : { std::pair{ "a grid item", app.CenterOf(itemIDs[1]) },
                                             std::pair{ "the grid's background", int2{ grid.x + grid.w - 10, grid.y + grid.h - 10 } },
                                             std::pair{ "a tree row", app.CenterOf(treeID) } })
            {
                EXPECT_EQ(drop(at), paneID) << where;
                EXPECT_EQ(taken, (std::vector<std::string>{ "C:/a.png" })) << where;
            }
            EXPECT_TRUE(appDrops.empty());
        }

        // A pane in a cached scope (a docked panel's content) takes the drag and the drop even on
        // frames it's drawn from its capture, when nothing in it is built: the OS sends no mouse
        // moves while another application drags, which would have it built again.
        TEST(FileDrop, APaneInACachedScopeTakesTheDragAndTheDrop)
        {
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            const ID scopeID = app.idManager.CreateStaticID(), paneID = app.idManager.CreateStaticID();
            std::vector<std::string> taken;
            std::vector<FileDrop> appDrops;
            app.SetFileDropHandler([&](const FileDrop& drop) { appDrops.push_back(drop); });
            app.SetDrawCacheEnabled(true);
            app.InjectMouseMove({ 1900, 1000 });   // away from it, or it's built for the pointer
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    if (app.BeginCached(scopeID, app.theme.emptyVertStyleRef, 1, LayoutOverride{}.Size(400, 300)))
                    {
                        Element pane{ .id = paneID, .styleRef = app.theme.emptyVertStyleRef };
                        pane.eventHandler = [&](ElementEvent&& event) -> bool {
                            if (event.type == EventType::FileDragOver) return true;
                            if (event.type != EventType::FileDrop) return false;
                            taken = event.state.droppedFiles;
                            return true;
                        };
                        app.RegisterElement(std::move(pane), LayoutOverride{}.Size(300, 200));
                    }
                    app.EndCached();
                    app.EndRootWindow();
                });
            };
            frame();
            const int2 at = app.CenterOf(paneID);
            for (int i = 0; i < 8 && app.elementStorage.FindElementRefByID(paneID).IsValid(); ++i)
                frame();
            ASSERT_FALSE(app.elementStorage.FindElementRefByID(paneID).IsValid()) << "test setup: drawn from its capture";

            // As while another application drags: the OS reports no window of ours under the
            // pointer, so hover doesn't have the scope built for it.
            ID elsewhere;
            elsewhere.value.dynamicParts.mode      = IDMode::Static;
            elsewhere.value.dynamicParts.hashValue = 0x77;
            app.mouseWindow = elsewhere;
            app.SetMousePos(at);
            app.inputManager->OnFileDrop(app.RootWindowID(), {}, FileDropAction::Begin, "");
            app.inputManager->OnFileDrop(app.RootWindowID(), at, FileDropAction::Move, "");
            frame();
            frame();
            EXPECT_EQ(app.FileDragTargetID(), paneID) << "the drag";
            app.inputManager->OnFileDrop(app.RootWindowID(), {}, FileDropAction::Complete, "");
            frame();

            // Dropped at once, with no drag over it first, while it's drawn from its capture.
            for (int i = 0; i < 8 && app.elementStorage.FindElementRefByID(paneID).IsValid(); ++i)
                frame();
            ASSERT_FALSE(app.elementStorage.FindElementRefByID(paneID).IsValid()) << "test setup: drawn from its capture";
            app.inputManager->OnFileDrop(app.RootWindowID(), {}, FileDropAction::Begin, "");
            app.inputManager->OnFileDrop(app.RootWindowID(), at, FileDropAction::Move, "");
            app.inputManager->OnFileDrop(app.RootWindowID(), at, FileDropAction::File, "C:/a.png");
            app.inputManager->OnFileDrop(app.RootWindowID(), at, FileDropAction::Complete, "");
            frame();
            frame();
            EXPECT_EQ(taken, (std::vector<std::string>{ "C:/a.png" })) << "the drop";
            EXPECT_TRUE(appDrops.empty());

            // Taken once: not again next frame.
            taken.clear();
            frame();
            EXPECT_TRUE(taken.empty());

            // Built (the pointer over it): a drop on it, past the pane, is the application's at once.
            app.mouseWindow = app.RootWindowID();
            const IntRect scope = app.GetElementRect(scopeID);
            const int2 past{ scope.x + scope.w - 10, scope.y + scope.h - 10 };
            app.InjectMouseMove(past);
            frame();
            frame();
            ASSERT_TRUE(app.elementStorage.FindElementRefByID(paneID).IsValid()) << "test setup: built";
            app.SetMousePos(past);
            app.inputManager->OnFileDrop(app.RootWindowID(), {}, FileDropAction::Begin, "");
            app.inputManager->OnFileDrop(app.RootWindowID(), past, FileDropAction::File, "C:/b.png");
            app.inputManager->OnFileDrop(app.RootWindowID(), past, FileDropAction::Complete, "");
            frame();
            ASSERT_EQ(appDrops.size(), 1u) << "not held a frame";
            EXPECT_EQ(appDrops[0].files, (std::vector<std::string>{ "C:/b.png" }));
        }

        // The controls take no drags or drops from other applications by themselves: every
        // one is the application's.
        TEST(FileDrop, NoControlTakesADropOfItsOwn)
        {
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            std::vector<FileDrop> drops;
            app.SetFileDropHandler([&](const FileDrop& drop) { drops.push_back(drop); });

            bool toggle = false, checkbox = false, switchOn = false, toolbarToggle = false;
            std::string text = "text";
            float number = 1.0f, volume = 50.0f;
            int count = 3, level = 4, typed = 7, listSelection = 0, quality = 0;
            color4f color { 1.0f, 0.5f, 0.0f, 1.0f };
            Gradient gradient;
            std::vector<std::string> list { "one", "two" }, crumbs { "Graphs", "Terrain" };
            TableState tableState;
            std::vector<VirtualTableColumn> tableColumns;
            tableColumns.push_back({ .column = { .header = "Name", .initialWidth = 120, .minWidth = 60, .maxWidth = 300 },
                                     .generateCell = [&app](int r) { app.Cell(r == 0 ? "First" : "Second"); } });
            std::vector<ID> ids;
            for (int i = 0; i < 20; ++i)
                ids.push_back(app.idManager.CreateStaticID());
            auto frame = [&] {
                app.Frame([&] {
                    int n = 0;
                    app.BeginRootWindow();
                    app.Button(ids[n++], "Button", [] {});
                    app.Toggle(ids[n++], toggle, "Toggle");
                    app.Checkbox(ids[n++], checkbox, "Checkbox", [&](bool v) { checkbox = v; });
                    app.Switch(ids[n++], switchOn, "Switch");
                    app.Field(ids[n++], text);
                    app.Field(ids[n++], number);
                    app.SliderField(ids[n++], volume, 0.0f, 100.0f, "Volume");
                    app.SliderField(ids[n++], count, 0, 10, "Count");
                    app.Slider(ids[n++], level, 0, 10);
                    app.ColorField(ids[n++], color);
                    app.GradientField(ids[n++], gradient);
                    app.Field(ids[n++], list);
                    app.BeginToolbar();
                    app.Button(ids[n++], "Tool", [] {});
                    app.Toggle(ids[n++], toolbarToggle, "Snap");
                    app.BeginMenu(ids[n++], "View");
                    app.EndMenu();
                    app.EndToolbar();
                    app.Breadcrumb(ids[n++], crumbs, [](int) {});
                    app.TypedField(ids[n++], typed);
                    if (app.BeginListView(ids[n++], &listSelection, 2))
                    {
                        app.ListItem("First");
                        app.ListItem("Second");
                    }
                    app.EndListView();
                    app.VirtualTable(ids[n++], tableState, std::span<VirtualTableColumn>(tableColumns), 2, 0,
                                     app.theme.tableStyleRef, LayoutOverride{}.Size(200, 80));
                    app.Dropdown(ids[n++], quality, { "Low", "High" });
                    app.EndRootWindow();
                });
            };
            frame();
            frame();

            std::vector<std::pair<ID, int2>> centers;
            const auto& storage = app.elementStorage;
            for (auto it = storage.TopDownDepthScanBegin(); it != storage.TopDownDepthScanEnd(); ++it)
                for (const Element& e : it->GetChildren(storage))
                    centers.emplace_back(e.id, app.CenterOf(e.id));
            ASSERT_GT(centers.size(), 20u) << "test setup: the controls are there";

            for (const auto& [id, center] : centers)
            {
                app.SetMousePos(center);
                app.inputManager->OnFileDrop(app.RootWindowID(), {}, FileDropAction::Begin, "");
                app.inputManager->OnFileDrop(app.RootWindowID(), center, FileDropAction::Move, "");
                frame();
                EXPECT_EQ(app.FileDragTargetID(), ID::Invalid()) << "an element takes a drag by itself: " << app.TextOf(id);
                app.inputManager->OnFileDrop(app.RootWindowID(), center, FileDropAction::File, "C:/a.png");
                app.inputManager->OnFileDrop(app.RootWindowID(), center, FileDropAction::Complete, "");
                const size_t before = drops.size();
                frame();
                EXPECT_EQ(drops.size(), before + 1) << "an element takes a drop by itself: " << app.TextOf(id);
            }
        }
    }
}
