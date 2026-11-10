#include <gui_controls/gui_plot_types.h>

#include <algorithm>
#include <cmath>

namespace tachyon
{
    std::vector<float> HistogramBins(std::span<const float> samples, int bins, float min, float max)
    {
        if (bins <= 0)
            return {};
        std::vector<float> counts(size_t(bins), 0.0f);
        if (!(max > min))
            return counts;
        const float scale = float(bins) / (max - min);
        for (const float s : samples)
        {
            if (!(s >= min && s <= max))   // outside, or NaN
                continue;
            const int bin = std::min(bins - 1, int((s - min) * scale));
            counts[size_t(bin)] += 1.0f;
        }
        return counts;
    }

    void ApplyPlotEdit(PlotView& view, const PlotEdit& edit, bool undo)
    {
        const std::vector<float>& xs = undo ? edit.from : edit.to;
        if (xs.size() != view.handles.size())
            return;
        for (size_t h = 0; h < xs.size(); ++h)
            view.handles[h].x = xs[h];
    }

    PlotEdit MovePlotHandleEdit(const PlotView& view, int handle, float x)
    {
        if (handle < 0 || handle >= (int)view.handles.size() || !std::isfinite(x))
            return {};
        PlotEdit edit;
        for (const PlotHandle& h : view.handles)
            edit.from.push_back(h.x);
        edit.to = edit.from;
        const float lo = handle > 0 ? std::max(view.xMin, edit.from[size_t(handle - 1)]) : view.xMin;
        const float hi = handle + 1 < (int)edit.from.size() ? std::min(view.xMax, edit.from[size_t(handle + 1)]) : view.xMax;
        edit.to[size_t(handle)] = std::clamp(x, lo, std::max(lo, hi));
        return edit;
    }

    void CommitPlotEdit(PlotView& view, const PlotEdit& edit)
    {
        if (edit.empty())
            return;
        ApplyPlotEdit(view, edit);
        ++view.revision;
        if (view.onEdit)
            view.onEdit(edit);
    }

    float PlotScaleY(const PlotView& view, float value)
    {
        if (view.yScale == PlotScale::Log)
            return value > 0.0f ? std::log10(1.0f + value) : 0.0f;
        return value;
    }

    PlotRange PlotShownX(const PlotView& view)
    {
        float lo = view.xMin, hi = view.xMax;
        if (!std::isfinite(lo)) lo = 0.0f;
        if (!std::isfinite(hi) || !(hi > lo)) hi = lo + 1.0f;
        if (!view.shownSet || !std::isfinite(view.shownXMin) || !std::isfinite(view.shownXMax) || !(view.shownXMax > view.shownXMin))
            return { lo, hi };
        const float span = std::min(view.shownXMax - view.shownXMin, hi - lo);
        const float start = std::clamp(view.shownXMin, lo, hi - span);
        return { start, start + span };
    }

    PlotRange PlotShownY(const PlotView& view)
    {
        float lo = view.yMin, hi = view.yMax;
        if (view.fitY)
        {
            lo = 0.0f;
            hi = 0.0f;
            float xLo = view.xMin, xHi = view.xMax;
            if (!(xHi > xLo)) xHi = xLo + 1.0f;
            const PlotRange shown = PlotShownX(view);
            for (int s = 0; s < (int)view.series.size(); ++s)
            {
                const PlotSeries& series = view.series[size_t(s)];
                if (!series.visible || (view.shownSeries >= 0 && view.shownSeries != s))
                    continue;
                const int n = (int)series.values.size();
                const float slot = (xHi - xLo) / float(std::max(1, n));
                for (int i = 0; i < n; ++i)
                {
                    // Only the slots at least partly in view.
                    const float from = xLo + slot * float(i);
                    if (from + slot <= shown.min || from >= shown.max)
                        continue;
                    if (std::isfinite(series.values[size_t(i)]))
                        hi = std::max(hi, series.values[size_t(i)]);
                }
            }
        }
        lo = PlotScaleY(view, lo);
        hi = PlotScaleY(view, hi);
        if (!(hi > lo))
            hi = lo + 1.0f;
        return { lo, hi };
    }
}
