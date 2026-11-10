#include <gui_controls/gui_application.h>
#include <gui_controls/gui_plot_types.h>
#include <gui_controls/gui_plot_grid_internal.h>

#include <algorithm>
#include <cmath>
#include <format>

// Plot: series drawn as lines, bars or areas over an x range, a chip per series to show one
// on its own, the value under the pointer, and handles dragged along the x axis under it. It
// zooms and scrolls along x as the other canvases do (gui_navigation.h).

namespace tachyon
{
    namespace
    {
        // All of the x range, with room in it.
        PlotRange DataX(const PlotView& view)
        {
            float lo = view.xMin, hi = view.xMax;
            if (!std::isfinite(lo)) lo = 0.0f;
            if (!std::isfinite(hi) || !(hi > lo)) hi = lo + 1.0f;
            return { lo, hi };
        }

        // The part in view over `rect`.
        PlotGrid::Frame FrameOf(const PlotView& view, IntRect rect)
        {
            const PlotRange x = PlotShownX(view), y = PlotShownY(view);
            return { rect, x.min, x.max, y.min, y.max };
        }

        // Shows [lo, lo + span] of the x range; all of it again once it's that wide.
        void ShowX(PlotView& view, float lo, float span)
        {
            const PlotRange data = DataX(view);
            const float full = data.max - data.min;
            span = std::max(span, full / 1000.0f);
            if (!std::isfinite(lo) || span >= full)
            {
                view.shownSet = false;
                return;
            }
            view.shownXMin = std::clamp(lo, data.min, data.max - span);
            view.shownXMax = view.shownXMin + span;
            view.shownSet  = true;
        }

        // The polyline `points` cut to x in [lo, hi].
        std::vector<float2> ClipX(const std::vector<float2>& points, float lo, float hi)
        {
            std::vector<float2> out;
            auto at = [](float2 a, float2 b, float x) {
                const float t = (x - a.x) / (b.x - a.x);
                return float2{ x, a.y + (b.y - a.y) * t };
            };
            for (size_t i = 0; i < points.size(); ++i)
            {
                const float2 p = points[i];
                const bool inside = p.x >= lo && p.x <= hi;
                if (i > 0)
                {
                    const float2 q = points[i - 1];
                    if (q.x < lo && p.x > lo) out.push_back(at(q, p, lo));
                    if (q.x < hi && p.x > hi) { out.push_back(at(q, p, hi)); break; }
                }
                if (inside)
                    out.push_back(p);
            }
            return out;
        }

        // The handle under `pos`: the nearest within `reach` across, over the plot or the band
        // under it; -1 when none is.
        int HitHandle(const PlotView& view, const PlotGrid::Frame& f, float2 pos, float reach, float band)
        {
            if (pos.y < float(f.rect.y) || pos.y > float(f.rect.y + f.rect.h) + band)
                return -1;
            int best = -1;
            float bestDistance = reach;
            for (int h = 0; h < (int)view.handles.size(); ++h)
            {
                const float d = std::fabs(f.X(view.handles[size_t(h)].x) - pos.x);
                // Ties go to the later one, so handles pushed together still come apart to the right.
                if (d <= bestDistance) { best = h; bestDistance = d; }
            }
            return best;
        }

        std::vector<float> HandleXs(const PlotView& view)
        {
            std::vector<float> xs;
            for (const PlotHandle& h : view.handles)
                xs.push_back(h.x);
            return xs;
        }

        std::string Number(float v)
        {
            if (std::fabs(v) < 5e-5f) v = 0.0f;
            if (std::fabs(v - std::round(v)) < 1e-4f && std::fabs(v) < 1e7f)
                return std::format("{}", int64_t(std::llround(v)));
            return std::format("{:.3f}", v);
        }

        // The value of `series` in the slot under x; NaN outside.
        float ValueAt(const PlotSeries& series, float x, float xMin, float xMax)
        {
            const int n = (int)series.values.size();
            if (n == 0 || !(x >= xMin && x <= xMax))
                return std::nanf("");
            const int slot = std::min(n - 1, int((x - xMin) / (xMax - xMin) * float(n)));
            return series.values[size_t(slot)];
        }
    }

