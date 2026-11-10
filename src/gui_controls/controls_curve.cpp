#include <gui_controls/gui_application.h>
#include <gui_controls/gui_curve_types.h>

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

        CurveRange ShownOf(const CurveView& view) { return Sane(view.shownSet ? view.shown : view.range); }

        // A range shown over a rect, value up.
        struct Plot
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

        // A step of 1, 2 or 5 times a power of ten, at least `minPixels` apart over `pixels`.
        float NiceStep(float span, float pixels, float minPixels)
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

        float Distance(float2 a, float2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

        // The key under `pos`: the active channel's first, then the other visible ones.
        struct KeyHit { int channel = -1; int key = -1; ID id = ID::Invalid(); };
        KeyHit HitKey(const CurveView& view, const Plot& plot, float2 pos, float reach)
        {
            const int count = (int)view.channels.size();
            for (int n = 0; n < count; ++n)
            {
                const int c = n == 0 ? view.activeChannel : (n <= view.activeChannel ? n - 1 : n);
                if (c < 0 || c >= count || !view.channels[size_t(c)].visible)
                    continue;
                const auto& keys = view.channels[size_t(c)].curve.keys;
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

        // Whether a key shows a handle on a side: not on a Broken key's Linear or Constant side.
        bool HasHandle(const CurveKey& k, bool out)
        {
            return k.mode != CurveTangentMode::Broken || (out ? k.outMode : k.inMode) == CurveSideMode::Free;
        }

        // The end of a key's handle on a side, `length` pixels from it along its slope.
        float2 HandleEnd(const Plot& plot, const CurveKey& k, bool out, float length)
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
        HandleHit HitHandle(const CurveView& view, const Plot& plot, float2 pos, float reach, float length)
        {
            for (int c = 0; c < (int)view.channels.size(); ++c)
            {
                const CurveChannel& channel = view.channels[size_t(c)];
                if (!channel.visible)
                    continue;
                for (int k = 0; k < (int)channel.curve.keys.size(); ++k)
                {
                    const CurveKey& key = channel.curve.keys[size_t(k)];
                    if (!view.selectedKeys.contains(key.stableID))
                        continue;
                    for (const bool out : { true, false })
                        if (HasHandle(key, out) && Distance(HandleEnd(plot, key, out, length), pos) <= reach)
                            return { c, k, out };
                }
            }
            return {};
        }

        std::vector<Curve> CurvesOf(const CurveView& view)
        {
            std::vector<Curve> curves;
            for (const CurveChannel& channel : view.channels)
                curves.push_back(channel.curve);
            return curves;
        }

        // The curves as they are now, against `before`: the edit.
        CurveEdit ChangesSince(const CurveView& view, const std::vector<Curve>& before)
        {
            CurveEdit edit;
            for (int c = 0; c < (int)view.channels.size() && c < (int)before.size(); ++c)
                if (!(view.channels[size_t(c)].curve == before[size_t(c)]))
                    edit.changes.push_back({ c, before[size_t(c)], view.channels[size_t(c)].curve });
            return edit;
        }

        // The selected keys moved by `dt` and `dv` from where they were in `before`, kept in the
        // range; in time order again.
        void MoveSelected(CurveView& view, const std::vector<Curve>& before, float dt, float dv)
        {
            const CurveRange& r = view.range;
            const float tMin = std::min(r.timeMin, r.timeMax), tMax = std::max(r.timeMin, r.timeMax);
            const float vMin = std::min(r.valueMin, r.valueMax), vMax = std::max(r.valueMin, r.valueMax);
            for (int c = 0; c < (int)view.channels.size() && c < (int)before.size(); ++c)
            {
                if (!view.channels[size_t(c)].visible)
                    continue;
                Curve curve = before[size_t(c)];
                bool any = false;
                for (CurveKey& k : curve.keys)
                {
                    if (!view.selectedKeys.contains(k.stableID))
                        continue;
                    k.time = std::clamp(k.time + dt, tMin, tMax);
                    k.value += dv;
                    if (view.clampValues)
                        k.value = std::clamp(k.value, vMin, vMax);
                    any = true;
                }
                if (!any)
                    continue;
                std::stable_sort(curve.keys.begin(), curve.keys.end(), [](const CurveKey& a, const CurveKey& b) { return a.time < b.time; });
                UpdateCurveTangents(curve);
                view.channels[size_t(c)].curve = std::move(curve);
            }
        }

        // The plot shows every visible key, with a margin; the range when there are none.
        void FrameAll(CurveView& view)
        {
            float tMin = INFINITY, tMax = -INFINITY, vMin = INFINITY, vMax = -INFINITY;
            for (const CurveChannel& channel : view.channels)
                if (channel.visible)
                    for (const CurveKey& k : channel.curve.keys)
                    {
                        tMin = std::min(tMin, k.time); tMax = std::max(tMax, k.time);
                        vMin = std::min(vMin, k.value); vMax = std::max(vMax, k.value);
                    }
            CurveRange shown = view.range;
            if (tMin <= tMax)
            {
                // The range's span at least, so a lone key or a flat curve isn't stretched.
                shown.timeMin  = std::min(tMin, view.range.timeMin);
                shown.timeMax  = std::max(tMax, view.range.timeMax);
                shown.valueMin = std::min(vMin, view.range.valueMin);
                shown.valueMax = std::max(vMax, view.range.valueMax);
            }
            shown = Sane(shown);
            const float margin = 0.05f * (shown.valueMax - shown.valueMin);
            shown.valueMin -= margin;
            shown.valueMax += margin;
            view.shown = shown;
            view.shownSet = true;
        }

        // Zooms the plot round `at` (time, value); `zoomTime` / `zoomValue` pick the axes.
        void ZoomAround(CurveView& view, float2 at, float factor, bool zoomTime, bool zoomValue)
        {
            CurveRange s = ShownOf(view);
            if (zoomTime)  { s.timeMin  = at.x + (s.timeMin  - at.x) * factor; s.timeMax  = at.x + (s.timeMax  - at.x) * factor; }
            if (zoomValue) { s.valueMin = at.y + (s.valueMin - at.y) * factor; s.valueMax = at.y + (s.valueMax - at.y) * factor; }
            // Not so far in that the numbers run out, nor so far out.
            const float spanT = s.timeMax - s.timeMin, spanV = s.valueMax - s.valueMin;
            if (spanT < 1e-4f || spanV < 1e-4f || spanT > 1e7f || spanV > 1e7f)
                return;
            view.shown = s;
            view.shownSet = true;
        }

        // Draws `curve` on the plot: each segment between two keys as one cubic, the Bezier
        // form of its Hermite one, which the renderer draws smooth; a step as two straight lines.
        // Before the first key and after the last, dashed: the end value held, or the curve
        // repeated (backwards every other time, for ping-pong) as far as the plot shows.
        void DrawCurve(CustomDrawContext& ctx, const Plot& plot, const Curve& curve, const LineStyle& solid, const LineStyle& dashed)
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
                    const bool step = (a.mode == CurveTangentMode::Broken && a.outMode == CurveSideMode::Constant)
                                   || (b.mode == CurveTangentMode::Broken && b.inMode == CurveSideMode::Constant);
                    if (step || !(dt > 0.0f))
                    {
                        // Held at a's value up to b, then up or down to it; a mirrored copy
                        // runs the same way, from a's place to b's.
                        ctx.RenderLine(px(ta, a.value), px(tb, a.value), style);
                        ctx.RenderLine(px(tb, a.value), px(tb, b.value), style);
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
            if (std::fabs(v) < 5e-5f) v = 0.0f;   // no "-0.000"
            return std::format("{:.3f}", v);
        }

        // A grid line's number, as short as its step lets it be.
        std::string GridNumber(float v, float step)
        {
            const int decimals = std::clamp(int(std::ceil(-std::log10(step) - 1e-4f)), 0, 6);
            if (std::fabs(v) < step * 1e-3f) v = 0.0f;
            return std::format("{:.{}f}", v, decimals);
        }
    }

    // -------------------------------------------------------------------------
    // CurveField
    // -------------------------------------------------------------------------

    bool GUIApplication::CurveField(ID id, CurveView& view, LayoutOverride&& constraints)
    {
        CurveView* v = &view;
        const bool changed = view.revision != view.revisionSeen;
        view.revisionSeen = view.revision;
        const bool disabled = guiStateManager.Construction().IsDisabled();
        if (disabled)
            view.popupOpen = false;

        // Escape closes the editor, and the keyboard goes back to the field.
        if (view.popupOpen && TakeEscapeFor(view.popupID, id))
        {
            view.popupOpen = false;
            guiStateManager.Interaction().SetFocusID(id);
        }

        // The click toggles the editor from how it was when the field was built: a click on the
        // field closes an open one, which a click outside would close too.
        const bool wasOpen = view.popupOpen;
        if (wasOpen) BeginSelected();
        const ElementRef fieldRef = RegisterControl(
            Element{ .id = id,
                     .eventHandler = disabled ? EventHandler{} : MakeButtonHandler([this, v, wasOpen]() {
                         v->popupOpen = !wasOpen;
                         if (v->popupOpen && v->popupID == ID::Invalid())
                             v->popupID = idManager.CreateStaticID();
                     }),
                     .styleRef = theme.curveFieldStyleRef },
            std::move(constraints));
        if (wasOpen) EndSelected();

        // The curves over the range, the active channel's on top.
        VERIFY(elementStorage.AddCustomDraw(fieldRef, [this, v](CustomDrawContext& ctx)
        {
            const Plot plot{ ctx.ElementInnerRect(), Sane(v->range) };
            const float width = float(std::max(1, styleManager.Px(1))) * 1.5f;
            for (int n = 0; n < (int)v->channels.size(); ++n)
            {
                const int c = n == (int)v->channels.size() - 1 ? v->activeChannel : (n >= v->activeChannel ? n + 1 : n);
                if (c < 0 || c >= (int)v->channels.size() || !v->channels[size_t(c)].visible)
                    continue;
                const VisualColorStyleIndex color = theme.curveChannelColors[size_t(v->channels[size_t(c)].color)];
                const LineStyle style{ .colorStyleIndex = color, .lineWidth = width };
                DrawCurve(ctx, plot, v->channels[size_t(c)].curve, style, style);
            }
        }));

        // The editor, in a window under the field.
        if (view.popupOpen && view.popupID != ID::Invalid())
        {
            LayoutOverride at = LayoutOverride{}.Anchored(id, AnchorPlacement::OutsideBottom | AnchorPlacement::InsideLeft,
                                                         { 0, int16_t(styleManager.Px(4)) }, AutoFlipMode::FlipAvoidTarget);
            if (BeginWindow(view.popupID, "Curve", theme.curveWindowStyleRef, std::move(at),
                            ElementFlags::WindowCanMove | ElementFlags::WindowNoCaption | ElementFlags::Shadows))
            {
                CurveEditor(idManager.AllocateDynamicID("curve_editor"), view, LayoutOverride::Auto().FlexGrow(1));
            }
            else
                view.popupOpen = false;   // closed: a click outside, or the focus gone
            EndWindow();
        }
        guiStateManager.SetModified(changed);
        return changed;
    }

    // -------------------------------------------------------------------------
    // CurveEditor
    // -------------------------------------------------------------------------

    void GUIApplication::CurveEditor(ID id, CurveView& view, LayoutOverride&& constraints)
    {
        CurveView* v = &view;
        const int channelCount = (int)view.channels.size();
        view.activeChannel = channelCount > 0 ? std::clamp(view.activeChannel, 0, channelCount - 1) : 0;
        const bool disabled = guiStateManager.Construction().IsDisabled();
        auto newKeyID = [this]() { return idManager.CreateStaticID(); };

        BeginVertical(id, theme.emptyVertStyleRef, std::move(constraints));
        const bool pushed = idManager.PushParent(id);

        // ---- Over the plot: the channels, the selected key, frame all ------------
        BeginHorizontal(idManager.AllocateDynamicID("curve_top"), theme.curveToolbarStyleRef, LayoutOverride::Auto().NoShrink());
        if (channelCount > 1)
        {
            for (int c = 0; c < channelCount; ++c)
            {
                const CurveChannel& channel = view.channels[size_t(c)];
                const ID chipID = idManager.AllocateDynamicID("curve_channel");
                const bool active = c == view.activeChannel;
                if (active) BeginSelected();
                const ElementRef chipRef = RegisterControl(
                    Element{ .id = chipID,
                             .eventHandler = MakeButtonHandler([v, c]() { v->activeChannel = c; }),
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
                           if (c < (int)v->channels.size())
                           {
                               v->channels[size_t(c)].visible = !v->channels[size_t(c)].visible;
                               std::erase_if(v->selectedKeys, [&](ID key) {
                                   const auto& keys = v->channels[size_t(c)].curve.keys;
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
            for (int k = 0; k < (int)view.channels[size_t(c)].curve.keys.size(); ++k)
                if (view.selectedKeys.contains(view.channels[size_t(c)].curve.keys[size_t(k)].stableID))
                {
                    ++selCount;
                    selChannel = c;
                    selKey = k;
                }
        if (selCount == 1)
        {
            const CurveKey key = view.channels[size_t(selChannel)].curve.keys[size_t(selKey)];
            const CurveRange shown = ShownOf(view);
            auto setKey = [this, v, selChannel, id = key.stableID](float time, float value) {
                const std::vector<Curve> before = CurvesOf(*v);
                v->channels[size_t(selChannel)].curve = before[size_t(selChannel)];
                std::unordered_set<ID> only{ id };
                std::swap(v->selectedKeys, only);
                const CurveKey* k = nullptr;
                for (const CurveKey& candidate : before[size_t(selChannel)].keys)
                    if (candidate.stableID == id) k = &candidate;
                if (k)
                    MoveSelected(*v, before, time - k->time, value - k->value);
                std::swap(v->selectedKeys, only);
                const CurveEdit edit = ChangesSince(*v, before);
                for (int c = 0; c < (int)before.size(); ++c)
                    v->channels[size_t(c)].curve = before[size_t(c)];
                CommitCurveEdit(*v, edit);
            };
            float time = key.time, value = key.value;
            DragLabel(idManager.AllocateDynamicID("curve_time_label"), "Time", theme.dragLabelStyleRef);
            if (Field(idManager.AllocateDynamicID("curve_time"), time, TextEditBehavior::None, theme.controls->number,
                      NumericFieldConfig<float>{ .stepPerPixel = double(shown.timeMax - shown.timeMin) / 400.0 },
                      LayoutOverride::FixedWidth(Pixels{ styleManager.Px(64) })))
                setKey(time, key.value);
            DragLabel(idManager.AllocateDynamicID("curve_value_label"), "Value", theme.dragLabelStyleRef);
            if (Field(idManager.AllocateDynamicID("curve_value"), value, TextEditBehavior::None, theme.controls->number,
                      NumericFieldConfig<float>{ .stepPerPixel = double(shown.valueMax - shown.valueMin) / 400.0 },
                      LayoutOverride::FixedWidth(Pixels{ styleManager.Px(64) })))
                setKey(key.time, value);
        }
        Button(idManager.AllocateDynamicID("curve_frame_all"), { theme.curveFrameAllIcon, {}, "Frame all (A)" },
               [v]() { FrameAll(*v); }, theme.iconButtonStyleRef);
        EndHorizontal();

        // ---- The plot ----------------------------------------------------------
        const ID plotID = idManager.AllocateDynamicID("curve_plot");
        const Plot lastPlot{ view.plotRect, ShownOf(view) };

        // The grid's numbers, from where the plot was last frame.
        struct GridLabel { TextID text; float at; int2 size; bool value; };
        std::vector<GridLabel> labels;
        const float minGap = float(styleManager.Px(56)), minGapV = float(styleManager.Px(32));
        const float timeStep  = NiceStep(lastPlot.TimeSpan(),  float(lastPlot.rect.w), minGap);
        const float valueStep = NiceStep(lastPlot.ValueSpan(), float(lastPlot.rect.h), minGapV);
        auto addLabels = [&](float lo, float hi, float step, bool isValue, const char* hint) {
            if (!(step > 0.0f))
                return;
            const int first = int(std::ceil(lo / step - 1e-3f)), last = int(std::floor(hi / step + 1e-3f));
            if (last < first || last - first > 200)
                return;
            const ID rangeID = idManager.AllocateRangedID(uint32_t(last - first + 1), hint);
            for (int i = first; i <= last; ++i)
            {
                const float at = float(i) * step;
                const TextID text = textSystem->RegisterText(idManager.GetSubID(rangeID, uint32_t(i - first)), GridNumber(at, step),
                                                             theme.curveLabelStyleRef, ElementState::None);
                const TextMeasurement m = textSystem->MeasureText(text, TextWrapContext{});
                labels.push_back({ text, at, { m.maxWidth, m.maxHeight }, isValue });
            }
        };
        if (lastPlot.rect.w > 0 && lastPlot.rect.h > 0)
        {
            addLabels(lastPlot.shown.timeMin, lastPlot.shown.timeMax, timeStep, false, "curve_time_labels");
            addLabels(lastPlot.shown.valueMin, lastPlot.shown.valueMax, valueStep, true, "curve_value_labels");
        }

        // The selected key's time and value beside it.
        TextID readout = TextID::Invalid;
        int2 readoutSize {};
        if (selCount == 1)
        {
            const CurveChannel& channel = view.channels[size_t(selChannel)];
            const CurveKey& key = channel.curve.keys[size_t(selKey)];
            const std::string text = channelCount > 1 ? std::format("{} \xC2\xB7 {} \xC2\xB7 {}", channel.name, Number(key.time), Number(key.value))
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
            plotHandler = [&gsm, &es, v, reach, handleLength, dragDistance, newKeyID](ElementEvent&& event) -> bool
            {
                if (event.rect == nullptr)
                    return false;
                const Plot plot{ event.rect->innerRect, ShownOf(*v) };
                const int2 cursor = event.LocalCursor(es);
                const float2 at{ float(cursor.x), float(cursor.y) };
                const bool shift = (event.state.modifiers & KeyModifier::Shift)   != KeyModifier::None;
                const bool ctrl  = (event.state.modifiers & KeyModifier::Control) != KeyModifier::None;
                auto restore = [v]() {
                    for (int c = 0; c < (int)v->channels.size() && c < (int)v->before.size(); ++c)
                        v->channels[size_t(c)].curve = v->before[size_t(c)];
                };
                auto finish = [v]() {
                    // Already where it was dropped: the edit, for undo.
                    const CurveEdit edit = ChangesSince(*v, v->before);
                    if (!edit.empty())
                    {
                        ++v->revision;
                        if (v->onEdit)
                            v->onEdit(edit);
                    }
                };
                switch (event.type)
                {
                    case EventType::MouseDown:
                    {
                        if (event.state.MiddlePressed())
                        {
                            v->pan.Press(event, es, MouseButtons::Middle, /*atOnce*/ true);
                            v->panFrom = ShownOf(*v);
                            gsm.Interaction().SetCaptureID(event.id);
                            return true;
                        }
                        if (!event.state.LeftPressed())
                            return false;
                        gsm.Interaction().SetFocusID(event.id);
                        gsm.TextEditorFocus().Clear();
                        // A double click is two at the same spot.
                        const bool second = v->plotClicks.IsDoubleClick(event.id) && Distance(at, v->lastClick) <= reach;
                        v->lastClick = at;

                        // A tangent handle: its slope, as dragged.
                        if (const HandleHit handle = HitHandle(*v, plot, at, reach, handleLength); handle.key >= 0)
                        {
                            v->tangentDrag.Press(event, es, MouseButtons::Left, /*atOnce*/ true);
                            v->tangentKey = v->channels[size_t(handle.channel)].curve.keys[size_t(handle.key)].stableID;
                            v->tangentOut = handle.out;
                            v->before = CurvesOf(*v);
                            v->keyMoved = false;
                            gsm.Interaction().SetCaptureID(event.id);
                            return true;
                        }
                        // A key: selected (Shift adds, Ctrl toggles), and the selection dragged.
                        if (const KeyHit hit = HitKey(*v, plot, at, reach); hit.key >= 0)
                        {
                            v->activeChannel = hit.channel;
                            const bool wasSelected = v->selectedKeys.contains(hit.id);
                            if (ctrl)
                            {
                                if (wasSelected) v->selectedKeys.erase(hit.id); else v->selectedKeys.insert(hit.id);
                            }
                            else if (shift)
                                v->selectedKeys.insert(hit.id);
                            else if (!wasSelected)
                                v->selectedKeys = { hit.id };
                            if (!ctrl || !wasSelected)
                            {
                                v->keyDrag.Press(event, es);
                                v->keyDragFrom = { plot.Time(at.x), plot.Value(at.y) };
                                v->before = CurvesOf(*v);
                                v->keyMoved = false;
                                gsm.Interaction().SetCaptureID(event.id);
                            }
                            return true;
                        }
                        // A double click: a key on the active channel, on its curve when near it.
                        if (second && v->activeChannel < (int)v->channels.size())
                        {
                            const Curve& curve = v->channels[size_t(v->activeChannel)].curve;
                            const float time = plot.Time(at.x);
                            float value = plot.Value(at.y);
                            if (!curve.keys.empty() && std::fabs(plot.Y(EvaluateCurve(curve, time)) - at.y) <= reach)
                                value = EvaluateCurve(curve, time);
                            const ID keyID = newKeyID();
                            const CurveEdit edit = AddCurveKeyEdit(*v, v->activeChannel, time, value, keyID);
                            if (!edit.empty())
                            {
                                CommitCurveEdit(*v, edit);
                                v->selectedKeys = { keyID };
                            }
                            return true;
                        }
                        v->boxSelect.Press(event, es);
                        v->boxFrom = v->boxTo = { plot.Time(at.x), plot.Value(at.y) };
                        v->boxBase = (shift || ctrl) ? v->selectedKeys : std::unordered_set<ID>{};
                        gsm.Interaction().SetCaptureID(event.id);
                        return true;
                    }
                    case EventType::MouseMove:
                    {
                        if (v->pan.IsPressed(event.id))
                        {
                            const int2 d = v->pan.ScreenDelta(event.state);
                            const Plot from{ plot.rect, v->panFrom };
                            const float dt = -float(d.x) / float(std::max(1, plot.rect.w)) * from.TimeSpan();
                            const float dv =  float(d.y) / float(std::max(1, plot.rect.h)) * from.ValueSpan();
                            v->shown = { v->panFrom.timeMin + dt, v->panFrom.timeMax + dt, v->panFrom.valueMin + dv, v->panFrom.valueMax + dv };
                            v->shownSet = true;
                            return true;
                        }
                        if (v->tangentDrag.IsPressed(event.id))
                        {
                            for (int c = 0; c < (int)v->channels.size() && c < (int)v->before.size(); ++c)
                            {
                                Curve curve = v->before[size_t(c)];
                                for (CurveKey& k : curve.keys)
                                {
                                    if (k.stableID != v->tangentKey)
                                        continue;
                                    // The slope to the pointer; the handle stays on its side.
                                    float dt = plot.Time(at.x) - k.time;
                                    const float minDt = plot.TimeSpan() * 1e-4f;
                                    dt = v->tangentOut ? std::max(dt, minDt) : std::min(dt, -minDt);
                                    const float slope = (plot.Value(at.y) - k.value) / dt;
                                    if (k.mode == CurveTangentMode::Broken)
                                    {
                                        (v->tangentOut ? k.outSlope : k.inSlope) = slope;
                                        (v->tangentOut ? k.outMode : k.inMode) = CurveSideMode::Free;
                                    }
                                    else
                                    {
                                        k.mode = CurveTangentMode::FreeSmooth;
                                        k.inSlope = k.outSlope = slope;
                                    }
                                    UpdateCurveTangents(curve);
                                    v->channels[size_t(c)].curve = curve;
                                    v->keyMoved = true;
                                }
                            }
                            return true;
                        }
                        if (v->keyDrag.IsPressed(event.id))
                        {
                            if (!v->keyDrag.Move(event, dragDistance))
                                return true;
                            if (v->before.size() != v->channels.size())
                            {
                                v->keyDrag = {};
                                return true;
                            }
                            MoveSelected(*v, v->before, plot.Time(at.x) - v->keyDragFrom.x, plot.Value(at.y) - v->keyDragFrom.y);
                            v->keyMoved = true;
                            return true;
                        }
                        if (v->boxSelect.IsPressed(event.id))
                        {
                            if (!v->boxSelect.Move(event, dragDistance))
                                return true;
                            v->boxTo = { plot.Time(at.x), plot.Value(at.y) };
                            const float t0 = std::min(v->boxFrom.x, v->boxTo.x), t1 = std::max(v->boxFrom.x, v->boxTo.x);
                            const float v0 = std::min(v->boxFrom.y, v->boxTo.y), v1 = std::max(v->boxFrom.y, v->boxTo.y);
                            std::unordered_set<ID> selection = v->boxBase;
                            for (const CurveChannel& channel : v->channels)
                                if (channel.visible)
                                    for (const CurveKey& k : channel.curve.keys)
                                        if (k.time >= t0 && k.time <= t1 && k.value >= v0 && k.value <= v1)
                                        {
                                            if (ctrl && v->boxBase.contains(k.stableID)) selection.erase(k.stableID);
                                            else selection.insert(k.stableID);
                                        }
                            v->selectedKeys = std::move(selection);
                            return true;
                        }
                        return false;
                    }
                    case EventType::MouseUp:
                    {
                        if (v->pan.IsPressed(event.id))
                            return v->pan.Release(event) != PointerDrag::Ended::No;
                        if (v->tangentDrag.IsPressed(event.id))
                        {
                            v->tangentDrag.Release(event);
                            if (v->keyMoved)
                                finish();
                            v->tangentKey = ID::Invalid();
                            v->before.clear();
                            return true;
                        }
                        if (v->keyDrag.IsPressed(event.id))
                        {
                            v->keyDrag.Release(event);
                            if (v->keyMoved)
                                finish();
                            v->before.clear();
                            return true;
                        }
                        if (const PointerDrag::Ended ended = v->boxSelect.Release(event); ended != PointerDrag::Ended::No)
                        {
                            // A click on the empty plot clears the selection.
                            if (ended == PointerDrag::Ended::Click && !shift && !ctrl)
                                v->selectedKeys.clear();
                            return true;
                        }
                        return false;
                    }
                    case EventType::MouseScroll:
                    {
                        // The wheel zooms round the pointer; Shift only along the time, Ctrl only the values.
                        const float ticks = event.state.scroll.y;
                        if (ticks == 0.0f)
                            return false;
                        const float factor = std::pow(1.15f, -ticks);
                        ZoomAround(*v, { plot.Time(at.x), plot.Value(at.y) }, factor, !ctrl, !shift);
                        return true;
                    }
                    case EventType::KeyDown:
                    {
                        const KeyCode key = event.state.lastKey;
                        if (key == KeyCode::Escape)
                        {
                            if (v->keyDrag.IsPressed(event.id) || v->tangentDrag.IsPressed(event.id))
                            {
                                restore();
                                v->keyDrag.Cancel(event.id);
                                v->tangentDrag.Cancel(event.id);
                                v->before.clear();
                                return true;
                            }
                            if (v->boxSelect.IsPressed(event.id))
                            {
                                v->boxSelect.Cancel(event.id);
                                v->selectedKeys = v->boxBase;
                                return true;
                            }
                            if (!v->selectedKeys.empty())
                            {
                                v->selectedKeys.clear();
                                return true;
                            }
                            return false;
                        }
                        if (key == KeyCode::Delete || key == KeyCode::Backspace)
                        {
                            CommitCurveEdit(*v, DeleteSelectedCurveKeysEdit(*v));
                            return true;
                        }
                        if (key == KeyCode::KeyA && ctrl)
                        {
                            v->selectedKeys.clear();
                            if (v->activeChannel < (int)v->channels.size())
                                for (const CurveKey& k : v->channels[size_t(v->activeChannel)].curve.keys)
                                    v->selectedKeys.insert(k.stableID);
                            return true;
                        }
                        if (key == KeyCode::KeyA && !ctrl)
                        {
                            FrameAll(*v);
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
            Element{ .id = plotID, .eventHandler = std::move(plotHandler), .styleRef = theme.curvePlotStyleRef },
            LayoutOverride::Auto().FlexGrow(1));
        VERIFY(elementStorage.AddPostLayout(plotRef, [v](PostLayoutContext& ctx) { v->plotRect = ctx.ElementInnerRect(); }));
        {
            const int32_t px1 = std::max(1, styleManager.Px(1)), px4 = styleManager.Px(4);
            const float keySize = float(styleManager.Px(8)), selectedSize = float(styleManager.Px(11)), edge = float(std::max(1, styleManager.Px(1)));
            const float handleLength = float(styleManager.Px(40)), handleRadius = float(styleManager.Px(4));
            const float lineWidth = float(px1) * 2.0f, otherWidth = float(px1) * 1.5f, handleWidth = float(px1) * 1.25f;
            const float dash = float(styleManager.Px(3));
            const VisualBoxStyleIndex readoutBox = theme.curveReadoutStyleRef.visualBoxStyleIndex;
            const VisualBoxStyleIndex boxSelect = theme.curveBoxSelectStyleRef.visualBoxStyleIndex;
            const int2 readoutPad{ styleManager.Px(6), styleManager.Px(2) };
            VERIFY(elementStorage.AddCustomDraw(plotRef, [=, this](CustomDrawContext& ctx)
            {
                const IntRect outer = ctx.ElementRect(), inner = ctx.ElementInnerRect();
                const Plot plot{ inner, ShownOf(*v) };

                // The grid: minor lines a quarter of a labelled step apart, the labelled ones
                // brighter, the lines at zero brighter still.
                auto gridLines = [&](float lo, float hi, float step, bool vertical) {
                    if (!(step > 0.0f))
                        return;
                    const float minor = step / 4.0f;
                    const int first = int(std::ceil(lo / minor - 1e-3f)), last = int(std::floor(hi / minor + 1e-3f));
                    if (last - first > 2000)
                        return;
                    for (int i = first; i <= last; ++i)
                    {
                        const VisualColorStyleIndex color = i == 0 ? theme.curveZeroColor : (i % 4 == 0 ? theme.curveGridMajorColor : theme.curveGridColor);
                        if (vertical)
                            ctx.RenderQuad(IntRect{ int32_t(plot.X(float(i) * minor)), inner.y, px1, inner.h }, inner, color);
                        else
                            ctx.RenderQuad(IntRect{ inner.x, int32_t(plot.Y(float(i) * minor)), inner.w, px1 }, inner, color);
                    }
                };
                const float tStep = NiceStep(plot.TimeSpan(), float(inner.w), minGap);
                const float vStep = NiceStep(plot.ValueSpan(), float(inner.h), minGapV);
                gridLines(plot.shown.timeMin, plot.shown.timeMax, tStep, true);
                gridLines(plot.shown.valueMin, plot.shown.valueMax, vStep, false);
                for (const GridLabel& label : labels)
                {
                    if (label.value)
                        ctx.RenderText(label.text, IntRect{ inner.x - label.size.x - px4, int32_t(plot.Y(label.at)) - label.size.y / 2,
                                                            label.size.x, label.size.y });
                    else
                        ctx.RenderText(label.text, IntRect{ int32_t(plot.X(label.at)) - label.size.x / 2, inner.y + inner.h + px4,
                                                            label.size.x, label.size.y });
                }

                // The curves: the others thinner and faded, the active one on top; before the
                // first key and after the last dashed.
                const int count = (int)v->channels.size();
                for (int n = 0; n < count; ++n)
                {
                    const int c = n == count - 1 ? v->activeChannel : (n >= v->activeChannel ? n + 1 : n);
                    if (c < 0 || c >= count || !v->channels[size_t(c)].visible)
                        continue;
                    const CurveChannel& channel = v->channels[size_t(c)];
                    const bool active = c == v->activeChannel || count == 1;
                    const VisualColorStyleIndex color = active ? theme.curveChannelColors[size_t(channel.color)]
                                                               : theme.curveChannelFadedColors[size_t(channel.color)];
                    const float width = active ? lineWidth : otherWidth;
                    DrawCurve(ctx, plot, channel.curve, LineStyle{ .colorStyleIndex = color, .lineWidth = width },
                              LineStyle{ .colorStyleIndex = color, .lineWidth = width, .dashSize = dash });

                    // Its keys: rounded squares in its color with an edge of the plot's; a
                    // selected one larger and light, ringed in the accent, with its handles.
                    for (const CurveKey& k : channel.curve.keys)
                    {
                        const float2 p = plot.At(k);
                        if (p.x < float(outer.x) || p.x > float(outer.x + outer.w))
                            continue;
                        const bool selected = v->selectedKeys.contains(k.stableID);
                        if (selected)
                        {
                            for (const bool out : { false, true })
                            {
                                if (!HasHandle(k, out))
                                    continue;
                                const float2 end = HandleEnd(plot, k, out, handleLength);
                                ctx.RenderLine(int2{ int32_t(p.x), int32_t(p.y) }, int2{ int32_t(end.x), int32_t(end.y) },
                                               LineStyle{ .colorStyleIndex = theme.curveHandleColor, .lineWidth = handleWidth });
                                ctx.RenderCircle(end, handleRadius, LineStyle{ .colorStyleIndex = theme.curveHandleColor, .lineWidth = handleWidth });
                            }
                            const float s = selectedSize;
                            ctx.RenderQuad(IntRect{ int32_t(p.x - s / 2), int32_t(p.y - s / 2), int32_t(s), int32_t(s) }, outer, theme.curveHandleColor, s / 4.0f);
                            const float t = s - 4.0f * edge;
                            ctx.RenderQuad(IntRect{ int32_t(p.x - t / 2), int32_t(p.y - t / 2), int32_t(t), int32_t(t) }, outer, theme.curveSelectedKeyColor, t / 4.0f);
                        }
                        else
                        {
                            const float s = keySize + 2.0f * edge;
                            ctx.RenderQuad(IntRect{ int32_t(p.x - s / 2), int32_t(p.y - s / 2), int32_t(s), int32_t(s) }, outer, theme.curveKeyEdgeColor, s / 4.0f);
                            ctx.RenderQuad(IntRect{ int32_t(p.x - keySize / 2), int32_t(p.y - keySize / 2), int32_t(keySize), int32_t(keySize) },
                                           outer, active ? theme.curveChannelColors[size_t(channel.color)] : theme.curveChannelFadedColors[size_t(channel.color)],
                                           keySize / 4.0f);
                        }
                    }
                }

                // The box being dragged.
                if (v->boxSelect.IsDragging(plotID))
                {
                    const int32_t x0 = int32_t(plot.X(std::min(v->boxFrom.x, v->boxTo.x))), x1 = int32_t(plot.X(std::max(v->boxFrom.x, v->boxTo.x)));
                    const int32_t y0 = int32_t(plot.Y(std::max(v->boxFrom.y, v->boxTo.y))), y1 = int32_t(plot.Y(std::min(v->boxFrom.y, v->boxTo.y)));
                    ctx.RenderBox(IntRect{ x0, y0, x1 - x0, y1 - y0 }, inner, boxSelect);
                }

                // The selected key's time and value, beside it.
                if (readout != TextID::Invalid && selChannel >= 0 && selChannel < count
                    && selKey < (int)v->channels[size_t(selChannel)].curve.keys.size())
                {
                    const float2 p = plot.At(v->channels[size_t(selChannel)].curve.keys[size_t(selKey)]);
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
            if (!view.menuOpen)
            {
                // Just opened: what's under the right click.
                view.menuKey = ID::Invalid();
                const Plot plot{ view.plotRect, ShownOf(view) };
                const int2 at = contextMenuScreenPos.value_or(int2{ view.plotRect.x + view.plotRect.w / 2, view.plotRect.y + view.plotRect.h / 2 });
                const float2 p{ float(at.x), float(at.y) };
                view.menuAt = { plot.Time(p.x), plot.Value(p.y) };
                if (const KeyHit hit = HitKey(view, plot, p, float(styleManager.Px(7))); hit.key >= 0)
                {
                    view.menuKey = hit.id;
                    view.activeChannel = hit.channel;
                    if (!view.selectedKeys.contains(hit.id))
                        view.selectedKeys = { hit.id };
                }
            }
            view.menuOpen = true;
            if (view.menuKey != ID::Invalid())
            {
                MenuItem("Delete key", [v]() { CommitCurveEdit(*v, DeleteSelectedCurveKeysEdit(*v)); });
                MenuSeparator();
                // The mode of the key right-clicked, checked.
                CurveTangentMode mode = CurveTangentMode::ClampedAuto;
                CurveSideMode inMode = CurveSideMode::Free, outMode = CurveSideMode::Free;
                for (const CurveChannel& channel : view.channels)
                    for (const CurveKey& k : channel.curve.keys)
                        if (k.stableID == view.menuKey) { mode = k.mode; inMode = k.inMode; outMode = k.outMode; }
                static constexpr std::pair<CurveTangentMode, const char*> kModes[] = {
                    { CurveTangentMode::ClampedAuto, "Clamped auto" }, { CurveTangentMode::Auto, "Auto" },
                    { CurveTangentMode::FreeSmooth, "Free smooth" },   { CurveTangentMode::Flat, "Flat" },
                    { CurveTangentMode::Broken, "Broken" } };
                for (const auto& [m, name] : kModes)
                {
                    if (m == mode) BeginSelected();
                    MenuItem(name, [v, m]() { CommitCurveEdit(*v, SetCurveTangentModeEdit(*v, m)); });
                    if (m == mode) EndSelected();
                }
                MenuSeparator();
                static constexpr std::pair<CurveSideMode, const char*> kSides[] = {
                    { CurveSideMode::Free, "Free" }, { CurveSideMode::Linear, "Linear" }, { CurveSideMode::Constant, "Constant" } };
                const bool broken = mode == CurveTangentMode::Broken;
                for (const auto& [label, in, out] : { std::tuple{ "Left tangent", true, false }, std::tuple{ "Right tangent", false, true },
                                                      std::tuple{ "Both tangents", true, true } })
                {
                    if (BeginMenu(idManager.AllocateDynamicID("curve_side_menu"), label))
                    {
                        for (const auto& [side, name] : kSides)
                        {
                            const bool checked = broken && (!in || inMode == side) && (!out || outMode == side);
                            if (checked) BeginSelected();
                            MenuItem(name, [v, side = side, in = in, out = out]() { CommitCurveEdit(*v, SetCurveSideModeEdit(*v, side, in, out)); });
                            if (checked) EndSelected();
                        }
                    }
                    EndMenu();
                }
            }
            else
            {
                MenuItem("Add key", [v, newKeyID]() {
                    const ID keyID = newKeyID();
                    const CurveEdit edit = AddCurveKeyEdit(*v, v->activeChannel, v->menuAt.x, v->menuAt.y, keyID);
                    if (!edit.empty())
                    {
                        CommitCurveEdit(*v, edit);
                        v->selectedKeys = { keyID };
                    }
                });
                MenuItem("Frame all", [v]() { FrameAll(*v); });
            }
        }
        else
            view.menuOpen = false;
        if (!disabled)
            EndContextMenu();

        // ---- Under the plot: the presets, and what the curve does past its ends ----
        BeginHorizontal(idManager.AllocateDynamicID("curve_bottom"), theme.curveToolbarStyleRef, LayoutOverride::Auto().NoShrink());
        const std::span<const CurvePreset> presets = view.presets.empty() ? DefaultCurvePresets() : std::span<const CurvePreset>(view.presets);
        const int active = view.activeChannel;
        const VisualColorStyleIndex presetColor = channelCount > 0 ? theme.curveChannelColors[size_t(view.channels[size_t(active)].color)]
                                                                   : theme.curveChannelColors[0];
        for (size_t p = 0; p < presets.size(); ++p)
        {
            const CurvePreset preset = presets[p];
            const ElementRef presetRef = RegisterControl(
                Element{ .id = idManager.AllocateDynamicID("curve_preset"),
                         .eventHandler = disabled ? EventHandler{} : MakeButtonHandler([v, preset, active, newKeyID]() {
                             CommitCurveEdit(*v, ApplyCurvePresetEdit(*v, active, preset, newKeyID));
                         }),
                         .styleRef = theme.curvePresetStyleRef },
                LayoutOverride{});
            SetTooltip(preset.name);
            const float width = float(std::max(1, styleManager.Px(1))) * 1.25f;
            VERIFY(elementStorage.AddCustomDraw(presetRef, [preset, presetColor, width](CustomDrawContext& ctx)
            {
                Curve shape = preset.curve;
                shape.preWrap = shape.postWrap = CurveWrapMode::Clamp;
                const LineStyle style{ .colorStyleIndex = presetColor, .lineWidth = width };
                DrawCurve(ctx, Plot{ ctx.ElementInnerRect(), CurveRange{} }, shape, style, style);
            }));
        }
        BeginHorizontal(idManager.AllocateDynamicID("curve_bottom_gap"), theme.emptyHorzStyleRef, LayoutOverride{}.FlexGrow(1));
        EndHorizontal();
        if (channelCount > 0)
        {
            static constexpr std::string_view kWraps[] = { "Clamp", "Loop", "Ping-pong" };
            const Curve& curve = view.channels[size_t(active)].curve;
            int pre = int(curve.preWrap), post = int(curve.postWrap);
            if (Dropdown(idManager.AllocateDynamicID("curve_pre_wrap"), pre, kWraps, LayoutOverride::FixedWidth(Pixels{ styleManager.Px(96) })))
                CommitCurveEdit(view, SetCurveWrapEdit(view, active, CurveWrapMode(pre), curve.postWrap));
            SetTooltip("Before the first key");
            if (Dropdown(idManager.AllocateDynamicID("curve_post_wrap"), post, kWraps, LayoutOverride::FixedWidth(Pixels{ styleManager.Px(96) })))
                CommitCurveEdit(view, SetCurveWrapEdit(view, active, view.channels[size_t(active)].curve.preWrap, CurveWrapMode(post)));
            SetTooltip("After the last key");
        }
        EndHorizontal();

        if (pushed) (void)idManager.PopParent();
        EndVertical();
    }
}
