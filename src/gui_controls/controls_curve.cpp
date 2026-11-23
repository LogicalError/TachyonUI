#include <gui_controls/gui_application.h>
#include <gui_controls/gui_curve_types.h>
#include <gui_controls/gui_plot_grid_internal.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <tuple>

// CurveField and the curve editor it opens: a curve in a box, and in a window under it the
// plot with its grid, curves, keys and tangent handles; the channels and the selected key over
// it, the presets and wrap modes under it. Every change is a CurveEdit, for undo.

namespace tachyon
{
    namespace
    {
        // Preset `p` of the curves' own list, or of the built-in ones when they have none.
        const CurvePreset* PresetAt(const CurveData& data, size_t p)
        {
            const std::span<const CurvePreset> list = data.presets.empty() ? DefaultCurvePresets() : std::span<const CurvePreset>(data.presets);
            return p < list.size() ? &list[p] : nullptr;
        }

        // `r` with room in it both ways.
        CurveRange Sane(CurveRange r)
        {
            auto widen = [](float& lo, float& hi) {
                if (!std::isfinite(lo)) lo = 0.0f;
                if (!std::isfinite(hi)) hi = lo + 1.0f;
                if (!(hi > lo)) { const float mid = (lo + hi) * 0.5f; lo = mid - 0.5f; hi = mid + 0.5f; }
            };
            widen(r.timeMin, r.timeMax);
            widen(r.valueMin, r.valueMax);
            return r;
        }

        CurveRange ShownOf(ConstCurveViewRef view) { return Sane(view.state.shownSet ? view.state.shown : view.data.range); }

        // A range shown over a rect, value up.
        struct CurvePlot
        {
            IntRect    rect;
            CurveRange shown;

            float TimeSpan()  const { return shown.timeMax - shown.timeMin; }
            float ValueSpan() const { return shown.valueMax - shown.valueMin; }
            float X(float t) const { return float(rect.x) + (t - shown.timeMin) / TimeSpan() * float(rect.w); }
            float Y(float v) const { return float(rect.y + rect.h) - (v - shown.valueMin) / ValueSpan() * float(rect.h); }
            float Time(float x)  const { return shown.timeMin + (x - float(rect.x)) / float(std::max(1, rect.w)) * TimeSpan(); }
            float Value(float y) const { return shown.valueMin + (float(rect.y + rect.h) - y) / float(std::max(1, rect.h)) * ValueSpan(); }
            float2 At(const CurveKey& k) const { return { X(k.time), Y(k.value) }; }
        };

        using PlotGrid::NiceStep;

        PlotGrid::Frame GridFrame(const CurvePlot& plot)
        {
            return { plot.rect, plot.shown.timeMin, plot.shown.timeMax, plot.shown.valueMin, plot.shown.valueMax };
        }

        float Distance(float2 a, float2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

        // The key under `pos`: the active channel's first, then the other visible ones.
        struct KeyHit { int channel = -1; int key = -1; ID id = ID::Invalid(); };
        KeyHit HitKey(ConstCurveViewRef view, const CurvePlot& plot, float2 pos, float reach)
        {
            const int count = (int)view.data.channels.size();
            for (int n = 0; n < count; ++n)
            {
                const int c = n == 0 ? view.state.activeChannel : (n <= view.state.activeChannel ? n - 1 : n);
                if (c < 0 || c >= count || !view.data.channels[size_t(c)].visible)
                    continue;
                const auto& keys = view.data.channels[size_t(c)].curve.keys;
                KeyHit best;
                float bestDistance = reach;
                for (int k = 0; k < (int)keys.size(); ++k)
                {
                    const float d = Distance(plot.At(keys[size_t(k)]), pos);
                    if (d <= bestDistance) { best = { c, k, keys[size_t(k)].stableID }; bestDistance = d; }
                }
                if (best.key >= 0)
                    return best;
            }
            return {};
        }

        // The visible curve passing within `reach` of a point, the active one first; -1 for none.
        // The curve is sampled across the reach so steep parts are caught as well as flat ones.
        int HitCurve(ConstCurveViewRef view, const CurvePlot& plot, float2 pos, float reach)
        {
            constexpr int kSamples = 8;
            auto segmentDistance = [](float2 p, float2 a, float2 b) {
                const float2 ab{ b.x - a.x, b.y - a.y };
                const float lengthSq = ab.x * ab.x + ab.y * ab.y;
                const float t = lengthSq > 0.0f ? std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lengthSq, 0.0f, 1.0f) : 0.0f;
                return Distance(p, float2{ a.x + ab.x * t, a.y + ab.y * t });
            };
            const int count = (int)view.data.channels.size();
            for (int n = 0; n < count; ++n)
            {
                const int c = n == 0 ? view.state.activeChannel : (n <= view.state.activeChannel ? n - 1 : n);
                if (c < 0 || c >= count || !view.data.channels[size_t(c)].visible)
                    continue;
                const Curve& curve = view.data.channels[size_t(c)].curve;
                if (curve.keys.empty())
                    continue;
                float2 previous{};
                for (int s = 0; s <= kSamples; ++s)
                {
                    const float x = pos.x - reach + 2.0f * reach * float(s) / float(kSamples);
                    const float2 point{ x, plot.Y(EvaluateCurve(curve, plot.Time(x))) };
                    if (s > 0 && segmentDistance(pos, previous, point) <= reach)
                        return c;
                    previous = point;
                }
            }
            return -1;
        }

        // Whether key `index` shows a handle on a side: only where a Bezier segment takes its
        // slope, the one before it arriving and its own leaving.
        bool HasHandle(const Curve& curve, size_t index, bool out)
        {
            const auto& keys = curve.keys;
            if (out)
                return index + 1 < keys.size() && keys[index].interpolation == CurveInterpolation::Bezier;
            return index > 0 && keys[index - 1].interpolation == CurveInterpolation::Bezier;
        }

        // The end of a key's handle on a side, `length` pixels from it along its slope.
        float2 HandleEnd(const CurvePlot& plot, const CurveKey& k, bool out, float length)
        {
            const float sx = float(plot.rect.w) / plot.TimeSpan(), sy = float(plot.rect.h) / plot.ValueSpan();
            const float slope = out ? k.outSlope : k.inSlope;
            float dx = 1.0f, dy = -slope * sy / std::max(sx, 1e-6f);
            const float len = std::max(std::hypot(dx, dy), 1e-6f);
            dx /= len; dy /= len;
            if (!out) { dx = -dx; dy = -dy; }
            const float2 at = plot.At(k);
            return { at.x + dx * length, at.y + dy * length };
        }

        // The handle under `pos`, of a selected key on a visible channel.
        struct HandleHit { int channel = -1; int key = -1; bool out = false; };
        HandleHit HitHandle(ConstCurveViewRef view, const CurvePlot& plot, float2 pos, float reach, float length)
        {
            for (int c = 0; c < (int)view.data.channels.size(); ++c)
            {
                const CurveChannel& channel = view.data.channels[size_t(c)];
                if (!channel.visible)
                    continue;
                for (int k = 0; k < (int)channel.curve.keys.size(); ++k)
                {
                    const CurveKey& key = channel.curve.keys[size_t(k)];
                    if (!view.state.selectedKeys.contains(key.stableID))
                        continue;
                    for (const bool out : { true, false })
                        if (HasHandle(channel.curve, size_t(k), out) && Distance(HandleEnd(plot, key, out, length), pos) <= reach)
                            return { c, k, out };
                }
            }
            return {};
        }

        std::vector<Curve> CurvesOf(ConstCurveViewRef view)
        {
            std::vector<Curve> curves;
            for (const CurveChannel& channel : view.data.channels)
                curves.push_back(channel.curve);
            return curves;
        }

        // The curves as they are now, against `before`: the edit.
        CurveEdit ChangesSince(ConstCurveViewRef view, const std::vector<Curve>& before)
        {
            CurveEdit edit;
            for (int c = 0; c < (int)view.data.channels.size() && c < (int)before.size(); ++c)
                if (!(view.data.channels[size_t(c)].curve == before[size_t(c)]))
                    edit.changes.push_back({ c, before[size_t(c)], view.data.channels[size_t(c)].curve });
            return edit;
        }

        // The selected keys moved by `dt` and `dv` from where they were in `before`, kept in the
        // range; in time order again.
        // The frame `time` is on, as shown.
        int64_t FrameOf(ConstCurveViewRef view, float time) { return int64_t(std::llround(double(time) * double(view.data.fps))); }

        void MoveSelected(CurveViewRef view, const std::vector<Curve>& before, float dt, float dv)
        {
            const CurveRange& r = view.data.range;
            const float tMin = std::min(r.timeMin, r.timeMax), tMax = std::max(r.timeMin, r.timeMax);
            const float vMin = std::min(r.valueMin, r.valueMax), vMax = std::max(r.valueMin, r.valueMax);
            for (int c = 0; c < (int)view.data.channels.size() && c < (int)before.size(); ++c)
            {
                if (!view.data.channels[size_t(c)].visible)
                    continue;
                Curve curve = before[size_t(c)];
                bool any = false;
                for (CurveKey& k : curve.keys)
                {
                    if (!view.state.selectedKeys.contains(k.stableID))
                        continue;
                    k.time = std::clamp(SnapToFrame(view.data, k.time + dt), tMin, tMax);
                    k.value += dv;
                    if (view.data.clampValues)
                        k.value = std::clamp(k.value, vMin, vMax);
                    any = true;
                }
                if (!any)
                    continue;
                std::stable_sort(curve.keys.begin(), curve.keys.end(), [](const CurveKey& a, const CurveKey& b) { return a.time < b.time; });
                UpdateCurveTangents(curve);
                view.data.channels[size_t(c)].curve = std::move(curve);
            }
        }

