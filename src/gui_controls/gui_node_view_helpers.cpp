#include <gui_controls/gui_node_view_helpers.h>

#include <gui_core/graphics/gui_custom_draw_context.h>
#include <gui_core/graphics/gui_renderer.h>   // LineStyle

#include <algorithm>
#include <cmath>
#include <functional>

namespace tachyon
{
    namespace
    {
        // The view's zoom range, put right if the app set it wrong (none, or upside down).
        float MinZoom(const NodeView& view) { return view.minZoom > 0.0f ? view.minZoom : 0.1f; }
        float MaxZoom(const NodeView& view) { return std::max(view.maxZoom, MinZoom(view)); }
    }

    float NodeViewZoom(const NodeView& view)
    {
        const float3x2& t = view.transform;
        float zoom = std::sqrt((t[0].x * t[0].x + t[0].y * t[0].y + t[1].x * t[1].x + t[1].y * t[1].y) * 0.5f);
        if (t[0].x * t[0].y - t[1].x * t[1].y < 0.0f) zoom = -zoom;
        return zoom;
    }

    void ZoomNodeView(NodeView& view, float factor, int2 pivot)
    {
        const float current = NodeViewZoom(view);
        const float target  = current * factor;
        if (current == 0.0f)
            return;
        if (target < MinZoom(view) || target > MaxZoom(view))
            view.transform = view.transform * float3x2::Scale(std::clamp(target, MinZoom(view), MaxZoom(view)) / current);
        else
            view.transform = float3x2::Translate(pivot) * float3x2::Scale(factor) * float3x2::Translate(-pivot) * view.transform;
    }

    void SetNodeViewCenter(NodeView& view, float2 center, float zoom)
    {
        view.transform = float3x2::Scale(std::clamp(zoom, MinZoom(view), MaxZoom(view)))
                       * float3x2::Translate(float2{ -center.x, -center.y });
    }

    void FrameAllNodes(NodeView& view)
    {
        std::vector<int> all(view.nodes.size());
        for (int n = 0; n < int(all.size()); ++n)
            all[size_t(n)] = n;
        FrameNodes(view, all);
    }

    void FrameSelectedNodes(NodeView& view)
    {
        std::vector<int> selected;
        for (int n = 0; n < int(view.nodes.size()); ++n)
            if (view.selectedNodeIDs.contains(view.nodes[size_t(n)].stableID))
                selected.push_back(n);
        if (selected.empty())
            FrameAllNodes(view);
        else
            FrameNodes(view, selected);
    }

    void FrameNodes(NodeView& view, std::span<const int> indices)
    {
        bool any = false;
        int2 bmin{}, bmax{};
        for (const int n : indices)
        {
            if (n < 0 || n >= int(view.nodes.size()))
                continue;
            const NodeModel& node = view.nodes[size_t(n)];
            const int2 lo = node.pos, hi{ node.pos.x + node.lastSize.x, node.pos.y + node.lastSize.y };
            bmin = any ? int2{ std::min(bmin.x, lo.x), std::min(bmin.y, lo.y) } : lo;
            bmax = any ? int2{ std::max(bmax.x, hi.x), std::max(bmax.y, hi.y) } : hi;
            any = true;
        }
        if (!any)
            return;
        const int bw = std::max(1, bmax.x - bmin.x);
        const int bh = std::max(1, bmax.y - bmin.y);
        const int2 vp = view.lastInnerSize;
        // Fitting never magnifies past 100%: a small graph shows at its own size.
        const float zoom = (vp.x > 0 && vp.y > 0)
                         ? std::min({ (float)vp.x / (float)bw * 0.9f, (float)vp.y / (float)bh * 0.9f, 1.0f })
                         : 1.0f;
        SetNodeViewCenter(view, float2{ (float)((bmin.x + bmax.x) / 2), (float)((bmin.y + bmax.y) / 2) }, zoom);
    }

    void ScrollNodeIntoView(NodeView& view, int nodeIdx, int32_t margin)
    {
        if (nodeIdx < 0 || nodeIdx >= (int)view.nodes.size())
            return;
        const int2 vp = view.lastInnerSize;
        if (vp.x <= 0 || vp.y <= 0)
            return;
        const NodeModel& node = view.nodes[size_t(nodeIdx)];
        const float2 a = view.transform.TransformPoint(float2{ (float)node.pos.x, (float)node.pos.y });
        const float2 b = view.transform.TransformPoint(float2{ (float)(node.pos.x + node.lastSize.x),
                                                               (float)(node.pos.y + node.lastSize.y) });
        // The smallest move that brings each side in, the top-left winning for a node larger
        // than the view. View space has the canvas centre at 0.
        auto shift = [margin](float lo, float hi, int32_t size)
        {
            const float half = 0.5f * (float)size;
            const float m    = (float)margin;
            float d = 0.0f;
            if (hi + d > half - m)  d = (half - m) - hi;
            if (lo + d < -half + m) d = (-half + m) - lo;
            return d;
        };
        const float2 d{ shift(std::min(a.x, b.x), std::max(a.x, b.x), vp.x),
                        shift(std::min(a.y, b.y), std::max(a.y, b.y), vp.y) };
        if (d.x != 0.0f || d.y != 0.0f)
            view.transform = float3x2::Translate(d) * view.transform;
    }

