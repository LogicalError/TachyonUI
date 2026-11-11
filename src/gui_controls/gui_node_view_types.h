#pragma once

#include <gui_core/gui_base_types.h>     // int2, float3x2, color4f
#include <gui_core/gui_id.h>
#include <gui_core/input/gui_command_manager.h>          // CommandMode
#include <gui_core/input/gui_gesture.h>
#include <gui_core/graphics/gui_custom_draw_context.h>   // VisualColorStyleIndex

#include <gui_controls/gui_widget_state_types.h>          // DoubleClickTracker

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Node-view (graph editor) data model. The caller owns one NodeView per view.

namespace tachyon
{
    /// @addtogroup controls_nodes
    /// @{

    /// A property on a node. Default-constructed is invalid: in a socket it means "present, unwired".
    struct PropertyEndpoint
    {
        int nodeIdx = -1;   ///< index into NodeView::nodes
        int propIdx = -1;   ///< index into that node's properties

        /// True when both indices are set.
        bool valid() const { return nodeIdx >= 0 && propIdx >= 0; }
        bool operator==(const PropertyEndpoint&) const = default;
    };

    /// One row of a node: an optional input socket, a body, and an optional output socket.
    struct NodeProperty
    {
        /// Left socket, rendered when has_value(); holds the source output feeding it (one at most).
        std::optional<PropertyEndpoint> input;

        /// Right socket, rendered when true. It drives every input that names it as its source.
        bool output = false;

        /// Row body, built between the two sockets; may call any widget (Label, Field, ...).
        std::function<void()> renderBody;

        /// Socket dot colour, also used for wires leaving this output.
        tachyon::color4f socketColor{ 0.9f, 0.9f, 0.9f, 1.0f };

        /// Row height in row pitches, for tall bodies (previews, multi-line). Whole pitches
        /// keep later sockets on the grid.
        int rowSpan = 1;

        /// What its sockets' tooltip says: the pin's name, its type (muted, monospace) and its
        /// value now, asked for only while the tooltip shows; a wired input also says where
        /// its value comes from ("from Noise.Value", the source node's title and this name of
        /// its property). No tooltip when the name, type and value are all empty.
        std::string                  name;
        std::string                  type;
        std::function<std::string()> value;
    };

    /// One node of a node view.
    struct NodeModel
    {
        /// Assigned once on creation; the node's element ID and the salt for its sub-IDs, so
        /// ID-keyed state survives z-order changes (dynamic IDs depend on render order).
        tachyon::ID                    stableID     = tachyon::ID::Invalid();

        std::string                title;        ///< header text
        /// Category name, shown right-aligned in the header.
        std::string                category;
        /// Color of the header's category square; alpha 0 uses the accent.
        tachyon::color4f               categoryColor = {};
        /// Top-left in canvas layout space (whole pixels relative to the canvas, before pan/zoom).
        tachyon::int2                  pos          = {};
        tachyon::int2                  startPos     = {};   ///< internal: pos at drag start
        tachyon::int2                  lastSize     = {};   ///< outer-rect size from last frame's layout
        bool                       collapsed    = false;   ///< rows hidden, header only (double-click toggles)
        /// A reroute: a dot that a wire passes through, a waypoint, instead of a box. Its
        /// first row's input (one wire in) and output (any number out) meet in the dot; no
        /// header or other rows show. `pos` is the top-left of its two-grid-cell square, so
        /// the dot sits on a grid point. See RerouteWireEdit.
        bool                       reroute      = false;
        std::vector<NodeProperty>  properties;              ///< the rows, top to bottom
        /// Internal: inside a collapsed frame, as last drawn: not shown, clicked or selected.
        bool                       hidden       = false;
        /// In a state machine (NodeViewKind::StateMachine): the state holds a machine of its
        /// own. It's drawn with a mark, and a double-click on it calls NodeView::onOpenState.
        bool                       subMachine   = false;
    };

    /// What a node view edits: a graph of nodes with fields wired output to input, or a state
    /// machine, where nodes are states joined by transitions and cycles are the point.
    enum class NodeViewKind : uint8_t
    {
        Graph,          ///< nodes with properties and sockets, wired output to input
        StateMachine    ///< states (nodes without properties) and transitions between them
    };

