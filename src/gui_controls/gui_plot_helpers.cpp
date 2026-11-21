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

    void ApplyPlotEdit(PlotData& data, const PlotEdit& edit, bool undo)
    {
        const std::vector<float>& xs = undo ? edit.from : edit.to;
        if (xs.size() != data.handles.size())
            return;
        for (size_t h = 0; h < xs.size(); ++h)
            data.handles[h].x = xs[h];
    }

    PlotEdit MovePlotHandleEdit(const PlotData& data, int handle, float x)
    {
        if (handle < 0 || handle >= (int)data.handles.size() || !std::isfinite(x))
            return {};
        PlotEdit edit;
        for (const PlotHandle& h : data.handles)
            edit.from.push_back(h.x);
        edit.to = edit.from;
        const float lo = handle > 0 ? std::max(data.xMin, edit.from[size_t(handle - 1)]) : data.xMin;
        const float hi = handle + 1 < (int)edit.from.size() ? std::min(data.xMax, edit.from[size_t(handle + 1)]) : data.xMax;
        edit.to[size_t(handle)] = std::clamp(x, lo, std::max(lo, hi));
        return edit;
    }

    void CommitPlotEdit(PlotViewRef view, const PlotEdit& edit)
    {
        if (edit.empty())
            return;
        ApplyPlotEdit(view.data, edit);
        ++view.state.edits;
        if (view.data.onEdit)
            view.data.onEdit(edit);
    }

    float PlotScaleY(const PlotData& data, float value)
    {
        if (data.yScale == PlotScale::Log)
            return value > 0.0f ? std::log10(1.0f + value) : 0.0f;
        return value;
    }

    PlotRange PlotShownX(ConstPlotViewRef view)
    {
        float lo = view.data.xMin, hi = view.data.xMax;
        if (!std::isfinite(lo)) lo = 0.0f;
        if (!std::isfinite(hi) || !(hi > lo)) hi = lo + 1.0f;
        if (!view.state.shownSet || !std::isfinite(view.state.shownXMin) || !std::isfinite(view.state.shownXMax) || !(view.state.shownXMax > view.state.shownXMin))
            return { lo, hi };
        const float span = std::min(view.state.shownXMax - view.state.shownXMin, hi - lo);
        const float start = std::clamp(view.state.shownXMin, lo, hi - span);
        return { start, start + span };
    }

    PlotRange PlotShownY(ConstPlotViewRef view)
    {
        float lo = view.data.yMin, hi = view.data.yMax;
        if (view.data.fitY)
        {
            // From yMin, as far as the values shown reach either way.
            lo = view.data.yMin;
            hi = view.data.yMin;
            float xLo = view.data.xMin, xHi = view.data.xMax;
            if (!(xHi > xLo)) xHi = xLo + 1.0f;
            const PlotRange shown = PlotShownX(view);
            for (int s = 0; s < (int)view.data.series.size(); ++s)
            {
                const PlotSeries& series = view.data.series[size_t(s)];
                if (!series.visible || (view.state.shownSeries >= 0 && view.state.shownSeries != s))
                    continue;
                const int n = (int)series.values.size();
                const float slot = (xHi - xLo) / float(std::max(1, n));
                for (int i = 0; i < n; ++i)
                {
                    // Only the slots at least partly in view.
                    const float from = xLo + slot * float(i);
                    if (from + slot <= shown.min || from >= shown.max)
                        continue;
                    if (const float value = series.values[size_t(i)]; std::isfinite(value))
                    {
                        lo = std::min(lo, value);
                        hi = std::max(hi, value);
                    }
                }
            }
        }
        lo = PlotScaleY(view.data, lo);
        hi = PlotScaleY(view.data, hi);
        if (!(hi > lo))
            hi = lo + 1.0f;
        return { lo, hi };
    }
}