        // The plot shows every visible key, with a margin; the range when there are none.
        void FrameAll(CurveViewRef view)
        {
            float tMin = INFINITY, tMax = -INFINITY, vMin = INFINITY, vMax = -INFINITY;
            for (const CurveChannel& channel : view.data.channels)
                if (channel.visible)
                    for (const CurveKey& k : channel.curve.keys)
                    {
                        tMin = std::min(tMin, k.time); tMax = std::max(tMax, k.time);
                        vMin = std::min(vMin, k.value); vMax = std::max(vMax, k.value);
                    }
            CurveRange shown = view.data.range;
            if (tMin <= tMax)
            {
                // The range's span at least, so a lone key or a flat curve isn't stretched.
                shown.timeMin  = std::min(tMin, view.data.range.timeMin);
                shown.timeMax  = std::max(tMax, view.data.range.timeMax);
                shown.valueMin = std::min(vMin, view.data.range.valueMin);
                shown.valueMax = std::max(vMax, view.data.range.valueMax);
            }
            shown = Sane(shown);
            const float margin = 0.05f * (shown.valueMax - shown.valueMin);
            shown.valueMin -= margin;
            shown.valueMax += margin;
            view.state.shown = shown;
            view.state.shownSet = true;
        }

        // Zooms the plot round `at` (time, value); `zoomTime` / `zoomValue` pick the axes.
        void ZoomAround(CurveViewRef view, float2 at, float factor, bool zoomTime, bool zoomValue)
        {
            CurveRange s = ShownOf(view);
            if (zoomTime)  { s.timeMin  = at.x + (s.timeMin  - at.x) * factor; s.timeMax  = at.x + (s.timeMax  - at.x) * factor; }
            if (zoomValue) { s.valueMin = at.y + (s.valueMin - at.y) * factor; s.valueMax = at.y + (s.valueMax - at.y) * factor; }
            // Not so far in that the numbers run out, nor so far out.
            const float spanT = s.timeMax - s.timeMin, spanV = s.valueMax - s.valueMin;
            if (spanT < 1e-4f || spanV < 1e-4f || spanT > 1e7f || spanV > 1e7f)
                return;
            view.state.shown = s;
            view.state.shownSet = true;
        }

        // Draws `curve` on the plot: each segment between two keys as one cubic, the Bezier
        // form of its Hermite one, which the renderer draws smooth; a step as two straight lines.
        // Before the first key and after the last, dashed: the end value held, or the curve
        // repeated (backwards every other time, for ping-pong) as far as the plot shows.
        void DrawCurve(CustomDrawContext& ctx, const CurvePlot& plot, const Curve& curve, const LineStyle& solid, const LineStyle& dashed)
        {
            const auto& keys = curve.keys;
            if (keys.empty() || plot.rect.w <= 0 || plot.rect.h <= 0)
                return;
            // Pixels, held well inside what an int can take.
            const float limit = 1.0e6f;
            auto px = [&](float t, float v) {
                return int2{ int32_t(std::lround(std::clamp(plot.X(t), -limit, limit))),
                             int32_t(std::lround(std::clamp(plot.Y(v), -limit, limit))) };
            };
            // `place` maps a key's time to where this copy of the curve draws it.
            auto segments = [&](const LineStyle& style, auto&& place) {
                for (size_t i = 0; i + 1 < keys.size(); ++i)
                {
                    const CurveKey& a = keys[i];
                    const CurveKey& b = keys[i + 1];
                    const float ta = place(a.time), tb = place(b.time);
                    const float dt = b.time - a.time;
                    if (std::max(ta, tb) < plot.shown.timeMin || std::min(ta, tb) > plot.shown.timeMax)
                        continue;
                    if (a.interpolation == CurveInterpolation::Constant || !(dt > 0.0f))
                    {
                        // Held at a's value up to b, then up or down to it; a mirrored copy
                        // runs the same way, from a's place to b's.
                        ctx.RenderLine(px(ta, a.value), px(tb, a.value), style);
                        ctx.RenderLine(px(tb, a.value), px(tb, b.value), style);
                        continue;
                    }
                    if (a.interpolation == CurveInterpolation::Linear)
                    {
                        ctx.RenderLine(px(ta, a.value), px(tb, b.value), style);
                        continue;
                    }
                    const float third = dt / 3.0f;
                    const float va1 = a.value + a.outSlope * third, vb1 = b.value - b.inSlope * third;
                    const float t1 = place(a.time + third), t2 = place(b.time - third);
                    ctx.RenderLine(px(ta, a.value), px(t1, va1), px(t2, vb1), px(tb, b.value), style);
                }
            };
            segments(solid, [](float t) { return t; });

            const float first = keys.front().time, last = keys.back().time, length = last - first;
            const float left = plot.shown.timeMin, right = plot.shown.timeMax;
            // One side past the keys: `dir` -1 before them, 1 after.
            auto beyond = [&](CurveWrapMode mode, int dir) {
                const float edge = dir < 0 ? first : last;
                const float value = dir < 0 ? keys.front().value : keys.back().value;
                if (dir < 0 ? edge <= left : edge >= right)
                    return;
                if (mode == CurveWrapMode::Clamp || !(length > 0.0f) || keys.size() < 2)
                {
                    ctx.RenderLine(px(edge, value), px(dir < 0 ? left : right, value), dashed);
                    return;
                }
                // Each copy a length further out, up to what the plot shows (and no more than
                // a few hundred).
                for (int n = 1; n <= 400; ++n)
                {
                    const float offset = float(dir * n) * length;
                    if (dir < 0 ? last + offset <= left : first + offset >= right)
                        break;
                    const bool mirror = mode == CurveWrapMode::PingPong && (n % 2) == 1;
                    if (mirror)
                    {
                        // Backwards: this copy starts where the one before it ended, and a key
                        // at t lands as far from that start as t is from the end it mirrors.
                        const float pivot = (dir < 0 ? first : last) + float(dir * (n - 1)) * length;
                        const float mirrorOf = dir < 0 ? first : last;
                        segments(dashed, [pivot, mirrorOf](float t) { return pivot + (mirrorOf - t); });
                    }
                    else
                    {
                        // A loop: from the copy before's end value to this copy's start value.
                        if (mode == CurveWrapMode::Loop)
                        {
                            const float at = dir < 0 ? last + offset : first + offset;
                            const float from = dir < 0 ? keys.front().value : keys.back().value;
                            const float to   = dir < 0 ? keys.back().value : keys.front().value;
                            ctx.RenderLine(px(at, from), px(at, to), dashed);
                        }
                        // Ping-pong's even copies are a whole period on: the forward curve, two
                        // lengths each.
                        const float shift = mode == CurveWrapMode::PingPong ? float(dir * n) * length : offset;
                        segments(dashed, [shift](float t) { return t + shift; });
                    }
                }
            };
            beyond(curve.preWrap, -1);
            beyond(curve.postWrap, 1);
        }

        std::string Number(float v)
        {
            if (std::fabs(v) < 5e-3f) v = 0.0f;   // no "-0.00"
            return std::format("{:.2f}", v);
        }
    }

    // -------------------------------------------------------------------------
    // CurveField
    // -------------------------------------------------------------------------

