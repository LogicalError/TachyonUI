#include "application.h"
#include <gui_controls/gui_tree_utils.h>

#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

//
// This file is for testing and prototyping the VirtualTreeView control.
// Data is a flat std::vector<TreeViewRow> with a depth field; reorders call
// RebuildTreeViewIndex to refresh the derived fields.
//

namespace tachyon
{

    namespace {

    constexpr uint32_t kHierarchyPayloadType = 0x54524545; // 'TREE'
    constexpr int      kVizColWidth     = 24;
    constexpr uint32_t kVizPayloadType  = 0x56495A21; // 'VIZ!' - paint-drag, not item reorder

    } // namespace

    // A Hierarchy panel's data: made the first time it shows, kept while it's hidden.
    struct HierarchyPanel
    {
        IDManager* ids = nullptr;   // the rows' static IDs come from it, and go back to it

        ~HierarchyPanel()
        {
            if (ids)
                for (const tachyon::TreeViewRow& row : treeRows)
                    ids->ReleaseStaticID(row.id);
        }

        std::vector<tachyon::TreeViewRow> treeRows;
        std::vector<int>                  treeVisibleIdx;
        tachyon::TableState               treeViewState;
        tachyon::MultiSelectState         treeMultiSelect;
        std::string                       renameBuffer; // TreeCell rename scratch

        // Search: a filtered copy of treeRows (matches plus ancestors, forced open),
        // rebuilt when the query changes. Rendered with its own state, no drag-drop
        // or rename.
        std::vector<tachyon::TreeViewRow> filteredRows;
        std::vector<int>                  filteredVisibleIdx;
        tachyon::TableState               filteredState;
        tachyon::MultiSelectState         filteredMultiSelect;
        std::string                       hierarchySearch;
        bool                              filterDirty = true;

        tachyon::ItemDragDropConfig hierDnD;
        int pendingSelectFlat = -1;

        // ---- Per-node visibility ("eye") state -----------------------------------
        // Only leaves store state (in hiddenLeaves == invisible); a branch's state
        // derives from its leaves. Keyed by static row ID so it survives reorders.
        // idToFlat maps into the master treeRows, so it also works while filtered.
        std::unordered_set<tachyon::ID>      hiddenLeaves;
        std::unordered_map<tachyon::ID, int> idToFlat;

        enum class VizAgg { On, Off, Mixed };

        void RebuildHierarchyIdToFlat()
        {
            idToFlat.clear();
            idToFlat.reserve(treeRows.size());
            for (int i = 0; i < int(treeRows.size()); ++i)
                idToFlat[treeRows[i].id] = i;
        }

        // Aggregate visibility of the subtree rooted at `id`, over its leaf
        // descendants (a leaf is its own subtree). All visible -> On, all
        // invisible -> Off, otherwise -> Mixed. Unknown / empty -> On.
        VizAgg HierComputeAgg(tachyon::ID id) const
        {
            auto it = idToFlat.find(id);
            if (it == idToFlat.end()) return VizAgg::On;
            const int i = it->second;
            if (i < 0 || i >= int(treeRows.size())) return VizAgg::On;
            const int last = i + treeRows[i].skipCount; // inclusive subtree end
            bool anyVis = false, anyInv = false;
            for (int j = i; j <= last && j < int(treeRows.size()); ++j)
            {
                if (treeRows[j].HasChildren()) continue; // only leaves store state
                if (hiddenLeaves.count(treeRows[j].id)) anyInv = true;
                else                                    anyVis = true;
                if (anyVis && anyInv) return VizAgg::Mixed;
            }
            return anyInv ? VizAgg::Off : VizAgg::On;
        }

        // Apply a visibility to every leaf in the subtree rooted at `id`.
        void HierApplyViz(tachyon::ID id, bool makeVisible)
        {
            auto it = idToFlat.find(id);
            if (it == idToFlat.end()) return;
            const int i = it->second;
            if (i < 0 || i >= int(treeRows.size())) return;
            const int last = i + treeRows[i].skipCount;
            for (int j = i; j <= last && j < int(treeRows.size()); ++j)
            {
                if (treeRows[j].HasChildren()) continue;
                if (makeVisible) hiddenLeaves.erase(treeRows[j].id);
                else             hiddenLeaves.insert(treeRows[j].id);
            }
        }
    };