    /// A transition of a state machine: from one state to another, or to itself.
    struct StateTransition
    {
        tachyon::ID stableID = tachyon::ID::Invalid();   ///< assigned once on creation, as a node's
        tachyon::ID from     = tachyon::ID::Invalid();   ///< the state it leaves, by stableID
        tachyon::ID to       = tachyon::ID::Invalid();   ///< the state it enters (`from` for a loop)
        std::string label;                               ///< shown on its arrow: an event or a condition

        bool operator==(const StateTransition&) const = default;
    };

    /// A frame (a comment box): a titled, colored box behind a group of nodes. Dragging its
    /// title moves it and what lies inside it; its right and bottom edges resize it.
    struct NodeFrame
    {
        /// Assigned once on creation, as a node's: the ID of its elements and of its
        /// selection.
        tachyon::ID      stableID = tachyon::ID::Invalid();
        std::string      title;          ///< shown in its title bar; double-click to rename
        /// Its tint; alpha 0 uses the theme's neutral one. The box is it, faint, and the title
        /// bar it, stronger.
        tachyon::color4f color    = {};
        tachyon::int2    pos      = {};  ///< top-left, in node.pos space
        tachyon::int2    size     = {};  ///< width and height, the title bar included, while expanded
        /// Shown as one node: its title bar, and a row for each wire crossing its edge, an
        /// input on the left for each wire in, an output on the right for each wire out. The
        /// nodes and frames inside it are hidden, and so are the wires between them.
        bool             collapsed = false;
        /// While collapsed: the nodes and frames it held when it collapsed, by stableID. Those
        /// are what it hides and moves; a node dragged over it later stays out. Filled in by
        /// CollapseFrameEdit, or by the view the first time it draws it collapsed; none
        /// while expanded, when what lies inside it is what it holds.
        std::optional<std::vector<tachyon::ID>> members;

        bool operator==(const NodeFrame&) const = default;
    };

    /// A wire being dragged. Committed wires are the reciprocal endpoints on NodeProperty.
    struct PendingConnection
    {
        /// The socket's press. The drag manager decides when it becomes a drag (its threshold),
        /// and the wire is real from then on.
        tachyon::Gesture<> press;
        /// A wire drag is in progress: the press has become a drag.
        bool IsActive() const { return press.Phase() == tachyon::GesturePhase::Dragging; }
        /// The socket the wire starts from: an output, or an unwired input when fromInput.
        PropertyEndpoint source        = {};
        bool             fromInput     = false;  ///< dragged from an input, towards an output
        tachyon::int2        currentScreen = {};   ///< most recent mouse screen pos
        /// This frame: the socket it is over and may connect to (an input, or an output when
        /// fromInput).
        PropertyEndpoint target        = {};
        std::optional<PropertyEndpoint> detachedInput; ///< the input the wire was pulled off, if any
    };

    /// Interior lane a drag handle adjusts: the corridor route's vertical lane (LaneX), or the
    /// detour route's verticals (ExitX, EntryX) and horizontal return lane (LaneY).
    enum class WireHandle : uint8_t { None, LaneX, ExitX, EntryX, LaneY };

    /// A wire's place among the wires between the same two nodes, so their default lanes
    /// nest instead of coinciding or crossing (ComputeWireLaneOffsets).
    struct WireLane
    {
        int32_t offset = 0;   ///< grid steps right for the corridor lane
        int32_t index  = 0;   ///< by input, top first
        int32_t count  = 1;   ///< wires in the group

        bool operator==(const WireLane&) const = default;
    };

    /// User-pinned lanes of one wire (unset = default), in the route's space. BuildWireRoute
    /// clamps them clear of the bodies, so stale values never cross a node.
    struct WireRouteOverrides
    {
        std::optional<int32_t> laneX;    ///< corridor vertical lane X
        std::optional<int32_t> exitX;    ///< detour vertical lane near the output
        std::optional<int32_t> entryX;   ///< detour vertical lane near the input
        std::optional<int32_t> laneY;    ///< detour horizontal return-lane Y
        WireLane               lane;     ///< where the unpinned lanes go in its group

        bool operator==(const WireRouteOverrides&) const = default;
    };

    /// Draggable interior segment; vertical segments drag horizontally and vice versa.
    struct WireSegmentHandle
    {
        tachyon::int2  a{}, b{}; ///< segment endpoints, same space as the route points
        WireHandle role     = WireHandle::None;  ///< which lane dragging it changes
        bool       vertical = false;
    };