    bool GUIApplication::CurveField(ID id, CurveViewRef view, LayoutOverride&& constraints)
    {
        PruneCurveSelection(view);
        CurveViewRef v = view;
        // The app may have taken channels away: the active one is one of those left.
        const int channelCount = (int)view.data.channels.size();
        view.state.activeChannel = channelCount > 0 ? std::clamp(view.state.activeChannel, 0, channelCount - 1) : 0;
        const bool changed = view.state.edits != view.state.editsSeen;
        view.state.editsSeen = view.state.edits;
        const bool disabled = guiStateManager.Construction().IsDisabled();
        if (disabled)
            view.state.popupOpen = false;

        // Escape closes the editor, and the keyboard goes back to the field.
        if (view.state.popupOpen && TakeEscapeFor(view.state.popupID, id))
        {
            view.state.popupOpen = false;
            guiStateManager.Interaction().SetFocusID(id);
        }

        // The click toggles the editor from how it was when the field was built: a click on the
        // field closes an open one, which a click outside would close too.
        const bool wasOpen = view.state.popupOpen;
        if (wasOpen) BeginSelected();
        const ElementRef fieldRef = RegisterControl(
            Element{ .id = id,
                     .eventHandler = disabled ? EventHandler{} : MakeButtonHandler([this, v, wasOpen]() {
                         v.state.popupOpen = !wasOpen;
                         if (v.state.popupOpen && v.state.popupID == ID::Invalid())
                             v.state.popupID = idManager.CreateStaticID();
                     }),
                     .styleRef = theme.curveFieldStyleRef },
            std::move(constraints));
        if (wasOpen) EndSelected();

        // The zero line, the curves over the range with the active channel's on top, and a color's
        // curves over a dashed identity line. Hovered, an open button in the top-right corner.
        const bool hovered = (guiStateManager.GetCurrentState(id) & ElementState::Hover) != ElementState::None;
        const VisualColorStyleIndex identityColor = styleManager.RegisterColorStyle(ColorStyle::Create(theme.GetColor("--text-2", color4f{ 0.68f, 0.71f, 0.75f, 1.0f })));
        const VisualColorStyleIndex buttonColor   = styleManager.RegisterColorStyle(ColorStyle::Create(theme.GetColor("--bg-control", color4f{ 0.12f, 0.13f, 0.16f, 1.0f })));
        VERIFY(elementStorage.AddCustomDraw(fieldRef, [this, v, hovered, identityColor, buttonColor](CustomDrawContext& ctx)
        {
            const CurvePlot plot{ ctx.ElementInnerRect(), Sane(v.data.range) };
            const float px = float(std::max(1, styleManager.Px(1)));
            const IntRect& r = plot.rect;
            if (plot.shown.valueMin < 0.0f && plot.shown.valueMax > 0.0f)
            {
                const int y = int(std::lround(plot.Y(0.0f)));
                ctx.RenderLine({ r.x, y }, { r.x + r.w, y }, LineStyle{ .colorStyleIndex = theme.curveGridMajorColor, .lineWidth = px });
            }
            bool colorChannels = false;
            for (const CurveChannel& channel : v.data.channels)
                colorChannels = colorChannels || channel.color == CurveChannelColor::Red || channel.color == CurveChannelColor::Green
                                              || channel.color == CurveChannelColor::Blue;
            for (int n = 0; n < (int)v.data.channels.size(); ++n)
            {
                const int c = n == (int)v.data.channels.size() - 1 ? v.state.activeChannel : (n >= v.state.activeChannel ? n + 1 : n);
                if (c < 0 || c >= (int)v.data.channels.size() || !v.data.channels[size_t(c)].visible)
                    continue;
                // A single value in the accent color; a color's channels in theirs.
                const CurveChannelColor tint = v.data.channels[size_t(c)].color;
                const LineStyle style = tint == CurveChannelColor::Value
                    ? LineStyle{ .colorStyleIndex = theme.accentColor, .lineWidth = px * 1.75f }
                    : LineStyle{ .colorStyleIndex = theme.curveChannelColors[size_t(tint)], .lineWidth = px * 1.5f };
                DrawCurve(ctx, plot, v.data.channels[size_t(c)].curve, style, style);
            }
            if (colorChannels)
            {
                // Where output equals input, from the range's corner as far as both spans go.
                const float lo = std::max(plot.shown.timeMin, plot.shown.valueMin);
                const float hi = std::min(plot.shown.timeMax, plot.shown.valueMax);
                if (lo < hi)
                    ctx.RenderLine({ int(std::lround(plot.X(lo))), int(std::lround(plot.Y(lo))) },
                                   { int(std::lround(plot.X(hi))), int(std::lround(plot.Y(hi))) },
                                   LineStyle{ .colorStyleIndex = identityColor, .lineWidth = px, .dashSize = px * 3.0f });
            }
            if (hovered)
            {
                const IntRect outer = ctx.ElementRect();
                const int size = styleManager.Px(18), inset = styleManager.Px(4);
                const IntRect button{ .position = { outer.x + outer.w - inset - size, outer.y + inset }, .size = { size, size } };
                if (button.x > outer.x)
                {
                    ctx.RenderQuad(button, buttonColor, float(styleManager.Px(4)));
                    // A chevron pointing right, 11px across.
                    const float2 c{ float(button.x) + float(size) * 0.5f, float(button.y) + float(size) * 0.5f };
                    const float s = float(styleManager.Px(11)) / 24.0f;
                    const int2 chevron[] = { { int(std::lround(c.x - 3.0f * s)), int(std::lround(c.y - 6.0f * s)) },
                                             { int(std::lround(c.x + 3.0f * s)), int(std::lround(c.y)) },
                                             { int(std::lround(c.x - 3.0f * s)), int(std::lround(c.y + 6.0f * s)) } };
                    ctx.RenderPolyline(chevron, false, LineStyle{ .colorStyleIndex = identityColor, .lineWidth = px });
                }
            }
        }));

        // The editor, in a window under the field.
        if (view.state.popupOpen && view.state.popupID != ID::Invalid())
        {
            LayoutOverride at = LayoutOverride{}.Anchored(id, AnchorPlacement::OutsideBottom | AnchorPlacement::InsideLeft,
                                                         { 0, int16_t(styleManager.Px(4)) }, AutoFlipMode::FlipAvoidTarget);
            if (BeginWindow(view.state.popupID, "Curve", theme.curveWindowStyleRef, std::move(at),
                            ElementFlags::WindowCanMove | ElementFlags::WindowNoCaption | ElementFlags::Shadows))
            {
                CurveEditor(idManager.AllocateDynamicID("curve_editor"), view, LayoutOverride::Auto().FlexGrow(1));
            }
            else
                view.state.popupOpen = false;   // closed: a click outside, or the focus gone
            EndWindow();
        }
        guiStateManager.SetModified(changed);
        return changed;
    }


    // -------------------------------------------------------------------------
    // The channel tree and the frame ruler (an animation curve editor's)
    // -------------------------------------------------------------------------

    void GUIApplication::CurveTree(CurveViewRef view)
    {
        CurveViewRef v = view;
        BeginVertical(idManager.AllocateDynamicID("curve_tree"), theme.curveTreeStyleRef, LayoutOverride::Auto().NoShrink());

        // The header: Channel, and the playhead's frame its values are at.
        BeginHorizontal(idManager.AllocateDynamicID("curve_tree_header"), theme.curveTreeHeaderStyleRef, LayoutOverride::Auto().NoShrink());
        Text(idManager.AllocateDynamicID("curve_tree_title"), "Channel", theme.curveTreeNameStyleRef);
        if (view.data.fps > 0.0f)
            Text(idManager.AllocateDynamicID("curve_tree_frame"), std::format("@ f {}", FrameOf(view, view.data.playhead)), theme.curveTreeValueStyleRef);
        EndHorizontal();

        const int32_t indent = styleManager.Px(14);
        auto row = [&](std::string_view key, StyleRef style, int depth, std::function<void()> onClick, bool selected) {
            if (selected) BeginSelected();
            const ElementRef ref = RegisterControl(
                // Hovered and focused while its name, value or eye is too.
                Element{ .id = idManager.AllocateDynamicID(key), .eventHandler = MakeButtonHandler(std::move(onClick)), .styleRef = style,
                         .flags = ElementFlags::PropagatesDescendantState },
                LayoutOverride::Auto().NoShrink());
            if (selected) EndSelected();
            VERIFY(elementStorage.BeginChildren());
            if (depth > 0)
                RegisterElement(Element{ .id = idManager.AllocateDynamicID(std::string(key) + "/indent"), .styleRef = theme.emptyHorzStyleRef,
                                         .flags = ElementFlags::NoInput },
                                LayoutOverride::FixedSize(Pixels{ indent * depth }, Pixels{ 1 }));
            return ref;
        };

        // Each group's row before its first channel, unless a group round it is closed.
        std::vector<std::string> open;
        for (int c = 0; c < (int)view.data.channels.size(); ++c)
        {
            const CurveChannel& channel = view.data.channels[size_t(c)];
            std::vector<std::string> segments;
            for (size_t start = 0; start < channel.path.size();)
            {
                const size_t slash = std::min(channel.path.find('/', start), channel.path.size());
                if (slash > start)
                    segments.push_back(channel.path.substr(start, slash - start));
                start = slash + 1;
            }
            size_t common = 0;
            while (common < open.size() && common < segments.size() && open[common] == segments[common])
                ++common;
            open.resize(common);
            bool hidden = false;   // under a closed group
            std::string path;
            for (size_t d = 0; d < segments.size(); ++d)
            {
                path += (d ? "/" : "") + segments[d];
                if (d >= common && !hidden)
                {
                    const bool closed = view.state.collapsedGroups.contains(path);
                    row("curve_group/" + path, theme.curveTreeGroupStyleRef, int(d),
                        [v, path, closed]() { if (closed) v.state.collapsedGroups.erase(path); else v.state.collapsedGroups.insert(path); },
                        false);
                    const ID nameID = idManager.AllocateDynamicID("curve_group_name/" + path);
                    RegisterElement(Element{ .id = nameID,
                                             .textID = textSystem->RegisterText(nameID, closed ? theme.treeArrowRight : theme.treeArrowDown,
                                                                                segments[d], theme.curveTreeNameStyleRef, ElementState::None),
                                             .styleRef = theme.curveTreeNameStyleRef, .flags = ElementFlags::NoInput },
                                    LayoutOverride{});
                    VERIFY(elementStorage.EndChildren());
                }
                if (d >= common)
                    open.push_back(segments[d]);
                hidden = hidden || view.state.collapsedGroups.contains(path);
            }
            if (hidden)
                continue;

            // The channel: its color, name, value at the playhead, and its eye.
            const std::string key = std::format("curve_channel/{}/{}", channel.path, c);
            row(key, channel.visible ? theme.curveTreeRowStyleRef : theme.curveTreeRowHiddenStyleRef, int(segments.size()),
                [v, c]() { v.state.activeChannel = c; }, c == view.state.activeChannel);
            Element swatch{ .id = idManager.AllocateDynamicID(key + "/swatch"), .styleRef = theme.curveTreeSwatchStyleRef,
                            .flags = ElementFlags::NoInput };
            swatch.colorStyleIndex = theme.curveChannelColors[size_t(channel.color)];
            RegisterElement(std::move(swatch), LayoutOverride{});
            Text(idManager.AllocateDynamicID(key + "/name"), channel.name, theme.curveTreeNameStyleRef);
            if (view.data.fps > 0.0f)
                Text(idManager.AllocateDynamicID(key + "/value"), Number(EvaluateCurve(channel.curve, view.data.playhead)), theme.curveTreeValueStyleRef);
            Button(idManager.AllocateDynamicID(key + "/eye"),
                   { channel.visible ? theme.timelineEyeIcon : theme.timelineEyeOffIcon, {}, channel.visible ? "Hide" : "Show" },
                   [v, c]() {
                       if (c >= (int)v.data.channels.size())
                           return;
                       v.data.channels[size_t(c)].visible = !v.data.channels[size_t(c)].visible;
                       std::erase_if(v.state.selectedKeys, [&](ID id) {
                           const auto& keys = v.data.channels[size_t(c)].curve.keys;
                           return std::any_of(keys.begin(), keys.end(), [id](const CurveKey& k) { return k.stableID == id; });
                       });
                   }, theme.timelineRowButtonStyleRef);   // as the timeline's eyes: dim, bright when hovered
            VERIFY(elementStorage.EndChildren());
        }
        EndVertical();
    }