    void RenderCanvasGrid(CustomDrawContext& ctx, int32_t baseGridSize,
                          color4f dotColor, float dotSize, int2 originOffset, IntRect clipCurrent)
    {
        if (baseGridSize <= 0) return;

        // Visible region in the current space: bounding box of all four clip corners
        // mapped back (all four, so axis flips work).
        const IntRect clip = ctx.ClipRect();
        if (clip.w <= 0 || clip.h <= 0) return;
        const float3x2 wlToCurrent = ctx.CurrentTransform().Inverse();
        auto toCurrent = [&](int2 wl) {
            const float2 p = wlToCurrent.TransformPoint({ (float)wl.x, (float)wl.y });
            return int2{ (int)p.x, (int)p.y };
        };
        const int2 c0 = toCurrent({ clip.x,          clip.y          });
        const int2 c1 = toCurrent({ clip.x + clip.w, clip.y          });
        const int2 c2 = toCurrent({ clip.x,          clip.y + clip.h });
        const int2 c3 = toCurrent({ clip.x + clip.w, clip.y + clip.h });
        int minX = std::min({ c0.x, c1.x, c2.x, c3.x });
        int maxX = std::max({ c0.x, c1.x, c2.x, c3.x });
        int minY = std::min({ c0.y, c1.y, c2.y, c3.y });
        int maxY = std::max({ c0.y, c1.y, c2.y, c3.y });

        // Clamp to the content box (the screen scissor is the wider border box, so dots
        // would otherwise bleed into the padding).
        if (clipCurrent.w > 0 && clipCurrent.h > 0)
        {
            minX = std::max(minX, clipCurrent.x);
            minY = std::max(minY, clipCurrent.y);
            maxX = std::min(maxX, clipCurrent.x + clipCurrent.w);
            maxY = std::min(maxY, clipCurrent.y + clipCurrent.h);
            if (minX > maxX || minY > maxY) return;
        }

        // One base cell in window-local pixels, measured over a wide span for precision at low zoom.
        constexpr int kSpan = 1024;
        const float3x2 currentToWL = ctx.CurrentTransform();
        const float2 s0 = currentToWL.TransformPoint({ 0.0f, 0.0f });
        const float2 sx = currentToWL.TransformPoint({ (float)(baseGridSize * kSpan), 0.0f });
        const float  screenPitch = std::hypot(sx.x - s0.x, sx.y - s0.y) / (float)kSpan;
        if (screenPitch <= 0.01f) return;

        // LOD: double the pitch each time the view is zoomed out by 2x.
        // ratio == baseGridSize / screenPitch == 1 / zoomScale.
        int   level = 0;
        float ratio = (float)baseGridSize / screenPitch;
        while (ratio >= 2.0f && level < 24) { ratio *= 0.5f; ++level; }
        const int step = baseGridSize << level;

        // First line at or below minV on the lattice originOffset + k*step.
        auto firstLine = [step](int minV, int origin) {
            int rel = minV - origin;
            int q   = rel / step;
            if ((rel % step) != 0 && rel < 0) --q;   // floor for negatives
            return origin + q * step;
        };

        // Emit in window-local space, snapped to whole pixels (the dot shader adds the half
        // pixel): transformed canvas coords land on sub-pixels, and a 1px dot would smear across two.
        ctx.SetSpaceRaw(float3x2::Identity());
        const int firstX = firstLine(minX, originOffset.x);
        for (int y = firstLine(minY, originOffset.y); y <= maxY; y += step)
            for (int x = firstX; x <= maxX; x += step)
            {
                const float2 p = currentToWL.TransformPoint({ (float)x, (float)y });
                ctx.RenderDot({ std::floor(p.x), std::floor(p.y) }, dotSize, dotColor);
            }
        ctx.SetSpace(DrawSpace::Local);
    }

    std::unordered_map<uint32_t, WireLane> ComputeWireLaneOffsets(const NodeView& view,
                                                                  PropertyEndpoint pendingOut,
                                                                  PropertyEndpoint pendingIn)
    {
        auto pairKey = [](int srcNode, int tgtNode) { return (uint64_t(uint32_t(srcNode)) << 32) | uint32_t(tgtNode); };
        const bool pending = pendingOut.valid() && pendingIn.valid();

        // Wires per (source node, target node). Connecting replaces the input's wire.
        std::unordered_map<uint64_t, std::vector<uint32_t>> groups;
        for (int n = 0; n < (int)view.nodes.size(); ++n)
            for (int p = 0; p < (int)view.nodes[n].properties.size(); ++p)
            {
                const auto& input = view.nodes[n].properties[p].input;
                if (!input.has_value() || !input->valid())
                    continue;
                if (pending && PropertyEndpoint{ n, p } == pendingIn)
                    continue;
                groups[pairKey(input->nodeIdx, n)].push_back(WireKey(n, p));
            }
        if (pending)
            groups[pairKey(pendingOut.nodeIdx, pendingIn.nodeIdx)].push_back(WireKey(pendingIn.nodeIdx, pendingIn.propIdx));

        std::unordered_map<uint32_t, WireLane> offsets;
        for (auto& [pair, wires] : groups)
        {
            std::sort(wires.begin(), wires.end());   // input order: same target node, by property
            const int srcNode = int(pair >> 32), tgtNode = int(uint32_t(pair));
            const bool up = view.nodes[size_t(tgtNode)].pos.y < view.nodes[size_t(srcNode)].pos.y;
            const int32_t first = (int32_t(wires.size()) - 1) / 2;
            for (size_t i = 0; i < wires.size(); ++i)
                offsets[wires[i]] = WireLane{ .offset = up ? int32_t(i) - first : first - int32_t(i),
                                              .index  = int32_t(i), .count = int32_t(wires.size()) };
        }
        return offsets;
    }

    WireRoute BuildCurvedWireRoute(int2 outPt, int2 inPt, int32_t reach)
    {
        const int32_t out = std::max(std::abs(inPt.x - outPt.x) / 2, reach);
        WireRoute route;
        route.points = { outPt, { outPt.x + out, outPt.y }, { inPt.x - out, inPt.y }, inPt };
        route.curved = true;
        return route;
    }

