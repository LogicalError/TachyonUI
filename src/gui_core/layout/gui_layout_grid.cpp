#include <gui_core/layout/gui_layout_grid.h>
#include <gui_core/layout/gui_layout_helpers.h>


#include <algorithm>
#include <assert.h>

namespace tachyon
{
    // Stage 1: Determine placement and effective grid dimensions
    void LayoutGrid::DeterminePlacement(std::span<const ElementRect> childRects,
                                        GridPlacement&               outPlacement)
    {
        if (childRects.empty())
        {
            outPlacement.cellPositions.clear();   // keeps its capacity: the placement is reused
            outPlacement.effectiveColumnCount = 0;
            outPlacement.effectiveRowCount    = 0;
            return;
        }

        // Each in-flow item's lines, kept within the track limit: a span of 1 to the limit, a
        // start no further than leaves room for its span. A start < 0 is auto. This and the
        // occupancy are kept per thread and reused, as the Scratch is.
        struct Item { int32_t col, row, colSpan, rowSpan; bool inFlow; };
        constexpr int32_t kMax = GridAxisProperties::kMaxTracks;
        thread_local std::vector<Item> items;
        items.assign(childRects.size(), Item{});
        int32_t cols = 1, rows = 1;
        for (size_t i = 0; i < childRects.size(); ++i)
        {
            Item& it = items[i];
            it.inFlow = childRects[i].constraints.IsInFlow();
            if (!it.inFlow)
                continue;
            const GridProperties grid = childRects[i].constraints.GetGridItem(childRects[i].layoutStyle);
            it.colSpan = std::clamp<int32_t>(grid.column.span, 1, kMax);
            it.rowSpan = std::clamp<int32_t>(grid.row.span, 1, kMax);
            it.col = grid.column.start < 0 ? -1 : std::min<int32_t>(grid.column.start, kMax - it.colSpan);
            it.row = grid.row.start    < 0 ? -1 : std::min<int32_t>(grid.row.start, kMax - it.rowSpan);
            // The columns are known up front, from the items' lines and spans; rows grow as
            // items need them, so the row-major occupancy below never has to be laid out again.
            cols = std::max(cols, it.col >= 0 ? it.col + it.colSpan : it.colSpan);
            rows = std::max(rows, it.row >= 0 ? it.row + it.rowSpan : it.rowSpan);
        }

        thread_local std::vector<uint8_t> occupied;
        occupied.assign(size_t(cols) * size_t(rows), 0);
        auto growRows = [&](int32_t needed)
        {
            if (needed > rows)
            {
                rows = needed;
                occupied.resize(size_t(cols) * size_t(rows), 0);
            }
        };
        auto fits = [&](int32_t col, int32_t row, const Item& it)
        {
            if (col + it.colSpan > cols)
                return false;
            growRows(row + it.rowSpan);
            for (int32_t r = row; r < row + it.rowSpan; ++r)
                for (int32_t c = col; c < col + it.colSpan; ++c)
                    if (occupied[size_t(r) * size_t(cols) + size_t(c)])
                        return false;
            return true;
        };
        outPlacement.cellPositions.assign(childRects.size(), int2{ 0, 0 });
        auto place = [&](size_t i, int32_t col, int32_t row)
        {
            const Item& it = items[i];
            growRows(row + it.rowSpan);
            for (int32_t r = row; r < row + it.rowSpan; ++r)
                for (int32_t c = col; c < col + it.colSpan; ++c)
                    occupied[size_t(r) * size_t(cols) + size_t(c)] = 1;
            outPlacement.cellPositions[i] = int2{ col, row };
        };

        // As CSS places them: items with both lines where they say (overlapping if they
        // overlap), then those with only a row in its first free columns, then the rest in
        // order from a cursor that only moves forward; one with only a column takes the first
        // row from the cursor's where it fits in that column.
        for (size_t i = 0; i < items.size(); ++i)
            if (items[i].inFlow && items[i].col >= 0 && items[i].row >= 0)
                place(i, items[i].col, items[i].row);

        for (size_t i = 0; i < items.size(); ++i)
        {
            const Item& it = items[i];
            if (!it.inFlow || it.col >= 0 || it.row < 0)
                continue;
            int32_t col = 0;
            while (col + it.colSpan <= cols && !fits(col, it.row, it))
                ++col;
            place(i, col + it.colSpan <= cols ? col : 0, it.row);   // no room in its row: over the start
        }

        int32_t cursorCol = 0, cursorRow = 0;
        for (size_t i = 0; i < items.size(); ++i)
        {
            const Item& it = items[i];
            if (!it.inFlow || it.row >= 0)
                continue;
            if (it.col >= 0)
            {
                if (it.col < cursorCol)
                    ++cursorRow;
                int32_t row = cursorRow;
                while (!fits(it.col, row, it))
                    ++row;
                place(i, it.col, row);
                cursorRow = row;
                cursorCol = it.col + it.colSpan;
                continue;
            }
            // Every span fits the columns, so a row past all the placed ones always takes it.
            for (int32_t row = cursorRow; ; ++row)
            {
                int32_t col = row == cursorRow ? cursorCol : 0;
                while (col + it.colSpan <= cols && !fits(col, row, it))
                    ++col;
                if (col + it.colSpan <= cols)
                {
                    place(i, col, row);
                    cursorRow = row;
                    cursorCol = col + it.colSpan;
                    break;
                }
            }
        }

        outPlacement.effectiveColumnCount = size_t(cols);
        outPlacement.effectiveRowCount    = size_t(rows);
    }

