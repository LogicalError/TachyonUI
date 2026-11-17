// Glyphs: atlas images, bitmap or distance field, tinted by their text color; also text shadows.
#include "all_common.slang"
#include "sdf_color.slang"

// The atlas' glyph distance field (SkribidiContext::kSdfDistanceScale): 128 on the edge,
// 8 levels per texel, higher outside. It reaches 16 texels each way, then stays flat.
static const float kSdfEdge        = 128.0 / 255.0;
static const float kSdfPerTexel    = 8.0 / 255.0;
static const float kSdfReachTexels = 15.5;

struct VSOutput
{
    float4 position : SV_Position;
    nointerpolation int   textColorIndex : TEXT_COLOR_INDEX; // VisualColorStyleIndex (1-based)
    nointerpolation int   textureIndex   : TEXTURE_INDEX;
    nointerpolation float scale          : SCALE;
    nointerpolation uint  bpp            : BPP;
    nointerpolation uint  mode           : MODE;
    nointerpolation float shadowBlur     : SHADOW_BLUR;      // pixels; below 0 = the glyph itself, not a shadow
    nointerpolation float opacity        : OPACITY;
    float2 uv : UV;
};

[shader("vertex")]
VSOutput vertexMain(int vertexIndex : SV_VulkanVertexID)
{
    Glyph glyph = LoadPrimitive<Glyph>(kGlyphSlot, vertexIndex / 4);
    float2 offset = QuadCorner(vertexIndex % 4); // the corner, from the index buffer

    // transformed box
    float3 glyphMin = float3(glyph.pos, 1);
    float3 glyphMax = glyphMin + float3(glyph.size, 0);
    glyphMin = ApplyTransform(glyph.transform0, glyph.transform1, glyphMin);
    glyphMax = ApplyTransform(glyph.transform0, glyph.transform1, glyphMax);

    float2 pos  = (glyphMin.xy / glyphMin.z);
    float2 size = (glyphMax.xy / glyphMax.z) - pos;

    // clip the position in (0,1) space
    float2 clipped_min    = float2(glyph.clipMin) - pos;
    float2 clipped_max    = float2(glyph.clipMax) - pos;
    float2 clipped_offset = clamp(offset * size, clipped_min, clipped_max);

    // The +0.5 on position pairs with the +0.5 texel on the UV below so each pixel
    // centre samples a texel centre; drop either and bilinear filtering blurs.
    // Pixel snapping happens at the origin upstream, not per glyph, which would
    // jitter the shared baseline.
    float2 position       = (pos + clipped_offset) + 0.5;
    float2 atlasPerScreen = abs(glyph.uvMax - glyph.uvMin) / max(size, float2(1.0));
    float2 uv             = lerp(glyph.uvMin, glyph.uvMax, clipped_offset / size) + 0.5 * atlasPerScreen;

    VSOutput output;
    output.uv             = uv;
    output.textureIndex   = max(0, glyph.textureIndex - 1);
    output.scale          = glyph.scale;
    output.bpp            = glyph.glyph_type.x;
    output.mode           = glyph.glyph_type.y;
    output.textColorIndex = int(glyph.textColorIndex);
    output.shadowBlur     = glyph.shadowBlur < 0 ? -1.0 : float(glyph.shadowBlur) / 8.0;
    output.position       = PrimitivePosition(position, glyph.transform0.w);
    output.opacity        = PrimitiveOpacity(glyph.transform1);
    return output;
}

// The glyph's texel: its own RGBA for a color glyph (4 bytes a pixel), else white over its coverage.
float4 SampleGlyph(uint textureIndex, uint bpp, float2 uv)
{
    float4 texel = SampleImage(textureIndex, uv);
    return (bpp == 4) ? texel : float4(1, 1, 1, texel.r);
}

// Depth is only tested here (occluders wrote it), so test it before shading.
[earlydepthstencil]
[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float2 uv = input.uv / ImageSize(input.textureIndex);

    uint bpp  = input.bpp;
    uint mode = input.mode;

    float4 glyphSample = srgbToLinear(SampleGlyph(input.textureIndex, bpp, uv));
    if (mode != 0)
    {
        float w = input.scale * 0.8;
        float d = (glyphSample.a - kSdfEdge) / kSdfPerTexel;
        float a = 1.f - clamp((d + 0.5 * w - 0.1) / w, 0.0, 1.0);
        if (input.shadowBlur > 0.0)
        {
            // Gaussian falloff across the edge (sigma = blur / 2, as CSS): 0.5 * erfc(d / (sigma * sqrt 2)).
            // A blur wider than the distance field reaches is narrowed until the falloff ends
            // inside it (3 sigma), so it fades out instead of stopping at an edge.
            float reachPx = kSdfReachTexels / input.scale;
            float sigma   = min(input.shadowBlur * 0.5, reachPx / 3.0);
            float x  = (d / input.scale) / (sigma * 1.41421356);
            float x2 = x * x;
            const float k = 0.147;
            float erfx = sign(x) * sqrt(1.0 - exp(-x2 * (1.27323954 + k * x2) / (1.0 + k * x2)));
            a = 0.5 * (1.0 - erfx) * (1.0 - smoothstep(kSdfReachTexels - 1.0, kSdfReachTexels, d));
        }

        glyphSample = float4(glyphSample.rgb, a);
    }

    float4 color;
    if (input.shadowBlur >= 0.0)
    {
        // A shadow takes the glyph's shape alone, color glyphs included.
        float4 shadowColor = getSDFColor(input.textColorIndex, float2(0), float2(1), 0.0, 0, float2(1), float4(0));
        color = float4(1, 1, 1, glyphSample.a) * srgbToLinear(shadowColor);
    }
    else if (bpp == 4)
    {
        // Colored glyph (emoji): use the glyph's own RGBA colors unchanged.
        color = glyphSample;
    }
    else
    {
        // SDF / grayscale glyph: tint with the ColorStyle text color.
        // glyphSample is white * alpha mask; multiply by resolved text color.
        float4 textColor = getSDFColor(input.textColorIndex, float2(0), float2(1), 0.0, 0, float2(1), float4(0));
        color = glyphSample * srgbToLinear(textColor);
    }

    if (color.a <= (1.0 / 512))
        discard;

    // convert color back to srgb
    color.rgb = linearToSrgb(color.rgb);
    color.a = clamp(color.a, 0, 1);

    // pre-multiply alpha
    color.rgb *= color.a;

    return color * input.opacity;   // premultiplied: the whole color fades
}
