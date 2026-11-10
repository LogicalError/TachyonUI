#pragma once

// The grid under a plot, shared by the curve editor and Plot: where its lines go, their
// numbers, and drawing both.

#include <gui_controls/gui_application.h>
#include <gui_core/graphics/gui_custom_draw_context.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>

namespace tachyon::PlotGrid
{
    /// A range shown over a rect, y up.
    struct Frame
    {
        IntRect rect;
        float   xMin = 0.0f, xMax = 1.0f, yMin = 0.0f, yMax = 1.0f;

        float XSpan() const { return xMax - xMin; }
        float YSpan() const { return yMax - yMin; }
        float X(float x) const { return float(rect.x) + (x - xMin) / XSpan() * float(rect.w); }
        float Y(float y) const { return float(rect.y + rect.h) - (y - yMin) / YSpan() * float(rect.h); }
        float AtX(float px) const { return xMin + (px - float(rect.x)) / float(std::max(1, rect.w)) * XSpan(); }
        float AtY(float py) const { return yMin + (float(rect.y + rect.h) - py) / float(std::max(1, rect.h)) * YSpan(); }
    };

    /// A step of 1, 2 or 5 times a power of ten, at least `minPixels` apart over `pixels`.
    inline float NiceStep(float span, float pixels, float minPixels)
    {
        if (!(span > 0.0f) || !(pixels > 0.0f))
            return 0.0f;
        const float raw = span * minPixels / pixels;
        const float power = std::pow(10.0f, std::floor(std::log10(raw)));
        for (const float m : { 1.0f, 2.0f, 5.0f })
            if (m * power >= raw)
                return m * power;
        return 10.0f * power;
    }

    /// A grid line's number, as short as its step lets it be.
    inline std::string Number(float v, float step)
    {
        const int decimals = std::clamp(int(std::ceil(-std::log10(step) - 1e-4f)), 0, 6);
        if (std::fabs(v) < step * 1e-3f) v = 0.0f;
        return std::format("{:.{}f}", v, decimals);
    }

    /// A labelled grid line's number, registered for drawing.
    struct Label
    {
        TextID text;
        float  at;          ///< where along its axis
        int2   size;
        bool   onYAxis;     ///< at the left edge; else under the plot
    };

    /// Registers the numbers of the labelled lines between lo and hi, `step` apart; `hint`
    /// names their IDs.
    inline void AddLabels(GUIApplication& app, StyleRef style, std::vector<Label>& out,
                          float lo, float hi, float step, bool onYAxis, std::string_view hint)
    {
        if (!(step > 0.0f))
            return;
        const int first = int(std::ceil(lo / step - 1e-3f)), last = int(std::floor(hi / step + 1e-3f));
        if (last < first || last - first > 200)
            return;
        const ID rangeID = app.idManager.AllocateRangedID(uint32_t(last - first + 1), hint);
        for (int i = first; i <= last; ++i)
        {
            const float at = float(i) * step;
            const TextID text = app.textSystem->RegisterText(app.idManager.GetSubID(rangeID, uint32_t(i - first)),
                                                             Number(at, step), style, ElementState::None);
            const TextMeasurement m = app.textSystem->MeasureText(text, TextWrapContext{});
            out.push_back({ text, at, { m.maxWidth, m.maxHeight }, onYAxis });
        }
    }

    /// The colors of the grid's lines.
    struct Colors
    {
        VisualColorStyleIndex minor, major, zero;
    };

    /// Lines a quarter of a labelled step apart, the labelled ones brighter, the lines at zero
    /// brighter still; a step of 0 draws none along that axis.
    inline void DrawLines(CustomDrawContext& ctx, const Frame& f, float xStep, float yStep, const Colors& colors, int32_t px1)
    {
        auto lines = [&](float lo, float hi, float step, bool vertical) {
            if (!(step > 0.0f))
                return;
            const float minor = step / 4.0f;
            const int first = int(std::ceil(lo / minor - 1e-3f)), last = int(std::floor(hi / minor + 1e-3f));
            if (last - first > 2000)
                return;
            for (int i = first; i <= last; ++i)
            {
                const VisualColorStyleIndex color = i == 0 ? colors.zero : (i % 4 == 0 ? colors.major : colors.minor);
                if (vertical)
                    ctx.RenderQuad(IntRect{ int32_t(f.X(float(i) * minor)), f.rect.y, px1, f.rect.h }, f.rect, color);
                else
                    ctx.RenderQuad(IntRect{ f.rect.x, int32_t(f.Y(float(i) * minor)), f.rect.w, px1 }, f.rect, color);
            }
        };
        lines(f.xMin, f.xMax, xStep, true);
        lines(f.yMin, f.yMax, yStep, false);
    }

    /// The numbers: the y axis's at the left edge, the x axis's `below` pixels under the plot.
    inline void DrawLabels(CustomDrawContext& ctx, const Frame& f, const std::vector<Label>& labels, int32_t gap, int32_t below)
    {
        for (const Label& label : labels)
        {
            if (label.onYAxis)
                ctx.RenderText(label.text, IntRect{ f.rect.x - label.size.x - gap, int32_t(f.Y(label.at)) - label.size.y / 2,
                                                    label.size.x, label.size.y });
            else
                ctx.RenderText(label.text, IntRect{ int32_t(f.X(label.at)) - label.size.x / 2, f.rect.y + f.rect.h + below,
                                                    label.size.x, label.size.y });
        }
    }
}