    ID Application::CreateHierarchyPanel(std::unordered_map<ID, Panel>& panels, std::string title)
    {
        auto panel = std::make_shared<HierarchyPanel>();
        return CreatePanel(panels, std::move(title), showcase.panelHierarchyIcon, [this, panel] { RenderHierarchyPanel(*panel); }, {},
                           [this, panel] { EnableHierarchyPanel(*panel); });
    }

    void Application::EnableHierarchyPanel(HierarchyPanel& p)
    {
        if (!p.treeRows.empty())
            return;   // made already: its order and what's hidden stay
        p.ids = &idManager;

        // ---- Dataset ------------------------------------------------------
        auto add = [&](const char* name, int depth, IconID icon, bool isOpen) {
            TreeViewRow r{};
            r.id     = idManager.CreateStaticID();
            r.name   = name;
            r.depth  = depth;
            r.icon   = icon;
            r.isOpen = isOpen;
            r.renameBuffer = &p.renameBuffer;
            p.treeRows.push_back(std::move(r));
        };
        add("Scene",     0, showcase.folderOpenIcon, true);
        add("Meshes",    1, showcase.object2Icon, true);
        add("Cube",      2, showcase.objectIcon,   false);
        add("Meshes",    2, showcase.object2Icon, true);
        add("Cube",      3, showcase.objectIcon,   false);
        add("Sphere",    3, showcase.objectIcon,   false);
        add("Plane",     3, showcase.objectIcon,   false);
        add("Sphere",    2, showcase.objectIcon,   false);
        add("Plane",     2, showcase.objectIcon,   false);
        add("Camera",    1, showcase.objectIcon,  false);
        add("Light",     1, showcase.objectIcon,  false);
        add("Materials", 0, showcase.folderOpenIcon, false);
        add("Default",   1, showcase.objectIcon, false);
        add("Metal",     1, showcase.objectIcon, false);
        add("Glass",     1, showcase.objectIcon, false);
        add("Settings",  0, showcase.folderOpenIcon, false);
        RebuildTreeViewIndex(p.treeRows);
        p.RebuildHierarchyIdToFlat();
        p.treeViewState.multiSelect = &p.treeMultiSelect;
        p.filterDirty = true;

        // ---- Drag-drop wiring ----------------------------------------------
        // The lambdas are referenced by handlers that fire end-of-frame; the
        // panel they capture outlives construction.
        p.hierDnD.payloadType = kHierarchyPayloadType;
        p.hierDnD.getPayload  = [&p](tachyon::ID rowID) -> uintptr_t {
            if (rowID.GetMode() != tachyon::IDMode::SubRange) return UINTPTR_MAX;
            const int visIdx = static_cast<int>(rowID.value.subRangeParts.index);
            if (visIdx < 0 || visIdx >= int(p.treeVisibleIdx.size())) return UINTPTR_MAX;
            return static_cast<uintptr_t>(p.treeVisibleIdx[visIdx]);
        };
        p.hierDnD.visualizer = [this, &p](const tachyon::DragVisualContext& visual) {
            if (visual.sourceID.GetMode() != tachyon::IDMode::SubRange) return;
            const int visIdx = static_cast<int>(visual.sourceID.value.subRangeParts.index);
            if (visIdx < 0 || visIdx >= int(p.treeVisibleIdx.size())) return;
            const int flatIdx = p.treeVisibleIdx[visIdx];
            if (flatIdx < 0 || flatIdx >= int(p.treeRows.size())) return;
            const IconID icon = p.treeRows[flatIdx].icon != IconID::Invalid
                ? p.treeRows[flatIdx].icon : showcase.circleIcon;
            if (BeginCursorGhostWindow(showcase.dragGhostWindowID, visual.screenPos))
            {
                AddGhostIcon(icon);
            }
            EndGhostWindow();
        };
        p.hierDnD.onReorder = [this, &p](uintptr_t srcPayload, tachyon::ID targetID,
                                   TreeDropPosition pos) {
            if (targetID.GetMode() != tachyon::IDMode::SubRange) return;
            if (srcPayload == UINTPTR_MAX) return;
            const int targetVis = static_cast<int>(targetID.value.subRangeParts.index);
            if (targetVis < 0 || targetVis >= int(p.treeVisibleIdx.size())) return;
            const int srcDs = static_cast<int>(srcPayload);
            const int dstDs = p.treeVisibleIdx[targetVis];
            const int newFlatIdx = TreeUtils::ReorderTreeRow(p.treeRows, srcDs, dstDs, pos);
            if (newFlatIdx < 0) return;
            RebuildTreeViewIndex(p.treeRows);
            p.RebuildHierarchyIdToFlat();
            // Deferred: the matching sub-range rowID needs next frame's
            // row IDs.
            p.pendingSelectFlat = newFlatIdx;
            p.filterDirty = true;
            p.treeViewState.selectedRowID        = tachyon::ID::Invalid();
            p.treeMultiSelect.selectedIDs.clear();
            p.treeMultiSelect.lastClickedID       = tachyon::ID::Invalid();
            guiStateManager.Interaction().SetFocusID(tachyon::ID::Invalid());
            guiStateManager.TextEditorFocus().Clear();
            transitionManager.Clear();
        };
        p.treeViewState.rowDragDrop = &p.hierDnD;
    }

