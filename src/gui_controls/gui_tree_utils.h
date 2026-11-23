#pragma once

// Flat-list tree helpers shared by VirtualTreeView / tree-table views: rows
// are a flat vector in depth-first order, each row carrying its depth.
// RebuildTreeIndex derives skipCount / ancestorLineMask / isLastSibling;
// ReorderTreeRow moves a subtree (with reparenting) for drag-drop.


#include <gui_controls/gui_widget_state_types.h>   // TreeDropPosition

#include <algorithm>
#include <concepts>
#include <iterator>
#include <stdint.h>
#include <vector>

namespace tachyon::TreeUtils
{
    /// @addtogroup controls_data
    /// @{

    /// A flat tree row: the caller sets depth; RebuildTreeIndex derives the rest. Whether a
    /// row is open is the caller's too: a bool on the row (`isOpen`), or a view's set.
    template <typename Row>
    concept TreeRowLike = requires(Row r)
    {
        { r.depth }            -> std::convertible_to<int>;
        { r.skipCount }        -> std::convertible_to<int>;
        { r.ancestorLineMask } -> std::convertible_to<uint32_t>;
        { r.isLastSibling }    -> std::convertible_to<bool>;
    };

    /// Recomputes the derived fields (skipCount, ancestorLineMask, isLastSibling) from
    /// each row's depth. Call once after structural edits; opening a row needs no rebuild.
    /// One pass from the last row up, so a large tree (a virtualized view's) costs O(n).
    template <TreeRowLike Row>
    void RebuildTreeIndex(std::vector<Row>& rows)
    {
        const int n = static_cast<int>(rows.size());
        int maxDepth = 0;
        for (const Row& row : rows)
            maxDepth = std::max(maxDepth, static_cast<int>(row.depth));
        // later[t]: a row below this one is at depth t with none shallower in between, so a
        // line at depth t runs on past this row. Only entries up to `highest` can be set.
        std::vector<uint8_t> later(static_cast<size_t>(maxDepth) + 1, 0);
        int highest = -1;
        // The rows below this one that no row below them is at or above: the first one at or
        // above this row's depth ends its subtree.
        std::vector<int> open;
        for (int i = n - 1; i >= 0; --i)
        {
            const int D = std::max(0, static_cast<int>(rows[i].depth));
            while (!open.empty() && rows[open.back()].depth > D)
                open.pop_back();
            rows[i].skipCount = (open.empty() ? n : open.back()) - i - 1;
            open.push_back(i);

            uint32_t mask = 0;
            for (int d = 0; d <= D - 2 && d < 32; d++)   // the mask is 32 bits wide
                if (d + 1 <= highest && later[size_t(d + 1)])
                    mask |= (1u << d);
            rows[i].ancestorLineMask = mask;
            rows[i].isLastSibling    = !(D <= highest && later[size_t(D)]);

            // For the rows above: lines deeper than this row end at it, its own runs on.
            for (int t = D + 1; t <= highest; t++)
                later[size_t(t)] = 0;
            later[size_t(D)] = 1;
            highest = D;
        }
    }

    /// Moves the subtree rooted at flat index `srcDs` to `pos` relative to row `dstDs`,
    /// shifting depths when the target depth differs; Inside/InsideLast open the target when
    /// its rows have an `isOpen` (open it in the view yourself when they don't).
    /// Rejects a drop on itself or into its own descendants. Call RebuildTreeIndex
    /// afterwards. Returns the moved row's new flat index, or -1 on rejection.
    template <TreeRowLike Row>
    int ReorderTreeRow(std::vector<Row>& rows, int srcDs, int dstDs,
                       tachyon::TreeDropPosition pos)
    {
        const int n = static_cast<int>(rows.size());
        if (srcDs < 0 || srcDs >= n) return -1;
        if (dstDs < 0 || dstDs >= n) return -1;
        if (srcDs == dstDs) return -1;

        const int srcStart      = srcDs;
        const int srcSliceCount = 1 + rows[srcStart].skipCount;
        const int srcEnd        = srcStart + srcSliceCount;

        // Cycle guard: dropping into the source's own subtree is invalid.
        if (dstDs >= srcStart && dstDs < srcEnd) return -1;

        int targetBaseDepth = rows[dstDs].depth;
        int insertAt        = dstDs;
        auto openTarget = [&] {
            if constexpr (requires { rows[dstDs].isOpen = true; })
                rows[dstDs].isOpen = true;
        };
        switch (pos)
        {
            case tachyon::TreeDropPosition::Before:
                insertAt        = dstDs;
                targetBaseDepth = rows[dstDs].depth;
                break;
            case tachyon::TreeDropPosition::Inside:
                // First child: splice in right after the parent row.
                insertAt        = dstDs + 1;
                targetBaseDepth = rows[dstDs].depth + 1;
                openTarget();
                break;
            case tachyon::TreeDropPosition::InsideLast:
                // Last child: splice in past the parent's whole subtree.
                insertAt        = dstDs + 1 + rows[dstDs].skipCount;
                targetBaseDepth = rows[dstDs].depth + 1;
                openTarget();
                break;
            case tachyon::TreeDropPosition::After:
                insertAt        = dstDs + 1 + rows[dstDs].skipCount;
                targetBaseDepth = rows[dstDs].depth;
                break;
            default: return -1;
        }

        // Lift the source slice into a temp, adjust depth on each lifted row.
        const int srcBaseDepth = rows[srcStart].depth;
        const int deltaDepth   = targetBaseDepth - srcBaseDepth;
        std::vector<Row> lifted;
        lifted.reserve(srcSliceCount);
        lifted.insert(lifted.end(),
                      std::make_move_iterator(rows.begin() + srcStart),
                      std::make_move_iterator(rows.begin() + srcEnd));
        if (deltaDepth != 0)
            for (auto& r : lifted) r.depth += deltaDepth;

        rows.erase(rows.begin() + srcStart, rows.begin() + srcEnd);
        if (insertAt > srcStart) insertAt -= srcSliceCount;
        rows.insert(rows.begin() + insertAt,
                    std::make_move_iterator(lifted.begin()),
                    std::make_move_iterator(lifted.end()));
        return insertAt;   // new flat index of the moved row's root
    }
    /// @}
}