    float2 WireCurvePoint(int2 p0, int2 p1, int2 p2, int2 p3, float t)
    {
        const float2 m{ 0.5f * float(p1.x + p2.x), 0.5f * float(p1.y + p2.y) };
        auto quadratic = [](float2 a, float2 b, float2 c, float u) {
            const float s = 1.0f - u;
            return float2{ s * s * a.x + 2.0f * s * u * b.x + u * u * c.x,
                           s * s * a.y + 2.0f * s * u * b.y + u * u * c.y };
        };
        if (t < 0.5f)
            return quadratic({ float(p0.x), float(p0.y) }, { float(p1.x), float(p1.y) }, m, 2.0f * t);
        return quadratic(m, { float(p2.x), float(p2.y) }, { float(p3.x), float(p3.y) }, 2.0f * t - 1.0f);
    }

    std::optional<uint32_t> WireAt(std::span<const DrawnWire> wires, int2 p, float tolerance)
    {
        std::optional<uint32_t> best;
        float bestDistance = tolerance;
        const float2 q{ float(p.x), float(p.y) };
        auto test = [&](float2 a, float2 b, uint32_t wireKey)
        {
            // Distance from q to the segment.
            const float dx = b.x - a.x, dy = b.y - a.y;
            const float lengthSq = dx * dx + dy * dy;
            float t = lengthSq > 0.0f ? ((q.x - a.x) * dx + (q.y - a.y) * dy) / lengthSq : 0.0f;
            t = std::clamp(t, 0.0f, 1.0f);
            const float ex = q.x - (a.x + t * dx), ey = q.y - (a.y + t * dy);
            const float distance = std::sqrt(ex * ex + ey * ey);
            if (distance <= bestDistance)
            {
                bestDistance = distance;
                best = wireKey;
            }
        };
        for (const DrawnWire& wire : wires)
        {
            if (wire.curved && wire.points.size() == 4)
            {
                // Short straight steps along the curve, close enough at any length a wire has.
                constexpr int kSteps = 32;
                const auto& c = wire.points;
                float2 a = WireCurvePoint(c[0], c[1], c[2], c[3], 0.0f);
                for (int i = 1; i <= kSteps; ++i)
                {
                    const float2 b = WireCurvePoint(c[0], c[1], c[2], c[3], float(i) / float(kSteps));
                    test(a, b, wire.wireKey);
                    a = b;
                }
                continue;
            }
            for (size_t i = 0; i + 1 < wire.points.size(); ++i)
                test({ float(wire.points[i].x), float(wire.points[i].y) },
                     { float(wire.points[i + 1].x), float(wire.points[i + 1].y) }, wire.wireKey);
        }
        return best;
    }

    namespace
    {
        NodeProperty* PropertyAt(NodeView& view, PropertyEndpoint e)
        {
            if (e.nodeIdx < 0 || e.nodeIdx >= (int)view.nodes.size()) return nullptr;
            auto& props = view.nodes[e.nodeIdx].properties;
            return (e.propIdx >= 0 && e.propIdx < (int)props.size()) ? &props[e.propIdx] : nullptr;
        }

        // Node indices from `at` on move by `by` (+1: a node goes in at `at`; -1: the one at
        // `at` is gone), in every wire endpoint, WireKey and z-order entry.
        void ShiftNodeIndices(NodeView& view, int at, int by)
        {
            auto shifted = [at, by](int n) { return n >= at ? n + by : n; };
            for (NodeModel& node : view.nodes)
                for (NodeProperty& prop : node.properties)
                    if (prop.input && prop.input->valid())
                        prop.input->nodeIdx = shifted(prop.input->nodeIdx);
            std::unordered_map<uint32_t, WireRouteOverrides> overrides;
            for (const auto& [key, lanes] : view.wireOverrides)
            {
                const PropertyEndpoint input = WireInput(key);
                overrides[WireKey(shifted(input.nodeIdx), input.propIdx)] = lanes;
            }
            view.wireOverrides = std::move(overrides);
            for (int& n : view.ordering)
                n = shifted(n);
            // What hovered or was grabbed last frame named wires by the old indices.
            view.hoveredWire.reset();
            view.wireMenuWire.reset();
            view.wireHandleCache.clear();
        }

        void InsertNode(NodeView& view, const NodeViewEdit::Node& change)
        {
            const int at = std::clamp(change.index, 0, (int)view.nodes.size());
            ShiftNodeIndices(view, at, +1);
            view.nodes.insert(view.nodes.begin() + at, change.node);
            view.ordering.push_back(at);   // in front
            for (const auto& [input, outputProp] : change.fed)
                if (NodeProperty* prop = PropertyAt(view, input); prop && prop->input)
                    prop->input = PropertyEndpoint{ at, outputProp };
            for (const auto& [key, lanes] : change.lanes)
                view.wireOverrides[key] = lanes;
        }

        void EraseNode(NodeView& view, const NodeViewEdit::Node& change)
        {
            const int at = change.index;
            if (at < 0 || at >= (int)view.nodes.size())
                return;
            // Its wires go with it: the ones into other nodes, and every one's lanes.
            for (NodeModel& node : view.nodes)
                for (NodeProperty& prop : node.properties)
                    if (prop.input && prop.input->valid() && prop.input->nodeIdx == at)
                        prop.input = PropertyEndpoint{};
            std::erase_if(view.wireOverrides, [&](const auto& entry) {
                const PropertyEndpoint input = WireInput(entry.first);
                return input.nodeIdx == at
                    || std::any_of(change.fed.begin(), change.fed.end(), [&](const auto& f) { return f.first == input; });
            });
            const ID id = view.nodes[size_t(at)].stableID;
            view.selectedNodeIDs.erase(id);
            if (view.lastClickedNodeID == id)
                view.lastClickedNodeID = ID::Invalid();
            view.nodes.erase(view.nodes.begin() + at);
            std::erase(view.ordering, at);
            ShiftNodeIndices(view, at + 1, -1);
        }