    bool GUIApplication::Plot(ID id, PlotView& view, LayoutOverride&& constraints)
    {
        PlotView* v = &view;
        const int count = (int)view.series.size();
        if (view.shownSeries >= count) view.shownSeries = -1;
        view.activeHandle = view.handles.empty() ? 0 : std::clamp(view.activeHandle, 0, (int)view.handles.size() - 1);
        const bool disabled = guiStateManager.Construction().IsDisabled();
        if (disabled && v->handleDrag.IsHeld())
        {
            ApplyPlotEdit(view, PlotEdit{ v->before, v->before });
            v->handleDrag.Reset();
            v->before.clear();
        }

        const bool changed = view.revision != view.revisionSeen;
        view.revisionSeen = view.revision;

        BeginVertical(id, theme.emptyVertStyleRef, std::move(constraints));
        const bool pushed = idManager.PushParent(id);

        // ---- The chips: all the series, or one ------------------------------------------
        if (count > 1)
        {
            BeginHorizontal(idManager.AllocateDynamicID("plot_chips"), theme.plotToolbarStyleRef, LayoutOverride::Auto().NoShrink());
            auto chip = [&](int s, std::string_view text, VisualColorStyleIndex color) {
                const bool picked = view.shownSeries == s;
                if (picked) BeginSelected();
                const ElementRef ref = RegisterControl(
                    Element{ .id = idManager.AllocateDynamicID(s < 0 ? std::string("plot_all") : std::format("plot_series_{}", s)),
                             .eventHandler = MakeButtonHandler([v, s]() { v->shownSeries = s; }),
                             .styleRef = theme.plotChannelStyleRef },
                    color == VisualColorStyleIndex{} ? std::string(text) : "   " + std::string(text), LayoutOverride{});
                if (picked) EndSelected();
                if (color == VisualColorStyleIndex{})
                    return;
                // Its color, as a small square before its name.
                const int32_t square = styleManager.Px(8), pad = styleManager.Px(8);
                VERIFY(elementStorage.AddCustomDrawAfter(ref, [color, square, pad](CustomDrawContext& ctx)
                {
                    const IntRect r = ctx.ElementRect();
                    ctx.RenderQuad(IntRect{ r.x + pad, r.y + (r.h - square) / 2, square, square }, color, float(square) / 4.0f);
                }));
            };
            chip(-1, "All", VisualColorStyleIndex{});
            for (int s = 0; s < count; ++s)
                chip(s, view.series[size_t(s)].name, theme.curveChannelColors[size_t(view.series[size_t(s)].color)]);
            EndHorizontal();
        }

        // ---- The plot ------------------------------------------------------------------
        const WheelMode wheelMode = GetWheelMode();
        const ID plotID = idManager.AllocateDynamicID("plot_area");
        const ElementState plotState = guiStateManager.GetCurrentState(plotID);
        if ((plotState & ElementState::Hover) == ElementState::None)
            view.hovered = false;
        const PlotGrid::Frame last = FrameOf(view, view.plotRect);
        const int32_t px1 = std::max(1, styleManager.Px(1)), px4 = styleManager.Px(4);
        const float band = float(styleManager.Px(12));   // the handles' strip under the plot

        // The grid's numbers, from where the plot was last frame; the y axis's on a linear scale.
        std::vector<PlotGrid::Label> labels;
        const float minGap = float(styleManager.Px(56)), minGapV = float(styleManager.Px(28));
        const float xStep = PlotGrid::NiceStep(last.XSpan(), float(last.rect.w), minGap);
        const float yStep = PlotGrid::NiceStep(last.YSpan(), float(last.rect.h), minGapV);
        if (view.labels && last.rect.w > 0 && last.rect.h > 0)
        {
            PlotGrid::AddLabels(*this, theme.plotLabelStyleRef, labels, last.xMin, last.xMax, xStep, /*onYAxis*/ false, "plot_x_labels");
            if (view.yScale == PlotScale::Linear)
                PlotGrid::AddLabels(*this, theme.plotLabelStyleRef, labels, last.yMin, last.yMax, yStep, /*onYAxis*/ true, "plot_y_labels");
        }

        // What's under the pointer: x, and each shown series' value there.
        TextID readout = TextID::Invalid;
        int2 readoutSize {};
        if (view.hovered && count > 0)
        {
            std::string text = Number(view.hoverX);
            for (int s = 0; s < count; ++s)
            {
                const PlotSeries& series = view.series[size_t(s)];
                if (!series.visible || (view.shownSeries >= 0 && view.shownSeries != s))
                    continue;
                const PlotRange data = DataX(view);
                const float value = ValueAt(series, view.hoverX, data.min, data.max);
                if (std::isfinite(value))
                    text += series.name.empty() ? std::format(" \xC2\xB7 {}", Number(value))
                                                : std::format(" \xC2\xB7 {} {}", series.name, Number(value));
            }
            readout = textSystem->RegisterText(idManager.AllocateDynamicID("plot_readout"), text, theme.plotReadoutStyleRef, ElementState::None);
            const TextMeasurement m = textSystem->MeasureText(readout, TextWrapContext{});
            readoutSize = { m.maxWidth, m.maxHeight };
        }

        EventHandler plotHandler;
        if (!disabled)
        {
            GUIStateManager& gsm = guiStateManager;
            const ElementStorage& es = elementStorage;
            const float reach = float(styleManager.Px(7));
            plotHandler = [&gsm, &es, v, reach, band, wheelMode](ElementEvent&& event) -> bool
            {
                if (event.rect == nullptr)
                    return false;
                const PlotGrid::Frame f = FrameOf(*v, event.rect->innerRect);
                const int2 cursor = event.LocalCursor(es);
                const float2 at{ float(cursor.x), float(cursor.y) };
                auto dragTo = [v, &f](float px) {
                    ApplyPlotEdit(*v, MovePlotHandleEdit(*v, v->activeHandle, f.AtX(px)));
                };
                switch (event.type)
                {
                    case EventType::MouseMove:
                    {
                        v->hovered = true;
                        v->hoverX = std::clamp(f.AtX(at.x), f.xMin, f.xMax);
                        if (v->panDrag.Move(event, 0))
                        {
                            const float span = f.xMax - f.xMin;
                            ShowX(*v, v->panFromMin - float(v->panDrag.ScreenDelta(event.state).x) * span / float(std::max(1, f.rect.w)), span);
                            return true;
                        }
                        if (v->handleDrag.Move(event, 0))
                        {
                            dragTo(at.x);
                            return true;
                        }
                        return false;
                    }
                    case EventType::MouseDown:
                    {
                        // The middle button (or Alt + left) drags the view, over a handle too.
                        if (const MouseButtons panButton = PanDragButton(event.state); panButton != MouseButtons::None)
                        {
                            gsm.Interaction().SetFocusID(event.id);
                            v->panDrag.Press(event, es, panButton, /*atOnce*/ true);
                            v->panFromMin = f.xMin;
                            gsm.Interaction().SetCaptureID(event.id);
                            return true;
                        }
                        if (!event.state.LeftPressed())
                            return false;
                        gsm.Interaction().SetFocusID(event.id);
                        gsm.TextEditorFocus().Clear();
                        const int hit = HitHandle(*v, f, at, reach, band);
                        if (hit < 0)
                            return true;
                        v->activeHandle = hit;
                        v->before = HandleXs(*v);
                        v->handleDrag.Press(event, es, MouseButtons::Left, /*atOnce*/ true);
                        gsm.Interaction().SetCaptureID(event.id);
                        return true;
                    }
                    case EventType::MouseScroll:
                    {
                        // Zooms round the pointer or scrolls along x, as the app's wheel mode
                        // says; up and down is left to what the plot is in.
                        const WheelNavigation nav = NavigateByWheel(event.state, wheelMode, 1.25f);
                        if (nav.zoom == 1.0f && nav.pan.x == 0.0f)
                            return false;
                        const float span = f.xMax - f.xMin;
                        if (nav.zoom != 1.0f)
                        {
                            const float pivot = std::clamp(f.AtX(at.x), f.xMin, f.xMax);
                            const float newSpan = span / nav.zoom;
                            ShowX(*v, pivot - (pivot - f.xMin) * newSpan / span, newSpan);
                        }
                        if (nav.pan.x != 0.0f)
                        {
                            const PlotRange shown = PlotShownX(*v);
                            ShowX(*v, shown.min + nav.pan.x * 0.1f * (shown.max - shown.min), shown.max - shown.min);
                        }
                        return true;
                    }
                    case EventType::MouseUp:
                    {
                        if (v->panDrag.IsPressed(event.id))
                            return v->panDrag.Release(event) != PointerDrag::Ended::No;
                        if (v->handleDrag.Release(event) == PointerDrag::Ended::No)
                            return false;
                        // Already where it was dropped: the edit, for undo.
                        const PlotEdit edit{ v->before, HandleXs(*v) };
                        v->before.clear();
                        if (!edit.empty())
                        {
                            ++v->revision;
                            if (v->onEdit)
                                v->onEdit(edit);
                        }
                        return true;
                    }
                    case EventType::KeyDown:
                    {
                        if (gsm.Interaction().GetFocusedID() != event.id || event.state.consumed)
                            return false;
                        const KeyCode key = event.state.lastKey;
                        if (key == KeyCode::Escape && v->panDrag.IsPressed(event.id))
                        {
                            ShowX(*v, v->panFromMin, f.xMax - f.xMin);
                            v->panDrag.Cancel(event.id);
                            return true;
                        }
                        if (key == KeyCode::KeyA && event.state.modifiers == KeyModifier::None)
                        {
                            v->shownSet = false;   // all of it
                            return true;
                        }
                        if (key == KeyCode::Escape)
                        {
                            if (!v->handleDrag.IsPressed(event.id))
                                return false;
                            ApplyPlotEdit(*v, PlotEdit{ v->before, v->before });
                            v->handleDrag.Cancel(event.id);
                            v->before.clear();
                            return true;
                        }
                        if ((key == KeyCode::Left || key == KeyCode::Right) && !v->handles.empty() && !v->handleDrag.IsHeld())
                        {
                            const bool shift = (event.state.modifiers & KeyModifier::Shift) != KeyModifier::None;
                            const float step = (f.xMax - f.xMin) / (shift ? 10.0f : 100.0f) * (key == KeyCode::Left ? -1.0f : 1.0f);
                            const int h = v->activeHandle;
                            CommitPlotEdit(*v, MovePlotHandleEdit(*v, h, v->handles[size_t(h)].x + step));
                            return true;
                        }
                        return false;
                    }
                    default:
                        return false;
                }
            };
        }

        const ElementRef plotRef = RegisterControl(
            Element{ .id = plotID, .eventHandler = std::move(plotHandler), .styleRef = theme.plotStyleRef, .state = plotState },
            LayoutOverride::Auto().FlexGrow(1));
        VERIFY(elementStorage.AddPostLayout(plotRef, [v](PostLayoutContext& ctx) { v->plotRect = ctx.ElementInnerRect(); }));

        const float lineWidth = float(px1) * 1.5f, shownWidth = float(px1) * 2.0f;
        const float handleSize = float(styleManager.Px(5));
        const VisualBoxStyleIndex readoutBox = theme.plotReadoutStyleRef.visualBoxStyleIndex;
        const int2 readoutPad{ styleManager.Px(6), styleManager.Px(2) };
        const int32_t labelBelow = int32_t(band) + px4;
        VERIFY(elementStorage.AddCustomDraw(plotRef, [=, this](CustomDrawContext& ctx)
        {
            const IntRect inner = ctx.ElementInnerRect();
            const PlotGrid::Frame f = FrameOf(*v, inner);

            PlotGrid::DrawLines(ctx, f, PlotGrid::NiceStep(f.XSpan(), float(inner.w), minGap),
                                v->yScale == PlotScale::Linear ? PlotGrid::NiceStep(f.YSpan(), float(inner.h), minGapV) : 0.0f,
                                { theme.curveGridColor, theme.curveGridMajorColor, theme.curveZeroColor }, px1);
            PlotGrid::DrawLabels(ctx, f, labels, px4, labelBelow);

            // The series: all overlaid and faded, or the one picked at full strength. Their
            // slots spread over all of the x range; the parts out of view aren't drawn.
            const PlotRange data = DataX(*v);
            const float left = float(inner.x), right = float(inner.x + inner.w);
            const float baseY = f.Y(std::clamp(PlotScaleY(*v, 0.0f), f.yMin, f.yMax));
            for (int s = 0; s < (int)v->series.size(); ++s)
            {
                const PlotSeries& series = v->series[size_t(s)];
                const int n = (int)series.values.size();
                if (!series.visible || n == 0 || (v->shownSeries >= 0 && v->shownSeries != s))
                    continue;
                const bool alone = v->shownSeries == s || v->series.size() == 1;
                const VisualColorStyleIndex full  = theme.curveChannelColors[size_t(series.color)];
                const VisualColorStyleIndex faded = theme.curveChannelFadedColors[size_t(series.color)];
                const float slot = (data.max - data.min) / float(n);
                auto top = [&](int i) {
                    const float value = series.values[size_t(i)];
                    return f.Y(std::clamp(PlotScaleY(*v, std::isfinite(value) ? value : 0.0f), f.yMin, f.yMax));
                };
                switch (series.kind)
                {
                    case PlotSeriesKind::Bars:
                    {
                        // Bars touch when narrow; a pixel apart once there's room.
                        const int32_t gap = f.X(data.min + slot) - f.X(data.min) >= float(4 * px1) ? px1 : 0;
                        for (int i = 0; i < n; ++i)
                        {
                            const int32_t x0 = int32_t(std::lround(f.X(data.min + slot * float(i))));
                            const int32_t x1 = int32_t(std::lround(f.X(data.min + slot * float(i + 1))));
                            if (float(x1) <= left || float(x0) >= right)
                                continue;
                            const float y = top(i);
                            const int32_t y0 = int32_t(std::lround(std::min(y, baseY))), y1 = int32_t(std::lround(std::max(y, baseY)));
                            if (y1 > y0)
                                ctx.RenderQuad(IntRect{ x0, y0, std::max(px1, x1 - x0 - gap), y1 - y0 }, inner, alone ? full : faded);
                        }
                        break;
                    }
                    case PlotSeriesKind::Area:
                    case PlotSeriesKind::Line:
                    {
                        // Through the middle of each slot, held level to the plot's edges.
                        std::vector<float2> all;
                        all.push_back({ f.X(data.min), top(0) });
                        for (int i = 0; i < n; ++i)
                            all.push_back({ f.X(data.min + slot * (float(i) + 0.5f)), top(i) });
                        all.push_back({ f.X(data.max), top(n - 1) });
                        const std::vector<float2> points = ClipX(all, left, right);
                        if (series.kind == PlotSeriesKind::Area)
                        {
                            for (size_t i = 1; i < points.size(); ++i)
                            {
                                const float2 a = points[i - 1], b = points[i];
                                const float2 aBase{ a.x, baseY }, bBase{ b.x, baseY };
                                // Two triangles a slot; the edges they share drawn hard, so no seam shows.
                                ctx.RenderTriangle(aBase, a, b, faded, ImageIndex::Invalid, TriangleEdges::Edge20 | TriangleEdges::Edge01);
                                ctx.RenderTriangle(aBase, b, bBase, faded, ImageIndex::Invalid, TriangleEdges::Edge01 | TriangleEdges::Edge12);
                            }
                        }
                        const LineStyle style{ .colorStyleIndex = full, .lineWidth = alone ? shownWidth : lineWidth };
                        for (size_t i = 1; i < points.size(); ++i)
                            ctx.RenderLine(int2{ int32_t(std::lround(points[i - 1].x)), int32_t(std::lround(points[i - 1].y)) },
                                           int2{ int32_t(std::lround(points[i].x)), int32_t(std::lround(points[i].y)) }, style);
                        break;
                    }
                }
            }

            // The handles: a line through the plot, and a triangle under it pointing up.
            for (int h = 0; h < (int)v->handles.size(); ++h)
            {
                const PlotHandle& handle = v->handles[size_t(h)];
                if (handle.x < f.xMin || handle.x > f.xMax)
                    continue;   // out of view
                const float x = std::round(f.X(handle.x));
                const bool active = h == v->activeHandle && (plotState & ElementState::Focused) != ElementState::None;
                ctx.RenderQuad(IntRect{ int32_t(x), inner.y, px1, inner.h }, inner, theme.curveZeroColor);
                const float y0 = float(inner.y + inner.h) + float(px1);
                const float2 tip{ x, y0 }, baseL{ x - handleSize, y0 + handleSize * 1.7f }, baseR{ x + handleSize, y0 + handleSize * 1.7f };
                const VisualColorStyleIndex fill = handle.color.a > 0.0f
                    ? styleManager.RegisterColorStyle(ColorStyle::Create(handle.color)) : theme.curveSelectedKeyColor;
                ctx.RenderTriangle(tip, baseL, baseR, fill);
                const int2 outline[] = { { int32_t(tip.x), int32_t(tip.y) }, { int32_t(baseL.x), int32_t(baseL.y) }, { int32_t(baseR.x), int32_t(baseR.y) } };
                ctx.RenderPolyline(outline, /*closed*/ true,
                                   LineStyle{ .colorStyleIndex = active ? theme.curveHandleColor : theme.curveKeyEdgeColor, .lineWidth = float(px1) });
            }

            // The value under the pointer, at the top left.
            if (readout != TextID::Invalid)
            {
                const IntRect box{ inner.x + px4, inner.y + px4, readoutSize.x + 2 * readoutPad.x, readoutSize.y + 2 * readoutPad.y };
                ctx.RenderBox(box, readoutBox);
                ctx.RenderText(readout, IntRect{ box.x + readoutPad.x, box.y + readoutPad.y, readoutSize.x, readoutSize.y });
            }
        }));

        if (pushed)
            (void)idManager.PopParent();
        EndVertical();
        return changed;
    }
}