    /// Polyline plus its draggable interior segments (never the end stubs).
    struct WireRoute
    {
        std::vector<tachyon::int2>         points;   ///< the polyline, output to input
        std::vector<WireSegmentHandle> handles;
        /// The points are a curved line's four instead: output, two control points, input
        /// (see WireCurvePoint).
        bool                           curved = false;
    };

    /// How a node view draws its wires (NodeView::wireShape).
    enum class NodeWireShape : uint8_t
    {
        Orthogonal,   ///< along the grid, round the nodes, with lanes that drag
        Curved,       ///< one smooth curve from output to input
    };

    /// Last frame's draggable segment (node.pos space), for laying out its grab element.
    /// `wireKey` identifies the wire by its target endpoint.
    struct CachedWireHandle
    {
        uint32_t   wireKey  = 0;
        WireHandle role     = WireHandle::None;
        bool       vertical = false;
        tachyon::int2  a{}, b{};
    };

    /// One thing the user did in a node view, as values before and after, so a host can keep
    /// the undo history (NodeView::onEdit) and undo or redo it (ApplyNodeViewEdit).
    struct NodeViewEdit
    {
        /// A node (index) moved; positions in node.pos space.
        struct Move     { int node = -1; tachyon::int2 from {}, to {}; };
        /// An input's source changed; an invalid endpoint means unwired.
        struct Rewire   { PropertyEndpoint input, from, to; };
        /// A node (index) collapsed or expanded.
        struct Collapse { int node = -1; bool from = false, to = false; };
        /// A wire's (by WireKey) lane overrides changed; default means no overrides.
        struct Lanes    { uint32_t wireKey = 0; WireRouteOverrides from, to; };
        /// A node added at `index` (or, when !added, taken from there), with the wires from
        /// its outputs into other nodes' inputs and the lane overrides of every wire into or
        /// out of it. The nodes after it move up or down one, and every endpoint, WireKey and
        /// z-order entry with them. Endpoints and keys are as they are while it's there.
        struct Node
        {
            int        index = -1;
            bool       added = true;
            NodeModel  node;
            /// Another node's input and the property of this node whose output feeds it.
            std::vector<std::pair<PropertyEndpoint, int>>          fed;
            std::vector<std::pair<uint32_t, WireRouteOverrides>> lanes;   ///< WireKey -> overrides
        };

        /// A frame (index) moved, resized, renamed or recolored: all of it before and after.
        struct FrameChange { int frame = -1; NodeFrame from, to; };
        /// A frame added at `index` (or, when !added, taken from there); the frames after it
        /// move up or down one.
        struct Frame       { int index = -1; bool added = true; NodeFrame frame; };
        /// A transition added at `index` (or, when !added, taken from there); the ones after it
        /// move up or down one.
        struct Transition       { int index = -1; bool added = true; StateTransition transition; };
        /// A transition (index) relabelled or redirected: all of it before and after.
        struct TransitionChange { int index = -1; StateTransition from, to; };
        /// The initial state changed (by stableID; Invalid: none).
        struct Initial          { tachyon::ID from = tachyon::ID::Invalid(), to = tachyon::ID::Invalid(); };

        std::vector<Move>     moves;
        std::vector<Rewire>   rewires;
        std::vector<Collapse> collapses;
        std::vector<Lanes>    lanes;
        /// Applied before the others (so a rewire may name an added node) and undone after.
        std::vector<Node>     nodes;
        std::vector<FrameChange> frameChanges;
        /// Applied before frameChanges, as nodes are, and undone after.
        std::vector<Frame>       frames;
        /// Applied before transitionChanges, and undone after.
        std::vector<Transition>       transitions;
        std::vector<TransitionChange> transitionChanges;
        std::optional<Initial>        initial;

        /// True when nothing changed.
        bool empty() const
        {
            return moves.empty() && rewires.empty() && collapses.empty() && lanes.empty() && nodes.empty()
                && frameChanges.empty() && frames.empty() && transitions.empty() && transitionChanges.empty()
                && !initial;
        }
    };

    /// A wire as drawn this frame, for hit-testing; points in node.pos space.
    struct DrawnWire
    {
        uint32_t                      wireKey = 0;
        std::vector<tachyon::int2> points;
        bool                          curved  = false;   ///< as WireRoute::curved
    };