        // The removal of the node at `at` as an edit: the node, the wires it feeds, and the
        // lanes of every wire into or out of it.
        NodeViewEdit::Node RemovalOf(const NodeView& view, int at)
        {
            NodeViewEdit::Node change{ .index = at, .added = false, .node = view.nodes[size_t(at)] };
            for (int n = 0; n < (int)view.nodes.size(); ++n)
                for (int p = 0; p < (int)view.nodes[size_t(n)].properties.size(); ++p)
                {
                    const auto& input = view.nodes[size_t(n)].properties[size_t(p)].input;
                    if (n != at && input && input->valid() && input->nodeIdx == at)
                        change.fed.push_back({ PropertyEndpoint{ n, p }, input->propIdx });
                }
            for (const auto& [key, lanes] : view.wireOverrides)
            {
                const PropertyEndpoint input = WireInput(key);
                if (input.nodeIdx == at
                    || std::any_of(change.fed.begin(), change.fed.end(), [&](const auto& f) { return f.first == input; }))
                    change.lanes.push_back({ key, lanes });
            }
            return change;
        }
    }

    NodeViewEdit AddNodeEdit(const NodeView& view, NodeModel node, int index)
    {
        NodeViewEdit edit;
        const int count = (int)view.nodes.size();
        edit.nodes.push_back({ .index = (index < 0 || index > count) ? count : index, .added = true, .node = std::move(node) });
        return edit;
    }

    NodeViewEdit RemoveNodesEdit(const NodeView& view, std::span<const int> indices)
    {
        // Last first, so each one's index is still its own when it goes; on a copy of the
        // graph, so each is recorded as it is then (its wires to the ones before it gone).
        std::vector<int> order;
        for (const int n : indices)
            if (n >= 0 && n < (int)view.nodes.size()) order.push_back(n);
        std::sort(order.begin(), order.end(), std::greater<int>());
        order.erase(std::unique(order.begin(), order.end()), order.end());

        NodeViewEdit edit;
        NodeView graph;
        graph.nodes         = view.nodes;
        graph.wireOverrides = view.wireOverrides;
        for (const int n : order)
        {
            edit.nodes.push_back(RemovalOf(graph, n));
            EraseNode(graph, edit.nodes.back());
        }
        return edit;
    }

    NodeViewEdit RerouteWireEdit(const NodeView& view, uint32_t wireKey, ID stableID, int2 pos)
    {
        const PropertyEndpoint input = WireInput(wireKey);
        if (input.nodeIdx >= (int)view.nodes.size() || input.propIdx >= (int)view.nodes[size_t(input.nodeIdx)].properties.size())
            return {};
        const auto& wired = view.nodes[size_t(input.nodeIdx)].properties[size_t(input.propIdx)].input;
        if (!wired || !wired->valid() || wired->nodeIdx >= (int)view.nodes.size()
            || wired->propIdx >= (int)view.nodes[size_t(wired->nodeIdx)].properties.size())
            return {};
        const PropertyEndpoint source = *wired;

        NodeModel reroute;
        reroute.stableID = stableID;
        reroute.title    = "Reroute";
        reroute.pos      = pos;
        reroute.reroute  = true;
        reroute.properties.push_back(NodeProperty{
            .input       = source,
            .output      = true,
            .socketColor = view.nodes[size_t(source.nodeIdx)].properties[size_t(source.propIdx)].socketColor });
        // Added first, so the rewire may name it.
        NodeViewEdit edit = AddNodeEdit(view, std::move(reroute));
        edit.rewires.push_back({ input, source, PropertyEndpoint{ edit.nodes[0].index, 0 } });
        if (const auto it = view.wireOverrides.find(wireKey); it != view.wireOverrides.end())
            edit.lanes.push_back({ wireKey, it->second, WireRouteOverrides{} });
        return edit;
    }

    NodeViewEdit RemoveRerouteEdit(const NodeView& view, int node)
    {
        if (node < 0 || node >= (int)view.nodes.size() || !view.nodes[size_t(node)].reroute)
            return {};
        const int only[] = { node };
        NodeViewEdit edit = RemoveNodesEdit(view, only);
        // The rewires apply after the removal, so they name nodes as numbered without it; the
        // inputs it fed are unwired by then.
        const auto& row = view.nodes[size_t(node)].properties;
        const PropertyEndpoint source = !row.empty() && row[0].input ? *row[0].input : PropertyEndpoint{};
        if (!source.valid())
            return edit;
        auto without = [node](PropertyEndpoint e) { if (e.nodeIdx > node) --e.nodeIdx; return e; };
        for (const auto& [input, outputProp] : edit.nodes[0].fed)
            edit.rewires.push_back({ without(input), PropertyEndpoint{}, without(source) });
        return edit;
    }

    NodeViewEdit AddFrameEdit(const NodeView& view, NodeFrame frame, int index)
    {
        NodeViewEdit edit;
        const int count = (int)view.frames.size();
        edit.frames.push_back({ .index = (index < 0 || index > count) ? count : index, .added = true, .frame = std::move(frame) });
        return edit;
    }

    NodeViewEdit RemoveFramesEdit(const NodeView& view, std::span<const int> indices)
    {
        // Last first, so each one's index is still its own when it goes.
        std::vector<int> order;
        for (const int f : indices)
            if (f >= 0 && f < (int)view.frames.size()) order.push_back(f);
        std::sort(order.begin(), order.end(), std::greater<int>());
        order.erase(std::unique(order.begin(), order.end()), order.end());
        NodeViewEdit edit;
        for (const int f : order)
            edit.frames.push_back({ .index = f, .added = false, .frame = view.frames[size_t(f)] });
        return edit;
    }

