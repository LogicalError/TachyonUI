// A node view as a state machine (NodeViewKind::StateMachine): states without fields, joined
// by transitions that may run both ways and back into the same state, drawn, picked and edited
// through the headless harness.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/gui_id.h>
#include <gui_controls/gui_node_view_helpers.h>

#include <cmath>
#include <vector>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        class NodeViewStateMachineTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            NodeView       view;
            ID             sleep{}, patrol{}, chase{};
            std::vector<NodeViewEdit> edits;

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                app.runPostLayoutCallbacks = true;   // the states' sizes, which the arrows are drawn between
                sleep  = app.idManager.CreateStaticID();
                patrol = app.idManager.CreateStaticID();
                chase  = app.idManager.CreateStaticID();
                view = {};
                view.kind      = NodeViewKind::StateMachine;
                view.transform = float3x2::Scale(1.0f);
                view.showMinimap = false;
                view.showZoomControls = false;
                view.nodes = {
                    NodeModel{ .stableID = sleep,  .title = "Sleep",  .pos = { -400, -120 } },
                    NodeModel{ .stableID = patrol, .title = "Patrol", .pos = { 80, -120 } },
                    NodeModel{ .stableID = chase,  .title = "Chase",  .pos = { 80, 200 } },
                };
                view.onEdit = [this](const NodeViewEdit& edit) { edits.push_back(edit); };
            }
            void TearDown() override { app.Cleanup(); }

            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow({ 1600, 1000 }, ElementFlags::None, app.theme.mainWindowStyleRef);
                    app.RenderNodeView(view);
                    app.EndRootWindow();
                });
            }
            void Settle() { Frame(); Frame(); }

            ID Transition(ID from, ID to, std::string label = {})
            {
                const ID id = app.idManager.CreateStaticID();
                view.transitions.push_back({ .stableID = id, .from = from, .to = to, .label = std::move(label) });
                return id;
            }
            const NodeModel& State(ID id) const { return view.nodes[size_t(NodeIndex(view, id))]; }
            IntRect Box(ID id) const
            {
                const NodeModel& s = State(id);
                return { s.pos.x, s.pos.y, s.lastSize.x, s.lastSize.y };
            }
            // A point in node.pos space on screen. node.pos is measured from the canvas's border
            // box, the screen mapping's points from its content box.
            int2 Screen(float2 canvas) const
            {
                const float2 p = NodeViewCanvasToScreen(view, { canvas.x + float(view.lastGridOrigin.x), canvas.y + float(view.lastGridOrigin.y) });
                return { int32_t(std::lround(p.x)), int32_t(std::lround(p.y)) };
            }
            int2 Centre(ID id) const
            {
                const IntRect b = Box(id);
                return Screen({ float(b.x) + float(b.w) / 2.0f, float(b.y) + float(b.h) / 2.0f });
            }
            // On the band round a state, right of it, half way down: where a transition starts.
            int2 RightEdge(ID id) const
            {
                const IntRect b = Box(id);
                return Screen({ float(b.x + b.w) + float(app.styleManager.Px(6)), float(b.y) + float(b.h) / 2.0f });
            }
            int2 Empty() const { return Screen({ 500.0f, 320.0f }); }
            int2 PathMiddle(ID transition) const
            {
                const TransitionPath path = StateTransitionPath(view, view.transitions[size_t(TransitionIndex(view, transition))],
                                                                app.styleManager.Px(6), app.styleManager.Px(24));
                if (path.loop)
                    return Screen({ float(path.labelAt.x), float(path.labelAt.y) });
                return Screen({ float(path.from.x + path.to.x) / 2.0f, float(path.from.y + path.to.y) / 2.0f });
            }
            void Single()
            {
                view.nodeDoubleClick  = {};
                view.transitionClicks = {};
            }
            void Drag(int2 from, int2 to)
            {
                Single();
                app.InjectMouseMove(from);
                Frame();
                app.InjectMouseDown(from);
                Frame();
                app.InjectMouseMove({ (from.x + to.x) / 2, (from.y + to.y) / 2 }, MouseButtons::Left);
                Frame();
                app.InjectMouseMove(to, MouseButtons::Left);
                Frame();
                app.InjectMouseUp(to);
                Settle();
            }
            void Click(int2 at)
            {
                Single();
                app.InjectMouseMove(at);
                Frame();
                app.ClickAt(at);
                Settle();
            }
            void Key(KeyCode key)
            {
                app.InjectKey(key, KeyAction::KeyDown);
                Frame();
                app.InjectKey(key, KeyAction::KeyUp);
                Frame();
            }
            int CountWithStyle(StyleRef style) const
            {
                int count = 0;
                for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels(); d++)
                    for (uint32_t i = 0; i < app.elementStorage.DepthSizeOf(d); i++)
                    {
                        const ElementRef ref{ d, i };
                        if (app.elementStorage.IsValidRef(ref) && app.elementStorage.GetElementUnsafe(ref).styleRef == style)
                            ++count;
                    }
                return count;
            }
        };

        // A pair both ways runs side by side, a single one centre to centre; a loop rises over
        // its state. The arrows end on the states' edges.
        TEST_F(NodeViewStateMachineTest, ArrowsRunBetweenTheEdgesPairsSideBySide)
        {
            const ID there = Transition(sleep, patrol);
            const ID back  = Transition(patrol, sleep);
            const ID on    = Transition(patrol, chase);
            const ID self  = Transition(chase, chase);
            Settle();
            const int32_t gap = app.styleManager.Px(6), loop = app.styleManager.Px(24);
            auto path = [&](ID t) { return StateTransitionPath(view, view.transitions[size_t(TransitionIndex(view, t))], gap, loop); };

            const TransitionPath a = path(there), b = path(back);
            ASSERT_TRUE(a.valid && b.valid);
            EXPECT_EQ(a.from.x, Box(sleep).x + Box(sleep).w) << "leaves the right edge";
            EXPECT_EQ(a.to.x, Box(patrol).x) << "ends on the left edge";
            EXPECT_NE(a.from.y, b.to.y) << "the pair doesn't overlap";
            EXPECT_EQ(std::abs(a.from.y - b.to.y), 2 * gap);

            const TransitionPath down = path(on);
            EXPECT_EQ(down.from.y, Box(patrol).y + Box(patrol).h) << "a single one aims centre to centre: out of the bottom";
            EXPECT_EQ(down.to.y, Box(chase).y) << "into the top";

            const TransitionPath l = path(self);
            EXPECT_TRUE(l.loop);
            EXPECT_EQ(l.from.y, Box(chase).y);
            EXPECT_LT(l.c1.y, Box(chase).y) << "over its top";
        }

        // A label sits beside its arrow, clear of it, on the arrow's right as it runs: the two
        // of a pair both ways on opposite sides. A loop's sits over it.
        TEST_F(NodeViewStateMachineTest, LabelsSitBesideTheirArrows)
        {
            const ID there = Transition(sleep, patrol);
            const ID back  = Transition(patrol, sleep);
            const ID down  = Transition(patrol, chase);
            const ID self  = Transition(chase, chase);
            Settle();
            const int32_t gap = app.styleManager.Px(6), loop = app.styleManager.Px(24), margin = app.styleManager.Px(4);
            const int2 size{ 60, 16 };
            auto path = [&](ID t) { return StateTransitionPath(view, view.transitions[size_t(TransitionIndex(view, t))], gap, loop); };

            // Sleep -> Patrol runs right: its label below its line; Patrol -> Sleep runs left: above.
            const TransitionPath a = path(there), b = path(back);
            const int2 la = TransitionLabelCentre(a, size, margin), lb = TransitionLabelCentre(b, size, margin);
            EXPECT_EQ(la.y - size.y / 2, a.from.y + margin) << "below, clear of the line by the margin";
            EXPECT_EQ(lb.y + size.y / 2, b.from.y - margin) << "above";
            EXPECT_EQ(la.x, (a.from.x + a.to.x) / 2) << "half way along";

            // Patrol -> Chase runs down: its label to its left (the arrow's right, facing down).
            const TransitionPath d = path(down);
            const int2 ld = TransitionLabelCentre(d, size, margin);
            EXPECT_LT(ld.x + size.x / 2, std::min(d.from.x, d.to.x)) << "clear of the line";

            // A loop's over its top.
            const TransitionPath l = path(self);
            const int2 ll = TransitionLabelCentre(l, size, margin);
            // Drawn as two quadratic halves, the curve's top is the middle of its controls.
            EXPECT_EQ(ll.y + size.y / 2, (l.c1.y + l.c2.y) / 2 - margin) << "clear of the curve's top by the margin";
            EXPECT_EQ(ll.x, (l.from.x + l.to.x) / 2);
        }

        // From the state's menu (StartTransition) a transition follows the pointer until a click on
        // a state adds it; a click elsewhere, or Escape, drops it.
        TEST_F(NodeViewStateMachineTest, AMenuStartsATransition)
        {
            Settle();
            StartTransition(view, NodeIndex(view, sleep));
            app.InjectMouseMove(Centre(chase));
            Frame();
            Click(Centre(chase));
            ASSERT_EQ(view.transitions.size(), 1u);
            EXPECT_EQ(view.transitions[0].from, sleep);
            EXPECT_EQ(view.transitions[0].to, chase);
            EXPECT_FALSE(view.transitionPicking);

            StartTransition(view, NodeIndex(view, patrol));
            Click(Empty());
            EXPECT_FALSE(view.transitionPicking);
            EXPECT_EQ(view.transitions.size(), 1u) << "a click elsewhere drops it";

            // The canvas has the focus from that click, so Escape comes to it.
            StartTransition(view, NodeIndex(view, patrol));
            Key(KeyCode::Escape);
            EXPECT_FALSE(view.transitionPicking) << "Escape drops it";
            EXPECT_EQ(view.transitions.size(), 1u);
        }

        // A state shows no fields: properties stay out of a state machine.
        TEST_F(NodeViewStateMachineTest, StatesShowNoFields)
        {
            view.nodes[0].properties.push_back(NodeProperty{ .input = PropertyEndpoint{}, .output = true });
            Settle();
            EXPECT_EQ(CountWithStyle(app.theme.nodeSocketStyleRef), 0);
        }

        // A drag from the band round a state onto another state adds a transition; onto itself,
        // a loop; onto nothing, none. Escape drops it. A press inside a state still moves it.
        TEST_F(NodeViewStateMachineTest, ADragFromTheEdgeAddsATransition)
        {
            Settle();
            Drag(RightEdge(sleep), Centre(patrol));
            ASSERT_EQ(view.transitions.size(), 1u);
            EXPECT_EQ(view.transitions[0].from, sleep);
            EXPECT_EQ(view.transitions[0].to, patrol);
            ASSERT_EQ(edits.size(), 1u);
            ASSERT_EQ(edits[0].transitions.size(), 1u);
            EXPECT_TRUE(edits[0].transitions[0].added);

            Drag(RightEdge(patrol), Centre(patrol));
            ASSERT_EQ(view.transitions.size(), 2u);
            EXPECT_EQ(view.transitions[1].from, patrol);
            EXPECT_EQ(view.transitions[1].to, patrol) << "a loop";

            Drag(RightEdge(sleep), Centre(patrol));
            EXPECT_EQ(view.transitions.size(), 2u) << "not the same one twice";

            Drag(RightEdge(sleep), Empty());
            EXPECT_EQ(view.transitions.size(), 2u);

            Single();
            app.InjectMouseMove(RightEdge(sleep));
            Frame();
            app.InjectMouseDown(RightEdge(sleep));
            Frame();
            app.InjectMouseMove(Centre(chase), MouseButtons::Left);
            Frame();
            Key(KeyCode::Escape);
            app.InjectMouseUp(Centre(chase));
            Settle();
            EXPECT_EQ(view.transitions.size(), 2u) << "Escape drops it";

            const int2 before = State(chase).pos;
            Drag(Centre(chase), Centre(chase) + int2{ 80, 0 });
            EXPECT_GT(State(chase).pos.x, before.x) << "inside, a press moves the state";
            EXPECT_EQ(view.transitions.size(), 2u);
        }

        // The band under the pointer, and the arrow under it, show they're hovered.
        TEST_F(NodeViewStateMachineTest, TheEdgeAndTheArrowShowHover)
        {
            const ID t = Transition(sleep, patrol);
            Settle();
            app.InjectMouseMove(RightEdge(sleep));
            Settle();
            EXPECT_EQ(view.stateEdgeHover.part, sleep);
            app.InjectMouseMove(Centre(sleep));
            Settle();
            EXPECT_EQ(view.stateEdgeHover.part, ID::Invalid()) << "inside isn't the band";
            app.InjectMouseMove(PathMiddle(t));
            Settle();
            EXPECT_EQ(view.hoveredTransition.part, std::optional<ID>{ t });
        }

        // A click on an arrow selects it alone; a click on the empty canvas clears it.
        TEST_F(NodeViewStateMachineTest, AClickSelectsATransition)
        {
            const ID t = Transition(sleep, patrol);
            Settle();
            view.selectedNodeIDs = { chase };
            Click(PathMiddle(t));
            EXPECT_EQ(view.selectedTransitionIDs, std::unordered_set<ID>{ t });
            EXPECT_TRUE(view.selectedNodeIDs.empty());
            Click(Empty());
            EXPECT_TRUE(view.selectedTransitionIDs.empty());
        }

        // A double-click on an arrow edits its label; Return keeps it as one edit.
        TEST_F(NodeViewStateMachineTest, ADoubleClickRelabelsATransition)
        {
            const ID t = Transition(sleep, patrol, "noise");
            Settle();
            const IntRect before = app.GetElementRect(sleep);
            ASSERT_EQ(view.transitionLabelBoxes.size(), 1u);
            const IntRect chip = view.transitionLabelBoxes[0].second;
            Single();
            app.InjectMouseMove(PathMiddle(t));
            Frame();
            app.ClickAt(PathMiddle(t));
            Frame();
            app.ClickAt(PathMiddle(t));
            Settle();
            ASSERT_EQ(view.renamingTransition, std::optional<ID>{ t });
            EXPECT_EQ(app.GetElementRect(sleep), before) << "the field taking the focus scrolls nothing";
            int fields = 0;
            for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels(); d++)
                for (uint32_t i = 0; i < app.elementStorage.DepthSizeOf(d); i++)
                    if (const ElementRef ref{ d, i }; app.elementStorage.IsValidRef(ref)
                        && app.elementStorage.GetElementUnsafe(ref).styleRef == app.theme.nodeTransitionRenameStyleRef)
                    {
                        ++fields;
                        EXPECT_EQ(app.elementStorage.GetElementRectUnsafe(ref).rect, chip) << "the field is where the label was";
                    }
            EXPECT_EQ(fields, 1);
            app.InjectKey(KeyCode::End, KeyAction::KeyDown);
            Frame();
            app.InjectText(" heard");
            Frame();
            Key(KeyCode::Return);
            Settle();
            EXPECT_FALSE(view.renamingTransition);
            EXPECT_EQ(view.transitions[0].label, "noise heard");
            EXPECT_EQ(app.GetElementRect(sleep), before) << "the states stay where their arrows are";
            ASSERT_EQ(edits.size(), 1u);
            ASSERT_EQ(edits[0].transitionChanges.size(), 1u);
            EXPECT_EQ(edits[0].transitionChanges[0].from.label, "noise");
        }

        // Labelling a fresh transition: typing keeps the caret in view, and that scrolls nothing
        // either, so the states stay where their arrows are.
        TEST_F(NodeViewStateMachineTest, TypingALabelMovesNoState)
        {
            Settle();
            Drag(RightEdge(sleep), Centre(chase));
            ASSERT_EQ(view.transitions.size(), 1u);
            const ID t = view.transitions[0].stableID;
            Settle();
            const IntRect before = app.GetElementRect(sleep);
            RenameTransition(view, 0);
            Settle();
            ASSERT_EQ(view.renamingTransition, std::optional<ID>{ t });
            for (const char* text : { "h", "i", "t", " by the player" })
            {
                app.InjectText(text);
                Frame();
                EXPECT_EQ(app.GetElementRect(sleep), before) << "after typing \"" << text << "\"";
            }
            Key(KeyCode::Return);
            Settle();
            EXPECT_EQ(view.transitions[0].label, "hit by the player");
            EXPECT_EQ(app.GetElementRect(sleep), before);
        }

        // The canvas never scrolls: its nodes go where its pan and zoom put them, as its arrows
        // do. Whatever scrolls it (a field bringing its caret into view) is undone before layout.
        TEST_F(NodeViewStateMachineTest, TheCanvasNeverScrolls)
        {
            Settle();
            const IntRect before = app.GetElementRect(sleep);
            ID canvas = ID::Invalid();
            for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels() && canvas == ID::Invalid(); d++)
                for (uint32_t i = 0; i < app.elementStorage.DepthSizeOf(d); i++)
                    if (const ElementRef ref{ d, i }; app.elementStorage.IsValidRef(ref)
                        && app.elementStorage.GetElementUnsafe(ref).styleRef == app.theme.nodeViewStyleRef)
                        canvas = app.elementStorage.GetElementUnsafe(ref).id;
            ASSERT_NE(canvas, ID::Invalid());
            auto& scrolling = app.guiStateManager.GetScrollManagement();
            scrolling.SetScrollOffset(scrolling.GetOrCreateScrollOffsetHandle(canvas), int2{ -180, -15 });
            Frame();
            EXPECT_EQ(app.GetElementRect(sleep), before);
            EXPECT_EQ(scrolling.GetScrollOffset(scrolling.GetOrCreateScrollOffsetHandle(canvas)), (int2{ 0, 0 }));
        }

        // Framing the selection takes in the states a selected transition joins.
        TEST_F(NodeViewStateMachineTest, FramingTheSelectionTakesInTransitions)
        {
            const ID t = Transition(sleep, chase);
            Settle();
            view.selectedTransitionIDs = { t };
            NodeView expected = view;
            const int both[] = { NodeIndex(view, sleep), NodeIndex(view, chase) };
            FrameNodes(expected, both);
            FrameSelectedNodes(view);
            EXPECT_EQ(view.transform.TransformPoint({ 0.0f, 0.0f }), expected.transform.TransformPoint({ 0.0f, 0.0f }));
            EXPECT_EQ(view.transform.TransformPoint({ 100.0f, 0.0f }), expected.transform.TransformPoint({ 100.0f, 0.0f }));
        }

        // Labelling one transition, then clicking another's label, keeps what was typed for the
        // first and leaves the second as it was; a double-click there starts on the second.
        TEST_F(NodeViewStateMachineTest, ClickingAnotherLabelKeepsTheFirst)
        {
            const ID a = Transition(sleep, patrol);
            const ID b = Transition(patrol, chase, "sees player");
            Settle();
            ASSERT_EQ(view.transitionLabelBoxes.size(), 1u);
            const IntRect chip = view.transitionLabelBoxes[0].second;
            const int2 onB = Screen({ float(chip.x + chip.w / 2), float(chip.y + chip.h / 2) });

            RenameTransition(view, TransitionIndex(view, a));
            Settle();
            app.InjectText("noise");
            Frame();
            Click(onB);
            EXPECT_FALSE(view.renamingTransition);
            EXPECT_EQ(view.transitions[size_t(TransitionIndex(view, a))].label, "noise");
            EXPECT_EQ(view.transitions[size_t(TransitionIndex(view, b))].label, "sees player");

            RenameTransition(view, TransitionIndex(view, a));
            Settle();
            app.InjectKey(KeyCode::End, KeyAction::KeyDown);
            Frame();
            app.InjectText(" heard");
            Frame();
            Single();
            app.InjectMouseMove(onB);
            Frame();
            app.ClickAt(onB);
            Frame();
            app.ClickAt(onB);
            Settle();
            EXPECT_EQ(view.transitions[size_t(TransitionIndex(view, a))].label, "noise heard") << "the first keeps what was typed";
            EXPECT_EQ(view.transitions[size_t(TransitionIndex(view, b))].label, "sees player");
            EXPECT_EQ(view.renamingTransition, std::optional<ID>{ b });
            EXPECT_EQ(view.renameText, "sees player") << "the second starts from its own";
        }

        // Labelling a transition, then clicking a state: the label keeps what was typed, the
        // field closes, and the state keeps its name.
        TEST_F(NodeViewStateMachineTest, ClickingAStateKeepsTheLabel)
        {
            const ID a = Transition(sleep, patrol);
            Settle();
            RenameTransition(view, TransitionIndex(view, a));
            Settle();
            app.InjectText("noise");
            Frame();
            Click(Centre(chase));
            Settle();
            EXPECT_FALSE(view.renamingTransition) << "the field closes";
            EXPECT_EQ(view.transitions[size_t(TransitionIndex(view, a))].label, "noise");
            EXPECT_EQ(State(chase).title, "Chase");
            EXPECT_EQ(State(sleep).title, "Sleep");
        }

        // A double-click on a state holding a machine opens it; on another state it does nothing.
        TEST_F(NodeViewStateMachineTest, ADoubleClickOpensANestedMachine)
        {
            view.nodes[2].subMachine = true;
            int opened = -1;
            view.onOpenState = [&](int node) { opened = node; };
            Settle();
            auto doubleClick = [&](int2 at) {
                Single();
                app.InjectMouseMove(at);
                Frame();
                app.ClickAt(at);
                Frame();
                app.ClickAt(at);
                Settle();
            };
            doubleClick(Centre(sleep));
            EXPECT_EQ(opened, -1);
            EXPECT_FALSE(State(sleep).collapsed) << "states don't collapse";
            doubleClick(Centre(chase));
            EXPECT_EQ(opened, 2);
        }

        // Removing a state takes its transitions and its being initial with it; undone, all of
        // it comes back.
        TEST_F(NodeViewStateMachineTest, RemovingAStateTakesItsTransitions)
        {
            Transition(sleep, patrol);
            Transition(patrol, sleep);
            const ID kept = Transition(sleep, chase);
            view.initialState = patrol;
            const std::vector<StateTransition> before = view.transitions;

            const int only[] = { 1 };
            const NodeViewEdit edit = RemoveNodesEdit(view, only);
            ApplyNodeViewEdit(view, edit);
            ASSERT_EQ(view.transitions.size(), 1u);
            EXPECT_EQ(view.transitions[0].stableID, kept);
            EXPECT_EQ(view.initialState, ID::Invalid());
            ApplyNodeViewEdit(view, edit, /*undo*/ true);
            EXPECT_EQ(view.transitions, before);
            EXPECT_EQ(view.initialState, patrol);
        }

        // The transition and initial-state edits, done and undone.
        TEST_F(NodeViewStateMachineTest, TransitionEditsUndo)
        {
            const ID a = Transition(sleep, patrol);
            const ID b = Transition(patrol, chase);
            const ID both[] = { a, b };
            const NodeViewEdit removed = RemoveTransitionsEdit(view, both);
            ApplyNodeViewEdit(view, removed);
            EXPECT_TRUE(view.transitions.empty());
            ApplyNodeViewEdit(view, removed, /*undo*/ true);
            ASSERT_EQ(view.transitions.size(), 2u);
            EXPECT_EQ(view.transitions[0].stableID, a);

            const NodeViewEdit initial = SetInitialStateEdit(view, sleep);
            ApplyNodeViewEdit(view, initial);
            EXPECT_EQ(view.initialState, sleep);
            EXPECT_TRUE(SetInitialStateEdit(view, sleep).empty());
            ApplyNodeViewEdit(view, initial, /*undo*/ true);
            EXPECT_EQ(view.initialState, ID::Invalid());

            const NodeViewEdit added = AddTransitionEdit(view, StateTransition{ .stableID = app.idManager.CreateStaticID(), .from = chase, .to = sleep }, 0);
            ApplyNodeViewEdit(view, added);
            EXPECT_EQ(view.transitions[0].from, chase);
            ApplyNodeViewEdit(view, added, /*undo*/ true);
            EXPECT_EQ(view.transitions[0].stableID, a);
        }

        // An arrow is found near its line, a loop near its curve, nothing far from both.
        TEST_F(NodeViewStateMachineTest, TransitionAtFindsTheArrow)
        {
            const ID t = Transition(sleep, patrol);
            const ID self = Transition(chase, chase);
            Settle();
            const int32_t gap = app.styleManager.Px(6), loop = app.styleManager.Px(24);
            const TransitionPath p = StateTransitionPath(view, view.transitions[0], gap, loop);
            const float2 mid{ float(p.from.x + p.to.x) / 2.0f, float(p.from.y + p.to.y) / 2.0f + 3.0f };
            EXPECT_EQ(TransitionAt(view, mid, 5.0f, gap, loop), std::optional<ID>{ t });
            const TransitionPath l = StateTransitionPath(view, view.transitions[1], gap, loop);
            EXPECT_EQ(TransitionAt(view, float2{ float(l.labelAt.x), float(l.labelAt.y) + 2.0f }, 6.0f, gap, loop),
                      std::optional<ID>{ self }) << "at its top, which it reaches as drawn";
            EXPECT_FALSE(TransitionAt(view, float2{ float(l.labelAt.x), float(l.labelAt.y) + float(loop) * 0.5f }, 6.0f, gap, loop))
                << "under its top, inside the loop";
            EXPECT_FALSE(TransitionAt(view, float2{ 900.0f, 900.0f }, 5.0f, gap, loop));
        }
    }
}
