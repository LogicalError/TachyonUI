#include <gui_controls/gui_viewport_types.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace tachyon
{
    namespace
    {
        constexpr float kPi = std::numbers::pi_v<float>;
        constexpr float kMaxPitch = kPi * 0.5f - 0.01f;   // never straight up or down: the up vector holds
        constexpr float kTurnPerPixel = 0.008f;           // radians a pixel of drag turns the camera
        constexpr float kHitPixels = 7.0f;                // how near the pointer has to be to a part

        float3 Axis(int i) { return i == 0 ? float3{ 1, 0, 0 } : i == 1 ? float3{ 0, 1, 0 } : float3{ 0, 0, 1 }; }

        float DistanceToSegment(float2 p, float2 a, float2 b)
        {
            const float2 ab = b - a;
            const float len2 = ab.x * ab.x + ab.y * ab.y;
            const float t = len2 > 0.0f ? std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2, 0.0f, 1.0f) : 0.0f;
            const float2 d = p - (a + ab * t);
            return std::sqrt(d.x * d.x + d.y * d.y);
        }

        float Distance2D(float2 a, float2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

        // Whether `p` is inside the quad a, b, c, d (in order round it).
        bool InsideQuad(float2 p, float2 a, float2 b, float2 c, float2 d)
        {
            auto side = [](float2 o, float2 e, float2 q) { return (e.x - o.x) * (q.y - o.y) - (e.y - o.y) * (q.x - o.x); };
            const float s0 = side(a, b, p), s1 = side(b, c, p), s2 = side(c, d, p), s3 = side(d, a, p);
            return (s0 >= 0 && s1 >= 0 && s2 >= 0 && s3 >= 0) || (s0 <= 0 && s1 <= 0 && s2 <= 0 && s3 <= 0);
        }

        // The gizmo's axes for `tool`: the world's, or the object's own for Scale.
        std::array<float3, 3> GizmoAxes(ViewportTool tool, const ViewportTransform& t)
        {
            if (tool == ViewportTool::Scale)
                return { t.rotation.Rotate({ 1, 0, 0 }), t.rotation.Rotate({ 0, 1, 0 }), t.rotation.Rotate({ 0, 0, 1 }) };
            return { Axis(0), Axis(1), Axis(2) };
        }

        int AxisOf(GizmoPart part)
        {
            switch (part)
            {
                case GizmoPart::X: return 0;
                case GizmoPart::Y: return 1;
                case GizmoPart::Z: return 2;
                default:           return -1;
            }
        }

        // A plane part's normal axis: the one it doesn't hold.
        int PlaneNormalOf(GizmoPart part)
        {
            switch (part)
            {
                case GizmoPart::YZ: return 0;
                case GizmoPart::XZ: return 1;
                case GizmoPart::XY: return 2;
                default:            return -1;
            }
        }
    }

    // -------------------------------------------------------------------------
    // The camera
    // -------------------------------------------------------------------------

    float3 ViewportCamera::Forward() const
    {
        return { -std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch) };
    }

    float3 ViewportCamera::Right() const
    {
        return { std::cos(yaw), 0.0f, -std::sin(yaw) };
    }

    float4x4 ViewportCamera::Projection(float aspect) const
    {
        if (!(aspect > 0.0f)) aspect = 1.0f;
        if (orthographic)
            return Orthographic(2.0f * distance * std::tan(fovY * 0.5f), aspect, -farZ, farZ);
        return Perspective(fovY, aspect, nearZ, farZ);
    }

    float ViewportCamera::WorldPerPixel(float3 at, float heightPixels) const
    {
        const float depth = orthographic ? distance : std::max(nearZ, Dot(at - Position(), Forward()));
        return 2.0f * depth * std::tan(fovY * 0.5f) / std::max(1.0f, heightPixels);
    }

    Ray ViewportRay(const ViewportCamera& camera, float2 pixel, float2 size)
    {
        // The pixel on the plane through the target, square to the view.
        const float perPixel = camera.WorldPerPixel(camera.target, size.y);
        const float3 onPlane = camera.target + camera.Right() * ((pixel.x - size.x * 0.5f) * perPixel)
                                             - camera.Up()    * ((pixel.y - size.y * 0.5f) * perPixel);
        if (camera.orthographic)
            return { onPlane - camera.Forward() * camera.farZ, camera.Forward() };
        const float3 eye = camera.Position();
        return { eye, Normalize(onPlane - eye) };
    }

    bool ViewportProject(const ViewportCamera& camera, float2 size, float3 point, float2& pixel)
    {
        const float3 eye = camera.Position();
        const float depth = Dot(point - eye, camera.Forward());
        if (!camera.orthographic && depth <= camera.nearZ * 0.5f)
            return false;
        // Onto the plane through the target, then to pixels.
        const float3 rel = point - camera.target;
        float3 onPlane = rel;
        if (!camera.orthographic)
            onPlane = (point - eye) * (camera.distance / depth) + eye - camera.target;
        const float perPixel = camera.WorldPerPixel(camera.target, size.y);
        pixel = { size.x * 0.5f + Dot(onPlane, camera.Right()) / perPixel,
                  size.y * 0.5f - Dot(onPlane, camera.Up()) / perPixel };
        return true;
    }

    void OrbitCamera(ViewportCamera& camera, float2 delta)
    {
        camera.yaw   -= delta.x * kTurnPerPixel;
        camera.pitch  = std::clamp(camera.pitch - delta.y * kTurnPerPixel, -kMaxPitch, kMaxPitch);
    }

    void LookCamera(ViewportCamera& camera, float2 delta)
    {
        const float3 eye = camera.Position();
        OrbitCamera(camera, delta);
        camera.target = eye + camera.Forward() * camera.distance;
    }

    void PanCamera(ViewportCamera& camera, float2 delta, float heightPixels)
    {
        const float perPixel = camera.WorldPerPixel(camera.target, heightPixels);
        camera.target = camera.target - camera.Right() * (delta.x * perPixel) + camera.Up() * (delta.y * perPixel);
    }

    void DollyCamera(ViewportCamera& camera, float factor)
    {
        if (!(factor > 0.0f) || !std::isfinite(factor))
            return;
        camera.distance = std::clamp(camera.distance / factor, camera.nearZ * 2.0f, camera.farZ * 0.5f);
    }

    void AlignCamera(ViewportCamera& camera, int axis, bool fromNegative)
    {
        switch (axis)
        {
            case 0: camera.yaw = fromNegative ? -kPi * 0.5f : kPi * 0.5f; camera.pitch = 0.0f; break;
            case 1: camera.yaw = 0.0f; camera.pitch = fromNegative ? kMaxPitch : -kMaxPitch; break;
            default: camera.yaw = fromNegative ? kPi : 0.0f; camera.pitch = 0.0f; break;
        }
    }

    void FrameSelection(ViewportView& view)
    {
        if (!view.hasSelection)
            return;
        view.camera.target = view.selection.position;
        // The radius takes a third of the view's height.
        view.camera.distance = std::max(view.camera.nearZ * 2.0f, 3.0f * view.selectionRadius / std::tan(view.camera.fovY * 0.5f));
    }

    void FlyCamera(ViewportView& view, float seconds)
    {
        const uint8_t k = view.flyKeys;
        if (k == 0 || !(seconds > 0.0f))
            return;
        auto axis = [k](uint8_t plus, uint8_t minus) { return float((k & plus) != 0) - float((k & minus) != 0); };
        const ViewportCamera& c = view.camera;
        const float3 move = c.Forward() * axis(FlyForward, FlyBack) + c.Right() * axis(FlyRight, FlyLeft)
                          + float3{ 0, 1, 0 } * axis(FlyUp, FlyDown);
        if (Length(move) == 0.0f)
            return;
        const float speed = view.flySpeed * (view.flyFast ? 3.0f : 1.0f);
        view.camera.target = view.camera.target + Normalize(move) * (speed * seconds);
    }

    // -------------------------------------------------------------------------
    // The gizmo
    // -------------------------------------------------------------------------

    GizmoPart HitGizmo(const ViewportView& view, float2 pixel, float2 size)
    {
        if (!view.hasSelection || view.tool == ViewportTool::Select)
            return GizmoPart::None;
        const ViewportCamera& camera = view.camera;
        const float3 c = view.selection.position;
        float2 center;
        if (!ViewportProject(camera, size, c, center))
            return GizmoPart::None;
        const float length = kGizmoPixels * camera.WorldPerPixel(c, size.y);
        const std::array<float3, 3> axes = GizmoAxes(view.tool, view.selection);

        if (view.tool != ViewportTool::Rotate && Distance2D(pixel, center) <= kHitPixels + 2.0f)
            return GizmoPart::Center;

        GizmoPart best = GizmoPart::None;
        float bestDistance = kHitPixels;
        if (view.tool == ViewportTool::Rotate)
        {
            // The rings, each round its axis, `length` across.
            for (int a = 0; a < 3; ++a)
            {
                const float3 u = axes[size_t((a + 1) % 3)], v = axes[size_t((a + 2) % 3)];
                float2 prev{};
                bool havePrev = false;
                for (int i = 0; i <= 64; ++i)
                {
                    const float angle = 2.0f * kPi * float(i) / 64.0f;
                    float2 p;
                    const bool shown = ViewportProject(camera, size, c + (u * std::cos(angle) + v * std::sin(angle)) * length, p);
                    if (shown && havePrev)
                    {
                        const float d = DistanceToSegment(pixel, prev, p);
                        if (d < bestDistance) { bestDistance = d; best = a == 0 ? GizmoPart::X : a == 1 ? GizmoPart::Y : GizmoPart::Z; }
                    }
                    prev = p;
                    havePrev = shown;
                }
            }
            return best;
        }

        // The plane squares (move only), between a quarter and half of an arrow out.
        if (view.tool == ViewportTool::Move)
        {
            constexpr GizmoPart planes[3] = { GizmoPart::YZ, GizmoPart::XZ, GizmoPart::XY };
            for (int n = 0; n < 3; ++n)
            {
                const float3 u = axes[size_t((n + 1) % 3)], v = axes[size_t((n + 2) % 3)];
                float2 q[4];
                const float3 corners[4] = { u * 0.2f + v * 0.2f, u * 0.5f + v * 0.2f, u * 0.5f + v * 0.5f, u * 0.2f + v * 0.5f };
                bool shown = true;
                for (int k = 0; k < 4; ++k)
                    shown = ViewportProject(camera, size, c + corners[k] * length, q[k]) && shown;
                if (shown && InsideQuad(pixel, q[0], q[1], q[2], q[3]))
                    return planes[n];
            }
        }

        // The arrows, or the scale handles' lines.
        for (int a = 0; a < 3; ++a)
        {
            float2 end;
            if (!ViewportProject(camera, size, c + axes[size_t(a)] * length, end))
                continue;
            const float d = DistanceToSegment(pixel, center, end);
            if (d < bestDistance) { bestDistance = d; best = a == 0 ? GizmoPart::X : a == 1 ? GizmoPart::Y : GizmoPart::Z; }
        }
        return best;
    }

    ViewportTransform DragGizmo(ViewportTool tool, GizmoPart part, const ViewportTransform& before,
                                const ViewportCamera& camera, float2 size, float2 from, float2 to)
    {
        ViewportTransform t = before;
        const std::array<float3, 3> axes = GizmoAxes(tool, before);
        const Ray r0 = ViewportRay(camera, from, size);
        const Ray r1 = ViewportRay(camera, to, size);
        const float3 c = before.position;
        const int axis = AxisOf(part);

        switch (tool)
        {
            case ViewportTool::Move:
            {
                if (axis >= 0)
                {
                    const float s0 = ClosestOnLine(r0, c, axes[size_t(axis)]);
                    const float s1 = ClosestOnLine(r1, c, axes[size_t(axis)]);
                    if (std::isfinite(s0) && std::isfinite(s1))
                        t.position = c + axes[size_t(axis)] * (s1 - s0);
                    return t;
                }
                // A plane's, or the view's for the middle.
                const int n = PlaneNormalOf(part);
                const float3 normal = n >= 0 ? axes[size_t(n)] : camera.Forward();
                const float d0 = IntersectPlane(r0, c, normal), d1 = IntersectPlane(r1, c, normal);
                if (d0 > 0.0f && d1 > 0.0f)
                    t.position = c + (r1.At(d1) - r0.At(d0));
                return t;
            }
            case ViewportTool::Rotate:
            {
                if (axis < 0)
                    return t;
                const float3 a = axes[size_t(axis)];
                const float d0 = IntersectPlane(r0, c, a), d1 = IntersectPlane(r1, c, a);
                float angle = 0.0f;
                if (d0 > 0.0f && d1 > 0.0f && std::fabs(Dot(a, camera.Forward())) > 0.1f)
                {
                    const float3 v0 = r0.At(d0) - c, v1 = r1.At(d1) - c;
                    angle = std::atan2(Dot(a, Cross(v0, v1)), Dot(v0, v1));
                }
                else
                {
                    // Seen edge on: turn by the drag across the view, the way the ring's front moves.
                    const float2 drag = to - from;
                    angle = (drag.x - drag.y) * 0.01f * (Dot(a, Cross(camera.Up(), camera.Right())) >= 0.0f ? 1.0f : -1.0f);
                }
                t.rotation = (quat::AxisAngle(a, angle) * before.rotation).Normalized();
                return t;
            }
            case ViewportTool::Scale:
            {
                if (axis >= 0)
                {
                    const float s0 = ClosestOnLine(r0, c, axes[size_t(axis)]);
                    const float s1 = ClosestOnLine(r1, c, axes[size_t(axis)]);
                    if (std::isfinite(s0) && std::isfinite(s1) && std::fabs(s0) > 1e-6f)
                        t.scale[axis] = std::max(1e-3f, before.scale[axis] * (s1 / s0));
                    return t;
                }
                if (part == GizmoPart::Center)
                {
                    // By how much farther from the middle the pointer is, on the view.
                    float2 center;
                    if (!ViewportProject(camera, size, c, center))
                        return t;
                    const float a = Distance2D(from, center), b = Distance2D(to, center);
                    if (a > 1.0f)
                    {
                        const float f = b / a;
                        t.scale = { std::max(1e-3f, before.scale.x * f), std::max(1e-3f, before.scale.y * f), std::max(1e-3f, before.scale.z * f) };
                    }
                }
                return t;
            }
            case ViewportTool::Select:
                return t;
        }
        return t;
    }

    // -------------------------------------------------------------------------
    // Edits
    // -------------------------------------------------------------------------

    void ApplyViewportEdit(ViewportView& view, const ViewportEdit& edit, bool undo)
    {
        view.selection = undo ? edit.from : edit.to;
    }

    void CommitViewportEdit(ViewportView& view, const ViewportEdit& edit)
    {
        if (edit.empty())
            return;
        ApplyViewportEdit(view, edit);
        ++view.revision;
        if (view.onEdit)
            view.onEdit(edit);
    }
}