    LayoutGrid::Scratch& LayoutGrid::ThreadScratch()
    {
        thread_local Scratch scratch;
        return scratch;
    }

    namespace
    {
        // One axis of a grid: 0 columns (x, width), 1 rows (y, height).
        template <int Axis> int32_t CellOf(int2 cell) { return Axis == 0 ? cell.x : cell.y; }
        template <int Axis> const GridAxisProperties& AxisOf(const GridProperties& grid) { return Axis == 0 ? grid.column : grid.row; }
        template <int Axis> int32_t ExtentOf(const IntRect& rect) { return Axis == 0 ? rect.w : rect.h; }

        // Children define a track: Fixed beats Fractional beats Auto, and the larger value wins.
        int TrackPriority(GridTrack::Type type)
        {
            switch (type)
            {
                case GridTrack::Type::Fixed:      return 3;
                case GridTrack::Type::Fractional: return 2;
                case GridTrack::Type::Auto:       return 1;
                default:                          return 0;
            }
        }
        void MergeTrack(GridTrack& existing, const GridTrack& child)
        {
            if (TrackPriority(child.type) > TrackPriority(existing.type))
                existing = child;
            else if (child.type == existing.type &&
                     (child.type == GridTrack::Type::Fixed || child.type == GridTrack::Type::Fractional))
                existing.value = std::max(existing.value, child.value);
        }
    }

