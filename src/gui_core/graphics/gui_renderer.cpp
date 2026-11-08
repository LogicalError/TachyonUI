#include <gui_core/graphics/gui_renderer.h>

#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_style_ref.h>

#include <algorithm>
#include <cmath>

namespace tachyon
{
    uint32_t GUIRenderer::StyleIndex(VisualBoxStyleIndex boxStyleIndex, tachyon::ElementState state)
    {
        if (boxStyleIndex == tachyon::VisualBoxStyleIndex::Invalid)
            return 0;
        // Match StyleManager::GetVisualBoxStyle: Disabled stands for every state it is in.
        state &= tachyon::ElementState::VisualMask;
        if ((state & tachyon::ElementState::Disabled) != tachyon::ElementState::None)
            state = tachyon::ElementState::Disabled;
        return (((int)boxStyleIndex - 1) * tachyon::Style::kStateCount) + (int)state;
    }

    void GUIRenderer::RenderBox(tachyon::IntRect rect, tachyon::IntRect clipRect, VisualBoxStyleIndex boxStyleIndex,
                                tachyon::ElementState state, tachyon::VisualColorStyleIndex colorStyleIndex, tachyon::ImageIndex textureIndex,
                                tachyon::IntRect snappedPx)
    {
        const uint32_t styleIndexValue = StyleIndex(boxStyleIndex, state);
        RenderBox(rect, clipRect, styleIndexValue, styleIndexValue, 1.0f, colorStyleIndex, textureIndex, snappedPx);
    }

    void GUIRenderer::RenderBox(tachyon::IntRect rect, tachyon::IntRect clipRect, VisualBoxStyleIndex boxStyleIndex,
                                tachyon::VisualColorStyleIndex colorStyleIndex, tachyon::ImageIndex textureIndex,
                                tachyon::IntRect snappedPx)
    {
        uint32_t styleIndexValue = 0;
        if (boxStyleIndex != tachyon::VisualBoxStyleIndex::Invalid)
            styleIndexValue = (((int)boxStyleIndex - 1) * tachyon::Style::kStateCount);

        RenderBox(rect, clipRect, styleIndexValue, styleIndexValue, 1.0f, colorStyleIndex, textureIndex, snappedPx);
    }

    static TriangleEdges FanInnerEdges(size_t i, size_t count)
    {
        // Triangle i of a fan from point 0: its edges back to point 0 are shared, but for
        // the first one's and the last one's, which are the polygon's own.
        TriangleEdges inner = TriangleEdges::None;
        if (i > 1)         inner |= TriangleEdges::Edge01;
        if (i + 2 < count) inner |= TriangleEdges::Edge20;
        return inner;
    }

    void GUIRenderer::RenderFilledPolygon(std::span<const tachyon::float2> points, tachyon::IntRect clipRect,
                                          VisualColorStyleIndex colorIndex, tachyon::ImageIndex textureIndex)
    {
        if (points.size() < 3) return;
        for (size_t i = 1; i + 1 < points.size(); i++)
            RenderTriangle(points[0], points[i], points[i + 1], clipRect, colorIndex, textureIndex, FanInnerEdges(i, points.size()));
    }

    void GUIRenderer::RenderArc(tachyon::float2 center, float radius, float width, float startTurns, float sweepTurns,
                                tachyon::IntRect clipRect, VisualColorStyleIndex colorIndex)
    {
        if (!(width > 0.0f) || !(radius > 0.0f) || sweepTurns == 0.0f)
            return;
        constexpr float kTau = 6.28318530718f;
        const bool  closed = std::fabs(sweepTurns) >= 1.0f;
        const float sweep  = closed ? 1.0f : sweepTurns;
        const float half   = width * 0.5f;
        const float inner  = std::max(0.0f, radius - half);
        const float outer  = radius + half;
        auto at = [&](float turns, float r)
        {
            const float a = turns * kTau;
            return tachyon::float2{ center.x + std::cos(a) * r, center.y + std::sin(a) * r };
        };

        // Short steps, so a chord strays from the circle by far less than a pixel. Each is
        // two triangles; the edges they share with each other, the next step or an end are
        // hard, so only the rims are soft.
        const int steps = std::max(2, int(std::ceil(std::fabs(sweep) * 96.0f)));
        for (int i = 0; i < steps; ++i)
        {
            const float t0 = startTurns + sweep * float(i) / float(steps);
            const float t1 = startTurns + sweep * float(i + 1) / float(steps);
            const tachyon::float2 o0 = at(t0, outer), o1 = at(t1, outer);
            const tachyon::float2 i0 = at(t0, inner), i1 = at(t1, inner);
            RenderTriangle(o0, o1, i1, clipRect, colorIndex, tachyon::ImageIndex::Invalid, TriangleEdges::Edge12 | TriangleEdges::Edge20);
            RenderTriangle(o0, i1, i0, clipRect, colorIndex, tachyon::ImageIndex::Invalid, TriangleEdges::Edge01 | TriangleEdges::Edge20);
        }
        if (closed)
            return;

        // A half disc on each end, bulging the way the arc doesn't go.
        constexpr int kCapSteps = 8;
        const float direction = sweep > 0.0f ? 1.0f : -1.0f;
        auto cap = [&](float turns, float outward)
        {
            const float a = turns * kTau;
            const tachyon::float2 radial { std::cos(a), std::sin(a) };
            const tachyon::float2 along  { -std::sin(a) * direction * outward, std::cos(a) * direction * outward };
            const tachyon::float2 c = at(turns, radius);
            auto point = [&](int j)
            {
                const float phi = 3.14159265359f * float(j) / float(kCapSteps);
                return tachyon::float2{ c.x + half * (std::cos(phi) * radial.x + std::sin(phi) * along.x),
                                        c.y + half * (std::cos(phi) * radial.y + std::sin(phi) * along.y) };
            };
            for (int j = 0; j < kCapSteps; ++j)
                RenderTriangle(c, point(j), point(j + 1), clipRect, colorIndex, tachyon::ImageIndex::Invalid,
                               TriangleEdges::Edge01 | TriangleEdges::Edge20);
        };
        cap(startTurns, -1.0f);
        cap(startTurns + sweep, 1.0f);
    }

    void GUIRenderer::RenderPolyline(std::span<const tachyon::int2> points, bool closed,
                                     tachyon::IntRect clipRect, const LineStyle& style)
    {
        if (points.size() < 2) return;
        for (size_t i = 0; i + 1 < points.size(); i++)
            RenderLine(points[i], points[i + 1], clipRect, style);
        if (closed)
            RenderLine(points.back(), points[0], clipRect, style);
    }

} // namespace tachyon
