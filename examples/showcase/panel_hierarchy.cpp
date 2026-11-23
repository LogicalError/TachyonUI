#include "application.h"
#include <gui_controls/gui_tree_utils.h>

#include <algorithm>
#include <format>
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

    // The scene's objects, which every Hierarchy panel shows: made when the first one shows,
    // kept while any of them is open.
    struct HierarchyScene
    {
        IDManager* ids = nullptr;   // the rows' static IDs come from it, and go back to it

        ~HierarchyScene()
        {
            if (ids)
                for (const tachyon::TreeViewRow& row : treeRows)
                    ids->ReleaseStaticID(row.id);
        }

        std::vector<tachyon::TreeViewRow> treeRows;
        std::string                       renameBuffer; // TreeCell rename scratch
        std::vector<tachyon::ID>          startOpen;    // the rows a new panel opens
        // Raised by each change to the rows: a panel showing a filtered copy makes it again.
        uint32_t                          revision = 0;

        // Loaded after the panel opens: rows still loading show as placeholders, and the row
        // streaming in says so at its end.
        std::unordered_set<tachyon::ID> loadingRows;
        tachyon::ID                     streamingRow = tachyon::ID::Invalid();

        // ---- Per-node visibility ("eye") state -----------------------------------
        // Only leaves store state (in hiddenLeaves == invisible); a branch's state
        // derives from its leaves. Keyed by static row ID so it survives reorders.
        // idToFlat maps into treeRows, so it also works while a panel shows a filtered copy.
        std::unordered_set<tachyon::ID>      hiddenLeaves;
        std::unordered_map<tachyon::ID, int> idToFlat;

        enum class VizAgg { On, Off, Mixed };

        // After rows were added, removed or moved.
        void RowsChanged()
        {
            GUIApplication::RebuildTreeViewIndex(treeRows);
            RebuildHierarchyIdToFlat();
            ++revision;
        }

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

        // Deletes the nodes and everything under them; false when none of them is here.
        bool DeleteNodes(std::span<const tachyon::ID> nodes)
        {
            std::vector<char> drop(treeRows.size(), 0);
            for (tachyon::ID node : nodes)
            {
                auto it = idToFlat.find(node);
                if (it == idToFlat.end()) continue;
                const int last = std::min(it->second + treeRows[it->second].skipCount, int(treeRows.size()) - 1);
                std::fill(drop.begin() + it->second, drop.begin() + last + 1, char(1));
            }
            size_t kept = 0;
            for (size_t i = 0; i < treeRows.size(); ++i)
            {
                if (drop[i])
                {
                    hiddenLeaves.erase(treeRows[i].id);
                    loadingRows.erase(treeRows[i].id);
                    if (ids) ids->ReleaseStaticID(treeRows[i].id);
                    continue;
                }
                if (kept != i) treeRows[kept] = std::move(treeRows[i]);
                ++kept;
            }
            if (kept == treeRows.size()) return false;
            treeRows.resize(kept);
            RowsChanged();
            return true;
        }
    };

    // A Hierarchy panel: its view of the scene, with what it opened, selected and searched.
    struct HierarchyPanel
    {
        std::shared_ptr<HierarchyScene> scene;
        std::weak_ptr<HierarchyPanel> self;   // what a question about it holds on to
        tachyon::ID windowID = tachyon::ID::Invalid();   // the window it was last built in, which asks about it

        std::vector<int>                  treeVisibleIdx;
        tachyon::TableState               treeViewState;
        tachyon::MultiSelectState         treeMultiSelect;

        // Search: a filtered copy of the scene's rows (matches plus ancestors, forced open),
        // rebuilt when the query or the rows change. Rendered with its own state, no
        // drag-drop or rename.
        std::vector<tachyon::TreeViewRow> filteredRows;
        std::vector<int>                  filteredVisibleIdx;
        tachyon::TableState               filteredState;
        tachyon::MultiSelectState         filteredMultiSelect;
        std::string                       hierarchySearch;
        bool                              filterDirty = true;
        uint32_t                          seenRevision = 0;   // the scene's, when filteredRows was made

        tachyon::ItemDragDropConfig hierDnD;
        // A node to select once the tree shows it: one just moved or added. Its row ID only
        // exists once the table has laid out its rows for the new row count.
        tachyon::ID pendingSelect = tachyon::ID::Invalid();
        bool        treeHasFocus  = false;

        // The tree on show: the filtered copy while searching. Both carry the nodes' IDs.
        bool Searching() const { return !hierarchySearch.empty(); }
        std::vector<tachyon::TreeViewRow>& ShownRows()       { return Searching() ? filteredRows : scene->treeRows; }
        std::vector<int>&                  ShownVisibleIdx() { return Searching() ? filteredVisibleIdx : treeVisibleIdx; }
        tachyon::TableState&               ShownState()      { return Searching() ? filteredState : treeViewState; }

        // The node a shown row stands for; Invalid when the row isn't one of this frame's.
        tachyon::ID NodeAtRow(int row)
        {
            const std::vector<int>& visibleIdx = ShownVisibleIdx();
            if (row < 0 || row >= int(visibleIdx.size())) return tachyon::ID::Invalid();
            return ShownRows()[visibleIdx[row]].id;
        }

        bool RowIsSelected(int row)
        {
            tachyon::TableState& state = ShownState();
            const tachyon::ID rowID = state.RowIDInRange(row);
            if (rowID == tachyon::ID::Invalid()) return false;
            return state.multiSelect ? state.multiSelect->selectedIDs.contains(rowID) : state.selectedRowID == rowID;
        }

        // The nodes selected in the shown tree.
        std::vector<tachyon::ID> SelectedNodes()
        {
            tachyon::TableState& state = ShownState();
            std::vector<tachyon::ID> nodes;
            auto add = [&](tachyon::ID rowID) {
                if (rowID.GetMode() != tachyon::IDMode::SubRange) return;
                const int row = int(rowID.value.subRangeParts.index);
                if (state.RowIDInRange(row) != rowID) return;   // another table's, or stale
                const tachyon::ID node = NodeAtRow(row);
                if (node != tachyon::ID::Invalid()) nodes.push_back(node);
            };
            if (state.multiSelect)
                for (tachyon::ID rowID : state.multiSelect->selectedIDs) add(rowID);
            else
                add(state.selectedRowID);
            return nodes;
        }

        void ClearSelection()
        {
            for (tachyon::TableState* state : { &treeViewState, &filteredState })
            {
                state->selectedRowID = tachyon::ID::Invalid();
                if (state->multiSelect)
                {
                    state->multiSelect->selectedIDs.clear();
                    state->multiSelect->lastClickedID = tachyon::ID::Invalid();
                }
            }
        }

        // Deletes the nodes and everything under them from the scene.
        void DeleteNodes(std::span<const tachyon::ID> nodes)
        {
            if (!scene->DeleteNodes(nodes))
                return;
            if (!scene->idToFlat.contains(pendingSelect)) pendingSelect = tachyon::ID::Invalid();
            ClearSelection();
        }

        // Selects `node` in the shown tree if it's one of its rows this frame.
        bool SelectNode(tachyon::ID node)
        {
            tachyon::TableState& state = ShownState();
            const std::vector<int>& visibleIdx = ShownVisibleIdx();
            const std::vector<tachyon::TreeViewRow>& rows = ShownRows();
            if (state.RowIDInRange(0) == tachyon::ID::Invalid()) return false;
            for (int row = 0; row < int(visibleIdx.size()); ++row)
            {
                if (rows[visibleIdx[row]].id != node) continue;
                const tachyon::ID rowID = state.RowIDForIndex(row);
                ClearSelection();
                state.selectedRowID = rowID;
                if (state.multiSelect)
                {
                    state.multiSelect->selectedIDs.insert(rowID);
                    state.multiSelect->lastClickedID = rowID;
                }
                return true;
            }
            return false;
        }
    };

    ID Application::CreateHierarchyPanel(std::unordered_map<ID, Panel>& panels, std::string title)
    {
        auto panel = std::make_shared<HierarchyPanel>();
        panel->self = panel;
        const ID id = CreatePanel(panels, std::move(title), showcase.panelHierarchyIcon,
                                  [this, panel] {
                                      RenderHierarchyPanel(*panel);
                                      if (panel->treeHasFocus) focusedHierarchy = panel;
                                      else if (focusedHierarchy.lock() == panel) focusedHierarchy.reset();
                                  }, {},
                                  [this, panel] { EnableHierarchyPanel(*panel); });
        // Rebuilt as its rows finish loading.
        panels[id].cacheSignature = [this]() -> std::optional<uint64_t> { return ProgressLook(); };
        return id;
    }

    // The objects that can be added: the search row's "+" and the tree's context menu list
    // them. The new object goes last under `parent` (opened), or last in the scene.
    void Application::HierarchyAddMenuItems(HierarchyPanel& p, ID parent)
    {
        const std::pair<const char*, IconID> kinds[] = {
            { "Empty",  showcase.groupIcon },  { "Cube",   showcase.objectIcon }, { "Sphere", showcase.objectIcon },
            { "Plane",  showcase.objectIcon }, { "Camera", showcase.cameraIcon }, { "Light",  showcase.lightIcon },
            { "Folder", showcase.treeFolderIcon } };
        for (const auto& [name, icon] : kinds)
            MenuItem({ icon, name }, [this, &p, name, icon, parent] {
                TreeViewRow row{};
                row.id           = idManager.CreateStaticID();
                row.name         = name;
                row.icon         = icon;
                HierarchyScene& scene = *p.scene;
                row.renameBuffer = &scene.renameBuffer;
                size_t at = scene.treeRows.size();
                auto it = scene.idToFlat.find(parent);
                if (it != scene.idToFlat.end())
                {
                    const TreeViewRow& under = scene.treeRows[it->second];
                    p.treeViewState.openRows.insert(under.id);
                    row.depth = under.depth + 1;
                    at        = size_t(it->second + under.skipCount + 1);
                }
                p.pendingSelect = row.id;
                p.ClearSelection();   // its rows move: the selection is made again from pendingSelect
                scene.treeRows.insert(scene.treeRows.begin() + ptrdiff_t(at), std::move(row));
                scene.RowsChanged();
            });
    }

    void Application::HierarchyDeleteSelected()
    {
        if (const std::shared_ptr<HierarchyPanel> p = focusedHierarchy.lock())
            RequestHierarchyDelete(*p, p->SelectedNodes());
    }

    void Application::RequestHierarchyDelete(HierarchyPanel& p, std::vector<ID> nodes)
    {
        if (nodes.size() < 2 || showcase.deleteDontAsk)
        {
            p.DeleteNodes(nodes);
            return;
        }
        showcase.deleteNames.clear();
        for (const ID node : nodes)
            if (const auto it = p.scene->idToFlat.find(node); it != p.scene->idToFlat.end())
                showcase.deleteNames.push_back(p.scene->treeRows[size_t(it->second)].name);
        showcase.deleteNodes = std::move(nodes);
        showcase.deletePanel = p.self;
        showcase.deleteWindow = p.windowID;
    }

    void Application::AskBeforeDeleting(ID window)
    {
        // Asked in the window the panel is in; from a panel torn off into a window of its own,
        // in the main window.
        const bool documentWindow = showcase.deleteWindow == showcase.windowLayoutID2
            || std::any_of(showcase.documentWindows.begin(), showcase.documentWindows.end(),
                           [this](const auto& w) { return w->windowID == showcase.deleteWindow; });
        if (window != (documentWindow || progress.held ? showcase.deleteWindow : showcase.windowLayoutID2))
            return;
        // Captures ask about no panel's objects.
        const std::shared_ptr<HierarchyPanel> p = showcase.deletePanel.lock();
        if ((!p && !progress.held) || showcase.deleteNodes.empty())
        {
            showcase.deleteNodes.clear();
            return;
        }
        const size_t count = showcase.deleteNodes.size();
        const std::string title = std::format("Delete {} objects?", count);
        const std::string label = std::format("Delete {} objects", count);
        const MessageButton buttons[] = {
            { .label = "Cancel" },
            { .label = label, .kind = MessageButtonKind::Danger },
        };
        std::vector<MessageDialogItem> items;
        for (const std::string& name : showcase.deleteNames)
            items.push_back({ .name = name });
        const int choice = MessageDialog(showcase.deleteDialogID, MessageDialogDesc{
            .icon = MessageIcon::Warning, .title = title,
            .message = "They're removed from the scene, with everything under them. This can't be undone.",
            .buttons = buttons, .defaultButton = 0, .cancelButton = 0, .items = items, .dontAskAgain = &showcase.deleteDontAsk });
        if (choice == 1 && p)
            p->DeleteNodes(showcase.deleteNodes);
        else if (choice == 0)
            showcase.deleteDontAsk = false;   // kept: it asks again next time
        if (choice >= 0)
            showcase.deleteNodes.clear();
    }

    void Application::EnableHierarchyPanel(HierarchyPanel& p)
    {
        if (p.scene)
            return;   // made already: what it opened and selected stays
        // The scene every Hierarchy shows: the first one to show makes it.
        p.scene = m_hierarchyScene.lock();
        if (!p.scene)
        {
            p.scene = std::make_shared<HierarchyScene>();
            m_hierarchyScene = p.scene;
            HierarchyScene& scene = *p.scene;
            scene.ids = &idManager;

            // ---- Dataset ------------------------------------------------------
            auto add = [&](const char* name, int depth, IconID icon, bool isOpen) {
                TreeViewRow r{};
                r.id     = idManager.CreateStaticID();
                r.name   = name;
                r.depth  = depth;
                r.icon   = icon;
                r.renameBuffer = &scene.renameBuffer;
                if (isOpen)
                    scene.startOpen.push_back(r.id);
                scene.treeRows.push_back(std::move(r));
            };
            // An icon per kind: the scene, groups, meshes, the camera, lights and folders.
            add("Scene",     0, showcase.sceneIcon,      true);
            add("Meshes",    1, showcase.groupIcon,      true);
            add("Cube",      2, showcase.objectIcon,     false);
            add("Meshes",    2, showcase.groupIcon,      true);
            add("Cube",      3, showcase.objectIcon,     false);
            add("Sphere",    3, showcase.objectIcon,     false);
            add("Plane",     3, showcase.objectIcon,     false);
            add("Sphere",    2, showcase.objectIcon,     false);
            add("Plane",     2, showcase.objectIcon,     false);
            add("Camera",    1, showcase.cameraIcon,     false);
            add("Light",     1, showcase.lightIcon,      false);
            add("Terrain",   1, showcase.groupIcon,      false);
            scene.streamingRow = scene.treeRows.back().id;
            add("Props",     1, showcase.groupIcon,      true);
            for (const char* prop : { "Rock", "Crate", "Barrel", "Fence" })
            {
                add(prop, 2, showcase.objectIcon, false);
                scene.loadingRows.insert(scene.treeRows.back().id);
            }
            add("Materials", 0, showcase.treeFolderIcon, false);
            add("Default",   1, showcase.objectIcon,     false);
            add("Metal",     1, showcase.objectIcon,     false);
            add("Glass",     1, showcase.objectIcon,     false);
            add("Settings",  0, showcase.treeFolderIcon, false);
            scene.RowsChanged();
        }
        p.treeViewState.openRows.insert(p.scene->startOpen.begin(), p.scene->startOpen.end());
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
        // By the cursor, the dragged object's icon as small as the tree shows it: the drop line
        // shows where it goes.
        p.hierDnD.visualizer = [this, &p](const tachyon::DragVisualContext& visual) {
            if (visual.sourceID.GetMode() != tachyon::IDMode::SubRange) return;
            const int visIdx = static_cast<int>(visual.sourceID.value.subRangeParts.index);
            if (visIdx < 0 || visIdx >= int(p.treeVisibleIdx.size())) return;
            const std::vector<TreeViewRow>& rows = p.scene->treeRows;
            const int flatIdx = p.treeVisibleIdx[visIdx];
            if (flatIdx < 0 || flatIdx >= int(rows.size())) return;
            const IconID icon = rows[flatIdx].icon != IconID::Invalid
                ? rows[flatIdx].icon : showcase.circleIcon;
            const int2 size{ theme.treeTableIconSize, theme.treeTableIconSize };
            if (BeginCursorGhostWindow(showcase.dragGhostWindowID, visual.screenPos, size))
                AddGhostIcon(icon, size);
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
            HierarchyScene& scene = *p.scene;
            const tachyon::ID target = dstDs >= 0 && dstDs < int(scene.treeRows.size()) ? scene.treeRows[size_t(dstDs)].id : tachyon::ID::Invalid();
            const int newFlatIdx = TreeUtils::ReorderTreeRow(scene.treeRows, srcDs, dstDs, pos);
            if (newFlatIdx < 0) return;
            if (pos == TreeDropPosition::Inside || pos == TreeDropPosition::InsideLast)
                p.treeViewState.openRows.insert(target);   // it shows what went into it
            scene.RowsChanged();
            p.pendingSelect = scene.treeRows[size_t(newFlatIdx)].id;
            p.ClearSelection();
            guiStateManager.Interaction().SetFocusID(tachyon::ID::Invalid());
            guiStateManager.TextEditorFocus().Clear();
            transitionManager.Clear();
        };
        p.treeViewState.rowDragDrop = &p.hierDnD;
    }

    void Application::RenderHierarchyPanel(HierarchyPanel& p)
    {
        p.windowID = windowRegistry.GetCurrentWindowID();
        // ---- Search box (top of panel) -----------------------------------
        BeginHorizontal(idManager.AllocateDynamicID("hierarchySearchRow"),
                        theme.searchRowStyleRef, tachyon::LayoutOverride::Auto().NoShrink());
        if (SearchField(p.hierarchySearch, { showcase.findIcon, "Filter objects" }, TextEditBehavior::ContinuousUpdate,
                        tachyon::LayoutOverride{}.FlexGrow(1)))
            p.filterDirty = true;
        if (BeginMenu(idManager.AllocateDynamicID("hierarchyAdd"), { theme.addIcon, {}, "Add object" },
                      theme.iconMenuButtonStyleRef, theme.menuStyleRef, theme.menuItemRowStyleRef))
            HierarchyAddMenuItems(p);
        EndMenu();
        EndHorizontal();

        const bool searching = !p.hierarchySearch.empty();

        // Refilter on demand: matched rows plus all their ancestors, ancestors
        // forced open so matches under collapsed parents show. Depths are
        // preserved, so the copy is a valid forest for RebuildTreeViewIndex.
        // Rows added, removed or moved, here or in another Hierarchy, make the copy again.
        const std::vector<TreeViewRow>& sceneRows = p.scene->treeRows;
        if (p.seenRevision != p.scene->revision)
        {
            p.seenRevision = p.scene->revision;
            p.filterDirty  = true;
        }
        if (searching && p.filterDirty)
        {
            const std::string q = SearchToLower(p.hierarchySearch);
            std::vector<char> keep(sceneRows.size(), 0);
            std::vector<int>  chain; // chain[d] = index of the current ancestor at depth d
            for (int i = 0; i < int(sceneRows.size()); ++i)
            {
                chain.resize(sceneRows[i].depth);
                chain.push_back(i);
                if (SearchMatches(sceneRows[i].name, q))
                    for (int a : chain) keep[a] = 1;
            }
            p.filteredRows.clear();
            for (int i = 0; i < int(sceneRows.size()); ++i)
            {
                if (!keep[i]) continue;
                TreeViewRow r  = sceneRows[i];
                r.renameBuffer = nullptr; // copies don't write back - no rename
                p.filteredRows.push_back(std::move(r));
            }
            RebuildTreeViewIndex(p.filteredRows);
            p.filteredState.openRows.clear();
            for (const TreeViewRow& r : p.filteredRows)
                p.filteredState.openRows.insert(r.id);
            p.filteredState.multiSelect = &p.filteredMultiSelect;
            // A new copy: its rows aren't the old one's, so nothing of its selection carries over.
            p.filteredState.selectedRowID = tachyon::ID::Invalid();
            p.filteredMultiSelect.selectedIDs.clear();
            p.filteredMultiSelect.lastClickedID = tachyon::ID::Invalid();
            p.filteredState.internal.treeVisibleIdx.clear();
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
            WalkTreeViewVisibility(rows, visibleIdx, state);   // a collapse keeps the selection on its nodes
            bool anyExpandable = false;
            for (TreeViewRow& r : rows)
            {
                if (r.HasChildren()) anyExpandable = true;
                if (r.icon == showcase.treeFolderOpenIcon || r.icon == showcase.treeFolderIcon)
                    r.icon = (r.HasChildren() && state.openRows.contains(r.id)) ? showcase.treeFolderOpenIcon : showcase.treeFolderIcon;
            }

            // Still loading at startup: the props' rows, and the terrain streaming in.
            const float sinceStart = ProgressElapsed(progress.startedAt, 1.0f);
            const bool  propsLoading = sinceStart < ProgressDemos::kPropsLoadSecs;
            const bool  streaming    = sinceStart < ProgressDemos::kTerrainStreamSecs;

            VirtualTableColumn cols[3];

            // Col 0 - visibility toggle. Shows the "on" eye only on hover (so
            // a fully-visible tree shows no icons); "off"/"mixed" show always.
            cols[0].column = TableColumn{ .header = {},
                .initialWidth = kVizColWidth, .minWidth = kVizColWidth,
                .maxWidth = kVizColWidth, .sizing = ColumnSizing::Fixed,
                .interactiveContent = true };
            cols[0].generateCell = [this, &p, &rows, &visibleIdx, &state, eyeHeight](int rowIdx)
            {
                if (rowIdx < 0 || rowIdx >= int(visibleIdx.size())) return;
                const tachyon::ID nodeId = rows[visibleIdx[rowIdx]].id;
                const tachyon::ID rowID  = state.RowIDInRange(rowIdx);
                const HierarchyScene::VizAgg agg = p.scene->HierComputeAgg(nodeId);
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
                        case HierarchyScene::VizAgg::On:    icon = hovered ? showcase.visibilityOnIcon : tachyon::IconID::Invalid; break;
                        case HierarchyScene::VizAgg::Off:   icon = showcase.visibilityOffIcon;   break;
                        case HierarchyScene::VizAgg::Mixed: icon = showcase.visibilityMixedIcon; break;
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
                                    const bool makeVisible = (p.scene->HierComputeAgg(nodeId) == HierarchyScene::VizAgg::Off);
                                    p.scene->HierApplyViz(nodeId, makeVisible);
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
                                p.scene->HierApplyViz(nodeId, ev.state.dragPayloadValue != 0);
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
            cols[1].generateCell = [this, &p, &rows, &visibleIdx, &state, anyExpandable, propsLoading](int rowIdx)
            {
                if (rowIdx < 0 || rowIdx >= int(visibleIdx.size())) return;
                const size_t flat = size_t(visibleIdx[rowIdx]);
                TreeViewRow& r = rows[flat];
                // A row still loading: a placeholder where its icon and name will be, indented as
                // its row will be.
                if (propsLoading && p.scene->loadingRows.contains(r.id))
                {
                    const int32_t indent = r.depth * theme.treeTableIndent + (anyExpandable ? theme.treeTableExpanderWidth : 0);
                    const int32_t width  = styleManager.Px(70 + int((r.id.Value() * 37u) % 60u));
                    Cell([this, indent, width] {
                        BeginHorizontal(idManager.AllocateDynamicID("loadingRow"), hierarchySkeletonRowStyleRef,
                                        LayoutOverride{}.FlexGrow(1));
                        RegisterElement(Element{ .id = idManager.AllocateDynamicID("indent"), .styleRef = theme.emptyHorzStyleRef },
                                        LayoutOverride::Auto().Width(Pixels{ indent }).NoShrink());
                        Skeleton(idManager.AllocateDynamicID("icon"), LayoutOverride::Auto().Size(Pixels{ styleManager.Px(13) }, Pixels{ styleManager.Px(13) }));
                        Skeleton(idManager.AllocateDynamicID("name"), LayoutOverride::Auto().Width(Pixels{ width }).Height(Pixels{ styleManager.Px(8) }));
                        EndHorizontal();
                    });
                    return;
                }
                TreeCell(r.name, r.depth,
                         TreeViewOpenRef(rows, flat, state),
                         r.icon, r.ancestorLineMask, r.isLastSibling,
                         r.renameBuffer, anyExpandable);
            };

            // tableFill (flex-basis:0 + grow:1) bounds the table to the remaining
            // panel height so its body scrolls; flex-basis:auto would grow to fit
            // every row and never scroll.
            state.showHeaders      = false;
            state.columnSeparators = false;   // the eye column reads as part of the row
            state.cellStyle        = hierarchyVizCellStyleRef;
            state.rowStyle         = theme.listRowStyleRef;   // rounded, no stripes
            state.commandMode      = hierarchyMode;   // Delete
            // On a row: add under it, and delete it, or the whole selection when it's part of it.
            // Below the rows: add to the scene.
            state.contextMenu      = [this, &p](int row) {
                const tachyon::ID node = p.NodeAtRow(row);
                HierarchyAddMenuItems(p, node);
                if (node == tachyon::ID::Invalid()) return;
                MenuSeparator();
                const CommandManager::Command* del = commandManager.GetCommand(hierarchyDelete);
                std::vector<tachyon::ID> nodes = p.RowIsSelected(row) ? p.SelectedNodes() : std::vector<tachyon::ID>{ node };
                DestructiveMenuItem({ showcase.deleteIcon, "Delete" },
                                    del && del->HasShortcut() ? std::string_view(del->displayString) : std::string_view{},
                                    [this, &p, nodes = std::move(nodes)] { RequestHierarchyDelete(p, nodes); });
            };
            // While something streams in, a column at the end says so on its row.
            // As wide as "streaming", its spinner and the room around them.
            const int32_t streamingWidth = textSystem->GetTextMeasurement(textSystem->RegisterText(
                tachyon::ID::Invalid(), "streaming", theme.taskProgressDetailStyleRef, tachyon::ElementState::None)).maxWidth
                + styleManager.Px(12 + 6 + 8 + 4);
            cols[2].column = TableColumn{ .header = {},
                .initialWidth = streamingWidth, .minWidth = streamingWidth,
                .maxWidth = streamingWidth, .sizing = ColumnSizing::Fixed };
            cols[2].generateCell = [this, &p, &rows, &visibleIdx](int rowIdx)
            {
                const bool here = rowIdx >= 0 && rowIdx < int(visibleIdx.size()) && rows[visibleIdx[rowIdx]].id == p.scene->streamingRow;
                Cell([this, here] {
                    if (!here) return;
                    BeginHorizontal(idManager.AllocateDynamicID("streaming"), hierarchyStreamingStyleRef, LayoutOverride{}.FlexGrow(1));
                    Text(idManager.AllocateDynamicID("streamingText"), "streaming", theme.taskProgressDetailStyleRef);
                    Spinner(idManager.AllocateDynamicID("streamingSpinner"), SpinnerSize::Small);
                    EndHorizontal();
                });
            };
            VirtualTable(id, state, std::span<VirtualTableColumn>(cols, streaming ? 3 : 2),
                         int(visibleIdx.size()), 0, hierarchyTableStyleRef,
                         tachyon::LayoutOverride::Auto().FlexGrow(1).MinHeight(0));
            p.treeHasFocus = guiStateManager.Interaction().IsFocusWithin(id);
        };

        // Filtered view: own state, no drag-drop (a reorder of the copy couldn't be mapped
        // back unambiguously). Visibility still works - aggregation/application run over the
        // master tree by node ID.
        if (searching)
            renderTree(idManager.AllocateDynamicID(), p.filteredRows, p.filteredVisibleIdx, p.filteredState);
        else
            renderTree(idManager.AllocateDynamicID(), p.scene->treeRows, p.treeVisibleIdx, p.treeViewState);

        // A node just moved or added is selected once its row shows; one the search hides
        // waits until it shows.
        if (p.pendingSelect != tachyon::ID::Invalid() && p.SelectNode(p.pendingSelect))
            p.pendingSelect = tachyon::ID::Invalid();
    }

}