    void Application::RenderHierarchyPanel(HierarchyPanel& p)
    {
        // ---- Search box (top of panel) -----------------------------------
        BeginHorizontal(idManager.AllocateDynamicID("hierarchySearchRow"),
                        theme.searchRowStyleRef, tachyon::LayoutOverride::Auto().NoShrink());
        if (SearchField(p.hierarchySearch, showcase.findIcon, TextEditBehavior::ContinuousUpdate,
                        tachyon::LayoutOverride{}.FlexGrow(1)))
            p.filterDirty = true;
        EndHorizontal();

        const bool searching = !p.hierarchySearch.empty();

        // Refilter on demand: matched rows plus all their ancestors, ancestors
        // forced open so matches under collapsed parents show. Depths are
        // preserved, so the copy is a valid forest for RebuildTreeViewIndex.
        if (searching && p.filterDirty)
        {
            const std::string q = SearchToLower(p.hierarchySearch);
            std::vector<char> keep(p.treeRows.size(), 0);
            std::vector<int>  chain; // chain[d] = index of the current ancestor at depth d
            for (int i = 0; i < int(p.treeRows.size()); ++i)
            {
                chain.resize(p.treeRows[i].depth);
                chain.push_back(i);
                if (SearchMatches(p.treeRows[i].name, q))
                    for (int a : chain) keep[a] = 1;
            }
            p.filteredRows.clear();
            for (int i = 0; i < int(p.treeRows.size()); ++i)
            {
                if (!keep[i]) continue;
                TreeViewRow r  = p.treeRows[i];
                r.isOpen       = true;
                r.renameBuffer = nullptr; // copies don't write back - no rename
                p.filteredRows.push_back(std::move(r));
            }
            RebuildTreeViewIndex(p.filteredRows);
            p.filteredState.multiSelect = &p.filteredMultiSelect;
            p.filterDirty = false;
        }

        // ---- Render the tree ---------------------------------------------
        // The eye is as high as the row's text line, so its icon showing or not doesn't
        // change the row's height.
        const int32_t eyeHeight = textSystem->GetTextMeasurement(
            textSystem->RegisterText(tachyon::ID::Invalid(), "Mg", hierarchyVizCellStyleRef, tachyon::ElementState::None)).minHeight;
        // A visibility "eye" column, then the tree. Built on VirtualTable directly
        // to add the leading column. Neither column has a sizer, so the on-stack
        // `cols` array needn't outlive the VirtualTable call.
        auto renderTree = [&](tachyon::ID id,
                              std::vector<TreeViewRow>& rows,
                              std::vector<int>& visibleIdx,
                              TableState& state)
        {
            WalkTreeViewVisibility(rows, visibleIdx);
            bool anyExpandable = false;
            for (TreeViewRow& r : rows)
            {
                if (r.HasChildren()) anyExpandable = true;
                if (r.icon == showcase.folderOpenIcon || r.icon == showcase.folderClosedIcon)
                    r.icon = (r.HasChildren() && r.isOpen) ? showcase.folderOpenIcon : showcase.folderClosedIcon;
            }

            VirtualTableColumn cols[2];

            // Col 0 - visibility toggle. Shows the "on" eye only on hover (so
            // a fully-visible tree shows no icons); "off"/"mixed" show always.
            cols[0].column = TableColumn{ .header = {},
                .initialWidth = kVizColWidth, .minWidth = kVizColWidth,
                .maxWidth = kVizColWidth, .sizing = ColumnSizing::Fixed,
                .interactiveContent = true,
                .columnBackground = hierarchyVizBgStyleRef };
            cols[0].generateCell = [this, &p, &rows, &visibleIdx, &state, eyeHeight](int rowIdx)
            {
                if (rowIdx < 0 || rowIdx >= int(visibleIdx.size())) return;
                const tachyon::ID nodeId = rows[visibleIdx[rowIdx]].id;
                const tachyon::ID rowID  = state.RowIDInRange(rowIdx);
                const HierarchyPanel::VizAgg agg = p.HierComputeAgg(nodeId);
                Cell([this, &p, nodeId, rowID, agg, eyeHeight]()
                {
                    // The column is interactiveContent, so the table makes this cell part of
                    // the row: hovering the eye highlights the row, and the eye keeps its own
                    // ID so clicks reach its toggle handler.
                    tachyon::Element wrap{};
                    wrap.id       = idManager.AllocateDynamicID("hierVizBox");
                    wrap.styleRef = hierarchyVisibilityStyleRef;
                    wrap.state    = guiStateManager.GetCurrentState(rowID);   // drawn with the row
                    auto wRes = elementStorage.Add(std::move(wrap), tachyon::LayoutOverride::Auto().FlexGrow(1).Height(Pixels{ eyeHeight }));
                    VERIFY(wRes.status);
                    VERIFY(windowRegistry.Add(wRes.ref));
                    VERIFY(elementStorage.BeginChildren());

                    // Stable parent so the eye's id can't drift with the visible row count.
                    (void)idManager.PushParent(rowID);
                    const tachyon::ID vizID = idManager.AllocateDynamicID("hierViz");
                    const tachyon::ElementState st = guiStateManager.GetCurrentState(vizID);
                    const bool hovered = (st & tachyon::ElementState::Hover) != tachyon::ElementState::None;
                    tachyon::IconID icon = tachyon::IconID::Invalid;
                    switch (agg)
                    {
                        case HierarchyPanel::VizAgg::On:    icon = hovered ? showcase.visibilityOnIcon : tachyon::IconID::Invalid; break;
                        case HierarchyPanel::VizAgg::Off:   icon = showcase.visibilityOffIcon;   break;
                        case HierarchyPanel::VizAgg::Mixed: icon = showcase.visibilityMixedIcon; break;
                    }
                    tachyon::Element el{};
                    el.id       = vizID;
                    el.state    = st;
                    el.styleRef = hierarchyVisibilityStyleRef;
                    if (icon != tachyon::IconID::Invalid)
                        el.textID = textSystem->RegisterText(vizID, icon, "", el.styleRef, el.state);
                    // Click toggles (hidden->show, otherwise hide); dragging paints that
                    // value over every row crossed (a VIZ-payload drag, not a reorder).
                    el.eventHandler = [this, &p, nodeId](tachyon::ElementEvent&& ev) -> bool
                    {
                        switch (ev.type)
                        {
                            case tachyon::EventType::MouseDown:
                                if (!ev.state.LeftPressed()) break;
                                {
                                    const bool makeVisible = (p.HierComputeAgg(nodeId) == HierarchyPanel::VizAgg::Off);
                                    p.HierApplyViz(nodeId, makeVisible);
                                    guiStateManager.Drag().Begin(ev.id, kVizPayloadType,
                                        makeVisible ? 1u : 0u, ev.state.screenPos);
                                }
                                return true;
                            case tachyon::EventType::MouseMove:
                                if ((ev.state.buttons & tachyon::MouseButtons::Left) == tachyon::MouseButtons::None) break;
                                guiStateManager.Drag().UpdateFromInput(ev.state);
                                return guiStateManager.Drag().GetDraggedID() == ev.id;
                            case tachyon::EventType::DragEnter:
                            case tachyon::EventType::DragOver:
                                if (ev.state.dragPayloadType != kVizPayloadType) return false;
                                p.HierApplyViz(nodeId, ev.state.dragPayloadValue != 0);
                                return true;
                            default: break;
                        }
                        return false;
                    };
                    auto res = elementStorage.Add(std::move(el), tachyon::LayoutOverride::Auto().FlexGrow(1).Height(Pixels{ eyeHeight }));
                    VERIFY(res.status);
                    VERIFY(windowRegistry.Add(res.ref));

                    (void)idManager.PopParent();
                    VERIFY(elementStorage.EndChildren());
                });
            };

            // Col 1 - the tree (mirrors VirtualTreeView's single column).
            cols[1].column = TableColumn{ .header = {} };
            cols[1].generateCell = [this, &rows, &visibleIdx, anyExpandable](int rowIdx)
            {
                if (rowIdx < 0 || rowIdx >= int(visibleIdx.size())) return;
                TreeViewRow& r = rows[visibleIdx[rowIdx]];
                TreeCell(r.name, r.depth,
                         r.HasChildren() ? &r.isOpen : nullptr,
                         r.icon, r.ancestorLineMask, r.isLastSibling,
                         r.renameBuffer, anyExpandable);
            };

            // tableFill (flex-basis:0 + grow:1) bounds the table to the remaining
            // panel height so its body scrolls; flex-basis:auto would grow to fit
            // every row and never scroll.
            state.showHeaders = false;
            state.cellStyle   = hierarchyVizCellStyleRef;
            VirtualTable(id, state, std::span<VirtualTableColumn>(cols, 2),
                         int(visibleIdx.size()), 0, theme.tableFillStyleRef,
                         tachyon::LayoutOverride::Auto().FlexGrow(1).MinHeight(0));
        };

        if (searching)
        {
            // Filtered view: own state, no drag-drop (a reorder of the copy
            // couldn't be mapped back unambiguously). Visibility still works -
            // aggregation/application run over the master tree by node ID.
            renderTree(idManager.AllocateDynamicID(),
                       p.filteredRows, p.filteredVisibleIdx, p.filteredState);
            return;
        }

        renderTree(idManager.AllocateDynamicID(),
                   p.treeRows, p.treeVisibleIdx, p.treeViewState);

        // Resolve a deferred post-drop selection now that VirtualTable has
        // (re)allocated the row IDs for this frame's row count.
        if (p.pendingSelectFlat >= 0
            && p.treeViewState.RowIDInRange(0) != tachyon::ID::Invalid())
        {
            const int targetFlat = p.pendingSelectFlat;
            int newVisIdx = -1;
            for (size_t i = 0; i < p.treeVisibleIdx.size(); ++i)
                if (p.treeVisibleIdx[i] == targetFlat) { newVisIdx = int(i); break; }
            if (newVisIdx >= 0)
            {
                const tachyon::ID newRowID = p.treeViewState.RowIDForIndex(newVisIdx);
                p.treeViewState.selectedRowID = newRowID;
                p.treeMultiSelect.selectedIDs.clear();
                p.treeMultiSelect.selectedIDs.insert(newRowID);
                p.treeMultiSelect.lastClickedID = newRowID;
            }
            p.pendingSelectFlat = -1;
        }
    }

}