    void GUIApplication::CurveRuler(CurveViewRef view)
    {
        CurveViewRef v = view;
        const ID rulerID = idManager.AllocateDynamicID("curve_ruler");
        const ElementStorage& es = elementStorage;
        GUIStateManager& gsm = guiStateManager;

        // A press or drag on it moves the playhead there, onto a frame.
        EventHandler handler;
        if (!guiStateManager.Construction().IsDisabled())
            handler = [&es, &gsm, v](ElementEvent&& event) -> bool {
                auto moveTo = [&]() {
                    const CurvePlot plot{ v.state.plotRect, ShownOf(v) };
                    if (plot.rect.w <= 0)
                        return;
                    v.data.playhead = SnapToFrame(v.data, plot.Time(float(event.LocalCursor(es).x)));
                    if (v.data.onPlayheadChanged)
                        v.data.onPlayheadChanged(v.data.playhead);
                };
                switch (event.type)
                {
                    case EventType::MouseDown:
                        if (!v.state.playheadDrag.Press(event, es, gsm, {}, { .atOnce = true, .focus = false }))
                            return false;
                        moveTo();
                        return true;
                    case EventType::MouseMove:
                        if (!v.state.playheadDrag.Move(event, 0))
                            return false;
                        moveTo();
                        return true;
                    case EventType::MouseUp:
                        return v.state.playheadDrag.Release(event) != PointerDrag::Ended::No;
                    default:
                        return false;
                }
            };
        const ElementRef ref = RegisterControl(Element{ .id = rulerID, .eventHandler = std::move(handler), .styleRef = theme.curveRulerStyleRef },
                                               LayoutOverride::Auto().NoShrink());
        if (!ref.IsValid())
            return;

        // The frame numbers, under the plot's last place; the playhead's in its pill.
        const CurvePlot last{ view.state.plotRect, ShownOf(view) };
        std::vector<PlotGrid::Label> labels;
        if (last.rect.w > 0)
        {
            const float frameStep = std::max(1.0f, NiceStep(last.TimeSpan() * view.data.fps, float(last.rect.w), float(styleManager.Px(56))));
            PlotGrid::AddLabels(*this, theme.curveRulerLabelStyleRef, labels, last.shown.timeMin * view.data.fps, last.shown.timeMax * view.data.fps,
                                frameStep, /*onYAxis*/ false, "curve_ruler_labels");
        }
        const TextID pill = textSystem->RegisterText(idManager.AllocateDynamicID("curve_playhead"), std::to_string(FrameOf(view, view.data.playhead)),
                                                     theme.curvePlayheadStyleRef, ElementState::None);
        const TextMeasurement pillSize = textSystem->MeasureText(pill, TextWrapContext{});
        const VisualBoxStyleIndex pillBox = theme.curvePlayheadStyleRef.visualBoxStyleIndex;
        const int2 pillPad{ styleManager.Px(6), styleManager.Px(1) };
        const int32_t px1 = std::max(1, styleManager.Px(1));
        // A press anywhere on the ruler moves the playhead, so its pill lights up while the
        // ruler is hovered or dragged.
        const ElementState pillState = view.state.playheadDrag.IsHeld() || (guiStateManager.GetCurrentState(rulerID) & ElementState::Hover) != ElementState::None
                                     ? ElementState::Hover : ElementState::None;
        VERIFY(elementStorage.AddCustomDraw(ref, [=, this](CustomDrawContext& ctx)
        {
            // In screen space, as the plot's place is.
            ctx.SetSpace(DrawSpace::Screen);
            const IntRect r = ctx.ElementRect();
            const CurvePlot plot{ v.state.plotRect, ShownOf(v) };
            if (plot.rect.w <= 0)
                return;
            for (const PlotGrid::Label& label : labels)
            {
                const float x = plot.X(label.at / v.data.fps);
                ctx.RenderQuad(IntRect{ int32_t(x), r.y + r.h - styleManager.Px(5), px1, styleManager.Px(5) }, r, theme.curveGridMajorColor);
                ctx.RenderText(label.text, IntRect{ int32_t(x) - label.size.x / 2, r.y + (r.h - label.size.y) / 2 - styleManager.Px(2),
                                                    label.size.x, label.size.y });
            }
            const float x = plot.X(v.data.playhead);
            if (x < float(plot.rect.x) || x > float(plot.rect.x + plot.rect.w))
                return;
            const int32_t w = pillSize.maxWidth + 2 * pillPad.x, h = pillSize.maxHeight + 2 * pillPad.y;
            // Centered on the playhead, but kept on the ruler at the ends of the plot.
            const int32_t boxX = std::max(r.x, std::min(int32_t(x) - w / 2, r.x + r.w - w));
            const IntRect box{ boxX, r.y + (r.h - h) / 2, w, h };
            ctx.RenderQuad(IntRect{ int32_t(x), r.y, px1, r.h }, r, theme.curveHandleColor);
            ctx.RenderBox(box, pillBox, pillState);
            ctx.RenderText(pill, IntRect{ box.x + pillPad.x, box.y + pillPad.y, pillSize.maxWidth, pillSize.maxHeight });
        }));
    }

    // -------------------------------------------------------------------------
    // CurveEditor
    // -------------------------------------------------------------------------