    NodeFrame FrameAroundNodes(const NodeView& view, std::span<const int> indices)
    {
        const int32_t grid = view.gridSize > 0 ? view.gridSize : 12;
        bool any = false;
        int2 lo{}, hi{};
        for (const int n : indices)
        {
            if (n < 0 || n >= (int)view.nodes.size()) continue;
            const NodeModel& node = view.nodes[size_t(n)];
            const int2 end{ node.pos.x + node.lastSize.x, node.pos.y + node.lastSize.y };
            lo  = any ? int2{ std::min(lo.x, node.pos.x), std::min(lo.y, node.pos.y) } : node.pos;
            hi  = any ? int2{ std::max(hi.x, end.x), std::max(hi.y, end.y) } : end;
            any = true;
        }
        if (!any)
            return {};
        // A cell of room round them, the title bar (a row pitch) above that.
        int2 from{ lo.x - grid, lo.y - grid - 2 * grid }, to{ hi.x + grid, hi.y + grid };
        if (view.snapToGrid)
        {
            from = { GridLineLE(from.x, 0, grid), GridLineLE(from.y, 0, grid) };
            to   = { GridLineGE(to.x, 0, grid), GridLineGE(to.y, 0, grid) };
        }
        return NodeFrame{ .pos = from, .size = { to.x - from.x, to.y - from.y } };
    }

    std::vector<int> NodesInFrame(const NodeView& view, int frame)
    {
        std::vector<int> inside;
        if (frame < 0 || frame >= (int)view.frames.size())
            return inside;
        const NodeFrame& f = view.frames[size_t(frame)];
        if (f.collapsed && f.members)
        {
            for (int n = 0; n < (int)view.nodes.size(); ++n)
                if (std::find(f.members->begin(), f.members->end(), view.nodes[size_t(n)].stableID) != f.members->end())
                    inside.push_back(n);
            return inside;
        }
        for (int n = 0; n < (int)view.nodes.size(); ++n)
        {
            const NodeModel& node = view.nodes[size_t(n)];
            if (node.pos.x >= f.pos.x && node.pos.y >= f.pos.y
                && node.pos.x + node.lastSize.x <= f.pos.x + f.size.x
                && node.pos.y + node.lastSize.y <= f.pos.y + f.size.y)
                inside.push_back(n);
        }
        return inside;
    }

    NodeViewEdit CollapseFrameEdit(const NodeView& view, int frame, bool collapse)
    {
        if (frame < 0 || frame >= (int)view.frames.size() || view.frames[size_t(frame)].collapsed == collapse)
            return {};
        const NodeFrame& from = view.frames[size_t(frame)];
        NodeFrame to = from;
        to.collapsed = collapse;
        to.members.reset();
        if (collapse)
        {
            // What it holds now: the nodes inside it, and the frames wholly inside it.
            std::vector<ID> members;
            for (const int n : NodesInFrame(view, frame))
                members.push_back(view.nodes[size_t(n)].stableID);
            for (int other = 0; other < (int)view.frames.size(); ++other)
            {
                const NodeFrame& inner = view.frames[size_t(other)];
                if (other != frame && inner.pos.x >= from.pos.x && inner.pos.y >= from.pos.y
                    && inner.pos.x + inner.size.x <= from.pos.x + from.size.x
                    && inner.pos.y + inner.size.y <= from.pos.y + from.size.y)
                    members.push_back(inner.stableID);
            }
            to.members = std::move(members);
        }
        return NodeViewEdit{ .frameChanges = { { frame, from, std::move(to) } } };
    }

    int FrameTitleAtCanvasPos(const NodeView& view, float2 canvasPos)
    {
        const int32_t bar = 2 * (view.gridSize > 0 ? view.gridSize : 12);
        for (int f = (int)view.frames.size() - 1; f >= 0; --f)
        {
            // As last drawn, when it was: a collapsed one by its box, a hidden one not at all.
            const NodeFrame& frame = view.frames[size_t(f)];
            const IntRect box = f < (int)view.frameBoxes.size() ? view.frameBoxes[size_t(f)]
                                                                : IntRect{ frame.pos.x, frame.pos.y, frame.size.x, frame.size.y };
            if (canvasPos.x >= float(box.x) && canvasPos.x < float(box.x + box.w)
                && canvasPos.y >= float(box.y) && canvasPos.y < float(box.y + std::min(bar, box.h)))
                return f;
        }
        return -1;
    }

    void RenameFrame(NodeView& view, int frame)
    {
        if (frame < 0 || frame >= (int)view.frames.size())
            return;
        view.renamingFrame = view.frames[size_t(frame)].stableID;
        view.renameText    = view.frames[size_t(frame)].title;
        view.renameFocused = false;
    }

    void ApplyNodeViewEdit(NodeView& view, const NodeViewEdit& edit, bool undo)
    {
        // Undo walks the changes backwards, so one that builds on another is undone first.
        auto each = [undo](const auto& changes, auto&& apply)
        {
            if (undo) for (auto it = changes.rbegin(); it != changes.rend(); ++it) apply(*it, it->from);
            else      for (const auto& change : changes)                         apply(change, change.to);
        };
        // Nodes first: the other changes may name a node added here.
        auto nodes = [&](bool backwards)
        {
            auto apply = [&](const NodeViewEdit::Node& change) {
                if (change.added != backwards) InsertNode(view, change);
                else                           EraseNode(view, change);
            };
            if (backwards) for (auto it = edit.nodes.rbegin(); it != edit.nodes.rend(); ++it) apply(*it);
            else           for (const auto& change : edit.nodes)                           apply(change);
        };
        // Frames as nodes: added or taken out first, their changes after.
        auto frames = [&](bool backwards)
        {
            auto apply = [&](const NodeViewEdit::Frame& change) {
                const int count = (int)view.frames.size();
                if (change.added != backwards)
                    view.frames.insert(view.frames.begin() + std::clamp(change.index, 0, count), change.frame);
                else if (change.index >= 0 && change.index < count)
                {
                    view.selectedFrameIDs.erase(view.frames[size_t(change.index)].stableID);
                    view.frames.erase(view.frames.begin() + change.index);
                }
            };
            if (backwards) for (auto it = edit.frames.rbegin(); it != edit.frames.rend(); ++it) apply(*it);
            else           for (const auto& change : edit.frames)                            apply(change);
        };
        if (!undo)
        {
            nodes(false);
            frames(false);
        }
        each(edit.frameChanges, [&](const NodeViewEdit::FrameChange& c, const NodeFrame& frame)
        {
            if (c.frame >= 0 && c.frame < (int)view.frames.size()) view.frames[size_t(c.frame)] = frame;
        });
        const int nodeCount = (int)view.nodes.size();
        each(edit.moves, [&](const NodeViewEdit::Move& m, int2 pos)
        {
            if (m.node >= 0 && m.node < nodeCount) view.nodes[m.node].pos = pos;
        });
        each(edit.collapses, [&](const NodeViewEdit::Collapse& c, bool collapsed)
        {
            if (c.node >= 0 && c.node < nodeCount) view.nodes[c.node].collapsed = collapsed;
        });
        each(edit.rewires, [&](const NodeViewEdit::Rewire& r, PropertyEndpoint source)
        {
            if (NodeProperty* prop = PropertyAt(view, r.input); prop && prop->input.has_value())
                prop->input = source;
        });
        each(edit.lanes, [&](const NodeViewEdit::Lanes& l, const WireRouteOverrides& lanes)
        {
            if (lanes == WireRouteOverrides{}) view.wireOverrides.erase(l.wireKey);
            else                               view.wireOverrides[l.wireKey] = lanes;
        });
        if (undo)
        {
            frames(true);
            nodes(true);
        }
    }

