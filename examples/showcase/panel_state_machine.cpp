#include "application.h"

#include <gui_controls/gui_node_view_helpers.h>   // AddTransitionEdit, SetInitialStateEdit, ...

#include <algorithm>
#include <string>
#include <vector>

//
// Demo panel for a node view as a state machine (NodeViewKind::StateMachine): an enemy's
// behaviour, its Combat state holding a machine of its own. Step runs it along a transition.
//

namespace tachyon
{
    namespace
    {
        NodeModel State(IDManager& ids, std::string title, int2 pos, bool subMachine = false)
        {
            return NodeModel{ .stableID = ids.CreateStaticID(), .title = std::move(title), .pos = pos, .subMachine = subMachine };
        }
        ID Join(IDManager& ids, NodeView& view, int from, int to, std::string label)
        {
            const ID id = ids.CreateStaticID();
            view.transitions.push_back({ .stableID = id, .from = view.nodes[size_t(from)].stableID,
                                         .to = view.nodes[size_t(to)].stableID, .label = std::move(label) });
            return id;
        }
    }

    void Application::SeedStateMachines()
    {
        // The enemy: asleep until it hears something, then patrols, chases what it sees, and
        // fights once it's close; hurt from patrolling or chasing, it goes back to chasing.
        NodeView& enemy = showcase.enemyMachine;
        enemy.kind = NodeViewKind::StateMachine;
        enemy.showMinimap = false;
        enemy.nodes = {
            State(idManager, "Sleep",  { -320, -140 }),
            State(idManager, "Patrol", { 80, -140 }),
            State(idManager, "Chase",  { 80, 100 }),
            State(idManager, "Hurt",   { -320, 100 }),
            State(idManager, "Combat", { 420, 100 }, /*subMachine*/ true),
        };
        Join(idManager, enemy, 0, 1, "noise");
        Join(idManager, enemy, 1, 0, "tired");
        Join(idManager, enemy, 1, 2, "sees player");
        Join(idManager, enemy, 2, 1, "lost player");
        Join(idManager, enemy, 1, 3, "hit");
        Join(idManager, enemy, 0, 3, "hit");
        Join(idManager, enemy, 3, 2, "recovered");
        Join(idManager, enemy, 2, 3, "hit");
        Join(idManager, enemy, 2, 4, "in reach");
        Join(idManager, enemy, 4, 2, "out of reach");
        Join(idManager, enemy, 1, 1, "next waypoint");
        enemy.initialState = enemy.nodes[0].stableID;
        enemy.activeState  = enemy.initialState;
        enemy.breadcrumb   = { "Enemy" };

        // Combat, inside: attack, block, dodge, round and round.
        NodeView& combat = showcase.combatMachine;
        combat.kind = NodeViewKind::StateMachine;
        combat.showMinimap = false;
        combat.nodes = {
            State(idManager, "Attack", { -200, -100 }),
            State(idManager, "Block",  { 160, -100 }),
            State(idManager, "Dodge",  { -20, 120 }),
        };
        Join(idManager, combat, 0, 1, "parried");
        Join(idManager, combat, 1, 0, "opening");
        Join(idManager, combat, 1, 2, "heavy blow");
        Join(idManager, combat, 2, 0, "recovered");
        Join(idManager, combat, 0, 2, "low health");
        combat.initialState = combat.nodes[0].stableID;
        combat.activeState  = combat.initialState;
        combat.breadcrumb   = { "Enemy", "Combat" };

        for (NodeView* view : { &enemy, &combat })
        {
            view->commandMode = stateMachineMode;
            // Each edit on the app's undo history, as the node graph's are.
            view->onEdit = [this, view](const NodeViewEdit& edit)
            {
                commandManager.History().Push({ .description = "Edit state machine",
                    .undo = [view, edit] { ApplyNodeViewEdit(*view, edit, /*undo*/ true); },
                    .redo = [view, edit] { ApplyNodeViewEdit(*view, edit, /*undo*/ false); } });
                commandManager.History().EndMerge();
            };
            // A state where the right click was.
            view->onCanvasContextMenu = [this, view](float2 at)
            {
                MenuItem("Add state", [this, view, at] {
                    NodeModel state = State(idManager, "State", { int32_t(at.x), int32_t(at.y) });
                    CommitNodeViewEdit(*view, AddNodeEdit(*view, std::move(state)));
                });
            };
            // The state's: start here, or go.
            view->onNodeContextMenu = [this, view](int node)
            {
                const ID id = view->nodes[size_t(node)].stableID;
                if (id != view->initialState)
                    MenuItem("Set as initial state", [view, id] { CommitNodeViewEdit(*view, SetInitialStateEdit(*view, id)); });
                MenuItem({ showcase.deleteIcon, "Delete state" }, "Delete", [this] { DeleteSelectedStates(); });
            };
        }
        // Into Combat, and back out by the breadcrumb.
        enemy.onOpenState = [this](int node)
        {
            if (showcase.enemyMachine.nodes[size_t(node)].title == "Combat")
                showcase.combatOpen = true;
        };
        combat.onBreadcrumb = [this](int segment)
        {
            if (segment == 0)
                showcase.combatOpen = false;
        };
    }

    void Application::RenderStateMachinePanel()
    {
        if (!showcase.stateMachinesSeeded)
        {
            SeedStateMachines();
            showcase.stateMachinesSeeded = true;
        }
        NodeView& view = showcase.combatOpen ? showcase.combatMachine : showcase.enemyMachine;
        BeginVertical(theme.emptyVertStyleRef, LayoutOverride{}.FlexGrow(1));
        BeginHorizontalWrap();
        // Step: the machine takes the next transition out of the state it's in, in turn.
        Button(idManager.AllocateDynamicID("state-machine-step"), "Step", [this, &view] {
            std::vector<const StateTransition*> out;
            for (const StateTransition& t : view.transitions)
                if (t.from == view.activeState) out.push_back(&t);
            if (out.empty()) return;
            view.activeState = out[showcase.stateMachineSteps++ % out.size()]->to;
        });
        Button(idManager.AllocateDynamicID("state-machine-restart"), "Restart", [&view] { view.activeState = view.initialState; });
        EndHorizontal();
        RenderNodeView(view);
        EndVertical();
    }

    void Application::DeleteSelectedStates()
    {
        // The selected states, their transitions with them, and the selected transitions, as
        // one edit.
        NodeView& view = showcase.combatOpen ? showcase.combatMachine : showcase.enemyMachine;
        std::vector<int> selected;
        for (int n = 0; n < (int)view.nodes.size(); ++n)
            if (view.selectedNodeIDs.contains(view.nodes[size_t(n)].stableID))
                selected.push_back(n);
        NodeViewEdit edit = RemoveNodesEdit(view, selected);
        // The selected transitions not already going with their states.
        std::vector<ID> transitions;
        for (const ID id : view.selectedTransitionIDs)
            if (std::none_of(edit.transitions.begin(), edit.transitions.end(),
                             [id](const NodeViewEdit::Transition& t) { return t.transition.stableID == id; }))
                transitions.push_back(id);
        // Applied after the states are gone: by then each one's index is as it is without them.
        NodeView after = view;
        ApplyNodeViewEdit(after, edit);
        const NodeViewEdit removed = RemoveTransitionsEdit(after, transitions);
        edit.transitions.insert(edit.transitions.end(), removed.transitions.begin(), removed.transitions.end());
        if (view.selectedNodeIDs.contains(view.activeState))
            view.activeState = ID::Invalid();
        CommitNodeViewEdit(view, edit);
    }
}