    // Stages 2 and 3: the axis's tracks and intrinsic sizes, then its final sizes and offsets.
    template <int Axis>
    void LayoutGrid::ComputeAxisOffsets(const GridProperties& grid, std::span<const ElementRect> childRects,
                                        int32_t available, Scratch& s)
    {
        const GridPlacement& placement = s.placement;
        const size_t count = Axis == 0 ? placement.effectiveColumnCount : placement.effectiveRowCount;
        s.tracks.assign(count, GridTrack::Auto());
        s.intrinsics.assign(count, 0);

        // The tracks the children ask for, and the single-span items' sizes as the tracks'
        // intrinsic ones.
        for (size_t i = 0; i < childRects.size(); ++i)
        {
            if (!childRects[i].constraints.IsInFlow())
                continue;
            const int32_t cell = CellOf<Axis>(placement.cellPositions[i]);
            if (cell < 0 || cell >= static_cast<int32_t>(count))
                continue;
            const GridAxisProperties item = AxisOf<Axis>(childRects[i].constraints.GetGridItem(childRects[i].layoutStyle));
            MergeTrack(s.tracks[size_t(cell)], item.track);
            if (item.span == 1)
                s.intrinsics[size_t(cell)] = std::max(s.intrinsics[size_t(cell)], ExtentOf<Axis>(childRects[i].rect));
        }

        // Multi-span items must fit across the tracks so far: a shortfall is shared out
        // evenly over the tracks they span.
        for (size_t i = 0; i < childRects.size(); ++i)
        {
            if (!childRects[i].constraints.IsInFlow())
                continue;
            const GridAxisProperties item = AxisOf<Axis>(childRects[i].constraints.GetGridItem(childRects[i].layoutStyle));
            if (item.span <= 1)
                continue;
            const int32_t start = std::max(0, CellOf<Axis>(placement.cellPositions[i]));
            const int32_t end   = std::min<int32_t>(static_cast<int32_t>(count), start + item.span);
            if (start >= end)
                continue;
            int32_t current = 0;
            for (int32_t t = start; t < end; ++t)
                current += s.intrinsics[size_t(t)];
            const int32_t needed = ExtentOf<Axis>(childRects[i].rect);
            if (needed <= current)
                continue;
            const int32_t deficit = needed - current;
            const int32_t spanned = end - start;
            const int32_t addEach = deficit / spanned;
            int32_t remainder = deficit - addEach * spanned;
            for (int32_t t = start; t < end; ++t)
            {
                s.intrinsics[size_t(t)] += addEach;
                if (remainder > 0)
                {
                    s.intrinsics[size_t(t)] += 1;
                    remainder--;
                }
            }
        }

        // The space is the content box's; the gaps go in with the offsets.
        const int16_t gap = AxisOf<Axis>(grid).gap;
        const int32_t totalGaps = static_cast<int32_t>(std::max<size_t>(1, count) - 1) * gap;
        const int32_t availableTracks = available == INT32_MAX ? INT32_MAX : std::max(0, available - totalGaps);
        s.sizes.resize(count);
        ResolveTrackSizes(s.tracks, s.intrinsics, availableTracks, s.sizes);
        s.offsets.resize(count + 1);
        ComputeOffsets(s.sizes, gap, s.offsets);
    }