    void GUIApplication::CurveEditor(ID id, CurveViewRef view, LayoutOverride&& constraints)
    {
        // The curves changed since this view last built (an edit here, or in another view of
        // them): let go of what's gone.
        PruneCurveSelection(view);
        CurveViewRef v = view;
        const int channelCount = (int)view.data.channels.size();
        view.state.activeChannel = channelCount > 0 ? std::clamp(view.state.activeChannel, 0, channelCount - 1) : 0;
        const bool disabled = guiStateManager.Construction().IsDisabled();
        IDManager* const ids = &idManager;   // new keys' IDs, given back when an edit adds none

        // With fps set, an animation editor: interpolation and tangents over the plot, frames
        // on a ruler with the playhead. With paths, the channels as a tree down the left.
        const bool animation = view.data.fps > 0.0f;
        const bool tree = std::any_of(view.data.channels.begin(), view.data.channels.end(), [](const CurveChannel& c) { return !c.path.empty(); });

        BeginVertical(id, theme.emptyVertStyleRef, std::move(constraints));
        const bool pushed = idManager.PushParent(id);

        // ---- Over the plot: the channels, the selected key, frame all --------------
        // A key's interpolation and tangents are in its menu (a right click on it).
        BeginHorizontal(idManager.AllocateDynamicID("curve_top"), theme.curveToolbarStyleRef, LayoutOverride::Auto().NoShrink());
        if (animation)
        {
            const ID frameAllID = idManager.AllocateDynamicID("curve_frame_all");
            Button(frameAllID, { theme.curveFrameAllIcon, "Frame all" }, [v]() { FrameAll(v); },
                   theme.curveFrameAllStyleRef, LayoutOverride{}.NoGrow());   // its icon and text, no wider
            SetTooltip(frameAllID, "Frame all", "A");
        }
        else if (channelCount > 1 && !tree)
        {
            for (int c = 0; c < channelCount; ++c)
            {
                const CurveChannel& channel = view.data.channels[size_t(c)];
                const ID chipID = idManager.AllocateDynamicID("curve_channel");
                const bool active = c == view.state.activeChannel;
                if (active) BeginSelected();
                const ElementRef chipRef = RegisterControl(
                    Element{ .id = chipID,
                             .eventHandler = MakeButtonHandler([v, c]() { v.state.activeChannel = c; }),
                             .styleRef = theme.curveChannelStyleRef },
                    "   " + channel.name, LayoutOverride{});
                if (active) EndSelected();
                // Its color, as a small square before its name.
                const VisualColorStyleIndex color = channel.visible ? theme.curveChannelColors[size_t(channel.color)]
                                                                    : theme.curveChannelFadedColors[size_t(channel.color)];
                const int32_t square = styleManager.Px(8), pad = styleManager.Px(8);
                VERIFY(elementStorage.AddCustomDrawAfter(chipRef, [color, square, pad](CustomDrawContext& ctx)
                {
                    const IntRect r = ctx.ElementRect();
                    ctx.RenderQuad(IntRect{ r.x + pad, r.y + (r.h - square) / 2, square, square }, color, float(square) / 4.0f);
                }));
                Button(idManager.AllocateDynamicID("curve_channel_eye"),
                       { channel.visible ? theme.timelineEyeIcon : theme.timelineEyeOffIcon, {}, channel.visible ? "Hide" : "Show" },
                       [v, c]() {
                           if (c < (int)v.data.channels.size())
                           {
                               v.data.channels[size_t(c)].visible = !v.data.channels[size_t(c)].visible;
                               std::erase_if(v.state.selectedKeys, [&](ID key) {
                                   const auto& keys = v.data.channels[size_t(c)].curve.keys;
                                   return std::any_of(keys.begin(), keys.end(), [key](const CurveKey& k) { return k.stableID == key; });
                               });
                           }
                       }, theme.iconButtonStyleRef);
            }
        }
        BeginHorizontal(idManager.AllocateDynamicID("curve_top_gap"), theme.emptyHorzStyleRef, LayoutOverride{}.FlexGrow(1));
        EndHorizontal();

        // The selected key's time and value, when there's one.
        int selChannel = -1, selKey = -1, selCount = 0;
        for (int c = 0; c < channelCount; ++c)
            for (int k = 0; k < (int)view.data.channels[size_t(c)].curve.keys.size(); ++k)
                if (view.state.selectedKeys.contains(view.data.channels[size_t(c)].curve.keys[size_t(k)].stableID))
                {
                    ++selCount;
                    selChannel = c;
                    selKey = k;
                }
        if (selCount == 1)
        {
            const CurveKey key = view.data.channels[size_t(selChannel)].curve.keys[size_t(selKey)];
            const CurveRange shown = ShownOf(view);
            auto setKey = [this, v, selChannel, id = key.stableID](float time, float value) {
                const std::vector<Curve> before = CurvesOf(v);
                v.data.channels[size_t(selChannel)].curve = before[size_t(selChannel)];
                std::unordered_set<ID> only{ id };
                std::swap(v.state.selectedKeys, only);
                const CurveKey* k = nullptr;
                for (const CurveKey& candidate : before[size_t(selChannel)].keys)
                    if (candidate.stableID == id) k = &candidate;
                if (k)
                    MoveSelected(v, before, time - k->time, value - k->value);
                std::swap(v.state.selectedKeys, only);
                const CurveEdit edit = ChangesSince(v, before);
                for (int c = 0; c < (int)before.size(); ++c)
                    v.data.channels[size_t(c)].curve = before[size_t(c)];
                CommitCurveEdit(v, edit);
            };
            float time = key.time, value = key.value;
            if (animation)
            {
                // The time as a frame.
                int64_t frame = FrameOf(view, key.time);
                DragLabel(idManager.AllocateDynamicID("curve_time_label"), "Frame", theme.dragLabelStyleRef);
                if (Field(idManager.AllocateDynamicID("curve_frame"), frame, TextEditBehavior::None, theme.controls->number,
                          NumericFieldConfig<int64_t>{ .stepPerPixel = 0.25 },
                          LayoutOverride::FixedWidth(Pixels{ styleManager.Px(64) })))
                    setKey(float(frame) / view.data.fps, key.value);
            }
            else
            {
                DragLabel(idManager.AllocateDynamicID("curve_time_label"), "Time", theme.dragLabelStyleRef);
                if (Field(idManager.AllocateDynamicID("curve_time"), time, TextEditBehavior::None, theme.controls->number,
                          NumericFieldConfig<float>{ .stepPerPixel = double(shown.timeMax - shown.timeMin) / 400.0 },
                          LayoutOverride::FixedWidth(Pixels{ styleManager.Px(64) })))
                    setKey(time, key.value);
            }
            DragLabel(idManager.AllocateDynamicID("curve_value_label"), "Value", theme.dragLabelStyleRef);
            if (Field(idManager.AllocateDynamicID("curve_value"), value, TextEditBehavior::None, theme.controls->number,
                      NumericFieldConfig<float>{ .stepPerPixel = double(shown.valueMax - shown.valueMin) / 400.0 },
                      LayoutOverride::FixedWidth(Pixels{ styleManager.Px(64) })))
                setKey(key.time, value);
        }
        if (!animation)
            Button(idManager.AllocateDynamicID("curve_frame_all"), { theme.curveFrameAllIcon, {}, "Frame all (A)" },
                   [v]() { FrameAll(v); }, theme.iconButtonStyleRef);
        EndHorizontal();

        // ---- The tree, the ruler and the plot --------------------------------------
        BeginHorizontal(idManager.AllocateDynamicID("curve_body"), theme.emptyHorzStyleRef, LayoutOverride{}.FlexGrow(1));
        if (tree)
            CurveTree(view);
        BeginVertical(idManager.AllocateDynamicID("curve_main"), theme.emptyVertStyleRef, LayoutOverride{}.FlexGrow(1));
        if (animation)
            CurveRuler(view);

        const ID plotID = idManager.AllocateDynamicID("curve_plot");
        const CurvePlot lastPlot{ view.state.plotRect, ShownOf(view) };

        // The grid's numbers, from where the plot was last frame.
        std::vector<PlotGrid::Label> labels;
        const float minGap = float(styleManager.Px(56)), minGapV = float(styleManager.Px(32));
        const float timeStep  = NiceStep(lastPlot.TimeSpan(),  float(lastPlot.rect.w), minGap);
        const float valueStep = NiceStep(lastPlot.ValueSpan(), float(lastPlot.rect.h), minGapV);
        if (lastPlot.rect.w > 0 && lastPlot.rect.h > 0)
        {
            // With fps set, the times are on the ruler instead.
            if (!animation)
                PlotGrid::AddLabels(*this, theme.curveLabelStyleRef, labels, lastPlot.shown.timeMin, lastPlot.shown.timeMax, timeStep,
                                    /*onYAxis*/ false, "curve_time_labels");
            PlotGrid::AddLabels(*this, theme.curveLabelStyleRef, labels, lastPlot.shown.valueMin, lastPlot.shown.valueMax, valueStep,
                                /*onYAxis*/ true, "curve_value_labels");
        }

        // The selected key's time and value beside it.
        TextID readout = TextID::Invalid;
        int2 readoutSize {};
        if (selCount == 1)
        {
            const CurveChannel& channel = view.data.channels[size_t(selChannel)];
            const CurveKey& key = channel.curve.keys[size_t(selKey)];
            const std::string text = animation ? std::format("{} \xC2\xB7 f {} \xC2\xB7 {}", channel.name, FrameOf(view, key.time), Number(key.value))
                                   : channelCount > 1 ? std::format("{} \xC2\xB7 {} \xC2\xB7 {}", channel.name, Number(key.time), Number(key.value))
                                                      : std::format("{} \xC2\xB7 {}", Number(key.time), Number(key.value));
            readout = textSystem->RegisterText(idManager.AllocateDynamicID("curve_readout"), text, theme.curveReadoutStyleRef, ElementState::None);
            const TextMeasurement m = textSystem->MeasureText(readout, TextWrapContext{});
            readoutSize = { m.maxWidth, m.maxHeight };
        }

        EventHandler plotHandler;
        if (!disabled)
        {
            GUIStateManager& gsm = guiStateManager;
            const ElementStorage& es = elementStorage;
            const float reach = float(styleManager.Px(7));
            const float handleLength = float(styleManager.Px(40));
            const int32_t dragDistance = styleManager.Px(kDragStartDistance);
            plotHandler = [&gsm, &es, v, reach, handleLength, dragDistance, ids, wheelMode = GetWheelMode()](ElementEvent&& event) -> bool
            {
                if (event.rect == nullptr)
                    return false;
                const CurvePlot plot{ event.rect->innerRect, ShownOf(v) };
                const int2 cursor = event.LocalCursor(es);
                const float2 at{ float(cursor.x), float(cursor.y) };
                const bool shift = (event.state.modifiers & KeyModifier::Shift)   != KeyModifier::None;
                const bool ctrl  = (event.state.modifiers & KeyModifier::Control) != KeyModifier::None;
                auto restore = [v](const std::vector<Curve>& before) {
                    for (int c = 0; c < (int)v.data.channels.size() && c < (int)before.size(); ++c)
                        v.data.channels[size_t(c)].curve = before[size_t(c)];
                };
                auto finish = [v](const std::vector<Curve>& before) {
                    // Already where it was dropped: the edit, for undo.
                    const CurveEdit edit = ChangesSince(v, before);
                    if (!edit.empty())
                    {
                        ++v.data.revision;
                        ++v.state.edits;
                        if (v.data.onEdit)
                            v.data.onEdit(edit);
                    }
                };
                switch (event.type)
                {
                    case EventType::MouseDown:
                    {
                        // The middle button (or Alt + left) drags the view.
                        if (const MouseButtons panButton = PanDragButton(event.state); panButton != MouseButtons::None)
                        {
                            v.state.pan.Press(event, es, gsm, ShownOf(v), { .button = panButton, .atOnce = true, .focus = false });
                            return true;
                        }
                        if (!event.state.LeftPressed())
                            return false;
                        TakeFocus(gsm, event.id);
                        // A double click is two at the same spot.
                        const bool second = v.state.plotClicks.IsDoubleClick(event.id) && Distance(at, v.state.lastClick) <= reach;
                        v.state.lastClick = at;

                        // A tangent handle: its slope, as dragged.
                        if (const HandleHit handle = HitHandle(v, plot, at, reach, handleLength); handle.key >= 0)
                        {
                            v.state.tangentDrag.Press(event, es, gsm, CurvesOf(v), { .atOnce = true });
                            v.state.tangentKey = v.data.channels[size_t(handle.channel)].curve.keys[size_t(handle.key)].stableID;
                            v.state.tangentOut = handle.out;
                            v.state.keyMoved = false;
                            return true;
                        }
                        // A key: selected (Shift adds, Ctrl toggles), and the selection dragged.
                        if (const KeyHit hit = HitKey(v, plot, at, reach); hit.key >= 0)
                        {
                            v.state.activeChannel = hit.channel;
                            const bool wasSelected = v.state.selectedKeys.contains(hit.id);
                            if (ctrl)
                            {
                                if (wasSelected) v.state.selectedKeys.erase(hit.id); else v.state.selectedKeys.insert(hit.id);
                            }
                            else if (shift)
                                v.state.selectedKeys.insert(hit.id);
                            else if (!wasSelected)
                                v.state.selectedKeys = { hit.id };
                            if (!ctrl || !wasSelected)
                            {
                                v.state.keyDrag.Press(event, es, gsm, CurvesOf(v));
                                v.state.keyDragFrom = { plot.Time(at.x), plot.Value(at.y) };
                                v.state.keyMoved = false;
                            }
                            return true;
                        }
                        // A press on a curve makes it the active one; a drag from there still
                        // draws a box.
                        if (const int curveHit = HitCurve(v, plot, at, reach); curveHit >= 0)
                            v.state.activeChannel = curveHit;
                        // A double click: a key on the active channel, on its curve when near it.
                        if (second && v.state.activeChannel < (int)v.data.channels.size())
                        {
                            const Curve& curve = v.data.channels[size_t(v.state.activeChannel)].curve;
                            const float time = plot.Time(at.x);
                            float value = plot.Value(at.y);
                            if (!curve.keys.empty() && std::fabs(plot.Y(EvaluateCurve(curve, time)) - at.y) <= reach)
                                value = EvaluateCurve(curve, time);
                            const ID keyID = ids->CreateStaticID();
                            const CurveEdit edit = AddCurveKeyEdit(v.data, v.state.activeChannel, time, value, keyID);
                            if (!edit.empty())
                            {
                                CommitCurveEdit(v, edit);
                                v.state.selectedKeys = { keyID };
                            }
                            else
                                ids->ReleaseStaticID(keyID);
                            return true;
                        }
                        v.state.boxSelect.Press(event, es, gsm, (shift || ctrl) ? v.state.selectedKeys : std::unordered_set<ID>{});
                        v.state.boxFrom = v.state.boxTo = { plot.Time(at.x), plot.Value(at.y) };
                        return true;
                    }
                    case EventType::MouseMove:
                    {
                        // What's under the pointer, for its hover look; not while dragging.
                        v.state.hover.Track(AnyHeld(v.state.pan, v.state.keyDrag, v.state.tangentDrag, v.state.boxSelect), [&] {
                            CurveViewState::HoverTarget under;
                            const HandleHit handle = HitHandle(v, plot, at, reach, handleLength);
                            under.handleKey = handle.key >= 0 ? v.data.channels[size_t(handle.channel)].curve.keys[size_t(handle.key)].stableID : ID::Invalid();
                            under.handleOut = handle.out;
                            const KeyHit key = handle.key >= 0 ? KeyHit{} : HitKey(v, plot, at, reach);
                            under.key = key.id;
                            under.channel = handle.key >= 0 || key.key >= 0 ? -1 : HitCurve(v, plot, at, reach);
                            return under;
                        });
                        if (v.state.pan.IsPressed(event.id))
                        {
                            const int2 d = v.state.pan.drag.ScreenDelta(event.state);
                            const CurveRange& start = v.state.pan.before;
                            const CurvePlot from{ plot.rect, start };
                            const float dt = -float(d.x) / float(std::max(1, plot.rect.w)) * from.TimeSpan();
                            const float dv =  float(d.y) / float(std::max(1, plot.rect.h)) * from.ValueSpan();
                            v.state.shown = { start.timeMin + dt, start.timeMax + dt, start.valueMin + dv, start.valueMax + dv };
                            v.state.shownSet = true;
                            return true;
                        }
                        if (v.state.tangentDrag.IsPressed(event.id))
                        {
                            const std::vector<Curve>& before = v.state.tangentDrag.before;
                            for (int c = 0; c < (int)v.data.channels.size() && c < (int)before.size(); ++c)
                            {
                                Curve curve = before[size_t(c)];
                                for (CurveKey& k : curve.keys)
                                {
                                    if (k.stableID != v.state.tangentKey)
                                        continue;
                                    // The slope to the pointer; the handle stays on its side.
                                    float dt = plot.Time(at.x) - k.time;
                                    const float minDt = plot.TimeSpan() * 1e-4f;
                                    dt = v.state.tangentOut ? std::max(dt, minDt) : std::min(dt, -minDt);
                                    const float slope = (plot.Value(at.y) - k.value) / dt;
                                    // A Free key's side alone; any other key turns Aligned,
                                    // the slope on both sides.
                                    if (k.tangents == CurveTangentMode::Free)
                                        (v.state.tangentOut ? k.outSlope : k.inSlope) = slope;
                                    else
                                    {
                                        k.tangents = CurveTangentMode::Aligned;
                                        k.inSlope = k.outSlope = slope;
                                    }
                                    UpdateCurveTangents(curve);
                                    v.data.channels[size_t(c)].curve = curve;
                                    v.state.keyMoved = true;
                                }
                            }
                            return true;
                        }
                        if (v.state.keyDrag.IsPressed(event.id))
                        {
                            if (!v.state.keyDrag.Move(event, dragDistance))
                                return true;
                            if (v.state.keyDrag.before.size() != v.data.channels.size())
                            {
                                v.state.keyDrag = {};
                                return true;
                            }
                            MoveSelected(v, v.state.keyDrag.before, plot.Time(at.x) - v.state.keyDragFrom.x, plot.Value(at.y) - v.state.keyDragFrom.y);
                            v.state.keyMoved = true;
                            return true;
                        }
                        if (v.state.boxSelect.IsPressed(event.id))
                        {
                            if (!v.state.boxSelect.Move(event, dragDistance))
                                return true;
                            v.state.boxTo = { plot.Time(at.x), plot.Value(at.y) };
                            const float t0 = std::min(v.state.boxFrom.x, v.state.boxTo.x), t1 = std::max(v.state.boxFrom.x, v.state.boxTo.x);
                            const float v0 = std::min(v.state.boxFrom.y, v.state.boxTo.y), v1 = std::max(v.state.boxFrom.y, v.state.boxTo.y);
                            std::unordered_set<ID> selection = v.state.boxSelect.before;
                            for (const CurveChannel& channel : v.data.channels)
                                if (channel.visible)
                                    for (const CurveKey& k : channel.curve.keys)
                                        if (k.time >= t0 && k.time <= t1 && k.value >= v0 && k.value <= v1)
                                        {
                                            if (ctrl && v.state.boxSelect.before.contains(k.stableID)) selection.erase(k.stableID);
                                            else selection.insert(k.stableID);
                                        }
                            v.state.selectedKeys = std::move(selection);
                            return true;
                        }
                        return false;
                    }
                    case EventType::MouseUp:
                    {
                        if (v.state.pan.IsPressed(event.id))
                            return v.state.pan.Release(event) != PointerDrag::Ended::No;
                        if (v.state.tangentDrag.IsPressed(event.id))
                        {
                            v.state.tangentDrag.Release(event);
                            if (v.state.keyMoved)
                                finish(v.state.tangentDrag.before);
                            v.state.tangentKey = ID::Invalid();
                            v.state.tangentDrag.before.clear();
                            return true;
                        }
                        if (v.state.keyDrag.IsPressed(event.id))
                        {
                            v.state.keyDrag.Release(event);
                            if (v.state.keyMoved)
                                finish(v.state.keyDrag.before);
                            v.state.keyDrag.before.clear();
                            return true;
                        }
                        if (const PointerDrag::Ended ended = v.state.boxSelect.Release(event); ended != PointerDrag::Ended::No)
                        {
                            // A click on the empty plot clears the selection.
                            if (ended == PointerDrag::Ended::Click && !shift && !ctrl)
                                v.state.selectedKeys.clear();
                            return true;
                        }
                        return false;
                    }
                    case EventType::MouseScroll:
                    {
                        // The wheel zooms round the pointer or scrolls the view, as the app's
                        // wheel mode says; a zoom with Alt only along the time. A pinch zooms.
                        const WheelNavigation nav = NavigateByWheel(event.state, wheelMode, 1.15f);
                        if (!nav.Any())
                            return false;
                        if (nav.zoom != 1.0f)
                        {
                            const bool alt = (event.state.modifiers & KeyModifier::Alt) != KeyModifier::None;
                            ZoomAround(v, { plot.Time(at.x), plot.Value(at.y) }, 1.0f / nav.zoom, true, !alt);
                        }
                        if (nav.pan.x != 0.0f || nav.pan.y != 0.0f)
                        {
                            // A notch scrolls a tenth of the view.
                            CurveRange s = ShownOf(v);
                            const float dt = nav.pan.x * 0.1f * (s.timeMax - s.timeMin);
                            const float dv = -nav.pan.y * 0.1f * (s.valueMax - s.valueMin);
                            v.state.shown = { s.timeMin + dt, s.timeMax + dt, s.valueMin + dv, s.valueMax + dv };
                            v.state.shownSet = true;
                        }
                        return true;
                    }
                    case EventType::KeyDown:
                    {
                        const KeyCode key = event.state.lastKey;
                        if (key == KeyCode::Escape)
                        {
                            for (Gesture<std::vector<Curve>>* drag : { &v.state.keyDrag, &v.state.tangentDrag })
                                if (drag->Cancel(event, gsm))
                                {
                                    restore(drag->before);
                                    drag->before.clear();
                                    return true;
                                }
                            if (v.state.boxSelect.Cancel(event, gsm, v.state.selectedKeys))
                                return true;
                            if (!v.state.selectedKeys.empty())
                            {
                                v.state.selectedKeys.clear();
                                return true;
                            }
                            return false;
                        }
                        if (key == KeyCode::Delete || key == KeyCode::Backspace)
                        {
                            CommitCurveEdit(v, DeleteSelectedCurveKeysEdit(v));
                            return true;
                        }
                        if (key == KeyCode::KeyA && ctrl)
                        {
                            v.state.selectedKeys.clear();
                            if (v.state.activeChannel < (int)v.data.channels.size())
                                for (const CurveKey& k : v.data.channels[size_t(v.state.activeChannel)].curve.keys)
                                    v.state.selectedKeys.insert(k.stableID);
                            return true;
                        }
                        if (key == KeyCode::KeyA && !ctrl)
                        {
                            FrameAll(v);
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
            Element{ .id = plotID, .eventHandler = std::move(plotHandler), .styleRef = animation ? theme.curvePlotFramesStyleRef : theme.curvePlotStyleRef },
            LayoutOverride::Auto().FlexGrow(1));
        VERIFY(elementStorage.AddPostLayout(plotRef, [v](PostLayoutContext& ctx) { v.state.plotRect = ctx.ElementInnerRect(); }));
        {
            const int32_t px1 = std::max(1, styleManager.Px(1)), px4 = styleManager.Px(4);
            const float keySize = float(styleManager.Px(8)), selectedSize = float(styleManager.Px(11)), edge = float(std::max(1, styleManager.Px(1)));
            const float handleLength = float(styleManager.Px(40)), handleRadius = float(styleManager.Px(4));
            // The key or handle under the pointer, while it's over the plot: drawn lighter and larger.
            const bool plotHovered = !disabled && (guiStateManager.GetCurrentState(plotID) & ElementState::Hover) != ElementState::None;
            const CurveViewState::HoverTarget hoverTarget = view.state.hover.Shown(plotHovered);
            const ID   hoverKey       = hoverTarget.key;
            const ID   hoverHandleKey = hoverTarget.handleKey;
            const bool hoverHandleOut = hoverTarget.handleOut;
            const int  hoverChannel   = view.state.boxSelect.IsHeld() ? -1 : hoverTarget.channel;
            const float hoverGrow     = float(std::max(1, styleManager.Px(2)));
            const float lineWidth = float(px1) * 2.0f, otherWidth = float(px1) * 1.5f, handleWidth = float(px1) * 1.25f;
            const float dash = float(styleManager.Px(3));
            const VisualBoxStyleIndex readoutBox = theme.curveReadoutStyleRef.visualBoxStyleIndex;
            const VisualBoxStyleIndex boxSelect = theme.curveBoxSelectStyleRef.visualBoxStyleIndex;
            const int2 readoutPad{ styleManager.Px(6), styleManager.Px(2) };
            VERIFY(elementStorage.AddCustomDraw(plotRef, [=, this](CustomDrawContext& ctx)
            {
                const IntRect outer = ctx.ElementRect(), inner = ctx.ElementInnerRect();
                const CurvePlot plot{ inner, ShownOf(v) };

                // The grid and its numbers.
                const PlotGrid::Frame grid = GridFrame(plot);
                PlotGrid::DrawLines(ctx, grid, NiceStep(plot.TimeSpan(), float(inner.w), minGap), NiceStep(plot.ValueSpan(), float(inner.h), minGapV),
                                    { theme.curveGridColor, theme.curveGridMajorColor, theme.curveZeroColor }, px1);
                PlotGrid::DrawLabels(ctx, grid, labels, px4, px4);

                // The playhead, through the plot.
                if (v.data.fps > 0.0f)
                {
                    const float x = plot.X(v.data.playhead);
                    if (x >= float(inner.x) && x <= float(inner.x + inner.w))
                        ctx.RenderQuad(IntRect{ int32_t(x), inner.y, px1, inner.h }, inner, theme.curveHandleColor);
                }

                // The curves: the others thinner and faded, the active one on top; before the
                // first key and after the last dashed. The one under the pointer is drawn in
                // full color and a little thicker.
                const int count = (int)v.data.channels.size();
                for (int n = 0; n < count; ++n)
                {
                    const int c = n == count - 1 ? v.state.activeChannel : (n >= v.state.activeChannel ? n + 1 : n);
                    if (c < 0 || c >= count || !v.data.channels[size_t(c)].visible)
                        continue;
                    const CurveChannel& channel = v.data.channels[size_t(c)];
                    const bool active = c == v.state.activeChannel || count == 1;
                    const bool curveHovered = c == hoverChannel;
                    const VisualColorStyleIndex color = active || curveHovered ? theme.curveChannelColors[size_t(channel.color)]
                                                                               : theme.curveChannelFadedColors[size_t(channel.color)];
                    const float width = (active ? lineWidth : otherWidth) + (curveHovered ? float(px1) : 0.0f);
                    DrawCurve(ctx, plot, channel.curve, LineStyle{ .colorStyleIndex = color, .lineWidth = width },
                              LineStyle{ .colorStyleIndex = color, .lineWidth = width, .dashSize = dash });

                    // Its keys: rounded squares in its color with an edge of the plot's; a
                    // selected one larger and light, ringed in the accent, with its handles.
                    for (size_t index = 0; index < channel.curve.keys.size(); ++index)
                    {
                        const CurveKey& k = channel.curve.keys[index];
                        const float2 p = plot.At(k);
                        if (p.x < float(outer.x) || p.x > float(outer.x + outer.w))
                            continue;
                        const bool selected = v.state.selectedKeys.contains(k.stableID);
                        if (selected)
                        {
                            for (const bool out : { false, true })
                            {
                                if (!HasHandle(channel.curve, index, out))
                                    continue;
                                const float2 end = HandleEnd(plot, k, out, handleLength);
                                ctx.RenderLine(int2{ int32_t(p.x), int32_t(p.y) }, int2{ int32_t(end.x), int32_t(end.y) },
                                               LineStyle{ .colorStyleIndex = theme.curveHandleColor, .lineWidth = handleWidth });
                                // Hovered: filled, a little larger.
                                if (k.stableID == hoverHandleKey && out == hoverHandleOut)
                                {
                                    const float r = handleRadius + hoverGrow / 2.0f;
                                    ctx.RenderQuad(IntRect{ int32_t(end.x - r), int32_t(end.y - r), int32_t(2 * r), int32_t(2 * r) }, outer, theme.curveHandleColor, r);
                                }
                                else
                                    ctx.RenderCircle(end, handleRadius, LineStyle{ .colorStyleIndex = theme.curveHandleColor, .lineWidth = handleWidth });
                            }
                            const float s = selectedSize + (k.stableID == hoverKey ? hoverGrow : 0.0f);
                            ctx.RenderQuad(IntRect{ int32_t(p.x - s / 2), int32_t(p.y - s / 2), int32_t(s), int32_t(s) }, outer, theme.curveHandleColor, s / 4.0f);
                            const float t = s - 4.0f * edge;
                            ctx.RenderQuad(IntRect{ int32_t(p.x - t / 2), int32_t(p.y - t / 2), int32_t(t), int32_t(t) }, outer, theme.curveSelectedKeyColor, t / 4.0f);
                        }
                        else
                        {
                            // Hovered: a light edge, a little larger.
                            const bool hovered = k.stableID == hoverKey;
                            const float s = keySize + 2.0f * edge + (hovered ? hoverGrow : 0.0f);
                            ctx.RenderQuad(IntRect{ int32_t(p.x - s / 2), int32_t(p.y - s / 2), int32_t(s), int32_t(s) }, outer,
                                           hovered ? theme.curveHandleColor : theme.curveKeyEdgeColor, s / 4.0f);
                            ctx.RenderQuad(IntRect{ int32_t(p.x - keySize / 2), int32_t(p.y - keySize / 2), int32_t(keySize), int32_t(keySize) },
                                           outer, active ? theme.curveChannelColors[size_t(channel.color)] : theme.curveChannelFadedColors[size_t(channel.color)],
                                           keySize / 4.0f);
                        }
                    }
                }

                // The box being dragged.
                if (v.state.boxSelect.IsDragging(plotID))
                {
                    const int32_t x0 = int32_t(plot.X(std::min(v.state.boxFrom.x, v.state.boxTo.x))), x1 = int32_t(plot.X(std::max(v.state.boxFrom.x, v.state.boxTo.x)));
                    const int32_t y0 = int32_t(plot.Y(std::max(v.state.boxFrom.y, v.state.boxTo.y))), y1 = int32_t(plot.Y(std::min(v.state.boxFrom.y, v.state.boxTo.y)));
                    ctx.RenderBox(IntRect{ x0, y0, x1 - x0, y1 - y0 }, inner, boxSelect);
                }

                // The selected key's time and value, beside it.
                if (readout != TextID::Invalid && selChannel >= 0 && selChannel < count
                    && selKey < (int)v.data.channels[size_t(selChannel)].curve.keys.size())
                {
                    const float2 p = plot.At(v.data.channels[size_t(selChannel)].curve.keys[size_t(selKey)]);
                    const int32_t w = readoutSize.x + 2 * readoutPad.x, h = readoutSize.y + 2 * readoutPad.y;
                    int32_t x = int32_t(p.x) + px4 * 3, y = int32_t(p.y) - h - px4 * 2;
                    if (x + w > outer.x + outer.w) x = int32_t(p.x) - w - px4 * 3;
                    if (y < outer.y) y = int32_t(p.y) + px4 * 2;
                    ctx.RenderBox(IntRect{ x, y, w, h }, outer, readoutBox);
                    ctx.RenderText(readout, IntRect{ x + readoutPad.x, y + readoutPad.y, readoutSize.x, readoutSize.y });
                }
            }));
        }

        // A right click: on a key, its delete and tangent modes; elsewhere, a key there.
        if (!disabled && BeginContextMenu(plotID))
        {
            if (view.state.menuOpening != ContextMenuOpenings())
            {
                // Just opened, or opened again by another right click: what's under it.
                view.state.menuOpening = ContextMenuOpenings();
                view.state.menuKey = ID::Invalid();
                const CurvePlot plot{ view.state.plotRect, ShownOf(view) };
                const int2 at = contextMenuScreenPos.value_or(int2{ view.state.plotRect.x + view.state.plotRect.w / 2, view.state.plotRect.y + view.state.plotRect.h / 2 });
                const float2 p{ float(at.x), float(at.y) };
                view.state.menuAt = { plot.Time(p.x), plot.Value(p.y) };
                if (const KeyHit hit = HitKey(view, plot, p, float(styleManager.Px(7))); hit.key >= 0)
                {
                    view.state.menuKey = hit.id;
                    view.state.activeChannel = hit.channel;
                    if (!view.state.selectedKeys.contains(hit.id))
                        view.state.selectedKeys = { hit.id };
                }
                // On a curve: it becomes the active one and a key added lands on it.
                else if (const int curveHit = HitCurve(view, plot, p, float(styleManager.Px(7))); curveHit >= 0)
                {
                    view.state.activeChannel = curveHit;
                    view.state.menuAt.y = EvaluateCurve(view.data.channels[size_t(curveHit)].curve, view.state.menuAt.x);
                }
            }
            if (view.state.menuKey != ID::Invalid())
            {
                MenuItem("Delete key", [v]() { CommitCurveEdit(v, DeleteSelectedCurveKeysEdit(v)); });
                MenuSeparator();
                // The interpolation and tangents of the key right-clicked, checked.
                CurveTangentMode mode = CurveTangentMode::Auto;
                CurveInterpolation interpolation = CurveInterpolation::Bezier;
                for (const CurveChannel& channel : view.data.channels)
                    for (const CurveKey& k : channel.curve.keys)
                        if (k.stableID == view.state.menuKey) { mode = k.tangents; interpolation = k.interpolation; }
                const std::tuple<CurveInterpolation, IconID, const char*> interpolations[] = {
                    { CurveInterpolation::Constant, theme.curveConstantIcon, "Constant" },
                    { CurveInterpolation::Linear,   theme.curveLinearIcon,   "Linear" },
                    { CurveInterpolation::Bezier,   theme.curveBezierIcon,   "Bezier" } };
                MenuHeader("Interpolation");
                for (const auto& [i, icon, name] : interpolations)
                {
                    if (i == interpolation) BeginSelected();
                    MenuItem({ icon, name }, [v, i = i]() { CommitCurveEdit(v, SetCurveInterpolationEdit(v, i)); });
                    if (i == interpolation) EndSelected();
                }
                MenuSeparator();
                static constexpr std::pair<CurveTangentMode, const char*> kModes[] = {
                    { CurveTangentMode::Auto, "Auto" }, { CurveTangentMode::Aligned, "Aligned" },
                    { CurveTangentMode::Free, "Free" }, { CurveTangentMode::Flat, "Flat" } };
                MenuHeader("Tangents");
                for (const auto& [m, name] : kModes)
                {
                    if (m == mode) BeginSelected();
                    MenuItem(name, [v, m]() { CommitCurveEdit(v, SetCurveTangentModeEdit(v, m)); });
                    if (m == mode) EndSelected();
                }
            }
            else
            {
                MenuItem("Add key", [v, ids]() {
                    const ID keyID = ids->CreateStaticID();
                    const CurveEdit edit = AddCurveKeyEdit(v.data, v.state.activeChannel, v.state.menuAt.x, v.state.menuAt.y, keyID);
                    if (!edit.empty())
                    {
                        CommitCurveEdit(v, edit);
                        v.state.selectedKeys = { keyID };
                    }
                    else
                        ids->ReleaseStaticID(keyID);
                });
                MenuItem("Frame all", [v]() { FrameAll(v); });
            }
        }
        if (!disabled)
            EndContextMenu();

        EndVertical();     // the ruler and the plot
        EndHorizontal();   // the tree beside them

        // ---- Under the plot: the presets, and what the curve does past its ends ----
        if (!animation)
        {
        BeginHorizontal(idManager.AllocateDynamicID("curve_bottom"), theme.curveToolbarStyleRef, LayoutOverride::Auto().NoShrink());
        const std::span<const CurvePreset> presets = view.data.presets.empty() ? DefaultCurvePresets() : std::span<const CurvePreset>(view.data.presets);
        const int active = view.state.activeChannel;
        const VisualColorStyleIndex presetColor = channelCount > 0 ? theme.curveChannelColors[size_t(view.data.channels[size_t(active)].color)]
                                                                   : theme.curveChannelColors[0];
        // A preset is looked up where it's used, in the curves' list or the built-in ones, not
        // copied (its name and keys) into its handler and its drawing each frame. The handler
        // takes the channel active when it's clicked, which is the one shown: what it keeps
        // stays in std::function's small buffer.
        const CurveData* data = &view.data;
        const float width = float(std::max(1, styleManager.Px(1))) * 1.25f;
        for (size_t p = 0; p < presets.size(); ++p)
        {
            const ElementRef presetRef = RegisterControl(
                Element{ .id = idManager.AllocateDynamicID("curve_preset"),
                         .eventHandler = disabled ? EventHandler{} : MakeButtonHandler([v, p, ids]() {
                             const CurvePreset* preset = PresetAt(v.data, p);
                             if (preset == nullptr)
                                 return;
                             std::vector<ID> made;
                             const CurveEdit edit = ApplyCurvePresetEdit(v.data, v.state.activeChannel, *preset,
                                                                         [&] { return made.emplace_back(ids->CreateStaticID()); });
                             if (edit.empty())
                                 for (const ID id : made)
                                     ids->ReleaseStaticID(id);   // the curve is that preset already
                             else
                                 CommitCurveEdit(v, edit);
                         }),
                         .styleRef = theme.curvePresetStyleRef },
                LayoutOverride{});
            SetTooltip(presets[p].name);
            VERIFY(elementStorage.AddCustomDraw(presetRef, [data, p, presetColor, width](CustomDrawContext& ctx)
            {
                const CurvePreset* preset = PresetAt(*data, p);
                if (preset == nullptr)
                    return;
                const LineStyle style{ .colorStyleIndex = presetColor, .lineWidth = width };
                const CurvePlot plot{ ctx.ElementInnerRect(), CurveRange{} };
                const Curve& curve = preset->curve;
                if (curve.preWrap == CurveWrapMode::Clamp && curve.postWrap == CurveWrapMode::Clamp)
                    DrawCurve(ctx, plot, curve, style, style);
                else
                {
                    // Drawn over its own span alone: a copy that doesn't wrap.
                    Curve shape = curve;
                    shape.preWrap = shape.postWrap = CurveWrapMode::Clamp;
                    DrawCurve(ctx, plot, shape, style, style);
                }
            }));
        }
        BeginHorizontal(idManager.AllocateDynamicID("curve_bottom_gap"), theme.emptyHorzStyleRef, LayoutOverride{}.FlexGrow(1));
        EndHorizontal();
        if (channelCount > 0)
        {
            static constexpr std::string_view kWraps[] = { "Clamp", "Loop", "Ping-pong" };
            const Curve& curve = view.data.channels[size_t(active)].curve;
            int pre = int(curve.preWrap), post = int(curve.postWrap);
            if (Dropdown(idManager.AllocateDynamicID("curve_pre_wrap"), pre, kWraps, LayoutOverride::FixedWidth(Pixels{ styleManager.Px(96) })))
                CommitCurveEdit(view, SetCurveWrapEdit(view.data, active, CurveWrapMode(pre), curve.postWrap));
            SetTooltip("Before the first key");
            if (Dropdown(idManager.AllocateDynamicID("curve_post_wrap"), post, kWraps, LayoutOverride::FixedWidth(Pixels{ styleManager.Px(96) })))
                CommitCurveEdit(view, SetCurveWrapEdit(view.data, active, view.data.channels[size_t(active)].curve.preWrap, CurveWrapMode(post)));
            SetTooltip("After the last key");
        }
        EndHorizontal();
        }

        if (pushed) (void)idManager.PopParent();
        EndVertical();
    }
}
