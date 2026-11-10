#include <gui_controls/gui_curve_types.h>

#include <algorithm>
#include <cmath>

// Curves: evaluating them, the slopes the key modes work out, the presets, and the edits a
// CurveField makes, each as the curves before and after so undo puts the old ones back.

namespace tachyon
{
    namespace
    {
        bool ValidChannel(const CurveView& view, int channel)
        {
            return channel >= 0 && channel < (int)view.channels.size();
        }

        float Finite(float v, float fallback = 0.0f) { return std::isfinite(v) ? v : fallback; }

        // Steps from `a` to its next key: a held value when either side is Constant.
        bool IsStep(const CurveKey& a, const CurveKey& b)
        {
            return (a.mode == CurveTangentMode::Broken && a.outMode == CurveSideMode::Constant)
                || (b.mode == CurveTangentMode::Broken && b.inMode == CurveSideMode::Constant);
        }

        // Between two keys, `time` inside them.
        float EvaluateSegment(const CurveKey& a, const CurveKey& b, float time)
        {
            const float dt = b.time - a.time;
            if (!(dt > 0.0f))
                return b.value;
            if (IsStep(a, b))
                return a.value;
            const float s  = std::clamp((time - a.time) / dt, 0.0f, 1.0f);
            const float s2 = s * s, s3 = s2 * s;
            const float h00 = 2.0f * s3 - 3.0f * s2 + 1.0f;
            const float h10 = s3 - 2.0f * s2 + s;
            const float h01 = -2.0f * s3 + 3.0f * s2;
            const float h11 = s3 - s2;
            return h00 * a.value + h10 * a.outSlope * dt + h01 * b.value + h11 * b.inSlope * dt;
        }

        // `time` moved into the keys' span as the wrap mode says.
        float Wrapped(float time, float first, float last, CurveWrapMode mode)
        {
            const float length = last - first;
            if (mode == CurveWrapMode::Clamp || !(length > 0.0f))
                return std::clamp(time, first, last);
            const float t = time - first;
            if (mode == CurveWrapMode::Loop)
            {
                const float r = std::fmod(t, length);
                return first + (r < 0.0f ? r + length : r);
            }
            const float period = 2.0f * length;
            float r = std::fmod(t, period);
            if (r < 0.0f) r += period;
            return first + (r <= length ? r : period - r);
        }

        void SortKeys(Curve& curve)
        {
            std::stable_sort(curve.keys.begin(), curve.keys.end(),
                             [](const CurveKey& a, const CurveKey& b) { return a.time < b.time; });
        }

        // The edit that makes each channel's curve what `change` returns, for the channels it
        // changes.
        template<typename F>
        CurveEdit ChangeEach(const CurveView& view, F&& change)
        {
            CurveEdit edit;
            for (int c = 0; c < (int)view.channels.size(); ++c)
            {
                const Curve& from = view.channels[size_t(c)].curve;
                Curve to = from;
                change(c, to);
                UpdateCurveTangents(to);
                if (!(to == from))
                    edit.changes.push_back({ c, from, std::move(to) });
            }
            return edit;
        }
    }

    float EvaluateCurve(const Curve& curve, float time)
    {
        const auto& keys = curve.keys;
        if (keys.empty())
            return 0.0f;
        time = Finite(time);
        const float first = keys.front().time, last = keys.back().time;
        if (time < first)
            time = Wrapped(time, first, last, curve.preWrap);
        else if (time > last)
            time = Wrapped(time, first, last, curve.postWrap);
        if (keys.size() == 1 || time <= first)
            return keys.front().value;
        if (time >= last)
            return keys.back().value;
        const auto next = std::upper_bound(keys.begin(), keys.end(), time,
                                           [](float t, const CurveKey& k) { return t < k.time; });
        return EvaluateSegment(*std::prev(next), *next, time);
    }

