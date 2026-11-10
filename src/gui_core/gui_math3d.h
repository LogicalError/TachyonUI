#pragma once

// 3D math for a viewport: vectors, rotations, the camera's matrices and rays.
//
// Matrices act on column vectors: p' = M * p, with r0..r3 the matrix's rows; the translation
// is the last column (r0.w, r1.w, r2.w). A GLSL shader takes them as they are with
// `layout(row_major)`, or transposed. Right-handed, y up; clip space is Vulkan's: x and y
// from -1 to 1 with y down, depth from 0 (near) to 1 (far).

#include <gui_core/gui_base_types.h>

#include <algorithm>
#include <cmath>

namespace tachyon
{
    /// @addtogroup math
    /// @{

    inline float  Dot(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline float3 Cross(float3 a, float3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
    inline float  Length(float3 v) { return std::sqrt(Dot(v, v)); }
    /// `v` at length 1; zero stays zero.
    inline float3 Normalize(float3 v)
    {
        const float length = Length(v);
        return length > 0.0f ? v / length : float3{};
    }
    inline float3 Scale(float3 a, float3 b) { return { a.x * b.x, a.y * b.y, a.z * b.z }; }

    /// A rotation, as a unit quaternion.
    struct quat
    {
        float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;

        bool operator==(const quat&) const = default;

        /// `radians` round `axis` (any length but zero), counterclockwise looking down it.
        static quat AxisAngle(float3 axis, float radians)
        {
            const float3 a = Normalize(axis);
            const float s = std::sin(radians * 0.5f);
            return { a.x * s, a.y * s, a.z * s, std::cos(radians * 0.5f) };
        }

        /// This rotation after `b`: rotating by the product is rotating by `b`, then this.
        quat operator*(const quat& b) const
        {
            return { w * b.x + x * b.w + y * b.z - z * b.y,
                     w * b.y - x * b.z + y * b.w + z * b.x,
                     w * b.z + x * b.y - y * b.x + z * b.w,
                     w * b.w - x * b.x - y * b.y - z * b.z };
        }

        /// `v` rotated.
        float3 Rotate(float3 v) const
        {
            const float3 u{ x, y, z };
            const float3 t = Cross(u, v) * 2.0f;
            return v + t * w + Cross(u, t);
        }

        /// At length 1; a zero quaternion becomes no rotation.
        quat Normalized() const
        {
            const float length = std::sqrt(x * x + y * y + z * z + w * w);
            if (!(length > 0.0f)) return {};
            return { x / length, y / length, z / length, w / length };
        }
    };

    inline float4 Mul(const float4x4& m, float4 v)
    {
        return { m.r0.x * v.x + m.r0.y * v.y + m.r0.z * v.z + m.r0.w * v.w,
                 m.r1.x * v.x + m.r1.y * v.y + m.r1.z * v.z + m.r1.w * v.w,
                 m.r2.x * v.x + m.r2.y * v.y + m.r2.z * v.z + m.r2.w * v.w,
                 m.r3.x * v.x + m.r3.y * v.y + m.r3.z * v.z + m.r3.w * v.w };
    }
    /// A point through `m`, divided by w.
    inline float3 TransformPoint(const float4x4& m, float3 p)
    {
        const float4 r = Mul(m, { p.x, p.y, p.z, 1.0f });
        const float w = r.w != 0.0f ? r.w : 1.0f;
        return { r.x / w, r.y / w, r.z / w };
    }

    inline float4x4 Translation(float3 t)
    {
        return { { 1, 0, 0, t.x }, { 0, 1, 0, t.y }, { 0, 0, 1, t.z }, { 0, 0, 0, 1 } };
    }

    /// Scale, then rotate, then move: an object's transform in the world.
    inline float4x4 TRS(float3 position, quat rotation, float3 scale)
    {
        const float3 ax = rotation.Rotate({ scale.x, 0, 0 });
        const float3 ay = rotation.Rotate({ 0, scale.y, 0 });
        const float3 az = rotation.Rotate({ 0, 0, scale.z });
        return { { ax.x, ay.x, az.x, position.x }, { ax.y, ay.y, az.y, position.y },
                 { ax.z, ay.z, az.z, position.z }, { 0, 0, 0, 1 } };
    }

    /// The view matrix of a camera at `eye` looking at `target`, `up` roughly up.
    inline float4x4 LookAt(float3 eye, float3 target, float3 up)
    {
        const float3 f = Normalize(target - eye);
        float3 s = Normalize(Cross(f, up));
        if (Length(s) == 0.0f)   // looking straight up or down: any side will do
            s = Normalize(Cross(f, std::fabs(f.z) < 0.9f ? float3{ 0, 0, 1 } : float3{ 1, 0, 0 }));
        const float3 u = Cross(s, f);
        return { { s.x, s.y, s.z, -Dot(s, eye) }, { u.x, u.y, u.z, -Dot(u, eye) },
                 { -f.x, -f.y, -f.z, Dot(f, eye) }, { 0, 0, 0, 1 } };
    }

    /// A perspective projection to Vulkan's clip space: `fovY` in radians, `aspect` width over height.
    inline float4x4 Perspective(float fovY, float aspect, float nearZ, float farZ)
    {
        const float f = 1.0f / std::tan(fovY * 0.5f);
        const float range = farZ / (nearZ - farZ);
        return { { f / aspect, 0, 0, 0 }, { 0, -f, 0, 0 }, { 0, 0, range, nearZ * range }, { 0, 0, -1, 0 } };
    }

    /// An orthographic projection to Vulkan's clip space, `height` world units high.
    inline float4x4 Orthographic(float height, float aspect, float nearZ, float farZ)
    {
        const float h = 2.0f / height, w = h / aspect;
        const float range = 1.0f / (nearZ - farZ);
        return { { w, 0, 0, 0 }, { 0, -h, 0, 0 }, { 0, 0, range, nearZ * range }, { 0, 0, 0, 1 } };
    }

    /// `m`'s inverse; the identity when it has none.
    float4x4 Inverse(const float4x4& m);

    /// A ray: from `origin`, along `direction` (length 1).
    struct Ray
    {
        float3 origin;
        float3 direction;

        float3 At(float t) const { return origin + direction * t; }
    };

    /// How far along `ray` it meets the plane through `point` with `normal`; negative or NaN
    /// when it doesn't (parallel, or behind).
    inline float IntersectPlane(const Ray& ray, float3 point, float3 normal)
    {
        const float denom = Dot(ray.direction, normal);
        if (std::fabs(denom) < 1e-6f)
            return std::nanf("");
        return Dot(point - ray.origin, normal) / denom;
    }

    /// Where along the line through `point` with `direction` (length 1) it comes closest to
    /// `ray`; NaN when they're parallel.
    inline float ClosestOnLine(const Ray& ray, float3 point, float3 direction)
    {
        const float3 w = point - ray.origin;
        const float b = Dot(direction, ray.direction);
        const float denom = 1.0f - b * b;
        if (std::fabs(denom) < 1e-6f)
            return std::nanf("");
        return (b * Dot(ray.direction, w) - Dot(direction, w)) / denom;
    }

    /// @}
}
