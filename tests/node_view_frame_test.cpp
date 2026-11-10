// Frames in the node view: titled boxes behind a group of nodes. Dragging the title moves the
// frame and what's inside it; the right and bottom edges resize it; a double-click on the
// title renames it; a right click opens the app's menu for it. All of it as edits, for undo.

#include <gtest/gtest.h>

#include "headless_gui_app.h"
#include "skribidi_test_fixture.h"

#include <gui_core/gui_id.h>
#include <gui_core/graphics/gui_custom_draw_context.h>
#include <gui_controls/gui_node_view_helpers.h>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        class NodeViewFrameTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            NodeView       view;
            int32_t        g = 0;   // the grid
            ID             nodeA{}, nodeB{}, frameOuter{}, frameInner{};

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                g = app.theme.nodeGridSize;
                ASSERT_GT(g, 1);
                nodeA = app.idManager.CreateStaticID();
                nodeB = app.idManager.CreateStaticID();
                frameOuter = app.idManager.CreateStaticID();
                frameInner = app.idManager.CreateStaticID();

                NodeModel a{};
                a.stableID = nodeA;
                a.title    = "A";
                a.pos      = { 8 * g, 7 * g };
                a.properties.push_back(NodeProperty{ .output = true });
                NodeModel b{};
                b.stableID = nodeB;
                b.title    = "B";
                b.pos      = { 60 * g, 30 * g };
                b.properties.push_back(NodeProperty{ .input = PropertyEndpoint{} });

                view = {};
                view.transform = float3x2::Scale(1.0f);
                view.nodes  = { std::move(a), std::move(b) };
                // The outer frame holds A and the inner frame; B is out of both.
                view.frames = {
                    NodeFrame{ .stableID = frameOuter, .title = "Lighting", .pos = { 4 * g, 2 * g }, .size = { 40 * g, 22 * g } },
                    NodeFrame{ .stableID = frameInner, .title = "Notes", .color = { 0.3f, 0.8f, 0.7f, 1.0f },
                               .pos = { 26 * g, 12 * g }, .size = { 12 * g, 8 * g } },
                };
            }
            void TearDown() override { app.Cleanup(); }

            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow({ 1920, 1080 }, ElementFlags::None, app.theme.mainWindowStyleRef);
                    app.RenderNodeView(view);
                    app.EndRootWindow();
                });
            }
            void Settle() { Frame(); Frame(); }

            // The elements in `style`, in layout order.
            std::vector<ID> WithStyle(StyleRef style) const
            {
                std::vector<ID> out;
                for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels(); d++)
                    for (uint32_t i = 0; i < app.elementStorage.DepthSizeOf(d); i++)
                    {
                        const ElementRef ref{ d, i };
                        if (app.elementStorage.IsValidRef(ref) && app.elementStorage.GetElementUnsafe(ref).styleRef == style)
                            out.push_back(app.elementStorage.GetElementUnsafe(ref).id);
                    }
                return out;
            }

            // A frame's title element, by its title.
            ID TitleOf(std::string_view title) const
            {
                for (const ID id : WithStyle(app.theme.nodeFrameTitleStyleRef))
                    if (app.TextOf(id) == title) return id;
                return ID::Invalid();
            }
            int2 Middle(ID id) const { return app.CenterOf(id); }

            IntRect CanvasInner() const
            {
                for (const ID id : WithStyle(app.theme.nodeViewStyleRef))
                    if (const auto* rp = app.elementStorage.GetElementRectPtr(app.elementStorage.FindElementRefByID(id)))
                        return rp->innerRect;
                return {};
            }
            // A point on the canvas well clear of the nodes and frames.
            int2 Empty() const
            {
                const IntRect inner = CanvasInner();
                return { inner.x + inner.w * 7 / 8, inner.y + inner.h / 8 };
            }

            // A press that is never the second of a double-click. Two presses on the same item
            // within the double-click time are one, and the tracker goes by the real clock,
            // which a test runs through well within that time.
            void Single()
            {
                view.frameDoubleClick = {};
                view.nodeDoubleClick  = {};
            }

            void Drag(int2 from, int2 to)
            {
                Single();
                app.InjectMouseMove(from);
                app.InjectMouseDown(from);
                Frame();
                app.InjectMouseMove({ (from.x + to.x) / 2, (from.y + to.y) / 2 }, MouseButtons::Left);
                Frame();
                app.InjectMouseMove(to, MouseButtons::Left);
                Frame();
                app.InjectMouseUp(to);
                Frame();
            }
            void Click(int2 at)
            {
                Single();
                app.ClickAt(at);
                Frame();
                Frame();
            }
            void Key(KeyCode key, KeyModifier mods = KeyModifier::None)
            {
                app.InjectKey(key, KeyAction::KeyDown, mods);
                Frame();
                app.InjectKey(key, KeyAction::KeyUp, mods);
                Frame();
            }
        };

        // The title bar is a row pitch high across the frame's top, where the frame is; the
        // rest of the box takes no input, so a press there is the canvas's.
        TEST_F(NodeViewFrameTest, TheTitleBarLiesAcrossTheTop)
        {
            Settle();
            const ID title = TitleOf("Lighting");
            ASSERT_NE(title, ID::Invalid());
            const IntRect t = app.GetElementRect(title), a = app.GetElementRect(nodeA);
            EXPECT_EQ(t.w, 40 * g);
            EXPECT_EQ(t.h, 2 * g);
            EXPECT_EQ(t.x - a.x, (4 - 8) * g) << "where the frame is, in node.pos space";
            EXPECT_EQ(t.y - a.y, (2 - 7) * g);
            EXPECT_NE(TitleOf("Notes"), ID::Invalid());

            // A press in the box below the title starts a marquee, as on the empty canvas.
            const int2 inBox{ t.x + 2 * g, t.y + 18 * g };
            app.InjectMouseMove(inBox);
            app.InjectMouseDown(inBox);
            Frame();
            EXPECT_TRUE(view.marquee.IsHeld());
            app.InjectMouseUp(inBox);
            Frame();
        }

        // Selected as a node is: a click selects it alone, Shift adds, Ctrl toggles; a click
        // on the background, Escape or a click on a node takes it off; Ctrl+A takes them all.
        TEST_F(NodeViewFrameTest, AFrameIsSelectedAsANodeIs)
        {
            Settle();
            const uint32_t first = view.selectionGeneration;
            Click(Middle(TitleOf("Notes")));
            EXPECT_GT(view.selectionGeneration, first) << "a frame alone is a selection change too";
            Key(KeyCode::Escape);

            const uint32_t before = view.selectionGeneration;
            view.selectedNodeIDs = { nodeB };
            Click(Middle(TitleOf("Lighting")));
            EXPECT_EQ(view.selectedFrameIDs, std::unordered_set<ID>{ frameOuter });
            EXPECT_TRUE(view.selectedNodeIDs.empty()) << "alone";
            EXPECT_GT(view.selectionGeneration, before) << "reported as a selection change";

            Single();
            app.ClickAt(Middle(TitleOf("Notes")), MouseButtons::Left, KeyModifier::Shift);
            Frame();
            EXPECT_EQ(view.selectedFrameIDs, (std::unordered_set<ID>{ frameOuter, frameInner }));
            Single();
            app.ClickAt(Middle(TitleOf("Notes")), MouseButtons::Left, KeyModifier::Control);
            Frame();
            EXPECT_EQ(view.selectedFrameIDs, std::unordered_set<ID>{ frameOuter });

            Click(Empty());
            EXPECT_TRUE(view.selectedFrameIDs.empty());

            Click(Middle(TitleOf("Lighting")));
            Key(KeyCode::Escape);
            EXPECT_TRUE(view.selectedFrameIDs.empty());

            Click(Middle(TitleOf("Lighting")));
            const IntRect b = app.GetElementRect(nodeB);
            Click({ b.x + b.w / 2, b.y + g });
            EXPECT_TRUE(view.selectedFrameIDs.empty());
            EXPECT_EQ(view.selectedNodeIDs, std::unordered_set<ID>{ nodeB });

            Key(KeyCode::KeyA, KeyModifier::Control);
            EXPECT_EQ(view.selectedFrameIDs.size(), 2u);
            EXPECT_EQ(view.selectedNodeIDs.size(), 2u);
        }

        // Dragging the title moves the frame, the frames inside it and the nodes inside them,
        // all by one grid-snapped step, as one edit; undone, all of it goes back.
        TEST_F(NodeViewFrameTest, DraggingTheTitleMovesTheFrameAndWhatsInside)
        {
            Settle();
            std::vector<NodeViewEdit> edits;
            view.onEdit = [&](const NodeViewEdit& edit) { edits.push_back(edit); };
            const NodeFrame outer = view.frames[0], inner = view.frames[1];
            const int2 a = view.nodes[0].pos, b = view.nodes[1].pos;
            const int2 from = Middle(TitleOf("Lighting"));
            Drag(from, { from.x + 5 * g + 2, from.y + 3 * g - 1 });
            Settle();
            const int2 step{ 5 * g, 3 * g };
            EXPECT_EQ(view.frames[0].pos, (int2{ outer.pos.x + step.x, outer.pos.y + step.y }));
            EXPECT_EQ(view.frames[0].size, outer.size) << "moved, not resized";
            EXPECT_EQ(view.frames[1].pos, (int2{ inner.pos.x + step.x, inner.pos.y + step.y })) << "the frame inside it too";
            EXPECT_EQ(view.nodes[0].pos, (int2{ a.x + step.x, a.y + step.y })) << "the node inside it too";
            EXPECT_EQ(view.nodes[1].pos, b) << "not the node outside it";

            ASSERT_EQ(edits.size(), 1u);
            EXPECT_EQ(edits[0].frameChanges.size(), 2u);
            ASSERT_EQ(edits[0].moves.size(), 1u);
            EXPECT_EQ(edits[0].moves[0].node, 0);

            ApplyNodeViewEdit(view, edits[0], /*undo=*/true);
            EXPECT_EQ(view.frames[0], outer);
            EXPECT_EQ(view.frames[1], inner);
            EXPECT_EQ(view.nodes[0].pos, a);
            ApplyNodeViewEdit(view, edits[0]);
            EXPECT_EQ(view.nodes[0].pos, (int2{ a.x + step.x, a.y + step.y }));
        }

        // Moving the inner frame takes only what's inside it; a click doesn't move anything.
        TEST_F(NodeViewFrameTest, AFrameInsideAnotherMovesAlone)
        {
            Settle();
            int edits = 0;
            view.onEdit = [&](const NodeViewEdit&) { ++edits; };
            const NodeFrame outer = view.frames[0];
            const int2 a = view.nodes[0].pos;
            Click(Middle(TitleOf("Notes")));
            EXPECT_EQ(edits, 0) << "a click moves nothing";
            const int2 from = Middle(TitleOf("Notes"));
            Drag(from, { from.x - 2 * g, from.y + 2 * g });
            Settle();
            EXPECT_EQ(view.frames[1].pos, (int2{ 24 * g, 14 * g }));
            EXPECT_EQ(view.frames[0], outer);
            EXPECT_EQ(view.nodes[0].pos, a);
            EXPECT_EQ(edits, 1);
        }

        // Without snapping, the frame and its nodes move exactly as far as the pointer.
        TEST_F(NodeViewFrameTest, WithoutSnappingAFrameMovesAsFarAsThePointer)
        {
            view.snapToGrid = false;
            Settle();
            const int2 outer = view.frames[0].pos, a = view.nodes[0].pos;
            const int2 from = Middle(TitleOf("Lighting"));
            Drag(from, { from.x + 37, from.y + 11 });
            Settle();
            EXPECT_EQ(view.frames[0].pos, (int2{ outer.x + 37, outer.y + 11 }));
            EXPECT_EQ(view.nodes[0].pos, (int2{ a.x + 37, a.y + 11 }));
        }

        // A drag let go in the frame of its last move: the edit holds the place and size on
        // the grid, as they are drawn, not where the pointer let go.
        TEST_F(NodeViewFrameTest, ADragLetGoAtOnceEndsOnTheGrid)
        {
            Settle();
            std::vector<NodeViewEdit> edits;
            view.onEdit = [&](const NodeViewEdit& edit) { edits.push_back(edit); };
            auto dragAtOnce = [&](int2 from, int2 to) {
                Single();
                app.InjectMouseMove(from);
                app.InjectMouseDown(from);
                Frame();
                app.InjectMouseMove(to, MouseButtons::Left);
                app.InjectMouseUp(to);
                Frame();
                Settle();
            };
            const NodeFrame outer = view.frames[0];
            const int2 a = view.nodes[0].pos;
            const int2 from = Middle(TitleOf("Lighting"));
            dragAtOnce(from, { from.x + 5 * g + 2, from.y + 3 * g - 1 });
            ASSERT_EQ(edits.size(), 1u);
            ASSERT_FALSE(edits[0].frameChanges.empty());
            EXPECT_EQ(edits[0].frameChanges[0].to.pos, (int2{ outer.pos.x + 5 * g, outer.pos.y + 3 * g }));
            ASSERT_EQ(edits[0].moves.size(), 1u);
            EXPECT_EQ(edits[0].moves[0].to, (int2{ a.x + 5 * g, a.y + 3 * g }));

            // The corner, a little past a cell, and far smaller than it may be.
            const IntRect t = app.GetElementRect(TitleOf("Lighting"));
            const int2 corner{ t.x + outer.size.x - 3, t.y + outer.size.y - 3 };
            dragAtOnce(corner, { corner.x + 2 * g + 3, corner.y + g - 2 });
            ASSERT_EQ(edits.size(), 2u);
            ASSERT_EQ(edits[1].frameChanges.size(), 1u);
            EXPECT_EQ(edits[1].frameChanges[0].to.size, (int2{ outer.size.x + 2 * g, outer.size.y + g }));
            const int2 corner2{ t.x + outer.size.x + 2 * g - 3, t.y + outer.size.y + g - 3 };
            dragAtOnce(corner2, { corner2.x - 100 * g, corner2.y - 100 * g });
            ASSERT_EQ(edits.size(), 3u);
            ASSERT_EQ(edits[2].frameChanges.size(), 1u);
            EXPECT_EQ(edits[2].frameChanges[0].to.size, (int2{ 6 * g, 4 * g }));
        }

        TEST_F(NodeViewFrameTest, EscapeDuringAMovePutsItBack)
        {
            Settle();
            int edits = 0;
            view.onEdit = [&](const NodeViewEdit&) { ++edits; };
            const NodeFrame outer = view.frames[0];
            const int2 a = view.nodes[0].pos;
            const int2 from = Middle(TitleOf("Lighting"));
            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            Frame();
            app.InjectMouseMove({ from.x + 6 * g, from.y + 4 * g }, MouseButtons::Left);
            Frame();
            ASSERT_NE(view.frames[0].pos, outer.pos);
            app.InjectKey(KeyCode::Escape, KeyAction::KeyDown);
            Frame();
            app.InjectMouseUp({ from.x + 6 * g, from.y + 4 * g });
            Settle();
            EXPECT_EQ(view.frames[0], outer);
            EXPECT_EQ(view.nodes[0].pos, a);
            EXPECT_EQ(edits, 0);
        }

        // The corner resizes both ways, the right edge only the width, the bottom only the
        // height; whole grid cells, never under a title bar over a row; the nodes stay.
        TEST_F(NodeViewFrameTest, TheEdgesResizeIt)
        {
            Settle();
            std::vector<NodeViewEdit> edits;
            view.onEdit = [&](const NodeViewEdit& edit) { edits.push_back(edit); };
            const NodeFrame outer = view.frames[0];
            const int2 a = view.nodes[0].pos;
            const IntRect t = app.GetElementRect(TitleOf("Lighting"));
            const int2 corner{ t.x + outer.size.x - 3, t.y + outer.size.y - 3 };
            Drag(corner, { corner.x + 4 * g + 3, corner.y + 2 * g - 2 });
            Settle();
            EXPECT_EQ(view.frames[0].size, (int2{ outer.size.x + 4 * g, outer.size.y + 2 * g }));
            EXPECT_EQ(view.frames[0].pos, outer.pos);
            EXPECT_EQ(view.nodes[0].pos, a) << "a resize moves nothing inside";
            ASSERT_EQ(edits.size(), 1u);
            ASSERT_EQ(edits[0].frameChanges.size(), 1u);
            EXPECT_TRUE(edits[0].moves.empty());
            EXPECT_EQ(edits[0].frameChanges[0].from, outer);

            const int2 size = view.frames[0].size;
            const int2 right{ t.x + size.x - 2, t.y + size.y / 2 };
            Drag(right, { right.x - 3 * g, right.y + 5 * g });
            Settle();
            EXPECT_EQ(view.frames[0].size, (int2{ size.x - 3 * g, size.y })) << "the right edge: the width";
            const int2 width = view.frames[0].size;
            const int2 bottom{ t.x + width.x / 2, t.y + width.y - 2 };
            Drag(bottom, { bottom.x + 5 * g, bottom.y + g });
            Settle();
            EXPECT_EQ(view.frames[0].size, (int2{ width.x, width.y + g })) << "the bottom edge: the height";

            // Far smaller than it may be: a title bar over a row, six cells wide.
            const int2 now = view.frames[0].size;
            const int2 corner2{ t.x + now.x - 3, t.y + now.y - 3 };
            Drag(corner2, { corner2.x - 100 * g, corner2.y - 100 * g });
            Settle();
            EXPECT_EQ(view.frames[0].size, (int2{ 6 * g, 4 * g }));
        }

        // A frame set off the grid, or too small, is put on it and made big enough.
        TEST_F(NodeViewFrameTest, AFrameIsKeptOnTheGrid)
        {
            view.frames[1].pos  = { 26 * g + 3, 12 * g - 2 };
            view.frames[1].size = { g, g };
            view.frames[0].size = { 40 * g + 3, 22 * g - 2 };
            Settle();
            EXPECT_EQ(view.frames[1].pos, (int2{ 26 * g, 12 * g }));
            EXPECT_EQ(view.frames[1].size, (int2{ 6 * g, 4 * g }));
            EXPECT_EQ(view.frames[0].size, (int2{ 40 * g, 22 * g })) << "whole cells";
        }

        // A double-click on the title puts a field there; Enter keeps what was typed, as one
        // edit; Escape keeps the old title; going elsewhere keeps the new one.
        TEST_F(NodeViewFrameTest, ADoubleClickRenamesIt)
        {
            Settle();
            std::vector<NodeViewEdit> edits;
            view.onEdit = [&](const NodeViewEdit& edit) { edits.push_back(edit); };
            auto startRename = [&](std::string_view title) {
                const int2 at = Middle(TitleOf(title));
                Single();
                app.ClickAt(at);
                Frame();
                app.ClickAt(at);
                Frame();
                Frame();
            };
            auto field = [&] {
                const std::vector<ID> fields = WithStyle(app.theme.nodeFrameRenameStyleRef);
                return fields.empty() ? ID::Invalid() : fields[0];
            };

            startRename("Lighting");
            ASSERT_NE(field(), ID::Invalid());
            EXPECT_EQ(app.guiStateManager.Interaction().GetFocusedID(), field());
            EXPECT_EQ(app.GetElementRect(field()), app.GetElementRect(TitleOf("")));
            app.InjectKey(KeyCode::End, KeyAction::KeyDown);
            Frame();
            app.InjectText(" pass");
            Frame();
            Key(KeyCode::Return);
            Settle();
            EXPECT_EQ(field(), ID::Invalid());
            EXPECT_EQ(view.frames[0].title, "Lighting pass");
            ASSERT_EQ(edits.size(), 1u);
            ASSERT_EQ(edits[0].frameChanges.size(), 1u);
            EXPECT_EQ(edits[0].frameChanges[0].from.title, "Lighting");
            EXPECT_EQ(edits[0].frameChanges[0].to.title, "Lighting pass");

            startRename("Notes");
            app.InjectText("x");
            Frame();
            Key(KeyCode::Escape);
            Settle();
            EXPECT_EQ(field(), ID::Invalid());
            EXPECT_EQ(view.frames[1].title, "Notes");
            EXPECT_EQ(edits.size(), 1u);

            startRename("Notes");
            app.InjectKey(KeyCode::End, KeyAction::KeyDown);
            Frame();
            app.InjectText("!");
            Frame();
            Click(Empty());
            Settle();
            EXPECT_EQ(field(), ID::Invalid());
            EXPECT_EQ(view.frames[1].title, "Notes!");
            EXPECT_EQ(edits.size(), 2u);
        }

        // RenameFrame, for a menu's "Rename": the field as a double-click gives it.
        TEST_F(NodeViewFrameTest, RenameFrameStartsTheSameRename)
        {
            Settle();
            RenameFrame(view, 7);   // none such: nothing
            Settle();
            EXPECT_TRUE(WithStyle(app.theme.nodeFrameRenameStyleRef).empty());
            RenameFrame(view, 1);
            Settle();
            const std::vector<ID> fields = WithStyle(app.theme.nodeFrameRenameStyleRef);
            ASSERT_EQ(fields.size(), 1u);
            EXPECT_EQ(app.guiStateManager.Interaction().GetFocusedID(), fields[0]);
            EXPECT_EQ(app.TextOf(fields[0]), "Notes");
        }

        // A right click on the title opens the app's menu for it and selects it; one in the
        // box below opens the canvas's.
        TEST_F(NodeViewFrameTest, ARightClickOnTheTitleOpensItsMenu)
        {
            Settle();
            int framesMenu = -1, canvasMenus = 0;
            view.onFrameContextMenu  = [&](int frame) { framesMenu = frame; };
            view.onCanvasContextMenu = [&](float2) { ++canvasMenus; };
            app.ClickAt(Middle(TitleOf("Notes")), MouseButtons::Right);
            Settle();
            EXPECT_EQ(framesMenu, 1);
            EXPECT_EQ(view.selectedFrameIDs, std::unordered_set<ID>{ frameInner });
            EXPECT_EQ(canvasMenus, 0);

            Key(KeyCode::Escape);
            framesMenu = -1;
            const IntRect t = app.GetElementRect(TitleOf("Lighting"));
            app.ClickAt({ t.x + 2 * g, t.y + 18 * g }, MouseButtons::Right);
            Settle();
            EXPECT_EQ(framesMenu, -1);
            EXPECT_GT(canvasMenus, 0);
        }

        // The marquee selects the frames wholly inside it, as it does the nodes.
        TEST_F(NodeViewFrameTest, TheMarqueeSelectsTheFramesInsideIt)
        {
            Settle();
            // The marquee goes from the screen to node.pos space through the canvas's middle, as
            // the view is drawn in the app (the harness lays it out without that offset).
            const IntRect inner = CanvasInner();
            const int2 middle{ inner.x + inner.w / 2, inner.y + inner.h / 2 };
            Drag({ middle.x + 25 * g, middle.y + 11 * g }, { middle.x + 39 * g, middle.y + 21 * g });
            EXPECT_EQ(view.selectedFrameIDs, std::unordered_set<ID>{ frameInner }) << "not the outer one, only partly inside";
            // A bigger one holds both.
            Drag({ middle.x + 3 * g, middle.y + g }, { middle.x + 45 * g, middle.y + 25 * g });
            EXPECT_EQ(view.selectedFrameIDs, (std::unordered_set<ID>{ frameOuter, frameInner }));
        }

        // ---------------------------------------------------------------------
        // Collapsed
        // ---------------------------------------------------------------------

        // A wire into the outer frame (B.Out to A.In) and one out of it (A.Out to B.In).
        class NodeViewCollapsedFrameTest : public NodeViewFrameTest
        {
        protected:
            void SetUp() override
            {
                NodeViewFrameTest::SetUp();
                NodeModel& a = view.nodes[0];
                a.properties[0].name = "Out";
                a.properties.push_back(NodeProperty{ .input = PropertyEndpoint{ 1, 1 }, .name = "In" });
                NodeModel& b = view.nodes[1];
                b.properties[0].input = PropertyEndpoint{ 0, 0 };
                b.properties.push_back(NodeProperty{ .output = true, .name = "Value" });
            }

            // Clicks the chevron at the title's left.
            void ToggleAt(std::string_view title)
            {
                const IntRect t = app.GetElementRect(TitleOf(title));
                Click({ t.x + app.styleManager.Px(12), t.y + t.h / 2 });
            }

            std::vector<IntRect> SocketRects() const
            {
                std::vector<IntRect> out;
                for (const ID id : WithStyle(app.theme.nodeSocketStyleRef))
                    out.push_back(app.GetElementRect(id));
                return out;
            }
        };

        // The chevron collapses it: a node's box, no wider than a wide node, its title bar
        // over a row for the wire in and the wire out; what's inside is hidden. One edit.
        TEST_F(NodeViewCollapsedFrameTest, TheChevronCollapsesItIntoANodesBox)
        {
            Settle();
            std::vector<NodeViewEdit> edits;
            view.onEdit = [&](const NodeViewEdit& edit) { edits.push_back(edit); };
            ToggleAt("Lighting");
            Settle();
            ASSERT_TRUE(view.frames[0].collapsed);
            ASSERT_EQ(edits.size(), 1u);
            ASSERT_EQ(edits[0].frameChanges.size(), 1u);
            EXPECT_FALSE(edits[0].frameChanges[0].from.collapsed);
            EXPECT_EQ(view.frames[0].size, (int2{ 40 * g, 22 * g })) << "its size kept for when it opens";

            const IntRect title = app.GetElementRect(TitleOf("Lighting"));
            EXPECT_EQ(title.w, 16 * g);
            const std::vector<ID> boxes = WithStyle(app.theme.nodeStyleRef);
            ASSERT_FALSE(boxes.empty());
            IntRect box{};
            for (const ID id : boxes)
                if (app.GetElementRect(id).x == title.x && app.GetElementRect(id).y == title.y) box = app.GetElementRect(id);
            EXPECT_EQ(box.w, 16 * g);
            EXPECT_EQ(box.h, 2 * g + 2 * g + g) << "the title bar, one row, a cell under it";
            EXPECT_NE(app.FindByText("A.In"), ID::Invalid());
            EXPECT_NE(app.FindByText("A.Out"), ID::Invalid());

            // Hidden: the frame inside it, and A, which can't be clicked or selected.
            EXPECT_EQ(TitleOf("Notes"), ID::Invalid());
            EXPECT_TRUE(view.nodes[0].hidden);
            EXPECT_FALSE(view.nodes[1].hidden);
            view.nodes[0].lastSize = { 6 * g, 4 * g };   // as a layout would have left it
            EXPECT_EQ(NodeAtCanvasPos(view,{ float(view.nodes[0].pos.x + 2), float(view.nodes[0].pos.y + 2) }), -1);
            Key(KeyCode::KeyA, KeyModifier::Control);
            EXPECT_FALSE(view.selectedNodeIDs.contains(nodeA));
            EXPECT_TRUE(view.selectedNodeIDs.contains(nodeB));
            EXPECT_EQ(view.selectedFrameIDs, std::unordered_set<ID>{ frameOuter }) << "not the hidden one";

            // A's two sockets stand in on the box's edges, on its row; B's stay on B.
            int onLeft = 0, onRight = 0;
            for (const IntRect& s : SocketRects())
            {
                if (s.y + s.h / 2 != box.y + 3 * g) continue;
                if (s.x + s.w / 2 == box.x) ++onLeft;
                if (s.x + s.w / 2 == box.x + box.w) ++onRight;
            }
            EXPECT_EQ(onLeft, 1) << "the wire in, at the left";
            EXPECT_EQ(onRight, 1) << "the wire out, at the right";
        }

        // Expanded again by the chevron, by Enter on the selected frame, or by undo; all of it
        // comes back.
        TEST_F(NodeViewCollapsedFrameTest, ItExpandsAgain)
        {
            Settle();
            std::vector<NodeViewEdit> edits;
            view.onEdit = [&](const NodeViewEdit& edit) { edits.push_back(edit); };
            ToggleAt("Lighting");
            Settle();
            ASSERT_TRUE(view.frames[0].collapsed);
            ToggleAt("Lighting");
            Settle();
            EXPECT_FALSE(view.frames[0].collapsed);
            EXPECT_FALSE(view.nodes[0].hidden);
            EXPECT_NE(TitleOf("Notes"), ID::Invalid());
            EXPECT_EQ(app.GetElementRect(TitleOf("Lighting")).w, 40 * g);

            // Enter, the frame selected (the chevron only toggles, as a tree's arrow does).
            EXPECT_TRUE(view.selectedFrameIDs.empty());
            Click(Middle(TitleOf("Lighting")));
            ASSERT_EQ(view.selectedFrameIDs, std::unordered_set<ID>{ frameOuter });
            Key(KeyCode::Return);
            Settle();
            EXPECT_TRUE(view.frames[0].collapsed);
            Key(KeyCode::Return);
            Settle();
            EXPECT_FALSE(view.frames[0].collapsed);

            ASSERT_EQ(edits.size(), 4u);
            ApplyNodeViewEdit(view, edits[0], /*undo=*/false);
            Settle();
            EXPECT_TRUE(view.frames[0].collapsed);
            ApplyNodeViewEdit(view, edits[0], /*undo=*/true);
            Settle();
            EXPECT_FALSE(view.frames[0].collapsed);
            EXPECT_NE(TitleOf("Notes"), ID::Invalid());
        }

        // A node dragged over where a collapsed frame would be open, and let go there, stays a
        // node: the frame holds what it held as it collapsed. Opened, it holds what lies inside.
        TEST_F(NodeViewCollapsedFrameTest, ANodeDraggedOverACollapsedFrameStaysOut)
        {
            Settle();
            ToggleAt("Lighting");
            Settle();
            ASSERT_TRUE(view.frames[0].members.has_value());
            EXPECT_EQ(view.frames[0].members->size(), 2u) << "A and the inner frame";

            // B, from below and right of it, to where the open frame's middle would be (well
            // past the collapsed box), on frames of its own, as a pointer goes.
            const IntRect b = app.GetElementRect(nodeB);
            const int2 from{ b.x + b.w / 2, b.y + g };
            const int2 to{ from.x - 30 * g, from.y - 18 * g };
            Single();
            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            Frame();
            for (int step = 1; step <= 4; ++step)
            {
                app.InjectMouseMove({ from.x + (to.x - from.x) * step / 4, from.y + (to.y - from.y) * step / 4 }, MouseButtons::Left);
                Frame();
                EXPECT_FALSE(view.nodes[1].hidden) << "step " << step;
                EXPECT_TRUE(view.nodeDrag.IsHeld()) << "the drag goes on, step " << step;
            }
            app.InjectMouseUp(to);
            Settle();
            EXPECT_FALSE(view.nodes[1].hidden);
            EXPECT_EQ(view.nodes[1].pos, (int2{ 30 * g, 12 * g })) << "where it was let go";
            EXPECT_EQ(NodesInFrame(view, 0), std::vector<int>{ 0 }) << "still only A";

            // Opened, B lies inside it, and is in it.
            ToggleAt("Lighting");
            Settle();
            EXPECT_FALSE(view.frames[0].members.has_value());
            EXPECT_EQ(NodesInFrame(view, 0), (std::vector<int>{ 0, 1 }));
        }

        // Collapsed in code, by setting the flag: it holds what lies inside it then.
        TEST_F(NodeViewCollapsedFrameTest, ACollapseInCodeNotesWhatItHolds)
        {
            view.selectedNodeIDs = { nodeA, nodeB };
            view.frames[0].collapsed = true;
            Settle();
            ASSERT_TRUE(view.frames[0].members.has_value());
            EXPECT_TRUE(view.nodes[0].hidden);
            EXPECT_EQ(view.selectedNodeIDs, std::unordered_set<ID>{ nodeB }) << "what can't be seen isn't selected";
            const NodeViewEdit expand = CollapseFrameEdit(view, 0, false);
            ASSERT_EQ(expand.frameChanges.size(), 1u);
            EXPECT_FALSE(expand.frameChanges[0].to.collapsed);
            EXPECT_TRUE(CollapseFrameEdit(view, 0, true).empty()) << "collapsed already";
            EXPECT_TRUE(CollapseFrameEdit(view, 9, true).empty());
        }

        // Collapsed, a drag on its title still moves what's inside it; it has no edges to resize.
        TEST_F(NodeViewCollapsedFrameTest, ACollapsedFrameMovesWithWhatsInside)
        {
            Settle();
            ToggleAt("Lighting");
            Settle();
            // Where its corner would be open: a press there is the canvas's.
            const IntRect t = app.GetElementRect(TitleOf("Lighting"));
            const int2 corner{ t.x + 40 * g - 3, t.y + 22 * g - 3 };
            Drag(corner, { corner.x + 4 * g, corner.y + 4 * g });
            Settle();
            EXPECT_EQ(view.frames[0].size, (int2{ 40 * g, 22 * g })) << "no edges while collapsed";

            const int2 a = view.nodes[0].pos, inner = view.frames[1].pos;
            const int2 from = Middle(TitleOf("Lighting"));
            Drag(from, { from.x + 4 * g, from.y + 2 * g });
            Settle();
            EXPECT_EQ(view.nodes[0].pos, (int2{ a.x + 4 * g, a.y + 2 * g }));
            EXPECT_EQ(view.frames[1].pos, (int2{ inner.x + 4 * g, inner.y + 2 * g }));
            EXPECT_TRUE(view.frames[0].collapsed);
            EXPECT_EQ(view.frames[0].size, (int2{ 40 * g, 22 * g }));
        }

        // The canvas's own pass (grid, frames, wires) into a stub renderer.
        void DrawCanvas(HeadlessGUIApp& app, GUITest::StubGUIRenderer& renderer)
        {
            for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels(); d++)
                for (uint32_t i = 0; i < app.elementStorage.DepthSizeOf(d); i++)
                {
                    const ElementRef ref{ d, i };
                    if (!app.elementStorage.IsValidRef(ref) || app.elementStorage.GetElementUnsafe(ref).styleRef != app.theme.nodeViewStyleRef)
                        continue;
                    const ElementCustomDraw* draw = app.elementStorage.GetCustomDraw(ref);
                    if (draw == nullptr || !draw->callback)
                        return;
                    const ElementRect* rects = app.elementStorage.GetElementRectPtr(ref);
                    CustomDrawContext ctx{ renderer, app.elementStorage, ref, rects->rect, rects->innerRect,
                                           IntRect{ -4000, -4000, 8000, 8000 }, float3x2{}, int2{ 0, 0 } };
                    draw->callback(ctx);
                    return;
                }
        }
        // How many line pieces the canvas's pass draws.
        size_t WireLinesDrawn(HeadlessGUIApp& app)
        {
            GUITest::StubGUIRenderer renderer;
            DrawCanvas(app, renderer);
            return renderer.lineStarts.size() + renderer.triangles.size();
        }
        // How many boxes it draws: the frames' bodies and bands among them.
        size_t BoxesDrawn(HeadlessGUIApp& app)
        {
            GUITest::StubGUIRenderer renderer;
            DrawCanvas(app, renderer);
            return renderer.boxes.size();
        }

        // Collapsed, a frame isn't drawn behind the nodes as an open one is; its box is an
        // element of its own.
        TEST_F(NodeViewFrameTest, ACollapsedFrameIsNotDrawnOpen)
        {
            view.frames.resize(1);
            Settle();
            const size_t open = BoxesDrawn(app);
            view.frames[0].collapsed = true;
            Settle();
            const size_t collapsed = BoxesDrawn(app);
            view.frames.clear();
            Settle();
            const size_t none = BoxesDrawn(app);
            EXPECT_GT(open, none) << "test setup: an open frame is drawn";
            EXPECT_EQ(collapsed, none);
        }

        // Two nodes in a frame, wired to each other and to nothing else: collapsed, the wire
        // between them isn't drawn.
        TEST_F(NodeViewFrameTest, AWireBetweenTwoNodesInsideIsNotDrawnCollapsed)
        {
            NodeModel c{};
            c.stableID = app.idManager.CreateStaticID();
            c.title    = "C";
            c.pos      = { 20 * g, 16 * g };
            c.properties.push_back(NodeProperty{ .input = PropertyEndpoint{ 0, 0 } });
            view.nodes.push_back(std::move(c));
            view.frames.resize(1);   // just the outer one, round A and C
            Settle();
            const size_t expanded = WireLinesDrawn(app);
            view.frames[0].collapsed = true;
            Settle();
            const size_t collapsed = WireLinesDrawn(app);
            EXPECT_TRUE(view.nodes[0].hidden);
            EXPECT_TRUE(view.nodes[2].hidden);
            view.nodes[2].properties[0].input = PropertyEndpoint{};   // no wire at all
            view.frames[0].collapsed = false;
            Settle();
            const size_t none = WireLinesDrawn(app);
            EXPECT_GT(expanded, none) << "test setup: the wire is drawn while expanded";
            EXPECT_EQ(collapsed, none) << "collapsed, it isn't";
        }

        // A wire wholly inside a collapsed frame isn't drawn: neither end has a socket shown.
        TEST_F(NodeViewCollapsedFrameTest, AWireInsideIsNotShown)
        {
            // A third node in the outer frame, fed by A: that wire stays inside.
            NodeModel c{};
            c.stableID = app.idManager.CreateStaticID();
            c.title    = "C";
            c.pos      = { 20 * g, 16 * g };
            c.properties.push_back(NodeProperty{ .input = PropertyEndpoint{ 0, 0 } });
            view.nodes.push_back(std::move(c));
            Settle();
            const size_t expanded = SocketRects().size();
            ToggleAt("Lighting");
            Settle();
            // A's two crossing sockets stand in; C's input and A's are not shown.
            EXPECT_EQ(SocketRects().size(), 2u + 2u) << "A's two on the box, B's two on B (of " << expanded << ")";
            EXPECT_EQ(app.FindByText("C"), ID::Invalid());
        }

        // An output inside feeding two nodes outside is one row, not one per wire.
        TEST_F(NodeViewCollapsedFrameTest, AnOutputFeedingTwoNodesIsOneRow)
        {
            NodeModel c{};
            c.stableID = app.idManager.CreateStaticID();
            c.title    = "C";
            c.pos      = { 60 * g, 10 * g };
            c.properties.push_back(NodeProperty{ .input = PropertyEndpoint{ 0, 0 } });
            view.nodes.push_back(std::move(c));
            Settle();
            ToggleAt("Lighting");
            Settle();
            const IntRect title = app.GetElementRect(TitleOf("Lighting"));
            IntRect box{};
            for (const ID id : WithStyle(app.theme.nodeStyleRef))
                if (app.GetElementRect(id).x == title.x && app.GetElementRect(id).y == title.y) box = app.GetElementRect(id);
            EXPECT_EQ(box.h, 2 * g + 2 * g + g) << "A.In and A.Out side by side on one row";
        }

        // The marquee doesn't select what a collapsed frame hides, only the frame.
        TEST_F(NodeViewCollapsedFrameTest, TheMarqueeLeavesWhatsHidden)
        {
            Settle();
            ToggleAt("Lighting");
            Settle();
            const IntRect inner = CanvasInner();
            const int2 middle{ inner.x + inner.w / 2, inner.y + inner.h / 2 };
            Drag({ middle.x + 3 * g, middle.y + g }, { middle.x + 45 * g, middle.y + 25 * g });
            EXPECT_EQ(view.selectedFrameIDs, std::unordered_set<ID>{ frameOuter });
            EXPECT_TRUE(view.selectedNodeIDs.empty()) << "not A, though it lies inside";
        }

        // The canvas menu, a submenu first as the showcase has it: hovering the items after it
        // keeps the view and the menu as they are, frame after frame.
        TEST_F(NodeViewFrameTest, HoveringTheCanvasMenusItemsKeepsEverythingStill)
        {
            view.onCanvasContextMenu = [&](float2) {
                if (app.BeginMenu("Add node"))
                {
                    app.MenuItem("Value", [] {});
                    app.MenuItem("Math", [] {});
                }
                app.EndMenu();
                app.MenuItem("Add Reroute here", [] {});
                app.MenuSeparator();
                app.MenuItem("Frame all", [] {});
                app.MenuItem("Add frame here", [] {});
            };
            Settle();
            app.ClickAt(Empty(), MouseButtons::Right);
            Settle();
            ASSERT_TRUE(app.menuManager.IsOpen());
            const ID menuWindow = app.menuManager.levels[0].windowID;
            const float3x2 transform = view.transform;
            const IntRect a = app.GetElementRect(nodeA);
            for (const char* item : { "Add Reroute here", "Frame all", "Add frame here", "Add node", "Add Reroute here" })
            {
                const ID id = app.FindByText(item);
                ASSERT_NE(id, ID::Invalid()) << item;
                app.mouseWindow = menuWindow;
                app.InjectMouseMove(app.CenterOf(id));
                for (int f = 0; f < 6; ++f)
                {
                    Frame();
                    ASSERT_TRUE(app.menuManager.IsOpen()) << item << ", frame " << f;
                    EXPECT_EQ(app.menuManager.levels[0].windowID, menuWindow) << item << ", frame " << f;
                    EXPECT_EQ(view.transform, transform) << item << ", frame " << f;
                    EXPECT_EQ(app.GetElementRect(nodeA), a) << item << ", frame " << f;
                    EXPECT_EQ(app.FindByText(item), id) << item << " keeps its ID, frame " << f;
                }
            }
            app.mouseWindow = ID::Invalid();
        }

        // ---------------------------------------------------------------------
        // The edits and helpers
        // ---------------------------------------------------------------------

        TEST_F(NodeViewFrameTest, AddingAndRemovingFramesUndoes)
        {
            const ID added = app.idManager.CreateStaticID();
            const NodeViewEdit add = AddFrameEdit(view, NodeFrame{ .stableID = added, .title = "New", .pos = { 0, 0 }, .size = { 10 * g, 6 * g } }, 1);
            ApplyNodeViewEdit(view, add);
            ASSERT_EQ(view.frames.size(), 3u);
            EXPECT_EQ(view.frames[1].stableID, added);
            EXPECT_EQ(view.frames[2].stableID, frameInner);
            ApplyNodeViewEdit(view, add, /*undo=*/true);
            ASSERT_EQ(view.frames.size(), 2u);
            EXPECT_EQ(view.frames[1].stableID, frameInner);
            // No place given, or one past the end: the end.
            EXPECT_EQ(AddFrameEdit(view, NodeFrame{}, -1).frames[0].index, 2);
            EXPECT_EQ(AddFrameEdit(view, NodeFrame{}, 9).frames[0].index, 2);

            view.selectedFrameIDs = { frameOuter, frameInner };
            const int both[] = { 1, 0, 1, 7 };
            const NodeViewEdit remove = RemoveFramesEdit(view, both);
            ASSERT_EQ(remove.frames.size(), 2u);
            const std::vector<NodeFrame> before = view.frames;
            ApplyNodeViewEdit(view, remove);
            EXPECT_TRUE(view.frames.empty());
            EXPECT_TRUE(view.selectedFrameIDs.empty()) << "gone from the selection";
            EXPECT_EQ(view.nodes.size(), 2u) << "the nodes stay";
            ApplyNodeViewEdit(view, remove, /*undo=*/true);
            EXPECT_EQ(view.frames, before);
        }

        // Round the nodes as laid out: a cell of room on each side and the title bar above,
        // on the grid; nothing for no nodes.
        TEST_F(NodeViewFrameTest, AFrameAroundNodes)
        {
            Settle();
            // As a layout would have left them (the harness doesn't note sizes after layout).
            view.nodes[0].lastSize = { 6 * g, 4 * g };
            view.nodes[1].lastSize = { 7 * g + 5, 3 * g };
            const int both[] = { 0, 1 };
            const NodeFrame frame = FrameAroundNodes(view, both);
            const NodeModel& a = view.nodes[0];
            const NodeModel& b = view.nodes[1];
            ASSERT_GT(a.lastSize.x, 0);
            EXPECT_EQ(frame.pos, (int2{ a.pos.x - g, a.pos.y - 3 * g }));
            EXPECT_GE(frame.pos.x + frame.size.x, b.pos.x + b.lastSize.x + g);
            EXPECT_LT(frame.pos.x + frame.size.x, b.pos.x + b.lastSize.x + 2 * g);
            EXPECT_GE(frame.pos.y + frame.size.y, b.pos.y + b.lastSize.y + g);
            EXPECT_EQ(frame.size.x % g, 0);
            EXPECT_EQ(frame.size.y % g, 0);
            EXPECT_EQ(FrameAroundNodes(view, std::span<const int>{}).size, (int2{ 0, 0 }));

            view.frames = { frame };
            EXPECT_EQ(NodesInFrame(view, 0), (std::vector<int>{ 0, 1 }));
            EXPECT_TRUE(NodesInFrame(view, 3).empty());
        }

        // Round the selected nodes and frames, behind the frames it holds so they stay on top.
        TEST_F(NodeViewFrameTest, GroupingTheSelection)
        {
            Settle();
            view.nodes[0].lastSize = { 6 * g, 4 * g };
            view.nodes[1].lastSize = { 6 * g, 4 * g };
            const ID group = app.idManager.CreateStaticID();
            const NodeFrame named{ .stableID = group, .title = "Group", .color = { 0.9f, 0.5f, 0.2f, 1.0f } };
            EXPECT_TRUE(GroupSelectionEdit(view, named).frames.empty()) << "nothing selected";

            view.selectedNodeIDs  = { nodeB };
            view.selectedFrameIDs = { frameInner };
            const NodeViewEdit edit = GroupSelectionEdit(view, named);
            ASSERT_EQ(edit.frames.size(), 1u);
            EXPECT_EQ(edit.frames[0].index, 1) << "behind the inner frame";
            ApplyNodeViewEdit(view, edit);
            ASSERT_EQ(view.frames.size(), 3u);
            const NodeFrame& made = view.frames[1];
            EXPECT_EQ(made.stableID, group);
            EXPECT_EQ(made.title, "Group");
            EXPECT_EQ(made.color, named.color);
            const NodeFrame& inner = view.frames[2];
            const NodeModel& b = view.nodes[1];
            EXPECT_EQ(made.pos, (int2{ inner.pos.x - g, inner.pos.y - 3 * g })) << "a cell round, the title bar above";
            EXPECT_GE(made.pos.x + made.size.x, b.pos.x + b.lastSize.x + g);
            EXPECT_GE(made.pos.y + made.size.y, b.pos.y + b.lastSize.y + g);
            EXPECT_EQ(made.size.x % g, 0);
            EXPECT_EQ(NodesInFrame(view, 1), (std::vector<int>{ 1 })) << "B moves with it";
            ApplyNodeViewEdit(view, edit, /*undo=*/true);
            EXPECT_EQ(view.frames.size(), 2u);

            // Round both frames: behind the first of them.
            view.selectedFrameIDs = { frameOuter, frameInner };
            EXPECT_EQ(GroupSelectionEdit(view, named).frames[0].index, 0);

            // Only nodes: in front of the other frames.
            view.selectedFrameIDs.clear();
            EXPECT_EQ(GroupSelectionEdit(view, named).frames[0].index, 2);

            // A collapsed frame counts with its open size, so it fits when it opens again.
            view.selectedNodeIDs.clear();
            view.selectedFrameIDs = { frameInner };
            view.frames[1].collapsed = true;
            const NodeFrame around = GroupSelectionEdit(view, named).frames[0].frame;
            EXPECT_GE(around.pos.x + around.size.x, view.frames[1].pos.x + view.frames[1].size.x + g);
        }

        TEST_F(NodeViewFrameTest, TheNodesInAFrameAndItsTitle)
        {
            Settle();
            view.nodes[0].lastSize = { 6 * g, 4 * g };
            view.nodes[1].lastSize = { 6 * g, 4 * g };
            EXPECT_EQ(NodesInFrame(view, 0), std::vector<int>{ 0 });
            EXPECT_TRUE(NodesInFrame(view, 1).empty());
            view.nodes[0].lastSize = { 40 * g, 4 * g };
            EXPECT_TRUE(NodesInFrame(view, 0).empty()) << "sticking out to the right: not inside";
            view.nodes[0].lastSize = { 6 * g, 4 * g };
            // The title bar, a row pitch high; the inner frame is the frontmost where both are.
            EXPECT_EQ(FrameTitleAtCanvasPos(view, { float(5 * g), float(3 * g) }), 0);
            EXPECT_EQ(FrameTitleAtCanvasPos(view, { float(5 * g), float(4 * g) }), -1) << "below the title bar";
            EXPECT_EQ(FrameTitleAtCanvasPos(view, { float(27 * g), float(13 * g) }), 1);
            EXPECT_EQ(FrameTitleAtCanvasPos(view, { float(3 * g), float(3 * g) }), -1);
        }
    }
}