    void CommitNodeViewEdit(NodeView& view, const NodeViewEdit& edit)
    {
        if (edit.empty()) return;
        ApplyNodeViewEdit(view, edit);
        if (view.onEdit) view.onEdit(edit);
    }

    NodeViewEdit ConnectEdit(const NodeView& view, PropertyEndpoint output, PropertyEndpoint input)
    {
        NodeViewEdit edit;
        if (input.nodeIdx < 0 || input.nodeIdx >= (int)view.nodes.size()) return edit;
        const auto& props = view.nodes[input.nodeIdx].properties;
        if (input.propIdx < 0 || input.propIdx >= (int)props.size() || !props[input.propIdx].input.has_value()) return edit;
        const PropertyEndpoint before = *props[input.propIdx].input;
        if (!(before == output))
            edit.rewires.push_back({ input, before, output });
        return edit;
    }

    NodeViewEdit DisconnectEdit(const NodeView& view, PropertyEndpoint input)
    {
        NodeViewEdit edit = ConnectEdit(view, PropertyEndpoint{}, input);
        if (const auto it = view.wireOverrides.find(WireKey(input.nodeIdx, input.propIdx)); it != view.wireOverrides.end())
            edit.lanes.push_back({ it->first, it->second, WireRouteOverrides{} });
        return edit;
    }

    bool ConnectionAllowed(const NodeView& view, PropertyEndpoint out, PropertyEndpoint in)
    {
        if (out.nodeIdx == in.nodeIdx) return false;
        const int nodeCount = (int)view.nodes.size();
        if (out.nodeIdx < 0 || out.nodeIdx >= nodeCount) return false;
        if (in.nodeIdx  < 0 || in.nodeIdx  >= nodeCount) return false;
        if (out.propIdx < 0 || out.propIdx >= (int)view.nodes[out.nodeIdx].properties.size()) return false;
        if (in.propIdx  < 0 || in.propIdx  >= (int)view.nodes[in.nodeIdx].properties.size())  return false;
        return view.canConnect ? view.canConnect(view, out, in) : true;
    }

    PropertyEndpoint FirstInputAccepting(const NodeView& view, PropertyEndpoint output, int nodeIdx)
    {
        if (nodeIdx < 0 || nodeIdx >= (int)view.nodes.size())
            return {};
        const auto& properties = view.nodes[nodeIdx].properties;
        for (int p = 0; p < (int)properties.size(); ++p)
        {
            const PropertyEndpoint in{ nodeIdx, p };
            if (properties[p].input.has_value() && ConnectionAllowed(view, output, in))
                return in;
        }
        return {};
    }

    WireRoute BuildPendingWireRoute(int2 outPt, IntRect srcRect, int2 cursor,
                                    std::optional<int2> inPt, IntRect tgtRect,
                                    int32_t grid, int2 gridOrigin, WireLane lane)
    {
        if (inPt)
        {
            WireRouteOverrides overrides;
            overrides.lane = lane;
            return BuildWireRoute(outPt, *inPt, srcRect, tgtRect, grid, gridOrigin, overrides);
        }
        return BuildWireRoute(outPt, cursor, srcRect, IntRect{ cursor.x, cursor.y, 1, 1 }, grid, gridOrigin);
    }

    WireRoute BuildPendingWireRouteToInput(int2 inPt, IntRect tgtRect, int2 cursor,
                                           std::optional<int2> outPt, IntRect srcRect,
                                           int32_t grid, int2 gridOrigin, WireLane lane)
    {
        if (outPt)
        {
            WireRouteOverrides overrides;
            overrides.lane = lane;
            return BuildWireRoute(*outPt, inPt, srcRect, tgtRect, grid, gridOrigin, overrides);
        }
        return BuildWireRoute(cursor, inPt, IntRect{ cursor.x, cursor.y, 1, 1 }, tgtRect, grid, gridOrigin);
    }

    PropertyEndpoint FirstOutputAccepting(const NodeView& view, PropertyEndpoint input, int nodeIdx)
    {
        if (nodeIdx < 0 || nodeIdx >= (int)view.nodes.size())
            return {};
        const auto& properties = view.nodes[nodeIdx].properties;
        for (int p = 0; p < (int)properties.size(); ++p)
        {
            const PropertyEndpoint out{ nodeIdx, p };
            if (properties[p].output && ConnectionAllowed(view, out, input))
                return out;
        }
        return {};
    }

    int NodeAtCanvasPos(const NodeView& view, float2 canvasPos)
    {
        auto holds = [&](int n) {
            const NodeModel& node = view.nodes[size_t(n)];
            return !node.hidden && canvasPos.x >= float(node.pos.x) && canvasPos.x < float(node.pos.x + node.lastSize.x)
                && canvasPos.y >= float(node.pos.y) && canvasPos.y < float(node.pos.y + node.lastSize.y);
        };
        for (auto it = view.ordering.rbegin(); it != view.ordering.rend(); ++it)
            if (*it >= 0 && *it < (int)view.nodes.size() && holds(*it))
                return *it;
        return -1;
    }