    void LayoutGrid::ResolveTrackSizes(std::span<const GridTrack> tracks,
                                       std::span<const int32_t> intrinsicSizes,
                                       int32_t availableSpace,
                                       std::span<int32_t> outSizes)
    {
        assert(outSizes.size() == intrinsicSizes.size());
        assert(tracks.size() == intrinsicSizes.size());

        const size_t trackCount = outSizes.size();
        const bool hasBoundedAvailableSpace = (availableSpace != INT32_MAX);

        int64_t fixedTotal = 0;
        double  frTotal = 0.0;
        int32_t frCount = 0;
        // A track's share of free space (0.5fr = half of 1fr); non-positive counts as 1fr.
        auto FrWeight = [](const GridTrack& track) { return track.value > 0.0f ? double(track.value) : 1.0; };

        for (size_t i = 0; i < trackCount; ++i)
        {
            const GridTrack& track = tracks[i];

            switch (track.type)
            {
                case GridTrack::Type::Fixed:
                {
                    outSizes[i] = static_cast<int32_t>(track.value);
                    fixedTotal += outSizes[i];
                    break;
                }
                case GridTrack::Type::Fractional:
                {
                    // minmax(max(minPx, auto), Nfr): floor here, free space added below.
                    const int32_t floorPx = std::max<int32_t>(
                        static_cast<int32_t>(track.minPx), intrinsicSizes[i]);
                    if (hasBoundedAvailableSpace)
                    {
                        outSizes[i] = floorPx;
                        fixedTotal += outSizes[i];
                        frTotal += FrWeight(track);
                        ++frCount;
                    }
                    else
                    {
                        // Unbounded (intrinsic) pass: fr tracks don't expand.
                        outSizes[i] = floorPx;
                        fixedTotal += outSizes[i];
                    }
                    break;
                }
                case GridTrack::Type::Auto:
                default:
                {
                    outSizes[i] = intrinsicSizes[i];
                    fixedTotal += intrinsicSizes[i];
                    break;
                }
            }
        }

        if (hasBoundedAvailableSpace && frCount > 0 && availableSpace > fixedTotal)
        {
            const int64_t freeSpace = static_cast<int64_t>(availableSpace) - fixedTotal;
            int64_t distributed = 0;

            for (size_t i = 0; i < trackCount; ++i)
            {
                const GridTrack& track = tracks[i];
                if (track.type == GridTrack::Type::Fractional)
                {
                    const int64_t size64 = static_cast<int64_t>(static_cast<double>(freeSpace) * FrWeight(track) / frTotal);
                    const int32_t extra = static_cast<int32_t>(std::clamp<int64_t>(size64, 0, INT32_MAX));
                    // On top of the floor.
                    outSizes[i] += extra;
                    distributed += extra;
                }
            }

            // Distribute remainder to first Fractional track.
            const int64_t remainder = freeSpace - distributed;
            if (remainder > 0)
            {
                for (size_t i = 0; i < trackCount; ++i)
                {
                    const GridTrack& track = tracks[i];
                    if (track.type == GridTrack::Type::Fractional)
                    {
                        const int64_t boosted = static_cast<int64_t>(outSizes[i]) + remainder;
                        outSizes[i] = static_cast<int32_t>(std::min<int64_t>(boosted, INT32_MAX));
                        break;
                    }
                }
            }
        }
        else if (hasBoundedAvailableSpace && frCount == 0 && availableSpace > fixedTotal)
        {
            int32_t freeSpace = static_cast<int32_t>(static_cast<int64_t>(availableSpace) - fixedTotal);
            size_t autoZeroCount = 0;

            for (size_t i = 0; i < trackCount; ++i)
            {
                const GridTrack& track = tracks[i];
                if (track.type == GridTrack::Type::Auto && intrinsicSizes[i] == 0)
                    autoZeroCount++;
            }

            if (autoZeroCount > 0)
            {
                int32_t perTrack = freeSpace / static_cast<int32_t>(autoZeroCount);
                int32_t remainder = freeSpace - (perTrack * static_cast<int32_t>(autoZeroCount));

                for (size_t i = 0; i < trackCount; ++i)
                {
                    const GridTrack& track = tracks[i];
                    if (track.type == GridTrack::Type::Auto && intrinsicSizes[i] == 0)
                    {
                        outSizes[i] += perTrack;
                        if (remainder > 0)
                        {
                            outSizes[i] += 1;
                            remainder--;
                        }
                    }
                }
            }
        }
    }

    void LayoutGrid::ComputeOffsets(std::span<const int32_t> sizes,
                                    int16_t gap,
                                    std::span<int32_t> outOffsets)
    {
        assert(outOffsets.size() == sizes.size() + 1);
        outOffsets[0] = 0;
        for (size_t i = 0; i < sizes.size(); ++i)
        {
            outOffsets[i + 1] = outOffsets[i] + sizes[i] + 
                               ((i + 1 < sizes.size()) ? gap : 0);
        }
    }

    int32_t LayoutGrid::HorizontalSizing(const ElementRect&           parentRect,
                                         std::span<const ElementRect> childRects)
    {
        if (childRects.empty())
            return 0;

        //TACHYON_PROFILE_SCOPE("LayoutGrid::HorizontalSizing");
        Scratch& s = ThreadScratch();
        DeterminePlacement(childRects, s.placement);
        ComputeAxisOffsets<0>(parentRect.layoutStyle->layoutData.grid, childRects, INT32_MAX, s);
        return s.offsets.back();
    }

    int32_t LayoutGrid::VerticalSizing(const ElementRect&           parentRect,
                                       std::span<const ElementRect> childRects)
    {
        if (childRects.empty())
            return 0;

        //TACHYON_PROFILE_SCOPE("LayoutGrid::VerticalSizing");
        Scratch& s = ThreadScratch();
        DeterminePlacement(childRects, s.placement);
        ComputeAxisOffsets<1>(parentRect.layoutStyle->layoutData.grid, childRects, INT32_MAX, s);
        return s.offsets.back();
    }

