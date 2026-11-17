// Drags inside the application: a source starts a drag of a typed payload, the elements under
// the pointer say whether they take it, and the one that takes it gets the drop.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_controls/gui_default_handlers.h>
#include <gui_core/input/gui_input_manager.h>

#include <cstdint>
#include <vector>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        // Stacked top to bottom: a picture to drag, a box that takes pictures (with a part of
        // its own), one that takes files only, and one that takes nothing.
        class InAppDragTest : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                sourceID = app.idManager.CreateStaticID();
                takerID  = app.idManager.CreateStaticID();
                childID  = app.idManager.CreateStaticID();
                filesID  = app.idManager.CreateStaticID();
                plainID  = app.idManager.CreateStaticID();
                pixels.assign(2 * 2 * 4, 0x80);
            }
            void TearDown() override { app.Cleanup(); }

            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow({ 1920, 1080 }, ElementFlags::None, app.theme.emptyVertStyleRef);
                    {
                        const DraggedImage picture{ ImageIndex(7), uint2{ 2, 2 }, pixels };
                        Element source{ .id = sourceID, .styleRef = app.theme.emptyVertStyleRef };
                        source.eventHandler = MakeDragSourceHandler<DraggedImage>(app.guiStateManager, DragPayload::Image,
                            [picture] { return picture; }, app.ImageDragGhost(picture.image, picture.size));
                        app.RegisterElement(std::move(source), LayoutOverride{}.Size(100, 100));

                        Element taker{ .id = takerID, .styleRef = app.theme.emptyVertStyleRef };
                        EventHandler takesImages = MakeDropTargetHandler<DraggedImage>(DragPayload::Image,
                            [this](const DraggedImage& image, ID from) { dropped.push_back(image); droppedFrom = from; });
                        taker.eventHandler = [takesImages](ElementEvent&& event)
                        {
                            // The same control takes files from other applications.
                            if (event.type == EventType::FileDragOver)
                                return true;
                            return takesImages(std::move(event));
                        };
                        app.RegisterElement(std::move(taker), LayoutOverride{}.Size(200, 200));
                        VERIFY(app.elementStorage.BeginChildren());
                        app.RegisterElement(Element{ .id = childID, .styleRef = app.theme.emptyVertStyleRef }, LayoutOverride{}.Size(50, 50));
                        VERIFY(app.elementStorage.EndChildren());

                        Element files{ .id = filesID, .styleRef = app.theme.emptyVertStyleRef };
                        files.eventHandler = MakeDropTargetHandler<DraggedFiles>(DragPayload::Files,
                            [this](const DraggedFiles&, ID) { ++filesDropped; });
                        app.RegisterElement(std::move(files), LayoutOverride{}.Size(200, 200));

                        app.RegisterElement(Element{ .id = plainID, .styleRef = app.theme.emptyVertStyleRef }, LayoutOverride{}.Size(200, 100));
                    }
                    app.EndRootWindow();
                });
            }

            void Press(int2 at)  { app.InjectMouseMove(at); app.InjectMouseDown(at); Frame(); }
            void MoveTo(int2 at) { app.InjectMouseMove(at, MouseButtons::Left); Frame(); }
            void Release(int2 at) { app.InjectMouseUp(at); Frame(); }

            // Presses the picture and drags it to `at`.
            void DragTo(int2 at)
            {
                Press(app.CenterOf(sourceID));
                MoveTo(at);
            }

            bool Dragging() const { return app.guiStateManager.Drag().IsDragging(); }
            bool GhostShown() const { return app.HasElement(app.DragGhostWindowID()); }

            HeadlessGUIApp app;
            ID sourceID, takerID, childID, filesID, plainID;
            std::vector<uint8_t> pixels;
            std::vector<DraggedImage> dropped;
            ID droppedFrom;
            int filesDropped = 0;
        };

        TEST_F(InAppDragTest, APressBecomesADragPastTheStartDistance)
        {
            Frame();
            Frame();
            const int2 source = app.CenterOf(sourceID);
            Press(source);
            EXPECT_FALSE(Dragging()) << "a press alone";
            MoveTo(source + int2{ 1, 0 });
            EXPECT_FALSE(Dragging()) << "under the start distance";
            EXPECT_FALSE(GhostShown());

            MoveTo(app.CenterOf(plainID));
            EXPECT_TRUE(Dragging());
            EXPECT_EQ(app.guiStateManager.Drag().GetDraggedID(), sourceID);
            Frame();
            EXPECT_TRUE(GhostShown()) << "a ghost of the picture by the pointer";
            EXPECT_EQ(app.GetElementRect(app.DragGhostWindowID()).x, app.CenterOf(plainID).x + app.CursorGhostOffset().x);
        }

        TEST_F(InAppDragTest, OnlyAnElementThatTakesThePayloadIsTheDropTarget)
        {
            Frame();
            Frame();
            EXPECT_EQ(app.DropTargetID(), ID::Invalid());
            DragTo(app.CenterOf(takerID));
            ASSERT_TRUE(Dragging());
            EXPECT_EQ(app.DropTargetID(), takerID);
            MoveTo(app.CenterOf(childID));
            EXPECT_EQ(app.DropTargetID(), takerID) << "over a part of it, which doesn't take it itself";
            MoveTo(app.CenterOf(filesID));
            EXPECT_EQ(app.DropTargetID(), ID::Invalid()) << "takes files, not pictures";
            MoveTo(app.CenterOf(plainID));
            EXPECT_EQ(app.DropTargetID(), ID::Invalid()) << "takes nothing";
            MoveTo(app.CenterOf(takerID));
            EXPECT_EQ(app.DropTargetID(), takerID) << "back over it";
        }

        TEST_F(InAppDragTest, TheTargetThatTakesItGetsTheDrop)
        {
            Frame();
            Frame();
            DragTo(app.CenterOf(filesID));
            Release(app.CenterOf(filesID));
            EXPECT_FALSE(Dragging());
            EXPECT_EQ(filesDropped, 0) << "not a payload it takes";
            EXPECT_TRUE(dropped.empty());

            DragTo(app.CenterOf(childID));
            Release(app.CenterOf(childID));
            EXPECT_FALSE(Dragging());
            EXPECT_EQ(app.DropTargetID(), ID::Invalid());
            ASSERT_EQ(dropped.size(), 1u) << "up from the part it's dropped on";
            EXPECT_EQ(dropped[0].image, ImageIndex(7));
            EXPECT_EQ(dropped[0].size, (uint2{ 2, 2 }));
            EXPECT_EQ(dropped[0].rgba.data(), pixels.data()) << "the source's pixels, not a copy";
            EXPECT_EQ(droppedFrom, sourceID);
            Frame();
            EXPECT_FALSE(GhostShown());
        }

        TEST_F(InAppDragTest, EscapeCancelsTheDrag)
        {
            Frame();
            Frame();
            DragTo(app.CenterOf(takerID));
            Frame();
            ASSERT_EQ(app.DropTargetID(), takerID);
            ASSERT_TRUE(GhostShown());

            app.InjectKey(KeyCode::Escape);
            Frame();
            EXPECT_FALSE(Dragging());
            EXPECT_EQ(app.DropTargetID(), ID::Invalid());
            Frame();
            EXPECT_FALSE(GhostShown());

            MoveTo(app.CenterOf(childID));
            EXPECT_EQ(app.DropTargetID(), ID::Invalid()) << "the button still held: no new drag";
            Release(app.CenterOf(childID));
            EXPECT_TRUE(dropped.empty());
        }

        TEST_F(InAppDragTest, OneControlTakesADragFromEitherPlace)
        {
            Frame();
            Frame();
            const int2 taker = app.CenterOf(takerID);
            app.SetMousePos(taker);
            app.inputManager->OnFileDrop(app.RootWindowID(), {}, FileDropAction::Begin, "");
            app.inputManager->OnFileDrop(app.RootWindowID(), taker, FileDropAction::Move, "");
            Frame();
            EXPECT_EQ(app.FileDragTargetID(), takerID);
            EXPECT_EQ(app.DropTargetID(), takerID) << "files from another application";
            app.inputManager->OnFileDrop(app.RootWindowID(), {}, FileDropAction::Complete, "");
            Frame();
            EXPECT_EQ(app.DropTargetID(), ID::Invalid());

            DragTo(taker);
            EXPECT_EQ(app.FileDragTargetID(), ID::Invalid());
            EXPECT_EQ(app.DropTargetID(), takerID) << "a picture from this one";
        }

        // A target in a cached scope (a docked panel's content) draws itself as the drop target
        // in a build: the scope is built again when the drag comes and when it goes, though the
        // pointer is no longer over it then.
        TEST(InAppDrag, ATargetInACachedScopeIsBuiltAgainWhenTheDragLeaves)
        {
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            const ID sourceID = app.idManager.CreateStaticID(), scopeID = app.idManager.CreateStaticID(),
                     paneID   = app.idManager.CreateStaticID();
            app.SetDrawCacheEnabled(true);
            bool builtAsTarget = false;
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow({ 1920, 1080 }, ElementFlags::None, app.theme.emptyVertStyleRef);
                    Element source{ .id = sourceID, .styleRef = app.theme.emptyVertStyleRef };
                    source.eventHandler = MakeDragSourceHandler<DraggedFiles>(app.guiStateManager, DragPayload::Files,
                        [] { return DraggedFiles{ { "a.png" } }; });
                    app.RegisterElement(std::move(source), LayoutOverride{}.Size(100, 100));
                    if (app.BeginCached(scopeID, app.theme.emptyVertStyleRef, 1, LayoutOverride{}.Size(400, 300)))
                    {
                        builtAsTarget = app.DropTargetID() == paneID;
                        Element pane{ .id = paneID, .styleRef = app.theme.emptyVertStyleRef };
                        pane.eventHandler = MakeDropTargetHandler<DraggedFiles>(DragPayload::Files, [](const DraggedFiles&, ID) {});
                        app.RegisterElement(std::move(pane), LayoutOverride{}.Size(300, 200));
                    }
                    app.EndCached();
                    app.EndRootWindow();
                });
            };
            frame();
            const int2 source = app.CenterOf(sourceID), pane = app.CenterOf(paneID);
            ASSERT_NE(pane, (int2{ 0, 0 })) << "test setup: built the first frame";
            const int2 away{ 1800, 1000 };
            app.InjectMouseMove(source);
            app.InjectMouseDown(source);
            frame();
            app.InjectMouseMove(pane, MouseButtons::Left);
            frame();
            frame();
            ASSERT_EQ(app.DropTargetID(), paneID);
            frame();   // a build reads the target the frame before it set
            EXPECT_TRUE(builtAsTarget) << "built as the target";

            app.InjectMouseMove(away, MouseButtons::Left);
            frame();
            frame();
            EXPECT_EQ(app.DropTargetID(), ID::Invalid());
            EXPECT_FALSE(builtAsTarget) << "built again, no longer the target";

            app.InjectMouseMove(pane, MouseButtons::Left);
            frame();
            frame();
            frame();
            ASSERT_TRUE(builtAsTarget);
            app.InjectMouseMove(away, MouseButtons::Left);   // and leaves it the frame Escape ends the drag
            app.InjectKey(KeyCode::Escape);
            frame();
            frame();
            EXPECT_FALSE(builtAsTarget) << "built again after a cancel";
        }
    }
}
