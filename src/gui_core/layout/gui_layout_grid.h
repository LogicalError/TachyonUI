#pragma once

#include <stdint.h>
#include <span>
#include <vector>

namespace tachyon
{
    /// @addtogroup layout
    /// @{

    struct int2;
    struct IntRect;
    struct ElementRect;
    struct GridProperties;
    struct GridTrack;

    struct LayoutGrid
    {
        /// Returns the content width required by the grid.
        static int32_t HorizontalSizing(const ElementRect&           parentRect,
                                        std::span<const ElementRect> children);

        /// Returns the content height required by the grid.
        static int32_t VerticalSizing(const ElementRect&           parentRect,
                                      std::span<const ElementRect> children);

        static void HorizontalPositioning(const ElementRect&     parentRect,
                                          std::span<ElementRect> childRects);

        /// Returns the content bounds.
        static IntRect VerticalPositioning(const ElementRect&     parentRect,
                                           const IntRect&         clipRect,
                                           std::span<ElementRect> childRects);

      private:
        /// Grid cell per child.
        struct GridPlacement
        {
            std::vector<int2> cellPositions; ///< (col, row)
            size_t effectiveColumnCount = 0;
            size_t effectiveRowCount = 0;
        };

        /// What a sizing or positioning call works in: kept per thread and reused, so laying
        /// out a grid allocates nothing once the buffers have grown. A call is done with it
        /// before the next begins; grids don't nest inside a call.
        struct Scratch
        {
            GridPlacement          placement;
            std::vector<GridTrack> tracks;       ///< one axis's
            std::vector<int32_t>   intrinsics;   ///< one axis's max-content sizes
            std::vector<int32_t>   sizes;        ///< one axis's final sizes
            std::vector<int32_t>   offsets;      ///< one axis's track starts, count + 1
        };
        static Scratch& ThreadScratch();

        /// Stage 1: auto-placement and effective grid dimensions
        static void DeterminePlacement(std::span<const ElementRect> childRects,
                                       GridPlacement& outPlacement);

        /// Stages 2 and 3 along one axis (0: columns, 1: rows): its tracks, their intrinsic
        /// and final sizes in `available`, and their offsets, into `s`. A sizing or
        /// positioning pass needs only its own axis.
        template <int Axis>
        static void ComputeAxisOffsets(const GridProperties& grid, std::span<const ElementRect> childRects,
                                       int32_t available, Scratch& s);

        static void ResolveTrackSizes(std::span<const GridTrack> tracks,
                                      std::span<const int32_t> intrinsicSizes,
                                      int32_t availableSpace,
                                      std::span<int32_t> outSizes);

        static void ComputeOffsets(std::span<const int32_t> sizes, int16_t gap,
                                   std::span<int32_t> outOffsets);
    };

    /// @}
} // namespace tachyon