    void LayoutGrid::HorizontalPositioning(const ElementRect&      parentRect,
                                           std::span<ElementRect>  childRects)
    {
        if (childRects.empty())
            return;

        //TACHYON_PROFILE_SCOPE("LayoutGrid::HorizontalPositioning");
        const GridProperties& gridLayout = parentRect.layoutStyle->layoutData.grid;
        const IntRect& areaRect = parentRect.innerRect;

        Scratch& s = ThreadScratch();
        DeterminePlacement(childRects, s.placement);
        ComputeAxisOffsets<0>(gridLayout, childRects, areaRect.w, s);
        const GridPlacement& placement = s.placement;
        const size_t columnCount = placement.effectiveColumnCount;

        for (size_t i = 0; i < childRects.size(); ++i)
        {
            if (!childRects[i].constraints.IsInFlow())
                continue;

            const int2 cell = placement.cellPositions[i];
            if (cell.x >= static_cast<int32_t>(columnCount))
                continue;

            const GridProperties grid = childRects[i].constraints.GetGridItem(childRects[i].layoutStyle);

            // Calculate cell bounds accounting for spans.
            int32_t cellX = areaRect.x + s.offsets[cell.x];
            const size_t clampedEnd = std::min<size_t>(cell.x + grid.column.span, columnCount);
            int32_t cellEndX = areaRect.x + s.offsets[clampedEnd];

            // Offsets include a gap after each non-last track. Remove the trailing gap from this span.
            if (clampedEnd < columnCount)
                cellEndX -= gridLayout.column.gap;

            int32_t cellW = std::max(0, cellEndX - cellX);

            if (gridLayout.alignItemsSelf == AlignItems::Stretch)
            {
                // Stretch fills the cell only for an auto width; a set width starts the cell.
                childRects[i].rect.x = cellX;
                if (!childRects[i].constraints.HasFixedWidth())
                    childRects[i].rect.w = cellW;
            } else
            {
                IntRect cellRect{ cellX, 0, cellW, 0 };
                LayoutHelpers::JustifyChildren<0, false>(
                    childRects.subspan(i, 1),
                    gridLayout.justifyItems, 0, cellRect, childRects[i].rect.w, 1);
            }
        }
    }

    IntRect LayoutGrid::VerticalPositioning(const ElementRect&     parentRect,
                                            const IntRect&         clipRect,
                                            std::span<ElementRect> childRects)
    {
        if (childRects.empty())
        {
            return {};
        }

        //TACHYON_PROFILE_SCOPE("LayoutGrid::VerticalPositioning");
        const GridProperties& gridLayout = parentRect.layoutStyle->layoutData.grid;
        const IntRect& areaRect = parentRect.innerRect;

        Scratch& s = ThreadScratch();
        DeterminePlacement(childRects, s.placement);
        ComputeAxisOffsets<1>(gridLayout, childRects, areaRect.h, s);
        const GridPlacement& placement = s.placement;
        const size_t rowCount = placement.effectiveRowCount;

        IntRect contentRect {};

        for (size_t i = 0; i < childRects.size(); ++i)
        {
            if (!childRects[i].constraints.IsInFlow())
                continue;

            childRects[i].clipRect = clipRect;
            const int2 cell = placement.cellPositions[i];
            if (cell.y >= static_cast<int32_t>(rowCount))
                continue;

            const GridProperties grid = childRects[i].constraints.GetGridItem(childRects[i].layoutStyle);

            // Calculate cell bounds accounting for spans.
            int32_t cellY = areaRect.y + s.offsets[cell.y];
            const size_t clampedEnd = std::min<size_t>(cell.y + grid.row.span, rowCount);
            int32_t cellEndY = areaRect.y + s.offsets[clampedEnd];

            // Offsets include a gap after each non-last track. Remove the trailing gap from this span.
            if (clampedEnd < rowCount)
                cellEndY -= gridLayout.row.gap;

            int32_t cellH = std::max(0, cellEndY - cellY);

            IntRect cellRect{ 0, cellY, 0, cellH };

            LayoutHelpers::Align<1>(gridLayout.alignItems, cellRect, childRects[i]);

            // Bounds start at the content box's top-left (the empty rect they grow from): what a
            // scroll area can scroll over begins there, wherever the content starts.
            contentRect = IntRect::Union(contentRect, childRects[i].rect - areaRect.position);
        }

        return contentRect;
    }

} // namespace tachyon