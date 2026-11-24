#pragma once

// API-neutral GUI primitive renderer: per-window targets accumulate per-kind
// primitive streams, then Render() uploads each stream and replays the pass
// list through the RHI. No graphics-API types appear here - a backend
// supplies a RenderCommandList with a fence-protected transient upload arena
// and the pipeline set built from kPrimitiveSpecs.

#include <gui_core/gui_id.h>
#include <gui_core/graphics/gui_renderer.h>
#include <gui_core/graphics/gui_render_primitives.h>
#include <gui_core/graphics/gui_rhi.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace tachyon
{
    /// @addtogroup graphics
    /// @{

    using PipelineSet = std::array<RHI::GpuPipeline, kPrimitiveKindCount>;

    /// One contiguous same-kind run of primitives, drawn with one call; or, with `replay`
    /// set, a cached scope's draw (its own passes), and the other fields unused.
    struct PrimitiveRenderPass
    {
        int           firstPrimitive;
        int           primitiveCount;
        PrimitiveKind kind;
        int           replay = -1; ///< index into PrimitiveRenderTarget::replays

        bool operator==(const PrimitiveRenderPass&) const = default;
    };

    struct PrimitiveRenderTarget
    {
    public:
        /// Per-kind primitive stream: CPU bytes appended by the emit methods and
        /// packed together into the current frame slot's GPU upload arena.
        struct Stream
        {
            std::vector<std::byte> cpu;
            int                    passStart = 0; ///< index of the open pass's first primitive
        };
        std::array<Stream, kPrimitiveKindCount> streams;

        std::vector<PrimitiveRenderPass> renderPasses;
        int           currentKind = -1; ///< kind of the open pass; -1 when none
        tachyon::float3x2 currentTransform;
        float             currentOpacity = 1.0f; ///< 0 to 1, stamped into each primitive

        /// A cached scope drawn here, moved `delta` window pixels from where it was captured;
        /// its primitives' draw orders start at `baseOrder`.
        struct ScopeReplay
        {
            tachyon::ID   scope;
            tachyon::int2 delta;
            uint32_t      baseOrder = 0;
            bool operator==(const ScopeReplay&) const = default;
        };
        std::vector<ScopeReplay> replays;
        /// A cached scope captured again into this target this frame drew something else than
        /// before: what it replays changed, though its replay is the same.
        bool recaptured = false;

    public:
        void FinishRenderPass();
        void StartRenderPass(PrimitiveKind kind);

        /// Appends another target's passes, translated by `delta` pixels. Works for every kind:
        /// each primitive starts with the same header (transform0, transform1, clipMin, clipMax).
        void AppendCaptured(const PrimitiveRenderTarget& src, tachyon::int2 delta);
        /// Closes the open pass and adds a pass that draws cached scope `scope`, moved by `delta`;
        /// `captured` is what the scope drew: it gives the draw orders the replay takes and
        /// where on screen it draws.
        void AddReplay(tachyon::ID scope, tachyon::int2 delta, const PrimitiveRenderTarget& captured);

    public:
        /// Uploads the streams and draws the passes; replay passes are left to the caller
        /// (PrimitiveRenderer). Returns the bytes uploaded.
        size_t Render(RHI::RenderCommandList& cmdList, const PipelineSet& pipelines,
                      GPUDrawPushConstants& push_constants);
        void Reset();

        void SetTransform(tachyon::float3x2 transform);
        void SetOpacity(float opacity) { currentOpacity = opacity; }
        void RenderBox(tachyon::IntRect rect, tachyon::IntRect clipRect, const tachyon::StyleBlend& styles, tachyon::VisualColorStyleIndex colorStyleIndex = {}, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid, tachyon::IntRect snappedPx = {});
        void RenderBox(tachyon::IntRect rect, tachyon::IntRect clipRect, uint32_t fromStyleIndex, uint32_t toStyleIndex, float blendT, tachyon::VisualColorStyleIndex colorStyleIndex = {}, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid, tachyon::IntRect snappedPx = {})
        {
            RenderBox(rect, clipRect, tachyon::StyleBlend::Between(fromStyleIndex, toStyleIndex, blendT), colorStyleIndex, textureIndex, snappedPx);
        }
        void RenderGlyph(tachyon::float2 pos, tachyon::float2 size, tachyon::float2 uvMin, tachyon::float2 uvMax, float scale, int bpp, int mode, tachyon::VisualColorStyleIndex textColorIndex, tachyon::IntRect clipRect, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid, float shadowBlur = -1.0f);
        /// RenderGlyph for each of `glyphs`, moved by `offset`: one pass placement for them all,
        /// over the box they cover together.
        void RenderGlyphs(std::span<const GUIRenderer::GlyphInstance> glyphs, tachyon::float2 offset, tachyon::IntRect clipRect);
        void RenderLine(tachyon::int2 p0, tachyon::int2 p1, tachyon::int2 p2, tachyon::int2 p3, tachyon::IntRect clipRect, const tachyon::LineStyle& lineStyle);
        void RenderCircle(tachyon::float2 center, float radius, tachyon::IntRect clipRect, const tachyon::LineStyle& lineStyle);
        void RenderDot(tachyon::float2 pos, tachyon::IntRect clipRect, float size, tachyon::color4f color);
        /// RenderDot at each of `positions`: one pass placement for them all.
        void RenderDots(std::span<const tachyon::float2> positions, tachyon::IntRect clipRect, float size, tachyon::color4f color);
        void RenderTriangle(tachyon::float2 p0, tachyon::float2 p1, tachyon::float2 p2, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid, TriangleEdges hardEdges = TriangleEdges::None);
        void RenderQuad(tachyon::IntRect rect, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, float cornerRadius = 0.0f, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid, const tachyon::UVRect& uv = {}, const tachyon::ImageDisplay& display = {});
        void RenderShadow(tachyon::IntRect rect, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, float cornerRadius, int blur);
        void RenderOccluder(tachyon::IntRect rect, tachyon::IntRect clipRect);

        /// The draw order the next primitive gets; also counts a replay's primitives.
        uint32_t nextOrder = 0;

    private:
        /// Screen pixels [x0, x1) x [y0, y1); none when either is empty.
        struct ScreenBox
        {
            int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;

            bool Empty() const { return x1 <= x0 || y1 <= y0; }
            bool Overlaps(const ScreenBox& o) const
            {
                return !Empty() && !o.Empty() && x0 < o.x1 && o.x0 < x1 && y0 < o.y1 && o.y0 < y1;
            }
            /// Both boxes' pixels and those between; an empty one adds none.
            ScreenBox Cover(const ScreenBox& o) const
            {
                if (Empty()) return o;
                if (o.Empty()) return *this;
                return { std::min(x0, o.x0), std::min(y0, o.y0), std::max(x1, o.x1), std::max(y1, o.y1) };
            }
            ScreenBox Within(tachyon::IntRect clip) const
            {
                return { std::max(x0, clip.x), std::max(y0, clip.y), std::min(x1, clip.x + clip.w), std::min(y1, clip.y + clip.h) };
            }
            /// Moved `delta` pixels; an edge at or past +-`limit` stays there.
            ScreenBox Moved(tachyon::int2 delta, int32_t limit) const
            {
                auto move = [limit](int32_t v, int32_t d)
                {
                    return v <= -limit || v >= limit ? v : int32_t(std::clamp<int64_t>(int64_t(v) + d, -limit, limit));
                };
                return { move(x0, delta.x), move(y0, delta.y), move(x1, delta.x), move(y1, delta.y) };
            }
        };
        /// Every pass's bounds together: where this target draws.
        ScreenBox Covered() const;

        /// Where each of renderPasses' passes draws on screen (its primitives' bounds); a
        /// replay or an appended capture covers everything.
        std::vector<ScreenBox> passBounds;
        ScreenBox openBounds {};   ///< the open pass's
        /// Each kind's last pass in renderPasses, -1 when none: its primitives end its stream.
        std::array<int, kPrimitiveKindCount> lastPass = MakeNoPasses();
        static constexpr std::array<int, kPrimitiveKindCount> MakeNoPasses()
        {
            std::array<int, kPrimitiveKindCount> none {};
            none.fill(-1);
            return none;
        }

        /// Puts the next `count` primitives of `kind` (appended at its stream's end) in a pass: the open
        /// one when it's of that kind; else the kind's last pass when nothing drawn since
        /// overlaps `bounds` (drawn earlier, it blends over and under the same things); else a
        /// new one.
        void PlaceInPass(PrimitiveKind kind, ScreenBox bounds, int count = 1);
        /// The screen pixels a primitive spanning [min, max] in local coordinates can touch:
        /// its corners through the transform, `margin` pixels further, within `clipRect`.
        ScreenBox Bounds(tachyon::float2 min, tachyon::float2 max, float margin, tachyon::IntRect clipRect) const
        {
            const tachyon::float3x2& t = currentTransform;
            float x0, x1, y0, y1;
            if (t.r0.y == 0.0f && t.r1.x == 0.0f)
            {
                // No rotation or shear (nearly always): each axis on its own.
                const float ax = t.r0.x * min.x + t.r0.z, bx = t.r0.x * max.x + t.r0.z;
                const float ay = t.r1.y * min.y + t.r1.z, by = t.r1.y * max.y + t.r1.z;
                x0 = std::min(ax, bx); x1 = std::max(ax, bx);
                y0 = std::min(ay, by); y1 = std::max(ay, by);
            }
            else
            {
                const float cx[4] = { min.x, max.x, min.x, max.x }, cy[4] = { min.y, min.y, max.y, max.y };
                x0 = y0 = INFINITY;
                x1 = y1 = -INFINITY;
                for (int i = 0; i < 4; ++i)
                {
                    const float x = t.r0.x * cx[i] + t.r0.y * cy[i] + t.r0.z, y = t.r1.x * cx[i] + t.r1.y * cy[i] + t.r1.z;
                    x0 = std::min(x0, x); x1 = std::max(x1, x);
                    y0 = std::min(y0, y); y1 = std::max(y1, y);
                }
            }
            // Far past the screen counts as the screen's edge, so the ints don't overflow; a
            // NaN lands on the edge too, by the clamp's comparisons.
            static constexpr float kFar = float(1 << 28);
            auto px = [](float v) { return int32_t(std::clamp(v, -kFar, kFar)); };
            const ScreenBox box{ px(std::floor(x0 - margin)), px(std::floor(y0 - margin)),
                                 px(std::ceil(x1 + margin)), px(std::ceil(y1 + margin)) };
            return box.Within(clipRect);
        }

        /// Appends `primitive`, which can touch `bounds` on screen, to its kind's stream, in a
        /// pass (PlaceInPass).
        template <typename T>
        void Append(PrimitiveKind kind, const T& primitive, ScreenBox bounds)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            assert(sizeof(T) == kPrimitiveSpecs[size_t(kind)].stride);
            PlaceInPass(kind, bounds);
            auto& cpu = streams[size_t(kind)].cpu;
            T ordered = primitive;
            ordered.transform0.w = float(nextOrder++);
            ordered.transform1.w = 1.0f - currentOpacity;   // transparency: 0 is opaque
            const size_t at = cpu.size();
            cpu.resize(at + sizeof(T));
            std::memcpy(cpu.data() + at, &ordered, sizeof(T));
        }
    };

    class PrimitiveRenderer : public GUIRenderer
    {
    public:
        std::unordered_map<tachyon::ID, PrimitiveRenderTarget> idTargets;
        PrimitiveRenderTarget* currentTarget{ nullptr };
        PrimitiveRenderTarget* resizingTarget{ nullptr };

    public:
        void Reset() override;
        void SetTransform(tachyon::float3x2 transform) override;
        void SetOpacity(float opacity) override;
        using GUIRenderer::RenderBox;
        void RenderBox(tachyon::IntRect rect, tachyon::IntRect clipRect, const tachyon::StyleBlend& styles, tachyon::VisualColorStyleIndex colorStyleIndex = {}, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid, tachyon::IntRect snappedPx = {}) override;
        void RenderGlyph(tachyon::float2 pos, tachyon::float2 size, tachyon::float2 uvMin, tachyon::float2 uvMax, float scale, int bpp, int mode, tachyon::VisualColorStyleIndex textColorIndex, tachyon::IntRect clipRect, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid, float shadowBlur = -1.0f) override;
        void RenderGlyphs(std::span<const GlyphInstance> glyphs, tachyon::float2 offset, tachyon::IntRect clipRect) override;
        void RenderLine(tachyon::int2 p0, tachyon::int2 p1, tachyon::int2 p2, tachyon::int2 p3, tachyon::IntRect clipRect, const tachyon::LineStyle& lineStyle) override;
        void RenderCircle(tachyon::float2 center, float radius, tachyon::IntRect clipRect, const tachyon::LineStyle& lineStyle) override;
        void RenderDot(tachyon::float2 pos, tachyon::IntRect clipRect, float size, tachyon::color4f color) override;
        void RenderDots(std::span<const tachyon::float2> positions, tachyon::IntRect clipRect, float size, tachyon::color4f color) override;
        void RenderTriangle(tachyon::float2 p0, tachyon::float2 p1, tachyon::float2 p2, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid, TriangleEdges hardEdges = TriangleEdges::None) override;
        void RenderQuad(tachyon::IntRect rect, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, float cornerRadius = 0.0f, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid, const tachyon::UVRect& uv = {}, const tachyon::ImageDisplay& display = {}) override;
        void RenderShadow(tachyon::IntRect rect, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, float cornerRadius, int blur) override;
        void RenderOccluder(tachyon::IntRect rect, tachyon::IntRect clipRect) override;

    public:
        PrimitiveRenderTarget& GetTarget(tachyon::ID id)
        {
            auto target = idTargets.find(id);
            if (target == idTargets.end())
            {
                idTargets[id] = PrimitiveRenderTarget();
                return idTargets[id];
            }
            return target->second;
        }

        /// Drops the CPU-side target of window `id`, when the window is destroyed. Transient GPU
        /// storage belongs to the window's frame slots, not to this target.
        void ReleaseTarget(tachyon::ID id);
        void ReleaseAllTargets();

        bool StartResizing(tachyon::ID id) override
        {
            assert(resizingTarget == nullptr);
            auto target = idTargets.find(id);
            if (target == idTargets.end())
            {
                resizingTarget = nullptr;
            } else
                resizingTarget = &target->second;
            return resizingTarget != nullptr;
        }

        // The target may be gone already: the frame a live resize runs can close its window,
        // and ReleaseTarget lets go of it then.
        void StopResizing() override
        {
            resizingTarget = nullptr;
        }

        bool BeginWindow(tachyon::ID id) override;
        void EndWindow() override;
        bool DrawsChanged(tachyon::ID window) const override;

        // Draw caching. A capture's primitives go to the GPU once and are drawn from there,
        // moved by the world matrix, each time it is replayed; until their copy is on the GPU
        // they are uploaded with the frame.
        void BeginScopeCapture(tachyon::ID scopeId) override;
        void EndScopeCapture(tachyon::ID scopeId) override;
        bool ReplayScopeCapture(tachyon::ID scopeId, tachyon::int2 delta) override;
        bool HasScopeCapture(tachyon::ID scopeId) const override;
        void InvalidateScopeCapture(tachyon::ID scopeId) override;
        void ClearScopeCaptures() override;

        void Render(tachyon::ID id, RHI::RenderCommandList& cmdList, const PipelineSet& pipelines,
                    GPUDrawPushConstants& push_constants);

        /// Primitive bytes the last Render uploaded with the frame; cached scopes drawn from
        /// their GPU copy add nothing.
        size_t LastFrameUploadBytes() const { return m_lastUploadBytes; }
        /// Captured scopes with a GPU copy (made or on its way).
        size_t ResidentScopeCount() const;

        /// A capture neither recorded nor replayed for this many frames is dropped, its GPU copy
        /// freed (as UICacheManager drops its scopes).
        static constexpr uint64_t kEvictAfterFrames = 240;

    private:
        /// A cached scope's primitives, and their copy on the GPU.
        struct ScopeCapture
        {
            PrimitiveRenderTarget target;
            PrimitiveRenderTarget previous;   ///< while capturing again: what it drew before
            RHI::ResidentBufferId resident = 0;
            std::array<DeviceAddress, kPrimitiveKindCount> addresses{}; ///< in the resident buffer
            uint64_t lastUsedFrame = 0;
        };

        /// The addresses `capture` is drawn from this frame: its GPU copy when that is ready,
        /// else a transient upload of its primitives.
        std::array<DeviceAddress, kPrimitiveKindCount> CaptureAddresses(ScopeCapture& capture, RHI::RenderCommandList& cmdList);
        /// Drops the capture's GPU copy (freed on the next Render).
        void ReleaseResident(ScopeCapture& capture);

        /// Captured primitives per cached scope, kept across frames.
        std::unordered_map<tachyon::ID, ScopeCapture> scopeCaptures;
        /// GPU copies to free, on the next Render (which has the command list).
        std::vector<RHI::ResidentBufferId> m_releasedResidents;
        /// Where each replayed capture is drawn from in the frame being rendered; kept between
        /// frames for its storage.
        struct ResolvedReplay
        {
            const PrimitiveRenderTarget*                   passes = nullptr;
            std::array<DeviceAddress, kPrimitiveKindCount> addresses{};
            GPUDrawPushConstants                           push{};
        };
        std::vector<ResolvedReplay> m_resolvedReplays;
        /// The window target while a capture redirects emit calls.
        PrimitiveRenderTarget* m_captureSaved = nullptr;
        uint64_t m_frame = 0;
        size_t   m_lastUploadBytes = 0;

        /// A window's draws as of its last EndWindow, to tell whether the next frame's are the same.
        struct DrawSnapshot
        {
            PrimitiveRenderTarget draws;
            bool seen    = false;   ///< drawn at least once
            bool changed = true;    ///< the last EndWindow's draws differed from the ones before
        };
        std::unordered_map<tachyon::ID, DrawSnapshot> m_lastDraws;
        tachyon::ID m_currentWindow = tachyon::ID::Invalid();
        /// Compares the window's draws with its snapshot, and keeps them when they differ.
        void NoteDraws(tachyon::ID window, const PrimitiveRenderTarget& target);
    };
    /// @}
}
