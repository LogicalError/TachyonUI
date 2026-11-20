#include <gui_core/graphics/gui_primitive_renderer.h>
#include <gui_core/diagnostics/gui_profiler.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace tachyon
{

// Snapped edges are 16-bit: an edge past the range (a sizer stretched to kStretchHeight)
// saturates, it doesn't wrap around to the other side.
static int16_t SaturateInt16(int32_t v) { return int16_t(std::clamp<int32_t>(v, INT16_MIN, INT16_MAX)); }

namespace
{
    // What a replay or an appended capture covers: anything.
    constexpr int32_t kFarEdge = 1 << 29;

    // How far back a primitive looks for its kind's pass: the overlap test runs over every
    // pass in between.
    constexpr size_t kLookBack = 32;
}

// The transform's largest stretch: how much a length in local coordinates can grow on screen.
static float MaxScale(const float3x2& t)
{
    return std::max(std::abs(t.r0.x) + std::abs(t.r0.y), std::abs(t.r1.x) + std::abs(t.r1.y));
}

namespace
{
    // One upload request per kind's stream.
    std::array<RHI::UploadRequest, kPrimitiveKindCount> StreamUploads(const PrimitiveRenderTarget& target, size_t& bytes)
    {
        std::array<RHI::UploadRequest, kPrimitiveKindCount> uploads{};
        for (size_t k = 0; k < kPrimitiveKindCount; ++k)
        {
            const auto& cpu = target.streams[k].cpu;
            uploads[k] = RHI::UploadRequest{ cpu.data(), cpu.size(), 16 };
            bytes += cpu.size();
        }
        return uploads;
    }

    // Draws one same-kind pass of `source`; kinds share push-constant slots, so it writes its
    // own address.
    void DrawPass(const PrimitiveRenderPass& pass, const PrimitiveRenderTarget& source,
                  const std::array<DeviceAddress, kPrimitiveKindCount>& addresses,
                  RHI::RenderCommandList& cmdList, const PipelineSet& pipelines, GPUDrawPushConstants& push_constants)
    {
        const auto& spec = kPrimitiveSpecs[size_t(pass.kind)];
        // No address: the upload failed (out of memory). Drawing would read address 0.
        if (addresses[size_t(pass.kind)] == 0)
            return;
        push_constants.primitiveAddresses[spec.pushSlot] = addresses[size_t(pass.kind)];
        const auto& cpu = source.streams[size_t(pass.kind)].cpu;
        const std::span<const std::byte> primitives(cpu.data() + size_t(pass.firstPrimitive) * spec.stride,
                                                    size_t(pass.primitiveCount) * spec.stride);
        cmdList.DrawPrimitives(pipelines[size_t(pass.kind)], push_constants,
                               uint32_t(pass.primitiveCount), uint32_t(pass.firstPrimitive), spec.shape, primitives);
    }

    // Moves everything drawn with `world` by `delta` window pixels and `orders` draw orders.
    // The shaders clip and snap in window pixels before the world matrix, so a whole-pixel
    // move keeps both.
    tachyon::float4x4 Translated(tachyon::float4x4 world, tachyon::int2 delta, uint32_t orders)
    {
        // Column-major, as the shaders read it: the translation is the fourth column.
        float m[16];
        static_assert(sizeof(m) == sizeof(world));
        std::memcpy(m, &world, sizeof(m));
        m[12] += m[0] * float(delta.x) + m[4] * float(delta.y);
        m[13] += m[1] * float(delta.x) + m[5] * float(delta.y);
        m[14] += m[10] * float(orders);
        std::memcpy(&world, m, sizeof(m));
        return world;
    }
}

void PrimitiveRenderer::Render(tachyon::ID id, RHI::RenderCommandList& cmdList,
                               const PipelineSet& pipelines, GPUDrawPushConstants& push_constants)
{
    for (const RHI::ResidentBufferId resident : m_releasedResidents)
        cmdList.ReleaseResidentBuffer(resident);
    m_releasedResidents.clear();

    auto& target = GetTarget(id);
    target.FinishRenderPass();

    size_t bytes = 0;
    const auto uploads = StreamUploads(target, bytes);
    std::array<DeviceAddress, kPrimitiveKindCount> addresses{};
    cmdList.UploadTransientBuffers(uploads, addresses);
    m_lastUploadBytes = bytes;

    // Where each replayed capture is drawn from this frame.
    std::vector<ResolvedReplay>& replays = m_resolvedReplays;
    replays.assign(target.replays.size(), ResolvedReplay{});
    for (size_t r = 0; r < target.replays.size(); ++r)
    {
        const PrimitiveRenderTarget::ScopeReplay& replay = target.replays[r];
        auto capture = scopeCaptures.find(replay.scope);
        if (capture == scopeCaptures.end())
            continue;
        replays[r].passes    = &capture->second.target;
        replays[r].addresses = CaptureAddresses(capture->second, cmdList);
        replays[r].push      = push_constants;
        replays[r].push.worldMatrix = Translated(push_constants.worldMatrix, replay.delta, replay.baseOrder);
    }

    // Occluders first, writing depth; then everything else in order, testing it.
    for (const bool occluders : { true, false })
    {
        auto draws = [occluders](const PrimitiveRenderPass& pass) { return kPrimitiveSpecs[size_t(pass.kind)].depthOnly == occluders; };
        for (const auto& pass : target.renderPasses)
        {
            if (pass.replay < 0)
            {
                if (draws(pass))
                    DrawPass(pass, target, addresses, cmdList, pipelines, push_constants);
                continue;
            }
            ResolvedReplay& replay = replays[size_t(pass.replay)];
            if (replay.passes == nullptr)
                continue;
            for (const auto& capturePass : replay.passes->renderPasses)
                if (draws(capturePass))
                    DrawPass(capturePass, *replay.passes, replay.addresses, cmdList, pipelines, replay.push);
        }
    }
}

std::array<DeviceAddress, kPrimitiveKindCount> PrimitiveRenderer::CaptureAddresses(ScopeCapture& capture, RHI::RenderCommandList& cmdList)
{
    size_t bytes = 0;
    const auto uploads = StreamUploads(capture.target, bytes);

    // Lost with its device, or never made: make the GPU copy (ready from a later frame).
    if (capture.resident != 0 && cmdList.ResidentBufferState(capture.resident) == RHI::ResidentState::Gone)
        capture.resident = 0;
    if (capture.resident == 0)
        capture.resident = cmdList.UploadResidentBuffers(uploads, capture.addresses);

    std::array<DeviceAddress, kPrimitiveKindCount> addresses = capture.addresses;
    if (cmdList.ResidentBufferState(capture.resident) != RHI::ResidentState::Ready)
    {
        cmdList.UploadTransientBuffers(uploads, addresses);
        m_lastUploadBytes += bytes;
    }
    return addresses;
}

void PrimitiveRenderer::ReleaseResident(ScopeCapture& capture)
{
    if (capture.resident != 0)
        m_releasedResidents.push_back(capture.resident);
    capture.resident  = 0;
    capture.addresses = {};
}

size_t PrimitiveRenderer::ResidentScopeCount() const
{
    return size_t(std::count_if(scopeCaptures.begin(), scopeCaptures.end(),
                                [](const auto& entry) { return entry.second.resident != 0; }));
}


size_t PrimitiveRenderTarget::Render(RHI::RenderCommandList& cmdList, const PipelineSet& pipelines,
                                     GPUDrawPushConstants& push_constants)
{
    FinishRenderPass();

    size_t bytes = 0;
    const auto uploads = StreamUploads(*this, bytes);
    std::array<DeviceAddress, kPrimitiveKindCount> addresses{};
    cmdList.UploadTransientBuffers(uploads, addresses);

    for (const bool occluders : { true, false })
        for (const auto& pass : renderPasses)
            if (pass.replay < 0 && kPrimitiveSpecs[size_t(pass.kind)].depthOnly == occluders)
                DrawPass(pass, *this, addresses, cmdList, pipelines, push_constants);
    return bytes;
}


void PrimitiveRenderTarget::Reset()
{
    renderPasses.clear();
    passBounds.clear();
    lastPass   = MakeNoPasses();
    openBounds = {};
    replays.clear();
    for (auto& stream : streams)
    {
        stream.cpu.clear();
        stream.passStart = 0;
    }
    currentKind = -1;
    nextOrder   = 0;
    currentOpacity = 1.0f;
    recaptured  = false;
}


void PrimitiveRenderTarget::SetTransform(tachyon::float3x2 transform)
{
    currentTransform = transform;
}

void PrimitiveRenderTarget::RenderBox(tachyon::IntRect rect, tachyon::IntRect clipRect, const tachyon::StyleBlend& styles, tachyon::VisualColorStyleIndex colorStyleIndex, tachyon::ImageIndex textureIndex, tachyon::IntRect snappedPx)
{
    // Each style with its share in 8 bits; the shares add up to 255, the rounding going to the first.
    uint32_t packed[4] = { 0, 0, 0, 0 };
    int shares = 0;
    const int count = std::clamp(int(styles.count), 1, tachyon::StyleBlend::kMaxStyles);
    for (int i = 0; i < count; ++i)
    {
        assert(styles.styles[size_t(i)] < (1u << 24) && "a GPU style index takes 24 bits");
        const int share = std::clamp(int(std::lround(styles.weights[size_t(i)] * 255.0f)), 0, 255);
        shares += share;
        packed[i] = (styles.styles[size_t(i)] & 0xFFFFFFu) | (uint32_t(share) << 24);
    }
    const int first = std::clamp(int(packed[0] >> 24) + (255 - shares), 0, 255);
    packed[0] = (packed[0] & 0xFFFFFFu) | (uint32_t(first) << 24);

    // The box's rect and, where given, the pixels it's snapped to: a pixel past either for the rounding.
    ScreenBox bounds = Bounds(float2{ float(rect.x), float(rect.y) }, float2{ float(rect.x + rect.w), float(rect.y + rect.h) }, 1.0f, clipRect);
    if (snappedPx.w > 0 && snappedPx.h > 0)
        bounds = bounds.Cover(ScreenBox{ snappedPx.x - 1, snappedPx.y - 1, snappedPx.x + snappedPx.w + 1, snappedPx.y + snappedPx.h + 1 }.Within(clipRect));
    Append(PrimitiveKind::Box, BoxPrimitive
                    {
                        .transform0   = { currentTransform.r0.x, currentTransform.r0.y, currentTransform.r0.z, 0 },
                        .transform1   = { currentTransform.r1.x, currentTransform.r1.y, currentTransform.r1.z, 0 },
                        .clipMin      = { clipRect.Min(0), clipRect.Min(1) },
                        .clipMax      = { clipRect.Max(0), clipRect.Max(1) },

                        .pos          = { rect.x, rect.y }, .size = { rect.w, rect.h },

                        .textureIndex = textureIndex,
                        .colorIndex   = int16_t(colorStyleIndex),
                        .styles       = { packed[0], packed[1], packed[2], packed[3] },
                        .snappedPx    = { SaturateInt16(snappedPx.x), SaturateInt16(snappedPx.y),
                                          SaturateInt16(snappedPx.x + snappedPx.w), SaturateInt16(snappedPx.y + snappedPx.h) }
                    }, bounds);
}

void PrimitiveRenderTarget::RenderTriangle(tachyon::float2 p0, tachyon::float2 p1, tachyon::float2 p2, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, tachyon::ImageIndex textureIndex, tachyon::TriangleEdges hardEdges)
{
    const ScreenBox bounds = Bounds(float2{ std::min({ p0.x, p1.x, p2.x }), std::min({ p0.y, p1.y, p2.y }) },
                                  float2{ std::max({ p0.x, p1.x, p2.x }), std::max({ p0.y, p1.y, p2.y }) }, 2.0f, clipRect);
    Append(PrimitiveKind::Triangle, TrianglePrimitive
                    {
                        .transform0 = { currentTransform.r0.x, currentTransform.r0.y, currentTransform.r0.z, 0 },
                        .transform1 = { currentTransform.r1.x, currentTransform.r1.y, currentTransform.r1.z, 0 },
                        .clipMin = { clipRect.Min(0), clipRect.Min(1) },
                        .clipMax = { clipRect.Max(0), clipRect.Max(1) },

                        .p0 = { p0.x, p0.y },
                        .p1 = { p1.x, p1.y },
                        .p2 = { p2.x, p2.y },

                        .textureIndex = textureIndex,
                        .colorIndex = int16_t(colorStyleIndex),
                        .hardEdges = int16_t(hardEdges)
                    }, bounds);
}


void PrimitiveRenderTarget::RenderQuad(tachyon::IntRect rect, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, float cornerRadius, tachyon::ImageIndex textureIndex, const tachyon::UVRect& uv, const tachyon::ImageDisplay& display)
{
    const ScreenBox bounds = Bounds(float2{ float(rect.x), float(rect.y) }, float2{ float(rect.x + rect.w), float(rect.y + rect.h) }, 1.0f, clipRect);
    Append(PrimitiveKind::Quad, QuadPrimitive
                    {
                        .transform0 = { currentTransform.r0.x, currentTransform.r0.y, currentTransform.r0.z, 0 },
                        .transform1 = { currentTransform.r1.x, currentTransform.r1.y, currentTransform.r1.z, 0 },
                        .clipMin = { clipRect.Min(0), clipRect.Min(1) },
                        .clipMax = { clipRect.Max(0), clipRect.Max(1) },

                        .pos = { rect.x, rect.y }, .size = { rect.w, rect.h },

                        .textureIndex = textureIndex,
                        .colorIndex = int16_t(colorStyleIndex),
                        .cornerRadius = cornerRadius,
                        .displayFlags = display.PackedFlags(),
                        .uvRect = { uv.min.x, uv.min.y, uv.max.x, uv.max.y },
                        .tint = { display.tint.r, display.tint.g, display.tint.b, display.tint.a }
                    }, bounds);
}

void PrimitiveRenderTarget::RenderShadow(tachyon::IntRect rect, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, float cornerRadius, int blur)
{
    blur = std::clamp(blur, 0, 4096);
    const ScreenBox bounds = Bounds(float2{ float(rect.x - blur), float(rect.y - blur) },
                                  float2{ float(rect.x + rect.w + blur), float(rect.y + rect.h + blur) }, 1.0f, clipRect);
    Append(PrimitiveKind::Quad, QuadPrimitive
                    {
                        .transform0 = { currentTransform.r0.x, currentTransform.r0.y, currentTransform.r0.z, 0 },
                        .transform1 = { currentTransform.r1.x, currentTransform.r1.y, currentTransform.r1.z, 0 },
                        .clipMin = { clipRect.Min(0), clipRect.Min(1) },
                        .clipMax = { clipRect.Max(0), clipRect.Max(1) },

                        .pos = { rect.x - blur, rect.y - blur }, .size = { rect.w + 2 * blur, rect.h + 2 * blur },

                        .colorIndex = int16_t(colorStyleIndex),
                        .blur = int16_t(blur),
                        .cornerRadius = cornerRadius
                    }, bounds);
}

void PrimitiveRenderTarget::RenderOccluder(tachyon::IntRect rect, tachyon::IntRect clipRect)
{
    if (rect.w <= 0 || rect.h <= 0 || currentOpacity < 1.0f)
        return;
    const ScreenBox bounds = Bounds(float2{ float(rect.x), float(rect.y) }, float2{ float(rect.x + rect.w), float(rect.y + rect.h) }, 0.0f, clipRect);
    Append(PrimitiveKind::Occluder, OccluderPrimitive
                    {
                        .transform0 = { currentTransform.r0.x, currentTransform.r0.y, currentTransform.r0.z, 0 },
                        .transform1 = { currentTransform.r1.x, currentTransform.r1.y, currentTransform.r1.z, 0 },
                        .clipMin = { clipRect.Min(0), clipRect.Min(1) },
                        .clipMax = { clipRect.Max(0), clipRect.Max(1) },

                        .pos = { rect.x, rect.y }, .size = { rect.w, rect.h },
                    }, bounds);
}

void PrimitiveRenderTarget::RenderGlyph(tachyon::float2 pos, tachyon::float2 size, tachyon::float2 uvMin, tachyon::float2 uvMax, float scale, int bpp, int mode, tachyon::VisualColorStyleIndex textColorIndex, tachyon::IntRect clipRect, tachyon::ImageIndex textureIndex, float shadowBlur)
{
    const ScreenBox bounds = Bounds(pos, float2{ pos.x + size.x, pos.y + size.y }, 1.0f, clipRect);
    Append(PrimitiveKind::Glyph, GlyphPrimitive
                    {
                        .transform0 = { currentTransform.r0.x, currentTransform.r0.y, currentTransform.r0.z, 0 },
                        .transform1 = { currentTransform.r1.x, currentTransform.r1.y, currentTransform.r1.z, 0 },
                        .clipMin = { clipRect.Min(0), clipRect.Min(1) },
                        .clipMax = { clipRect.Max(0), clipRect.Max(1) },

                        .pos = { pos.x, pos.y }, .size = { size.x, size.y },

                        .uvMin = { uvMin.x, uvMin.y }, .uvMax = { uvMax.x, uvMax.y },

                        .scale = scale,
                        .bpp = uint16_t(bpp),
                        .mode = uint16_t(mode),

                        .textureIndex = textureIndex,

                        .textColorIndex = int16_t(textColorIndex),
                        .shadowBlur = shadowBlur < 0.0f ? int16_t(-1) : int16_t(std::min(shadowBlur * 8.0f + 0.5f, 32767.0f)),
                     }, bounds);
}

void PrimitiveRenderTarget::RenderGlyphs(std::span<const GUIRenderer::GlyphInstance> glyphs, tachyon::float2 offset, tachyon::IntRect clipRect)
{
    if (glyphs.empty())
        return;
    float2 lo{ INFINITY, INFINITY }, hi{ -INFINITY, -INFINITY };
    for (const GUIRenderer::GlyphInstance& g : glyphs)
    {
        const float x = g.pos.x + offset.x, y = g.pos.y + offset.y;
        lo = { std::min(lo.x, x), std::min(lo.y, y) };
        hi = { std::max(hi.x, x + g.size.x), std::max(hi.y, y + g.size.y) };
    }
    PlaceInPass(PrimitiveKind::Glyph, Bounds(lo, hi, 1.0f, clipRect), int(glyphs.size()));

    auto& cpu = streams[size_t(PrimitiveKind::Glyph)].cpu;
    const size_t at = cpu.size();
    cpu.resize(at + glyphs.size() * sizeof(GlyphPrimitive));
    std::byte* out = cpu.data() + at;
    for (const GUIRenderer::GlyphInstance& g : glyphs)
    {
        const GlyphPrimitive primitive
        {
            .transform0 = { currentTransform.r0.x, currentTransform.r0.y, currentTransform.r0.z, float(nextOrder++) },
            .transform1 = { currentTransform.r1.x, currentTransform.r1.y, currentTransform.r1.z, 1.0f - currentOpacity },
            .clipMin = { clipRect.Min(0), clipRect.Min(1) },
            .clipMax = { clipRect.Max(0), clipRect.Max(1) },
            .pos = { g.pos.x + offset.x, g.pos.y + offset.y }, .size = g.size,
            .uvMin = g.uvMin, .uvMax = g.uvMax,
            .scale = g.scale, .bpp = g.bpp, .mode = g.mode,
            .textureIndex = g.textureIndex,
            .textColorIndex = int16_t(g.color),
            .shadowBlur = -1,
        };
        std::memcpy(out, &primitive, sizeof(primitive));
        out += sizeof(primitive);
    }
}

void PrimitiveRenderTarget::RenderLine(tachyon::int2 p0, tachyon::int2 p1, tachyon::int2 p2, tachyon::int2 p3,
                                       tachyon::IntRect clipRect, const tachyon::LineStyle& lineStyle)
{
    // The points go to the shader in 16 bits: saturated there, and the bounds are of those.
    auto saturate = [](tachyon::int2 p) { return tachyon::int2{ SaturateInt16(p.x), SaturateInt16(p.y) }; };
    p0 = saturate(p0); p1 = saturate(p1); p2 = saturate(p2); p3 = saturate(p3);
    // The control points' hull, the stroke wide around it (in screen pixels, or scaled up).
    const ScreenBox bounds = Bounds(float2{ float(std::min({ p0.x, p1.x, p2.x, p3.x })), float(std::min({ p0.y, p1.y, p2.y, p3.y })) },
                                  float2{ float(std::max({ p0.x, p1.x, p2.x, p3.x })), float(std::max({ p0.y, p1.y, p2.y, p3.y })) },
                                  std::max(lineStyle.lineWidth, 1.0f) * std::max(1.0f, MaxScale(currentTransform)) + 2.0f, clipRect);
    Append(PrimitiveKind::Line, LinePrimitive
                    {
                        .transform0 = { currentTransform.r0.x, currentTransform.r0.y, currentTransform.r0.z, 0 },
                        .transform1 = { currentTransform.r1.x, currentTransform.r1.y, currentTransform.r1.z, 0 },
                        .clipMin = { clipRect.Min(0), clipRect.Min(1) },
                        .clipMax = { clipRect.Max(0), clipRect.Max(1) },

                        .colorIndex     = int(lineStyle.colorStyleIndex),
                        .lineWidth      = lineStyle.lineWidth,
                        .dashSize       = lineStyle.dashSize,
                        .dashOffset     = lineStyle.dashOffset,
                        .gradientOffset = lineStyle.gradientOffset,
                        .gradientScale  = lineStyle.gradientScale,

                        .p0 = { int16_t(p0.x), int16_t(p0.y) },
                        .p1 = { int16_t(p1.x), int16_t(p1.y) },
                        .p2 = { int16_t(p2.x), int16_t(p2.y) },
                        .p3 = { int16_t(p3.x), int16_t(p3.y) }
                    }, bounds);
}

void PrimitiveRenderTarget::RenderCircle(tachyon::float2 center, float radius, tachyon::IntRect clipRect, const tachyon::LineStyle& lineStyle)
{
    // Around the center on screen, as far as the largest stretch takes the radius: under a
    // non-uniform scale the shader's ring is no ellipse inside the transformed square.
    const float scale = MaxScale(currentTransform);
    const ScreenBox bounds = Bounds(center, center,
                                  radius * scale + std::max(lineStyle.lineWidth, 1.0f) * 0.5f * std::max(1.0f, scale) + 2.0f, clipRect);
    Append(PrimitiveKind::Circle, CirclePrimitive
                    {
                        .transform0 = { currentTransform.r0.x, currentTransform.r0.y, currentTransform.r0.z, 0 },
                        .transform1 = { currentTransform.r1.x, currentTransform.r1.y, currentTransform.r1.z, 0 },
                        .clipMin = { clipRect.Min(0), clipRect.Min(1) },
                        .clipMax = { clipRect.Max(0), clipRect.Max(1) },

                        .center         = { center.x, center.y },
                        .radius         = std::max(radius, 0.0f),
                        .lineWidth      = lineStyle.lineWidth,
                        .colorIndex     = int(lineStyle.colorStyleIndex),
                        .dashSize       = lineStyle.dashSize,
                        .dashOffset     = lineStyle.dashOffset,
                        .gradientOffset = lineStyle.gradientOffset,
                        .gradientScale  = lineStyle.gradientScale,
                    }, bounds);
}

void PrimitiveRenderTarget::RenderDot(tachyon::float2 pos, tachyon::IntRect clipRect, float size, tachyon::color4f color)
{
    // The triangle around the dot reaches about twice its radius out.
    const ScreenBox bounds = Bounds(pos, pos, std::max(size * MaxScale(currentTransform), 2.0f) + 4.0f, clipRect);
    Append(PrimitiveKind::Dot, DotPrimitive
                    {
                        .transform0 = { currentTransform.r0.x, currentTransform.r0.y, currentTransform.r0.z, 0 },
                        .transform1 = { currentTransform.r1.x, currentTransform.r1.y, currentTransform.r1.z, 0 },
                        .clipMin = { clipRect.Min(0), clipRect.Min(1) },
                        .clipMax = { clipRect.Max(0), clipRect.Max(1) },

                        .color = color,
                        .size = size,
                        .pos = { pos.x, pos.y }
                    }, bounds);
}


void PrimitiveRenderTarget::RenderDots(std::span<const tachyon::float2> positions, tachyon::IntRect clipRect, float size, tachyon::color4f color)
{
    if (positions.empty())
        return;
    float2 lo{ INFINITY, INFINITY }, hi{ -INFINITY, -INFINITY };
    for (const float2& p : positions)
    {
        lo = { std::min(lo.x, p.x), std::min(lo.y, p.y) };
        hi = { std::max(hi.x, p.x), std::max(hi.y, p.y) };
    }
    // As RenderDot's: the triangle around a dot reaches about twice its radius out.
    PlaceInPass(PrimitiveKind::Dot, Bounds(lo, hi, std::max(size * MaxScale(currentTransform), 2.0f) + 4.0f, clipRect), int(positions.size()));

    auto& cpu = streams[size_t(PrimitiveKind::Dot)].cpu;
    const size_t at = cpu.size();
    cpu.resize(at + positions.size() * sizeof(DotPrimitive));
    std::byte* out = cpu.data() + at;
    // The dots differ only in where they are and their draw order.
    DotPrimitive primitive
    {
        .transform0 = { currentTransform.r0.x, currentTransform.r0.y, currentTransform.r0.z, 0 },
        .transform1 = { currentTransform.r1.x, currentTransform.r1.y, currentTransform.r1.z, 1.0f - currentOpacity },
        .clipMin = { clipRect.Min(0), clipRect.Min(1) },
        .clipMax = { clipRect.Max(0), clipRect.Max(1) },
        .color = color,
        .size = size,
    };
    for (const float2& p : positions)
    {
        primitive.transform0.w = float(nextOrder++);
        primitive.pos[0] = p.x;
        primitive.pos[1] = p.y;
        std::memcpy(out, &primitive, sizeof(primitive));
        out += sizeof(primitive);
    }
}

void PrimitiveRenderTarget::FinishRenderPass()
{
    if (currentKind < 0)
        return;

    const auto& spec   = kPrimitiveSpecs[size_t(currentKind)];
    auto&       stream = streams[size_t(currentKind)];
    const int   count  = int(stream.cpu.size() / spec.stride);
    lastPass[size_t(currentKind)] = int(renderPasses.size());
    renderPasses.push_back(PrimitiveRenderPass
    {
        .firstPrimitive = stream.passStart,
        .primitiveCount = count - stream.passStart,
        .kind           = PrimitiveKind(currentKind)
    });
    passBounds.push_back(openBounds);
    currentKind = -1;
}


void PrimitiveRenderTarget::PlaceInPass(PrimitiveKind kind, ScreenBox bounds, int count)
{
    if (currentKind == int(kind))
    {
        openBounds = openBounds.Cover(bounds);
        return;
    }
    const int last = lastPass[size_t(kind)];
    const bool depthOnly = kPrimitiveSpecs[size_t(kind)].depthOnly;
    bool join = last >= 0;
    // Occluders only write the depth, the farthest draw order wins: in any order, in their own
    // loop before the colours. A colour primitive mustn't pass a colour draw it overlaps; a
    // replay's bounds are where its scope drew.
    if (join && !depthOnly)
    {
        join = renderPasses.size() - size_t(last) <= kLookBack;
        if (join && currentKind >= 0 && !kPrimitiveSpecs[size_t(currentKind)].depthOnly && openBounds.Overlaps(bounds))
            join = false;
        for (size_t i = size_t(last) + 1; join && i < renderPasses.size(); ++i)
        {
            const PrimitiveRenderPass& pass = renderPasses[i];
            if (pass.replay < 0 && kPrimitiveSpecs[size_t(pass.kind)].depthOnly)
                continue;
            if (passBounds[i].Overlaps(bounds))
                join = false;
        }
    }
    if (join)
    {
        // Its primitives end the kind's stream: the ones appended next are the pass's next.
        renderPasses[size_t(last)].primitiveCount += count;
        passBounds[size_t(last)] = passBounds[size_t(last)].Cover(bounds);
        return;
    }
    StartRenderPass(kind);
    openBounds = bounds;
}



void PrimitiveRenderTarget::StartRenderPass(PrimitiveKind kind)
{
    if (currentKind == int(kind))
        return;

    FinishRenderPass();
    auto& stream = streams[size_t(kind)];
    stream.passStart = int(stream.cpu.size() / kPrimitiveSpecs[size_t(kind)].stride);
    currentKind = int(kind);
}

PrimitiveRenderTarget::ScreenBox PrimitiveRenderTarget::Covered() const
{
    ScreenBox covered = currentKind >= 0 ? openBounds : ScreenBox{};
    for (const ScreenBox& box : passBounds)
        covered = covered.Cover(box);
    return covered;
}

void PrimitiveRenderTarget::AddReplay(tachyon::ID scope, tachyon::int2 delta, const PrimitiveRenderTarget& captured)
{
    FinishRenderPass();
    renderPasses.push_back(PrimitiveRenderPass{ .firstPrimitive = 0, .primitiveCount = 0, .kind = PrimitiveKind::Box,
                                                .replay = int(replays.size()) });
    passBounds.push_back(captured.Covered().Moved(delta, kFarEdge));
    replays.push_back(ScopeReplay{ scope, delta, nextOrder });
    nextOrder += captured.nextOrder;
}

void PrimitiveRenderTarget::AppendCaptured(const PrimitiveRenderTarget& src, tachyon::int2 delta)
{
    TACHYON_PROFILE_SCOPE("AppendCaptured");
    // Every primitive kind starts with the same header, so the translate is
    // kind-agnostic: transform0 @0 (z @8), transform1 @16 (z @24), then the
    // screen-space clip rect clipMin @32 / clipMax @40 (two int2s).
    static_assert(offsetof(BoxPrimitive, transform0) == 0,  "primitive header layout changed");
    static_assert(offsetof(BoxPrimitive, transform1) == 16, "primitive header layout changed");
    static_assert(offsetof(BoxPrimitive, clipMin)    == 32, "primitive header layout changed");
    static_assert(offsetof(BoxPrimitive, clipMax)    == 40, "primitive header layout changed");

    FinishRenderPass(); // close any open pass in the destination first

    const bool translate = (delta.x != 0 || delta.y != 0);
    for (const auto& pass : src.renderPasses)
    {
        const size_t   k       = size_t(pass.kind);
        const uint32_t stride  = kPrimitiveSpecs[k].stride;
        const auto&    srcBytes = src.streams[k].cpu;
        auto&          dstBytes = streams[k].cpu;

        const int    dstFirst = int(dstBytes.size() / stride);
        const size_t srcOff   = size_t(pass.firstPrimitive) * stride;
        const size_t len      = size_t(pass.primitiveCount) * stride;
        const size_t base     = dstBytes.size();
        dstBytes.insert(dstBytes.end(), srcBytes.begin() + srcOff, srcBytes.begin() + srcOff + len);

        for (int p = 0; p < pass.primitiveCount; p++)
            reinterpret_cast<float*>(dstBytes.data() + base + size_t(p) * stride)[3] = float(nextOrder++); // transform0.w

        if (translate)
        {
            for (int p = 0; p < pass.primitiveCount; p++)
            {
                std::byte* hdr = dstBytes.data() + base + size_t(p) * stride;
                float* t = reinterpret_cast<float*>(hdr);
                t[2] += float(delta.x); // transform0.z (translation x)
                t[6] += float(delta.y); // transform1.z (translation y)
                int32_t* clip = reinterpret_cast<int32_t*>(hdr + 32);
                clip[0] += delta.x; clip[1] += delta.y; // clipMin
                clip[2] += delta.x; clip[3] += delta.y; // clipMax
                if (pass.kind == PrimitiveKind::Box)
                {
                    BoxPrimitive* box = reinterpret_cast<BoxPrimitive*>(hdr);
                    if (box->snappedPx[2] > box->snappedPx[0] || box->snappedPx[3] > box->snappedPx[1])
                    {
                        box->snappedPx[0] = SaturateInt16(box->snappedPx[0] + delta.x);
                        box->snappedPx[1] = SaturateInt16(box->snappedPx[1] + delta.y);
                        box->snappedPx[2] = SaturateInt16(box->snappedPx[2] + delta.x);
                        box->snappedPx[3] = SaturateInt16(box->snappedPx[3] + delta.y);
                    }
                }
            }
        }

        // Its primitives' bounds aren't kept: it covers anything.
        lastPass[k] = int(renderPasses.size());
        renderPasses.push_back(PrimitiveRenderPass{ dstFirst, pass.primitiveCount, pass.kind });
        passBounds.push_back(ScreenBox{ -kFarEdge, -kFarEdge, kFarEdge, kFarEdge });
    }
    currentKind = -1;
}


void PrimitiveRenderer::Reset()
{
    if (resizingTarget != nullptr)
    {
        resizingTarget->Reset();
    } else
    {
        for (auto& target : idTargets) { target.second.Reset(); }

        ++m_frame;
        for (auto it = scopeCaptures.begin(); it != scopeCaptures.end(); )
        {
            if (m_frame - it->second.lastUsedFrame > kEvictAfterFrames)
            {
                ReleaseResident(it->second);
                it = scopeCaptures.erase(it);
            }
            else
                ++it;
        }
    }
    currentTarget = nullptr;
}

void PrimitiveRenderer::SetTransform(tachyon::float3x2 transform)
{
    assert(currentTarget != nullptr);
    currentTarget->SetTransform(transform);
}

void PrimitiveRenderer::SetOpacity(float opacity)
{
    assert(currentTarget != nullptr);
    currentTarget->SetOpacity(std::clamp(opacity, 0.0f, 1.0f));
}

void PrimitiveRenderer::ReleaseTarget(tachyon::ID id)
{
    auto it = idTargets.find(id);
    if (it == idTargets.end())
        return;

    if (currentTarget  == &it->second) currentTarget  = nullptr;
    if (resizingTarget == &it->second) resizingTarget = nullptr;
    idTargets.erase(it);
    m_lastDraws.erase(id);
}

void PrimitiveRenderer::ReleaseAllTargets()
{
    idTargets.clear();
    m_lastDraws.clear();
    currentTarget  = nullptr;
    resizingTarget = nullptr;
}

// Make it possible to render windows without caption/border/transparency etc.
bool PrimitiveRenderer::BeginWindow(tachyon::ID id)
{
    assert(currentTarget == nullptr);
    auto& target = GetTarget(id);
    if (resizingTarget != nullptr)
    {
        if (resizingTarget != &target)
            return false;
    }
    currentTarget   = &target;
    m_currentWindow = id;
    return true;
}

void PrimitiveRenderer::EndWindow()
{
    assert(currentTarget != nullptr);
    NoteDraws(m_currentWindow, *currentTarget);
    currentTarget   = nullptr;
    m_currentWindow = tachyon::ID::Invalid();
}

bool PrimitiveRenderer::DrawsChanged(tachyon::ID window) const
{
    const auto it = m_lastDraws.find(window);
    return it == m_lastDraws.end() || it->second.changed;
}

// Whether two targets hold the same draws: their primitives byte for byte, passes and replays.
static bool SameDraws(const PrimitiveRenderTarget& a, const PrimitiveRenderTarget& b)
{
    if (a.currentKind != b.currentKind || a.renderPasses != b.renderPasses || a.replays != b.replays)
        return false;
    for (size_t k = 0; k < kPrimitiveKindCount; ++k)
        if (a.streams[k].cpu != b.streams[k].cpu)
            return false;
    return true;
}

void PrimitiveRenderer::NoteDraws(tachyon::ID window, const PrimitiveRenderTarget& target)
{
    DrawSnapshot& last = m_lastDraws[window];
    const bool same = last.seen && !target.recaptured && SameDraws(last.draws, target);
    last.changed = !same;
    last.seen    = true;
    if (!same)
        last.draws = target;
}

void PrimitiveRenderer::BeginScopeCapture(tachyon::ID scopeId)
{
    assert(currentTarget != nullptr);
    assert(m_captureSaved == nullptr && "nested scope capture is not supported");
    m_captureSaved = currentTarget;
    ScopeCapture& cap = scopeCaptures[scopeId];
    // The old primitives aside, to tell at the end whether they changed.
    std::swap(cap.previous, cap.target);
    cap.target.Reset();
    cap.lastUsedFrame = m_frame;
    currentTarget = &cap.target; // redirect emit calls into the capture
}

void PrimitiveRenderer::EndScopeCapture(tachyon::ID scopeId)
{
    assert(m_captureSaved != nullptr);
    ScopeCapture& cap = scopeCaptures[scopeId];
    cap.target.FinishRenderPass();
    currentTarget  = m_captureSaved;
    m_captureSaved = nullptr;
    // The same as before: its GPU copy stays, and its window hasn't changed for it.
    if (!SameDraws(cap.previous, cap.target) || cap.target.renderPasses.empty())
    {
        ReleaseResident(cap);   // the old primitives' GPU copy
        currentTarget->recaptured = true;
    }
    cap.previous.Reset();
    // Drawn into the window this frame too, where it was captured.
    currentTarget->AddReplay(scopeId, tachyon::int2{ 0, 0 }, cap.target);
}

bool PrimitiveRenderer::ReplayScopeCapture(tachyon::ID scopeId, tachyon::int2 delta)
{
    assert(currentTarget != nullptr);
    auto it = scopeCaptures.find(scopeId);
    if (it == scopeCaptures.end() || it->second.target.renderPasses.empty())
        return false;
    it->second.lastUsedFrame = m_frame;
    currentTarget->AddReplay(scopeId, delta, it->second.target);
    return true;
}

bool PrimitiveRenderer::HasScopeCapture(tachyon::ID scopeId) const
{
    auto it = scopeCaptures.find(scopeId);
    return it != scopeCaptures.end() && !it->second.target.renderPasses.empty();
}

void PrimitiveRenderer::InvalidateScopeCapture(tachyon::ID scopeId)
{
    if (auto it = scopeCaptures.find(scopeId); it != scopeCaptures.end())
    {
        ReleaseResident(it->second);
        scopeCaptures.erase(it);
    }
}

void PrimitiveRenderer::ClearScopeCaptures()
{
    for (auto& [id, capture] : scopeCaptures)
        ReleaseResident(capture);
    scopeCaptures.clear();
}

void PrimitiveRenderer::RenderBox(tachyon::IntRect rect, tachyon::IntRect clipRect, const tachyon::StyleBlend& styles, tachyon::VisualColorStyleIndex colorStyleIndex, tachyon::ImageIndex textureIndex, tachyon::IntRect snappedPx)
{
    assert(currentTarget != nullptr);
    currentTarget->RenderBox(rect, clipRect, styles, colorStyleIndex, textureIndex, snappedPx);
}

void PrimitiveRenderer::RenderGlyph(tachyon::float2 pos, tachyon::float2 size, tachyon::float2 uvMin, tachyon::float2 uvMax, float scale, int bpp, int mode, tachyon::VisualColorStyleIndex textColorIndex, tachyon::IntRect clipRect, tachyon::ImageIndex textureIndex, float shadowBlur)
{
    assert(currentTarget != nullptr);
    currentTarget->RenderGlyph(pos, size, uvMin, uvMax, scale, bpp, mode, textColorIndex, clipRect, textureIndex, shadowBlur);
}

void PrimitiveRenderer::RenderGlyphs(std::span<const GlyphInstance> glyphs, tachyon::float2 offset, tachyon::IntRect clipRect)
{
    assert(currentTarget != nullptr);
    currentTarget->RenderGlyphs(glyphs, offset, clipRect);
}

void PrimitiveRenderer::RenderDots(std::span<const tachyon::float2> positions, tachyon::IntRect clipRect, float size, tachyon::color4f color)
{
    assert(currentTarget != nullptr);
    currentTarget->RenderDots(positions, clipRect, size, color);
}

void PrimitiveRenderer::RenderLine(tachyon::int2 p0, tachyon::int2 p1, tachyon::int2 p2, tachyon::int2 p3, tachyon::IntRect clipRect, const tachyon::LineStyle& lineStyle)
{
    assert(currentTarget != nullptr);
    currentTarget->RenderLine(p0, p1, p2, p3, clipRect, lineStyle);
}

void PrimitiveRenderer::RenderCircle(tachyon::float2 center, float radius, tachyon::IntRect clipRect, const tachyon::LineStyle& lineStyle)
{
    assert(currentTarget != nullptr);
    currentTarget->RenderCircle(center, radius, clipRect, lineStyle);
}

void PrimitiveRenderer::RenderDot(tachyon::float2 pos, tachyon::IntRect clipRect, float size, tachyon::color4f color)
{
    assert(currentTarget != nullptr);
    currentTarget->RenderDot(pos, clipRect, size, color);
}

void PrimitiveRenderer::RenderTriangle(tachyon::float2 p0, tachyon::float2 p1, tachyon::float2 p2, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, tachyon::ImageIndex textureIndex, tachyon::TriangleEdges hardEdges)
{
    assert(currentTarget != nullptr);
    currentTarget->RenderTriangle(p0, p1, p2, clipRect, colorStyleIndex, textureIndex, hardEdges);
}

void PrimitiveRenderer::RenderQuad(tachyon::IntRect rect, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, float cornerRadius, tachyon::ImageIndex textureIndex, const tachyon::UVRect& uv, const tachyon::ImageDisplay& display)
{
    assert(currentTarget != nullptr);
    currentTarget->RenderQuad(rect, clipRect, colorStyleIndex, cornerRadius, textureIndex, uv, display);
}

void PrimitiveRenderer::RenderOccluder(tachyon::IntRect rect, tachyon::IntRect clipRect)
{
    assert(currentTarget != nullptr);
    currentTarget->RenderOccluder(rect, clipRect);
}

void PrimitiveRenderer::RenderShadow(tachyon::IntRect rect, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, float cornerRadius, int blur)
{
    assert(currentTarget != nullptr);
    currentTarget->RenderShadow(rect, clipRect, colorStyleIndex, cornerRadius, blur);
}

}