    WireRoute BuildWireRoute(int2 outPt, int2 inPt,
                             IntRect srcRect, IntRect tgtRect,
                             int32_t grid, int2 gridOrigin,
                             const WireRouteOverrides& overrides)
    {
        WireRoute route;
        std::vector<int2>& pts = route.points;
        pts.reserve(6);
        pts.push_back(outPt);

        // No grid, or a node not laid out yet: fall back to a plain elbow.
        if (grid <= 0 || srcRect.IsEmpty() || tgtRect.IsEmpty())
        {
            pts.push_back(inPt);
            return route;
        }

        const int32_t srcR = srcRect.Max(0);   // source right edge
        const int32_t tgtL = tgtRect.Min(0);   // target left edge

        if (tgtL > srcR)
        {
            // Corridor between the bodies: one vertical lane, on the grid when it fits.
            // An override is clamped to the corridor.
            int32_t laneX = std::clamp(
                GridLineNearest((srcR + tgtL) / 2, gridOrigin.x, grid) + overrides.lane.offset * grid, srcR, tgtL);
            if (overrides.laneX)
                laneX = std::clamp(GridLineNearest(*overrides.laneX, gridOrigin.x, grid), srcR, tgtL);

            pts.push_back({ laneX, outPt.y });
            pts.push_back({ laneX, inPt.y  });
            route.handles.push_back(WireSegmentHandle{
                { laneX, outPt.y }, { laneX, inPt.y }, WireHandle::LaneX, /*vertical=*/true });
        }
        else
        {
            // No corridor: exit a cell right of the output, enter a cell left of the input,
            // joined by a return lane clear of both bodies. Overrides are clamped likewise.
            // Wires between the same nodes nest: the outermost wire (the top one going down,
            // the bottom one going up) takes the outer exit and return lane; its entry is
            // the inner one where the lane passes between the bodies, the outer one when it
            // goes round them.
            const int32_t count = std::max(1, overrides.lane.count);
            const int32_t index = std::clamp(overrides.lane.index, 0, count - 1);
            const int32_t first = (count - 1) / 2;
            // Steps out from the innermost wire: going down the top wire is outermost, going up the bottom one.
            const int32_t outerDown = count - 1 - index, outerUp = index;
            int32_t outer  = tgtRect.Min(1) < srcRect.Min(1) ? outerUp : outerDown;
            int32_t exitX  = GridLineGE(srcR + grid, gridOrigin.x, grid);
            int32_t entryX = GridLineLE(tgtL - grid, gridOrigin.x, grid);
            if (overrides.exitX)
                exitX  = std::max(GridLineNearest(*overrides.exitX, gridOrigin.x, grid),
                                  GridLineGE(srcR + 1, gridOrigin.x, grid));   // nearest clearing grid line
            if (overrides.entryX)
                entryX = std::min(GridLineNearest(*overrides.entryX, gridOrigin.x, grid),
                                  GridLineLE(tgtL - 1, gridOrigin.x, grid));

            int32_t laneY;
            const int32_t between = (first - index) * grid;   // spread across the gap, outer nearer the target
            // A lane set by hand between bodies one above the other goes where it's put, as
            // long as it clears both: in the gap between them, above them both or below them
            // both. One put inside a body goes to that body's nearer edge.
            auto clearOfBodies = [&](int32_t v, int32_t lo, int32_t hi) {
                const int32_t minTop = std::min(srcRect.Min(1), tgtRect.Min(1));
                const int32_t maxBot = std::max(srcRect.Max(1), tgtRect.Max(1));
                if (v <= minTop || v >= maxBot || (v >= lo && v <= hi))
                    return v;
                if (v < lo)   // in the upper body
                    return v - minTop <= lo - v ? minTop : lo;
                return v - hi <= maxBot - v ? hi : maxBot;   // in the lower body
            };
            if (srcRect.Max(1) < tgtRect.Min(1))        // source fully above target
            {
                const int32_t lo = srcRect.Max(1), hi = tgtRect.Min(1);
                if (!overrides.entryX) entryX -= (count - 1 - outer) * grid;
                laneY = std::clamp(GridLineNearest((lo + hi) / 2, gridOrigin.y, grid) + between, lo, hi);
                if (overrides.laneY)
                    laneY = clearOfBodies(GridLineNearest(*overrides.laneY, gridOrigin.y, grid), lo, hi);
            }
            else if (tgtRect.Max(1) < srcRect.Min(1))   // target fully above source
            {
                const int32_t lo = tgtRect.Max(1), hi = srcRect.Min(1);
                if (!overrides.entryX) entryX -= (count - 1 - outer) * grid;
                laneY = std::clamp(GridLineNearest((lo + hi) / 2, gridOrigin.y, grid) + between, lo, hi);
                if (overrides.laneY)
                    laneY = clearOfBodies(GridLineNearest(*overrides.laneY, gridOrigin.y, grid), lo, hi);
            }
            else
            {
                // Bodies overlap vertically - go around the nearer outside edge.
                const int32_t minTop = std::min(srcRect.Min(1), tgtRect.Min(1));
                const int32_t maxBot = std::max(srcRect.Max(1), tgtRect.Max(1));
                const int32_t topY = GridLineLE(minTop - grid, gridOrigin.y, grid);
                const int32_t botY = GridLineGE(maxBot + grid, gridOrigin.y, grid);
                const int32_t ref  = (outPt.y + inPt.y) / 2;
                const bool over = ref - topY <= botY - ref;   // round the top: the wires go up
                outer  = over ? outerUp : outerDown;
                laneY  = over ? topY - outer * grid : botY + outer * grid;
                if (!overrides.entryX) entryX -= outer * grid;
                if (overrides.laneY)
                {
                    // Keep the lane outside the bodies' combined vertical span;
                    // an override that lands inside snaps to the nearer edge.
                    const int32_t v = GridLineNearest(*overrides.laneY, gridOrigin.y, grid);
                    if      (v <= minTop) laneY = v;
                    else if (v >= maxBot) laneY = v;
                    else laneY = (v - minTop <= maxBot - v) ? (minTop - 1) : (maxBot + 1);
                }
            }

            if (!overrides.exitX)
                exitX += outer * grid;
            pts.push_back({ exitX,  outPt.y });
            pts.push_back({ exitX,  laneY   });
            pts.push_back({ entryX, laneY   });
            pts.push_back({ entryX, inPt.y  });
            route.handles.push_back(WireSegmentHandle{ { exitX, outPt.y }, { exitX, laneY  }, WireHandle::ExitX,  true  });
            route.handles.push_back(WireSegmentHandle{ { exitX, laneY   }, { entryX, laneY }, WireHandle::LaneY,  false });
            route.handles.push_back(WireSegmentHandle{ { entryX, laneY  }, { entryX, inPt.y }, WireHandle::EntryX, true  });
        }
        pts.push_back(inPt);

        pts.erase(std::unique(pts.begin(), pts.end(),
                              [](int2 a, int2 b){ return a.x == b.x && a.y == b.y; }),
                  pts.end());
        // Bends that go straight on (sockets in line with a lane) aren't bends.
        for (size_t i = 1; i + 1 < pts.size(); )
        {
            auto between = [](int32_t a, int32_t v, int32_t b) { return std::min(a, b) <= v && v <= std::max(a, b); };
            const bool straight = (pts[i - 1].x == pts[i].x && pts[i].x == pts[i + 1].x && between(pts[i - 1].y, pts[i].y, pts[i + 1].y))
                               || (pts[i - 1].y == pts[i].y && pts[i].y == pts[i + 1].y && between(pts[i - 1].x, pts[i].x, pts[i + 1].x));
            if (straight) pts.erase(pts.begin() + ptrdiff_t(i));
            else          ++i;
        }

        // A lane can collapse onto a socket (e.g. the sockets share a row), in
        // which case its segment is zero-length and not draggable.
        route.handles.erase(
            std::remove_if(route.handles.begin(), route.handles.end(),
                           [](const WireSegmentHandle& h){ return h.a.x == h.b.x && h.a.y == h.b.y; }),
            route.handles.end());

        return route;
    }

