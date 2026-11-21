#include "application.h"
#include <gui_controls/gui_tree_utils.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <numeric>
#include <span>
#include <string>

//
// Demo panel for TreeTable (hierarchical rows inside a VirtualTable).
//
// Data model: a flat row vector plus a per-row `skipCount` (descendants), so
// the visibility walk skips a closed subtree in one step. Each frame
// `visibleIdx` maps visible row -> flat row and its size is the row count.
//
// `skipCount` / `ancestorLineMask` / `isLastSibling` are derived from `depth`
// by TreeUtils::RebuildTreeIndex after adding or removing rows; toggling
// `isOpen` needs no rebuild.
//

namespace tachyon
{

    namespace {

    using TreeRow = Application::TreeTableRow;

    // Stable payload tag so the row drop targets reject foreign drags.
    constexpr uint32_t kTreeTableRowPayloadType = 0x54725452u; // 'TrTR'

    } // namespace

    // A TreeTable panel's own: its rows (which are open, their order), the table's state and
    // its selection. Two panels are two trees.
    struct TreeTablePanel
    {
        IDManager*                      ids = nullptr;   // the rows' static IDs come from it, and go back to it
        std::vector<TreeRow>            rows;
        TableState                      state;
        MultiSelectState                multiSelect;
        ItemDragDropConfig              dragDrop;        // payload: the flat row index
        std::vector<int>                visibleIdx;      // visible row -> flat row, rebuilt each frame
        std::vector<VirtualTableColumn> columns;
        // A moved row's new flat index, selected after the next VirtualTable call (which
        // allocates the row IDs).
        int                             pendingSelectFlatIdx = -1;

        ~TreeTablePanel()
        {
            if (ids)
                for (const TreeRow& row : rows)
                    ids->ReleaseStaticID(row.id);
        }
    };

    ID Application::CreateTreeTablePanel(std::unordered_map<ID, Panel>& panels, std::string title)
    {
        auto panel = std::make_shared<TreeTablePanel>();
        TreeTablePanel* const self = panel.get();
        panel->ids = &idManager;

        // ---- Dataset -------------------------------------------------------
        auto add = [&](const char* name, int depth, bool hasChildren, bool isOpen,
                       const char* size, const char* type, IconID icon) {
            TreeRow r;
            r.id          = idManager.CreateStaticID();
            r.name        = name;
            r.size        = size;
            r.type        = type;
            r.depth       = depth;
            r.hasChildren = hasChildren;
            r.isOpen      = isOpen;
            r.icon        = icon;
            self->rows.push_back(std::move(r));
        };

        add("Assets",           0, true,  true,  "",        "Folder",   showcase.folderClosedIcon);
        add("Meshes",           1, true,  true,  "",        "Folder",   showcase.folderClosedIcon);
        add("Characters",       2, true,  false, "",        "Folder",   showcase.folderClosedIcon);
        add("Hero.fbx",         3, false, false, "1.2 MB",  "Mesh",     showcase.starIcon);
        add("Enemy.fbx",        3, false, false, "800 KB",  "Mesh",     showcase.starIcon);
        add("Props",            2, true,  true,  "",        "Folder",   showcase.folderClosedIcon);
        add("Crate.fbx",        3, false, false, "120 KB",  "Mesh",     showcase.starIcon);
        add("Barrel.fbx",       3, false, false, "140 KB",  "Mesh",     showcase.starIcon);
        add("Sign.fbx",         3, false, false,  "48 KB",  "Mesh",     showcase.starIcon);
        add("Textures",         1, true,  true,  "",        "Folder",   showcase.folderClosedIcon);
        add("Albedo",           2, true,  false, "",        "Folder",   showcase.folderClosedIcon);
        add("wood.png",         3, false, false, "512 KB",  "Image",    showcase.heartIcon);
        add("metal.png",        3, false, false, "480 KB",  "Image",    showcase.heartIcon);
        add("Normal",           2, true,  false, "",        "Folder",   showcase.folderClosedIcon);
        add("wood_n.png",       3, false, false, "700 KB",  "Image",    showcase.heartIcon);
        add("metal_n.png",      3, false, false, "680 KB",  "Image",    showcase.heartIcon);
        add("Materials",        1, true,  false, "",        "Folder",   showcase.folderClosedIcon);
        add("Default.mat",      2, false, false,   "4 KB",  "Material", showcase.circleIcon);
        add("Metal.mat",        2, false, false,   "4 KB",  "Material", showcase.circleIcon);
        add("Glass.mat",        2, false, false,   "4 KB",  "Material", showcase.circleIcon);
        add("Scenes",           0, true,  true,  "",        "Folder",   showcase.folderClosedIcon);
        add("MainLevel.scene",  1, false, false, "256 KB",  "Scene",    showcase.circleIcon);
        add("Boss.scene",       1, false, false, "190 KB",  "Scene",    showcase.circleIcon);
        add("Docs",             0, true,  false, "",        "Folder",   showcase.folderClosedIcon);
        add("README.md",        1, false, false,   "8 KB",  "Text",     showcase.heartIcon);
        add("CHANGELOG.md",     1, false, false,   "3 KB",  "Text",     showcase.heartIcon);

        TreeUtils::RebuildTreeIndex(self->rows);

        self->state.multiSelect = &self->multiSelect;

        // Row drag-and-drop. The payload is the flat row index; a rowID's
        // sub-range index is the visible index, so translate it through
        // visibleIdx (closed branches would skew it otherwise).
        self->dragDrop.payloadType = kTreeTableRowPayloadType;
        self->dragDrop.getPayload  = [self](tachyon::ID rowID) -> uintptr_t {
            if (rowID.GetMode() != tachyon::IDMode::SubRange) return UINTPTR_MAX;
            const int visIdx = static_cast<int>(rowID.value.subRangeParts.index);
            const auto& v = self->visibleIdx;
            if (visIdx < 0 || visIdx >= int(v.size())) return UINTPTR_MAX;
            return static_cast<uintptr_t>(v[visIdx]);
        };
        self->dragDrop.onReorder = [self](uintptr_t srcPayload, tachyon::ID targetID, TreeDropPosition pos) {
            if (targetID.GetMode() != tachyon::IDMode::SubRange) return;
            if (srcPayload == UINTPTR_MAX) return;
            const int targetVis = static_cast<int>(targetID.value.subRangeParts.index);
            const auto& v = self->visibleIdx;
            if (targetVis < 0 || targetVis >= int(v.size())) return;
            const int srcDs = static_cast<int>(srcPayload);
            const int dstDs = v[targetVis];
            const int newFlatIdx = TreeUtils::ReorderTreeRow(self->rows, srcDs, dstDs, pos);
            if (newFlatIdx < 0) return;
            TreeUtils::RebuildTreeIndex(self->rows);
            // Deferred: the matching sub-range rowID needs the
            // row IDs VirtualTable allocates next frame.
            self->pendingSelectFlatIdx       = newFlatIdx;
            self->state.selectedRowID        = tachyon::ID::Invalid();
            self->multiSelect.selectedIDs.clear();
            self->multiSelect.lastClickedID  = tachyon::ID::Invalid();
        };
        self->state.rowDragDrop = &self->dragDrop;

        // ---- Columns -------------------------------------------------------
        // Column reorder is left disabled.
        self->columns =
        {
            // Locked so it stays anchored on the left. TreeCell handles indent,
            // chevron, icon and inline rename.
            {
                { .header = "Name", .initialWidth = 240, .minWidth = 80, .maxWidth = 600, .locked = true },
                [this, self](int rowIdx) {
                    auto& v = self->visibleIdx;
                    if (rowIdx < 0 || rowIdx >= int(v.size())) return;
                    TreeRow& r = self->rows[size_t(v[size_t(rowIdx)])];
                    // Arrow follows actual descendants, not `hasChildren`.
                    const bool branch = (r.skipCount > 0);
                    TreeCell(r.name, r.depth,
                             branch ? &r.isOpen : nullptr,
                             r.icon,
                             r.ancestorLineMask,
                             r.isLastSibling,
                             &r.name);
                }
            },
            {
                { .header = "Size", .initialWidth = 100, .minWidth = 40, .maxWidth = 200 },
                [this, self](int rowIdx) {
                    auto& v = self->visibleIdx;
                    if (rowIdx < 0 || rowIdx >= int(v.size())) return;
                    Cell(self->rows[size_t(v[size_t(rowIdx)])].size);
                }
            },
            {
                { .header = "Type", .initialWidth = 120, .minWidth = 40, .maxWidth = 200 },
                [this, self](int rowIdx) {
                    auto& v = self->visibleIdx;
                    if (rowIdx < 0 || rowIdx >= int(v.size())) return;
                    Cell(self->rows[size_t(v[size_t(rowIdx)])].type);
                }
            }
        };
        return CreatePanel(panels, std::move(title), showcase.panelMiscIcon, [this, panel] { RenderTreeTablePanel(*panel); });
    }