    /// Segment drag via mouse capture rather than DnD, so the resize cursor sticks.
    struct WireSegmentDrag
    {
        /// The handle's press, a drag from the start; the wire's overrides as they were, for the
        /// edit and for Escape.
        tachyon::Gesture<WireRouteOverrides> press;
        uint32_t   wireKey    = 0;
        WireHandle role       = WireHandle::None;
        bool       vertical   = false;
        int32_t    startValue  = 0;   ///< lane coordinate at drag start (node.pos space)
    };


    /// Which edges of a frame a resize moves.
    enum class FrameEdges : uint8_t { None = 0, Right = 1, Bottom = 2, Corner = Right | Bottom };

    /// A press on a frame's title or one of its edges, and what it moves.
    struct FrameDrag
    {
        tachyon::Gesture<>   press;      ///< a move's press becomes a drag past the drag distance; a resize's at once
        tachyon::ID          frameID = tachyon::ID::Invalid();
        FrameEdges           edges   = FrameEdges::None;   ///< None: a move, by the title
        /// At the press: the frames it moves (it, and those inside it) and the nodes inside
        /// them, by index, as they were.
        std::vector<std::pair<int, NodeFrame>>     frames;
        std::vector<std::pair<int, tachyon::int2>> nodes;
    };

    /// All state of one node-view widget; no global state.
    /// commandMode is active while the view has focus, for per-view shortcuts.
    struct NodeView
    {
        std::vector<NodeModel>  nodes;   ///< the graph; endpoints and edits refer to nodes by index
        /// A graph of fields and wires, or a state machine. In a state machine the nodes are
        /// states: their properties aren't shown, and `transitions` join them instead of wires.
        NodeViewKind            kind = NodeViewKind::Graph;

        /// @name State machine (NodeViewKind::StateMachine)
        ///@{
        /// The arrows between the states. A press on a state's edge and a drag to another
        /// state (or back onto itself) adds one; each can be selected, relabelled and deleted.
        std::vector<StateTransition>    transitions;
        tachyon::ID                     initialState = tachyon::ID::Invalid();   ///< where the machine starts: marked with an arrow in
        /// The state the machine is in, as the app runs it: highlighted. Invalid: none.
        tachyon::ID                     activeState  = tachyon::ID::Invalid();
        std::unordered_set<tachyon::ID> selectedTransitionIDs;   ///< selected transition stableIDs
        /// A double-click on a state that holds a machine of its own (NodeModel::subMachine):
        /// open it, typically by showing that machine's view and extending `breadcrumb`.
        std::function<void(int node)>   onOpenState;
        /// As onNodeContextMenu, for transition `transition` (an index into transitions):
        /// a right click on its arrow or label opens it and selects the transition.
        std::function<void(int transition)> onTransitionContextMenu;
        ///@}
        /// Frames, drawn behind the nodes and wires, the first at the back.
        std::vector<NodeFrame>  frames;
        /// Selected frame stableIDs. A click on a frame's title selects it as one on a node
        /// does; selecting is all it does to it.
        std::unordered_set<tachyon::ID> selectedFrameIDs;
        /// As onNodeContextMenu, for frame `frame` (an index into frames): a right click on
        /// its title opens it and selects the frame.
        std::function<void(int frame)>  onFrameContextMenu;

        /// Breadcrumb overlay at the canvas top-left, shown when non-empty.
        std::vector<std::string>        breadcrumb;
        std::function<void(int)>        onBreadcrumb;   ///< called with the clicked segment index
        bool                            showZoomControls = true;   ///< zoom overlay, bottom-left
        bool                            showMinimap      = true;   ///< minimap overlay, bottom-right
        /// The zoom range (1 = 100%) the wheel, the keys and the zoom controls stay in. Text
        /// is drawn at the zoom's size, so it stays sharp above 100%.
        float                           minZoom          = 0.1f;
        float                           maxZoom          = 4.0f;
        /// Lays out (and calls renderBody for) only the nodes in or near view: one far out
        /// of view stands in as its box, at its last size, with its sockets, so wires,
        /// the minimap and selection still have it. Tab still reaches its controls: it's
        /// laid out as the focus moves into it. Off: every node, every frame.
        bool                            virtualizeLayout = true;
        /// Keeps node positions on the grid: dragged nodes land on it, and positions set
        /// in code are moved onto it. Off: nodes go where they're dropped or put.
        bool                            snapToGrid       = true;
        /// Orthogonal: wires run along the grid, round the nodes, their lanes dragged by
        /// hand. Curved: each wire is one curve, leaving the output and entering the input
        /// level, with no lanes to drag.
        NodeWireShape                   wireShape        = NodeWireShape::Orthogonal;

