#include "application.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

//
// Demo panel for the Table widget. The first two sub-sections are temporarily
// disabled (BeginTable API); the third sub-section uses VirtualTable directly
// and is the only live demo here.
//
// Sorting: each column's sortAscending compares dataset row indices and the
// table builds the visual-order permutation; the data is never reordered.
//

namespace tachyon
{
    namespace
    {
        struct DemoRow
        {
            int         index;     // 0..N-1, kept for the # column
            std::string name;
            int         hex;
            int         multiple;
        };

        // 50k rows to exercise virtualization, sort and column reorder.
        const std::vector<DemoRow>& GetDemoRows()
        {
            static const std::vector<DemoRow> rows = []() {
                std::vector<DemoRow> v;
                v.reserve(50000);
                char buf[32];
                for (int i = 0; i < 50000; i++)
                {
                    std::snprintf(buf, sizeof(buf), "Row %05d", i);
                    v.push_back(DemoRow{ i, std::string(buf), i, i * 7 });
                }
                return v;
            }();
            return rows;
        }
    }

    void Application::SelectionModeSwitch(TableSelectionMode& mode)
    {
        static constexpr std::string_view kModes[] = { "Row", "Cell" };   // TableSelectionMode's order
        // A toolbar above the table, padded, with a line under it.
        BeginHorizontal(idManager.AllocateDynamicID("selectionModeBar"), tableToolbarStyleRef, LayoutOverride::Auto().NoShrink());
            Text(idManager.AllocateDynamicID(), "Selection mode", theme.staticTextStyleRef);
            Segmented(mode, kModes);
        EndHorizontal();
    }

    // A Table panel's own: its sort, widths, selection and column order.
    struct TablePanel
    {
        TableState                      state;
        std::vector<VirtualTableColumn> columns;
    };

    ID Application::CreateTablePanel(std::unordered_map<ID, Panel>& panels, std::string title)
    {
        auto panel = std::make_shared<TablePanel>();
        const auto& rows = GetDemoRows();
        panel->columns =
        {
            // The row-index column is locked - fixed landmark on the
            // left, and other columns can't be dragged past it.
            {
                .column = { .header = "#", .initialWidth = 64,  .minWidth = 40, .maxWidth = 160, .locked = true,
                            .sizing = ColumnSizing::Fixed },
                .generateCell = [this, &rows](int rowIdx) {
                    char buf[16]; std::snprintf(buf, sizeof(buf), "%d", rows[rowIdx].index); Cell(buf);
                },
                .sortAscending = [&rows](int a, int b) { return rows[a].index < rows[b].index; },
            },
            {
                .column = { .header = "Name", .initialWidth = 160, .minWidth = 60, .maxWidth = 400 },
                .generateCell = [this, &rows](int rowIdx) {
                    Cell(rows[rowIdx].name.c_str());
                },
                .sortAscending = [&rows](int a, int b) { return rows[a].name < rows[b].name; },
            },
            {
                .column = { .header = "Hex", .initialWidth = 120, .minWidth = 60, .maxWidth = 200 },
                .generateCell = [this, &rows](int rowIdx) {
                    char buf[16]; std::snprintf(buf, sizeof(buf), "0x%06X", rows[rowIdx].hex); Cell(buf);
                },
                .sortAscending = [&rows](int a, int b) { return rows[a].hex < rows[b].hex; },
            },
            {
                .column = { .header = "Multiple", .initialWidth = 140, .minWidth = 60, .maxWidth = 400 },
                .generateCell = [this, &rows](int rowIdx) {
                    char buf[40]; std::snprintf(buf, sizeof(buf), "%d x 7 = %d", rows[rowIdx].index, rows[rowIdx].multiple); Cell(buf);
                },
                .sortAscending = [&rows](int a, int b) { return rows[a].multiple < rows[b].multiple; },
            },
        };


        // Drag-to-reorder: rotate the column list (the table moves the
        // persisted widths itself).
        panel->state.selectionBar = true;
        panel->state.onReorderColumns = [columns = &panel->columns](int from, int to)
        {
            if (from == to) return;
            if (from < to) std::rotate(columns->begin() + from, columns->begin() + from + 1, columns->begin() + to + 1);
            else           std::rotate(columns->begin() + to, columns->begin() + from, columns->begin() + from + 1);
        };
        return CreatePanel(panels, std::move(title), showcase.panelMiscIcon, [this, panel] { RenderTablePanel(*panel); });
    }

    void Application::RenderTablePanel(TablePanel& panel)
    {
        // ---- Virtual-scrolled table (50k rows, click-to-sort, drag-to-reorder)
        std::vector<VirtualTableColumn>& columns = panel.columns;
        TableState& state = panel.state;

        // The row number is a count: right-aligned, so its digits line up. Hex codes and the
        // multiplications read as text, at the start, in the mono font. Set each frame as
        // styles reload.
        for (VirtualTableColumn& c : columns)
            c.column.cellStyle = c.column.header == "#"    ? dataTableNumberStyleRef
                               : c.column.header == "Name" ? StyleRef::Invalid()
                                                           : dataTableCodeStyleRef;

        const int kTotalRows = int(GetDemoRows().size());
        std::span<VirtualTableColumn> columnSpan(columns.data(), columns.size());


        // ---- Mode switcher -------------------------------------------------
        // Row mode selects whole rows; Cell mode selects single cells, with
        // Left/Right arrow navigation.
        SelectionModeSwitch(state.selectionMode);

        // Fills the panel and scrolls: sized to its 50k rows it would be past the
        // 16-bit layout range and not show at all.
        state.cellStyle = dataTableCellStyleRef;
        VirtualTable(idManager.AllocateDynamicID("tablePanelVirtual"),
                     state,
                     columnSpan, kTotalRows, 0, theme.tableFillStyleRef,
                     LayoutOverride::Auto().FlexGrow(1).MinHeight(0));
    }

}
