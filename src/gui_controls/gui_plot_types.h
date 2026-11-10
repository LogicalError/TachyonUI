#pragma once

// Plot: series of values drawn as lines, bars or a filled area over a range, with handles
// dragged along it (a Levels control's black, gray and white points).

#include <gui_controls/gui_curve_types.h>
#include <gui_core/input/gui_pointer_drag.h>
#include <gui_core/gui_base_types.h>

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace tachyon
{
    /// @addtogroup controls
    /// @{

    /// How a series is drawn.
    enum class PlotSeriesKind : uint8_t
    {
        Line,   ///< through the middle of each value's slot
        Bars,   ///< one bar per value, from zero
        Area    ///< filled from zero up to the line
    };

    /// How the y axis maps values.
    enum class PlotScale : uint8_t
    {
        Linear,
        Log     ///< log(1 + value): small counts stay visible beside large ones
    };

    /// One set of values, such as a histogram's red channel.
    struct PlotSeries
    {
        std::string       name;           ///< its chip's text when there are several
        PlotSeriesKind    kind = PlotSeriesKind::Bars;
        /// Its color, as a curve channel's: the warning color, the axis colors, the text color.
        CurveChannelColor color = CurveChannelColor::Value;
        /// Evenly spread over the x range: value i fills the i-th of values.size() equal slots.
        std::vector<float> values;
        bool              visible = true;
    };

    /// A marker dragged along the x axis under the plot.
    struct PlotHandle
    {
        std::string name;    ///< what it sets, for the app's own labels
        float       x = 0.0f;
        /// Its triangle's fill; alpha 0 uses the text color. A Levels control's are black,
        /// gray and white.
        color4f     color {};
    };

    /// A change to the handles: where each was and is.
    struct PlotEdit
    {
        std::vector<float> from, to;
        bool empty() const { return from == to; }
    };

    /// A plot: what it shows and how. Keep it from frame to frame, as a CurveView.
    struct PlotView
    {
        std::vector<PlotSeries> series;
        float xMin = 0.0f, xMax = 1.0f;
        /// The y range; with fitY, yMax follows the largest value shown.
        float yMin = 0.0f, yMax = 1.0f;
        bool  fitY = true;
        PlotScale yScale = PlotScale::Linear;
        bool  labels = true;       ///< the grid's numbers
        /// The series shown on its own (its chip picked), or -1 for all of them overlaid.
        int   shownSeries = -1;
        /// Kept in order, each between its neighbours and inside the x range.
        std::vector<PlotHandle> handles;

        /// A handle moved (already applied): put it in the undo history.
        std::function<void(const PlotEdit&)> onEdit;

        /// @name Internal: kept between frames while the user works
        ///@{
        PointerDrag        handleDrag;
        PointerDrag        panDrag;               ///< the view dragged (middle, or Alt + left)
        float              shownXMin = 0.0f, shownXMax = 1.0f;   ///< the part of the x range in view, once zoomed
        bool               shownSet = false;      ///< false: all of the x range
        float              panFromMin = 0.0f;     ///< shownXMin when the drag began
        int                activeHandle = 0;      ///< the one dragged last; the arrow keys move it
        std::vector<float> before;                ///< the handles as they were, for Escape and the edit
        IntRect            plotRect {};           ///< the plot last frame, in screen pixels
        float              hoverX = 0.0f;         ///< where the pointer is, along x
        bool               hovered = false;
        uint32_t           revision = 0;          ///< counts the edits committed, for Plot's return
        uint32_t           revisionSeen = 0;
        ///@}
    };

    /// `samples` counted into `bins` equal bins over [min, max]; a sample at max goes in the last
    /// one, and those outside or not finite in none.
    std::vector<float> HistogramBins(std::span<const float> samples, int bins, float min = 0.0f, float max = 1.0f);

    /// Does (or with `undo`, undoes) `edit` on `view`'s handles.
    void ApplyPlotEdit(PlotView& view, const PlotEdit& edit, bool undo = false);
    /// Moves handle `handle` to `x`, kept between its neighbours and inside the x range; the
    /// edit, or an empty one when it doesn't move.
    PlotEdit MovePlotHandleEdit(const PlotView& view, int handle, float x);
    /// Applies `edit` and hands it to `view.onEdit`; does nothing for an empty one.
    void CommitPlotEdit(PlotView& view, const PlotEdit& edit);

    /// A span of one axis.
    struct PlotRange { float min, max; };
    /// The part of the x range in view: all of it until the plot is zoomed, then the part
    /// zoomed to, kept inside [xMin, xMax].
    PlotRange PlotShownX(const PlotView& view);
    /// The y range drawn: yMax from the values in view with fitY, then mapped by yScale.
    PlotRange PlotShownY(const PlotView& view);
    /// `value` along the drawn y axis, by the view's scale.
    float PlotScaleY(const PlotView& view, float value);

    /// @}
}