        /// Last frame's minimap mapping (canvas = origin + minimap pixels / scale), for clicks.
        float                           minimapScale  = 0.0f;
        tachyon::float2              minimapOrigin {};

        PendingConnection       pendingConnection;   ///< internal: the wire being dragged
        tachyon::float3x2           transform;                  ///< identity = centered view (no pan/zoom)
        tachyon::int2               lastInnerSize{ 0, 0 };      ///< last frame's canvas size, for centering
        /// Last frame's canvas padding box, in screen pixels (as events have them), for
        /// NodeViewScreenToCanvas and NodeViewCanvasToScreen.
        tachyon::IntRect            lastCanvasRect {};
        /// Last frame's canvas border box less its content box: node.pos is measured from the
        /// border box, NodeViewScreenToCanvas's points from the content box.
        tachyon::int2               lastGridOrigin {};
        tachyon::ID                 revealedFocusID = tachyon::ID::Invalid();   ///< internal: the focus last scrolled into view
        tachyon::ID                 focusedNodeID   = tachyon::ID::Invalid();   ///< internal: the node holding the focus last frame
        /// Command mode set while the canvas has focus.
        CommandMode             commandMode = CommandMode::Default;
        /// Z-order, last == front (a clicked node moves there).
        std::vector<int>        ordering;

        /// Selected node stableIDs.
        std::unordered_set<tachyon::ID>     selectedNodeIDs;
        /// Called when the selection changed, once per RenderNodeView that finds it changed:
        /// by the user (clicks, the marquee, keys), a removed node, or the app itself. Read
        /// selectedNodeIDs for what it is now.
        std::function<void()>               onSelectionChanged;
        /// Goes up by one each time onSelectionChanged would be called, for an app that
        /// would rather compare it than be called.
        uint32_t                            selectionGeneration = 0;
        std::unordered_set<tachyon::ID>     reportedSelection;   ///< internal: the selection last reported
        std::unordered_set<tachyon::ID>     reportedFrameSelection;   ///< internal: the frames selected last reported
        /// StableID of the last clicked node.
        tachyon::ID                         lastClickedNodeID = tachyon::ID::Invalid();

        /// Double-click-to-collapse, per view so views (and test frames) don't share it.
        DoubleClickTracker              nodeDoubleClick;

        /// What a marquee starts from: the selection before it, put back by Escape, and the
        /// modifiers held at its press.
        struct MarqueeStart
        {
            std::unordered_set<tachyon::ID> nodes;    ///< the nodes selected
            std::unordered_set<tachyon::ID> frames;   ///< the frames selected
            bool shift = false;
            bool ctrl  = false;
        };
        /// A press on the empty background: a marquee once it moves the drag distance. Its
        /// points are screen space (marquee.drag.pressScreen is the start).
        tachyon::Gesture<MarqueeStart>      marquee;
        tachyon::int2                       marqueeCurrentScreen {};

        /// Pinned wire lanes, keyed by WireKey() of the wire's target endpoint.
        std::unordered_map<uint32_t, WireRouteOverrides> wireOverrides;
        /// Last frame's draggable segments, since grab elements are laid out before routing.
        std::vector<CachedWireHandle>                    wireHandleCache;
        WireSegmentDrag                                  wireDrag;