    void UpdateCurveTangents(Curve& curve)
    {
        auto& keys = curve.keys;
        const int count = (int)keys.size();
        for (int i = 0; i < count; ++i)
        {
            CurveKey& k = keys[size_t(i)];
            const CurveKey* prev = i > 0 ? &keys[size_t(i - 1)] : nullptr;
            const CurveKey* next = i + 1 < count ? &keys[size_t(i + 1)] : nullptr;
            // The straight slope between it and a neighbor; 0 when there's none, or no time between.
            auto toward = [&k](const CurveKey* other) {
                if (!other) return 0.0f;
                const float dt = other->time - k.time;
                return dt != 0.0f ? (other->value - k.value) / dt : 0.0f;
            };
            switch (k.mode)
            {
            case CurveTangentMode::Flat:
                k.inSlope = k.outSlope = 0.0f;
                break;
            case CurveTangentMode::Auto:
            case CurveTangentMode::ClampedAuto:
            {
                // Smooth through it: the slope between its neighbors. An end key is level.
                float slope = 0.0f;
                if (prev && next && next->time > prev->time)
                    slope = (next->value - prev->value) / (next->time - prev->time);
                if (k.mode == CurveTangentMode::ClampedAuto && prev && next)
                {
                    const float in = toward(prev), out = toward(next);
                    // A peak or a trough stays level; elsewhere no steeper than three times
                    // either side's straight slope, so it doesn't overshoot its neighbors.
                    if (in * out <= 0.0f)
                        slope = 0.0f;
                    else
                    {
                        const float limit = 3.0f * std::min(std::fabs(in), std::fabs(out));
                        slope = std::clamp(slope, -limit, limit);
                    }
                }
                k.inSlope = k.outSlope = slope;
                break;
            }
            case CurveTangentMode::FreeSmooth:
                k.inSlope = k.outSlope;
                break;
            case CurveTangentMode::Broken:
                if (k.inMode == CurveSideMode::Linear)   k.inSlope  = toward(prev);
                if (k.outMode == CurveSideMode::Linear)  k.outSlope = toward(next);
                if (k.inMode == CurveSideMode::Constant) k.inSlope  = 0.0f;
                if (k.outMode == CurveSideMode::Constant) k.outSlope = 0.0f;
                break;
            }
            k.inSlope  = Finite(k.inSlope);
            k.outSlope = Finite(k.outSlope);
        }
    }

    Curve MakeCurve(std::span<const float2> points, CurveTangentMode mode)
    {
        Curve curve;
        for (const float2 p : points)
            curve.keys.push_back({ .time = p.x, .value = p.y, .mode = mode });
        SortKeys(curve);
        UpdateCurveTangents(curve);
        return curve;
    }

    std::span<const CurvePreset> DefaultCurvePresets()
    {
        // Over 0 to 1 both ways.
        auto key = [](float time, float value, float slope, CurveTangentMode mode = CurveTangentMode::FreeSmooth) {
            return CurveKey{ .time = time, .value = value, .inSlope = slope, .outSlope = slope, .mode = mode };
        };
        auto linear = [](float time, float value) {
            return CurveKey{ .time = time, .value = value, .mode = CurveTangentMode::Broken,
                             .inMode = CurveSideMode::Linear, .outMode = CurveSideMode::Linear };
        };
        auto make = [](std::vector<CurveKey> keys) {
            Curve curve{ .keys = std::move(keys) };
            UpdateCurveTangents(curve);
            return curve;
        };
        static const std::vector<CurvePreset> presets = {
            { "Constant",            make({ key(0, 1, 0, CurveTangentMode::Flat), key(1, 1, 0, CurveTangentMode::Flat) }) },
            { "Linear",              make({ linear(0, 0), linear(1, 1) }) },
            { "Linear, falling",     make({ linear(0, 1), linear(1, 0) }) },
            { "Ease in",             make({ key(0, 0, 0), key(1, 1, 2) }) },
            { "Ease out",            make({ key(0, 0, 2), key(1, 1, 0) }) },
            { "Ease in and out",     make({ key(0, 0, 0, CurveTangentMode::Flat), key(1, 1, 0, CurveTangentMode::Flat) }) },
            { "Ease in and out, falling", make({ key(0, 1, 0, CurveTangentMode::Flat), key(1, 0, 0, CurveTangentMode::Flat) }) },
        };
        return presets;
    }

    void ApplyCurveEdit(CurveView& view, const CurveEdit& edit, bool undo)
    {
        for (const CurveEdit::Change& change : edit.changes)
            if (ValidChannel(view, change.channel))
                view.channels[size_t(change.channel)].curve = undo ? change.from : change.to;

        // Keys that are gone aren't selected.
        std::unordered_set<ID> present;
        for (const CurveChannel& channel : view.channels)
            for (const CurveKey& key : channel.curve.keys)
                present.insert(key.stableID);
        std::erase_if(view.selectedKeys, [&present](ID id) { return !present.contains(id); });
    }

