#include "application.h"

#include <gui_controls/gui_node_view_helpers.h>
#include <gui_controls/gui_view_state.h>
#include <gui_core/gui_json.h>   // AddTransitionEdit, SetInitialStateEdit, ...

#include <algorithm>
#include <chrono>
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
        ID Join(IDManager& ids, NodeGraph& view, int from, int to, std::string label)
        {
            const ID id = ids.CreateStaticID();
            view.transitions.push_back({ .stableID = id, .from = view.nodes[size_t(from)].stableID,
                                         .to = view.nodes[size_t(to)].stableID, .label = std::move(label) });
            return id;
        }
        // The showcase's clock, in seconds.
        double Now()
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
    }

    void Application::ApplyStateColors()
    {
        // Each seeded state's color from the palette; a state added later keeps the accent.
        struct StateColor { const char* title; const char* color; };
        static const StateColor colors[] = {
            { "Sleep", "--state-indigo" }, { "Patrol", "--state-teal" }, { "Chase", "--state-amber" },
            { "Hurt", "--state-red" },     { "Combat", "--state-violet" },
            { "Attack", "--state-red" },   { "Block", "--state-indigo" }, { "Dodge", "--state-teal" } };
        for (NodeGraph* machine : { &showcase.enemyMachine, &showcase.combatMachine })
            for (NodeModel& state : machine->nodes)
                for (const StateColor& c : colors)
                    if (state.title == c.title)
                        state.categoryColor = theme.GetColor(c.color);
    }

    void Application::SeedStateMachines()
    {
        // The enemy: asleep until it hears something, then patrols, chases what it sees, and
        // fights once it's close; hurt from patrolling or chasing, it goes back to chasing.
        NodeGraph& enemy = showcase.enemyMachine;
        enemy.kind = NodeViewKind::StateMachine;
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

        // Combat, inside: attack, block, dodge, round and round.
        NodeGraph& combat = showcase.combatMachine;
        combat.kind = NodeViewKind::StateMachine;
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
        ApplyStateColors();
        showcase.stateEnteredAt[0] = showcase.stateEnteredAt[1] = Now();

        // Each edit on the app's undo history, as the node graph's are.
        for (NodeGraph* machine : { &enemy, &combat })
            machine->onEdit = [this, machine](const NodeViewEdit& edit)
            {
                commandManager.History().Push({ .description = "Edit state machine",
                    .undo = [this, machine, edit] { ApplyNodeViewEdit({ *machine, showcase.noView }, edit, /*undo*/ true); },
                    .redo = [this, machine, edit] { ApplyNodeViewEdit({ *machine, showcase.noView }, edit, /*undo*/ false); } });
                commandManager.History().EndMerge();
            };
    }

    // A StateMachine panel's own: its views of the two machines, and which one it shows.
    struct StateMachinePanel
    {
        NodeViewState enemy, combat;
        bool          combatOpen = false;   // Combat's machine is the one shown
    };

    ID Application::CreateStateMachinePanel(std::unordered_map<ID, Panel>& panels, std::string title)
    {
        if (!showcase.stateMachinesSeeded)
        {
            SeedStateMachines();
            showcase.stateMachinesSeeded = true;
        }
        auto panel = std::make_shared<StateMachinePanel>();
        StateMachinePanel* const self = panel.get();
        for (NodeGraph* machine : { &showcase.enemyMachine, &showcase.combatMachine })
        {
            NodeViewState* const view = machine == &showcase.enemyMachine ? &self->enemy : &self->combat;
            view->commandMode = stateMachineMode;
            view->showMinimap = false;
            // A state where the right click was.
            view->onCanvasContextMenu = [this, machine, view](float2 at)
            {
                MenuItem("Add state", [this, machine, view, at] {
                    NodeModel state = State(idManager, "State", { int32_t(at.x), int32_t(at.y) });
                    CommitNodeViewEdit({ *machine, *view }, AddNodeEdit({ *machine, *view }, std::move(state)));
                });
            };
            // The state's: start here, or go.
            view->onNodeContextMenu = [this, machine, view](int node)
            {
                const ID id = machine->nodes[size_t(node)].stableID;
                if (id != machine->initialState)
                    MenuItem("Set as initial state", [machine, view, id] { CommitNodeViewEdit({ *machine, *view }, SetInitialStateEdit({ *machine, *view }, id)); });
                MenuItem({ showcase.deleteIcon, "Delete state" }, "Delete", [this, machine, view] { DeleteSelectedStates({ *machine, *view }); });
            };
        }
        // Into Combat, and back out by the toolbar's breadcrumb.
        self->enemy.onOpenState = [this, self](int node)
        {
            if (showcase.enemyMachine.nodes[size_t(node)].title == "Combat")
                self->combatOpen = true;
        };
        std::erase_if(m_stateMachinePanels, [](const auto& p) { return p.expired(); });   // closed ones
        m_stateMachinePanels.push_back(panel);

        const ID id = CreatePanel(panels, std::move(title), showcase.panelNodeViewIcon, [this, panel] { RenderStateMachinePanel(*panel); });
        // Both machines' views, and which one is open, kept with the layout.
        panels[id].saveState = [panel] {
            JsonValue state = JsonValue::MakeObject();
            state.Set("enemy", SaveViewState(panel->enemy));
            state.Set("combat", SaveViewState(panel->combat));
            state.Set("combatOpen", panel->combatOpen);
            return state.Write();
        };
        panels[id].restoreState = [panel](std::string_view text) {
            const Result<JsonValue> state = JsonValue::Parse(text);
            if (!state || !state->IsObject())
                return;
            if (const JsonValue* enemy = state->Find("enemy"); enemy && enemy->IsString())
                (void)RestoreViewState(panel->enemy, enemy->AsString());
            if (const JsonValue* combat = state->Find("combat"); combat && combat->IsString())
                (void)RestoreViewState(panel->combat, combat->AsString());
            if (const JsonValue* open = state->Find("combatOpen"); open && open->IsBool())
                panel->combatOpen = open->AsBool();
        };
        return id;
    }

    StateMachinePanel* Application::FocusedStateMachine()
    {
        const auto& interaction = guiStateManager.Interaction();
        for (const std::weak_ptr<StateMachinePanel>& weak : m_stateMachinePanels)
            if (const std::shared_ptr<StateMachinePanel> panel = weak.lock();
                panel && interaction.IsFocusWithin((panel->combatOpen ? panel->combat : panel->enemy).canvasID))
                return panel.get();
        return nullptr;
    }

    NodeViewRef Application::FocusedMachineView()
    {
        StateMachinePanel& panel = *FocusedStateMachine();
        return panel.combatOpen ? NodeViewRef{ showcase.combatMachine, panel.combat } : NodeViewRef{ showcase.enemyMachine, panel.enemy };
    }

    void Application::RenderStateMachinePanel(StateMachinePanel& panel)
    {
        const NodeViewRef view = panel.combatOpen ? NodeViewRef{ showcase.combatMachine, panel.combat }
                                                  : NodeViewRef{ showcase.enemyMachine, panel.enemy };
        NodeGraph& machine = view.graph;
        double& enteredAt = showcase.stateEnteredAt[panel.combatOpen ? 1 : 0];
        BeginVertical(theme.emptyVertStyleRef, LayoutOverride{}.FlexGrow(1));

        // The toolbar: Step and Restart, where the machine is, and whether it's running.
        BeginHorizontal(idManager.AllocateDynamicID("state-machine-toolbar"), stateMachineToolbarStyleRef, LayoutOverride::Auto().NoShrink());
        // Step: the machine takes the next transition out of the state it's in, in turn.
        Button(idManager.AllocateDynamicID("state-machine-step"), { showcase.stepIcon, "Step" }, [this, &machine, &enteredAt] {
            std::vector<const StateTransition*> out;
            for (const StateTransition& t : machine.transitions)
                if (t.from == machine.activeState) out.push_back(&t);
            if (out.empty()) return;
            machine.activeState = out[showcase.stateMachineSteps++ % out.size()]->to;
            enteredAt = Now();
        }, stateMachineButtonStyleRef);
        SetTooltip("Take the next transition out of the current state");
        Button(idManager.AllocateDynamicID("state-machine-restart"), { showcase.restartIcon, "Restart" }, [&machine, &enteredAt] {
            machine.activeState = machine.initialState;
            enteredAt = Now();
        }, stateMachineButtonStyleRef);
        SetTooltip("Back to the initial state");
        RegisterElement(Element{ .id = idManager.AllocateDynamicID("state-machine-divider"), .styleRef = stateMachineDividerStyleRef,
                                 .flags = ElementFlags::NoInput }, LayoutOverride{});
        static const std::string enemyPath[]  = { "Enemy" };
        static const std::string combatPath[] = { "Enemy", "Combat" };
        Breadcrumb(idManager.AllocateDynamicID("state-machine-breadcrumb"),
                   panel.combatOpen ? std::span<const std::string>(combatPath) : std::span<const std::string>(enemyPath),
                   [&panel](int segment) { if (segment == 0) panel.combatOpen = false; });
        RegisterElement(Element{ .id = idManager.AllocateDynamicID("state-machine-spacer"), .styleRef = theme.emptyHorzStyleRef,
                                 .flags = ElementFlags::NoInput }, LayoutOverride{}.FlexGrow(1));
        const bool running = machine.activeState != ID::Invalid();
        if (running)
            RegisterElement(Element{ .id = idManager.AllocateDynamicID("state-machine-dot"), .styleRef = statusDotOkStyleRef,
                                     .flags = ElementFlags::NoInput }, LayoutOverride{});
        Text(idManager.AllocateDynamicID("state-machine-status"), running ? "Running" : "Stopped", stateMachineStatusStyleRef);
        EndHorizontal();

        // The time in the current state, counting while it's shown.
        machine.activeStateTime = running ? float(Now() - enteredAt) : -1.0f;
        if (running)
            RequestRedrawIn(0.1f);
        RenderNodeView(view);
        EndVertical();
    }

    void Application::DeleteSelectedStates(NodeViewRef view)
    {
        // The selected states, their transitions with them, and the selected transitions, as
        // one edit.
        std::vector<int> selected;
        for (int n = 0; n < (int)view.graph.nodes.size(); ++n)
            if (view.state.selectedNodeIDs.contains(view.graph.nodes[size_t(n)].stableID))
                selected.push_back(n);
        NodeViewEdit edit = RemoveNodesEdit(view, selected);
        // The selected transitions not already going with their states.
        std::vector<ID> transitions;
        for (const ID id : view.state.selectedTransitionIDs)
            if (std::none_of(edit.transitions.begin(), edit.transitions.end(),
                             [id](const NodeViewEdit::Transition& t) { return t.transition.stableID == id; }))
                transitions.push_back(id);
        // Applied after the states are gone: by then each one's index is as it is without them.
        NodeGraph     afterGraph = view.graph;
        NodeViewState afterView  = view.state;
        ApplyNodeViewEdit({ afterGraph, afterView }, edit);
        const NodeViewEdit removed = RemoveTransitionsEdit({ afterGraph, afterView }, transitions);
        edit.transitions.insert(edit.transitions.end(), removed.transitions.begin(), removed.transitions.end());
        if (view.state.selectedNodeIDs.contains(view.graph.activeState))
            view.graph.activeState = ID::Invalid();
        CommitNodeViewEdit(view, edit);
    }
}