        /// The wire under the mouse (by WireKey), from the last frame: highlighted, removed by
        /// a middle click, and offered for deletion by a right click.
        tachyon::HoverPart<std::optional<uint32_t>> hoveredWire;
        /// Called once per finished user edit (a whole drag is one), after it is applied.
        std::function<void(const NodeViewEdit&)> onEdit;
        /// A new wire was released over the empty canvas (not on a node), e.g. to offer the
        /// nodes it could connect to. `from` is the socket it was dragged from: an output when
        /// `fromOutput`, else an unwired input. `canvasPos` is where, in node.pos space. A wire
        /// pulled off an input and dropped there is a disconnect (onEdit), not this.
        std::function<void(PropertyEndpoint from, bool fromOutput, float2 canvasPos)> onWireDroppedOnCanvas;
        std::optional<uint32_t> wireMenuWire;          ///< the wire the open context menu is for
        float2                  wireMenuPos {};        ///< internal: where on the canvas it was opened
        /// Builds the canvas's context menu (MenuItem and the like), each frame while it's
        /// open; without it there's none. It opens on a right click (not a pan) on the empty
        /// canvas, or on the Menu key or Shift+F10 with the canvas focused and no node
        /// selected. `canvasPos` is where, in node.pos space (the canvas's centre from the keys).
        std::function<void(float2 canvasPos)> onCanvasContextMenu;
        /// As onCanvasContextMenu, for node `node` (an index into nodes): opened by a right
        /// click on it, which selects it if it wasn't, or from the keys while one is selected
        /// (the one clicked last). Not for a reroute, whose menu is the view's own.
        std::function<void(int node)>         onNodeContextMenu;
        std::optional<float2>      canvasMenuPos;      ///< internal: where the open canvas menu is for
        std::optional<tachyon::ID> nodeMenuNode;       ///< internal: the stableID the open node menu is for
        std::optional<tachyon::ID> frameMenuFrame;     ///< internal: the stableID the open frame menu is for
        FrameDrag                  frameDrag;          ///< internal: a frame being moved or resized
        DoubleClickTracker         frameDoubleClick;   ///< internal: a double-click on a title renames
        std::optional<tachyon::ID> renamingFrame;      ///< internal: the frame whose title is being edited
        std::string                renameText;         ///< internal: its title as edited so far
        bool                       renameFocused = false;   ///< internal: the field has had the focus
        int32_t                    gridSize = 0;       ///< internal: the grid the view was last drawn with
        /// Internal: each frame's box as last drawn, by index (node.pos space): a collapsed one's
        /// node-like box, an empty rect for one hidden in a collapsed frame.
        std::vector<tachyon::IntRect> frameBoxes;
        /// Internal: the view dragged (middle, or Alt + left); once dragged, not a click. The
        /// transform before it, put back by Escape.
        tachyon::Gesture<tachyon::float3x2> pan;
        tachyon::Gesture<>   menuPress;           ///< internal: a right press; its release without a drag opens a menu
        tachyon::Gesture<>   nodeDrag;            ///< internal: the press on a node; moves the selection once dragged
        /// Internal: a press in the minimap; the transform before it, put back by Escape.
        tachyon::Gesture<tachyon::float3x2> minimapDrag;

        /// Internal: a transition being drawn from a state's edge, the state it leaves, and
        /// where the pointer is (screen).
        tachyon::Gesture<>   transitionDrag;
        tachyon::ID          transitionFrom = tachyon::ID::Invalid();
        tachyon::int2        transitionPointer {};
        /// Internal: a transition started from a menu (StartTransition): it follows the pointer
        /// until a click on a state finishes it, or a click elsewhere or Escape drops it.
        bool                 transitionPicking = false;
        /// Internal: the state whose edge is under the pointer (a transition starts there).
        tachyon::HoverPart<tachyon::ID> stateEdgeHover;
        /// Internal: the transition under the pointer, from the last frame: highlighted, and
        /// what a click selects. Its arrows as last drawn, for hit tests.
        tachyon::HoverPart<std::optional<tachyon::ID>> hoveredTransition;
        DoubleClickTracker         transitionClicks;    ///< internal: a double-click on a transition relabels it
        /// Internal: each transition's label chip as last laid out (node.pos space): a click on
        /// it is a click on its transition.
        std::vector<std::pair<tachyon::ID, tachyon::IntRect>> transitionLabelBoxes;
        std::optional<tachyon::ID> renamingTransition;  ///< internal: the transition whose label is being edited
        std::optional<tachyon::ID> transitionMenuTransition;   ///< internal: the transition the open menu is for

        /// Whether an output -> input wire is allowed; null allows any. Called only for
        /// bounds-checked, cross-node endpoints, so it may index view.nodes directly.
        std::function<bool(const NodeView& view,
                           PropertyEndpoint output,
                           PropertyEndpoint input)> canConnect;

        /// Packed RGBA -> registered color style, since RenderPolyline takes a style index.
        std::unordered_map<uint32_t, tachyon::VisualColorStyleIndex> wireColorStyles;
        uint32_t wireColorStylesGeneration = 0; ///< StyleManager::Generation() of the cache
    };
    /// @}
}