    float2 NodeViewScreenToCanvas(const float3x2& viewTransform, IntRect canvasInnerRectScreen, int2 screen)
    {
        // The canvas's contentTransform is T(innerSize/2) * viewTransform, so
        //   CANVAS_LAY = inverse(viewTransform)(SCREEN - innerRect.position - innerSize/2)
        const int2 canvasCenter = canvasInnerRectScreen.position
                                + int2{ canvasInnerRectScreen.w / 2, canvasInnerRectScreen.h / 2 };
        return viewTransform.Inverse().TransformPoint(
            float2{ (float)(screen.x - canvasCenter.x), (float)(screen.y - canvasCenter.y) });
    }

    float2 NodeViewScreenToCanvas(const NodeView& view, int2 screen)
    {
        return NodeViewScreenToCanvas(view.transform, IntRect{ .position = view.lastCanvasRect.position, .size = view.lastInnerSize }, screen);
    }

    float2 NodeViewCanvasToScreen(const NodeView& view, float2 canvasPos)
    {
        const float2 p = view.transform.TransformPoint(canvasPos);
        return float2{ p.x + float(view.lastCanvasRect.x + view.lastInnerSize.x / 2),
                       p.y + float(view.lastCanvasRect.y + view.lastInnerSize.y / 2) };
    }

    std::unordered_set<ID> ComputeNodesInMarquee(
        std::span<const NodeModel> nodes,
        IntRect                    canvasInnerRectScreen,
        const float3x2&            viewTransform,
        int2                       marqueeStartScreen,
        int2                       marqueeCurrentScreen)
    {
        std::unordered_set<ID> hits;

        auto screenToCanvasLay = [&](int2 screenPt) -> int2 {
            const float2 v = NodeViewScreenToCanvas(viewTransform, canvasInnerRectScreen, screenPt);
            return int2{ (int)v.x, (int)v.y };
        };

        const int2 startLay   = screenToCanvasLay(marqueeStartScreen);
        const int2 currentLay = screenToCanvasLay(marqueeCurrentScreen);
        const int mqMinX = std::min(startLay.x, currentLay.x);
        const int mqMinY = std::min(startLay.y, currentLay.y);
        const int mqMaxX = std::max(startLay.x, currentLay.x);
        const int mqMaxY = std::max(startLay.y, currentLay.y);

        for (const auto& node : nodes)
        {
            if (node.lastSize.x == 0 || node.lastSize.y == 0) continue;
            const int nodeMinX = node.pos.x;
            const int nodeMinY = node.pos.y;
            const int nodeMaxX = node.pos.x + node.lastSize.x;
            const int nodeMaxY = node.pos.y + node.lastSize.y;
            // Strict containment: node fully inside marquee.
            if (nodeMinX >= mqMinX && nodeMaxX <= mqMaxX &&
                nodeMinY >= mqMinY && nodeMaxY <= mqMaxY)
            {
                hits.insert(node.stableID);
            }
        }
        return hits;
    }

    void ConnectSockets(NodeView& view, PropertyEndpoint output, PropertyEndpoint input)
    {
        ApplyNodeViewEdit(view, ConnectEdit(view, output, input));
    }

    void BringNodeToFront(std::vector<int>& ordering, int n)
    {
        const auto it = std::find(ordering.begin(), ordering.end(), n);
        if (it == ordering.end() || it + 1 == ordering.end()) return;
        std::rotate(it, it + 1, ordering.end());
    }
}