    void Application::RenderTreeTablePanel(TreeTablePanel& panel)
    {
        // ---- Per-frame visibility walk -------------------------------------
        std::vector<int>& visibleIdx = panel.visibleIdx;
        visibleIdx.clear();
        for (size_t i = 0; i < panel.rows.size(); )
        {
            visibleIdx.push_back(int(i));
            if (panel.rows[i].hasChildren && !panel.rows[i].isOpen)
                i += 1 + panel.rows[i].skipCount;
            else
                i += 1;
        }

        // ---- Mode switcher -------------------------------------------------
        // Row mode selects whole rows; Cell mode selects single cells, with
        // Left/Right arrow navigation.
        SelectionModeSwitch(panel.state.selectionMode);

        std::span<VirtualTableColumn> columnSpan(panel.columns.data(), panel.columns.size());
        VirtualTable(idManager.AllocateDynamicID("treeTable"),
                     panel.state, columnSpan,
                     int(visibleIdx.size()), 0, theme.tableStyleRef,
                     LayoutOverride{}.Width(520).Height(400).BreakBefore());

        // Apply a pending post-drop selection now that VirtualTable has set
        // this frame's row IDs.
        if (panel.pendingSelectFlatIdx >= 0 && panel.state.RowIDInRange(0) != tachyon::ID::Invalid())
        {
            const int targetFlat = panel.pendingSelectFlatIdx;
            int newVisIdx = -1;
            for (size_t i = 0; i < visibleIdx.size(); ++i)
                if (visibleIdx[i] == targetFlat) { newVisIdx = int(i); break; }
            if (newVisIdx >= 0)
            {
                const tachyon::ID newRowID = panel.state.RowIDForIndex(newVisIdx);
                panel.state.selectedRowID = newRowID;
                panel.multiSelect.selectedIDs.clear();
                panel.multiSelect.selectedIDs.insert(newRowID);
                panel.multiSelect.lastClickedID = newRowID;
            }
            panel.pendingSelectFlatIdx = -1;
        }
    }
}