    void CommitCurveEdit(CurveView& view, const CurveEdit& edit)
    {
        if (edit.empty())
            return;
        ApplyCurveEdit(view, edit);
        ++view.revision;
        if (view.onEdit)
            view.onEdit(edit);
    }

    CurveEdit AddCurveKeyEdit(const CurveView& view, int channel, float time, float value, ID keyID)
    {
        if (!ValidChannel(view, channel) || !view.channels[size_t(channel)].visible)
            return {};
        const CurveRange& r = view.range;
        time = std::clamp(Finite(time), std::min(r.timeMin, r.timeMax), std::max(r.timeMin, r.timeMax));
        value = Finite(value);
        if (view.clampValues)
            value = std::clamp(value, std::min(r.valueMin, r.valueMax), std::max(r.valueMin, r.valueMax));
        // Not on top of one already there.
        const float near = std::max(1e-4f * std::fabs(r.timeMax - r.timeMin), 1e-6f);
        const Curve& from = view.channels[size_t(channel)].curve;
        if (std::any_of(from.keys.begin(), from.keys.end(), [&](const CurveKey& k) { return std::fabs(k.time - time) < near; }))
            return {};
        Curve to = from;
        to.keys.push_back({ .stableID = keyID, .time = time, .value = value });
        SortKeys(to);
        UpdateCurveTangents(to);
        return CurveEdit{ .changes = { { channel, from, std::move(to) } } };
    }

    CurveEdit DeleteSelectedCurveKeysEdit(const CurveView& view)
    {
        return ChangeEach(view, [&view](int, Curve& curve) {
            std::erase_if(curve.keys, [&view](const CurveKey& k) { return view.selectedKeys.contains(k.stableID); });
        });
    }

    CurveEdit SetCurveTangentModeEdit(const CurveView& view, CurveTangentMode mode)
    {
        return ChangeEach(view, [&view, mode](int, Curve& curve) {
            for (CurveKey& k : curve.keys)
            {
                if (!view.selectedKeys.contains(k.stableID))
                    continue;
                if (mode == CurveTangentMode::Broken && k.mode != CurveTangentMode::Broken)
                    k.inMode = k.outMode = CurveSideMode::Free;   // broken where they were
                k.mode = mode;
            }
        });
    }

    CurveEdit SetCurveSideModeEdit(const CurveView& view, CurveSideMode side, bool in, bool out)
    {
        return ChangeEach(view, [&view, side, in, out](int, Curve& curve) {
            for (CurveKey& k : curve.keys)
            {
                if (!view.selectedKeys.contains(k.stableID))
                    continue;
                if (k.mode != CurveTangentMode::Broken)
                    k.inMode = k.outMode = CurveSideMode::Free;
                k.mode = CurveTangentMode::Broken;
                if (in)  k.inMode  = side;
                if (out) k.outMode = side;
            }
        });
    }

    CurveEdit ApplyCurvePresetEdit(const CurveView& view, int channel, const CurvePreset& preset,
                                   const std::function<ID()>& makeID)
    {
        if (!ValidChannel(view, channel))
            return {};
        const CurveRange& r = view.range;
        const float timeSpan = r.timeMax - r.timeMin, valueSpan = r.valueMax - r.valueMin;
        const float slopeScale = timeSpan != 0.0f ? valueSpan / timeSpan : 0.0f;
        const Curve& from = view.channels[size_t(channel)].curve;
        Curve to = preset.curve;
        to.preWrap  = from.preWrap;
        to.postWrap = from.postWrap;
        for (CurveKey& k : to.keys)
        {
            k.stableID = makeID ? makeID() : ID::Invalid();
            k.time     = r.timeMin + k.time * timeSpan;
            k.value    = r.valueMin + k.value * valueSpan;
            k.inSlope  *= slopeScale;
            k.outSlope *= slopeScale;
        }
        UpdateCurveTangents(to);
        if (to == from)
            return {};
        return CurveEdit{ .changes = { { channel, from, std::move(to) } } };
    }

    CurveEdit SetCurveWrapEdit(const CurveView& view, int channel, CurveWrapMode pre, CurveWrapMode post)
    {
        if (!ValidChannel(view, channel))
            return {};
        const Curve& from = view.channels[size_t(channel)].curve;
        if (from.preWrap == pre && from.postWrap == post)
            return {};
        Curve to = from;
        to.preWrap  = pre;
        to.postWrap = post;
        return CurveEdit{ .changes = { { channel, from, std::move(to) } } };
    }
}
