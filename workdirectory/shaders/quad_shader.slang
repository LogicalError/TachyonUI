// Quads: images and shadows, a rect with one corner radius, an optional blur and channel mapping.
#include "all_common.slang"
#include "sdf_color.slang"

struct VSOutput
{
    float4 position : SV_Position;
    nointerpolation int    quadIndex      : QUAD_INDEX;
    nointerpolation float2 boxSize        : BOX_SIZE;
    nointerpolation int    textureIndex   : TEXTURE_INDEX;
    nointerpolation float  transformScale : TRANSFORM_SCALE;
    nointerpolation float  cornerRadius   : CORNER_RADIUS;
    nointerpolation float  opacity        : OPACITY;
    float2 uv : UV;
};

// Quads share the triangle slot of the push constants.
Quad LoadQuad(int index) { return LoadPrimitive<Quad>(kTriangleSlot, index); }

[shader("vertex")]
VSOutput vertexMain(VertexInput vertex)
{
    const int vertexIndex = VertexIndex(vertex);
    int  quadIndex = vertexIndex / 4;
    Quad quad      = LoadQuad(quadIndex);
    float2 uv      = QuadCorner(vertexIndex % 4); // the corner, from the index buffer

    float3 quadMin = float3(float2(quad.pos), 1);
    float3 quadMax = quadMin + float3(float2(quad.size), 0);
    quadMin = ApplyTransform(quad.transform0, quad.transform1, quadMin);
    quadMax = ApplyTransform(quad.transform0, quad.transform1, quadMax);

    float2 pos  = quadMin.xy / quadMin.z;
    float2 size = (quadMax.xy / quadMax.z) - pos;

    float2 clipped_uv = clamp(uv * size, float2(quad.clipMin) - pos, float2(quad.clipMax) - pos);

    float2 position = floor(pos + clipped_uv + 0.5) + 0.5; // snap to the pixel (half up, as boxes) + pixel-center alignment
    clipped_uv /= size;

    float scaleX = length(float2(quad.transform0.x, quad.transform1.x));
    float scaleY = length(float2(quad.transform0.y, quad.transform1.y));

    VSOutput output;
    output.uv             = clipped_uv;
    output.quadIndex      = quadIndex;
    output.boxSize        = size;
    output.textureIndex   = quad.textureIndex;
    output.transformScale = sqrt(scaleX * scaleY);
    output.cornerRadius   = quad.cornerRadius;
    output.position       = PrimitivePosition(position, quad.transform0.w);
    output.opacity        = PrimitiveOpacity(quad.transform1);
    return output;
}

// A channel of `c` (0 red, 1 green, 2 blue, 3 alpha), or a constant (4 zero, 5 one).
float imageChannel(float4 c, uint source)
{
    return source == 0u ? c.r : source == 1u ? c.g : source == 2u ? c.b : source == 3u ? c.a : source == 4u ? 0.0 : 1.0;
}

// ImageDisplay: each channel from its source, then sRGB-encoded when asked.
float4 applyImageDisplay(float4 c, uint flags)
{
    if ((flags & 0x80000000u) == 0u)
        return c;
    float4 shown = float4(imageChannel(c, flags & 7u), imageChannel(c, (flags >> 3) & 7u),
                          imageChannel(c, (flags >> 6) & 7u), imageChannel(c, (flags >> 9) & 7u));
    if ((flags & 0x1000u) != 0u)
        shown.rgb = linearToSrgb(clamp(shown.rgb, 0.0, 1.0));
    return shown;
}

// Rounded box SDF (uniform radius on all corners)
float sdf_roundedBox(float2 p, float2 b, float r)
{
    float2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

// Depth is only tested here (occluders wrote it), so test it before shading.
[earlydepthstencil]
[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    Quad quad = LoadQuad(input.quadIndex);

    float  pixelrange      = 1.0 / min(input.boxSize.x, input.boxSize.y);
    float  half_pixelrange = pixelrange * 0.5;
    float2 uvsize = input.boxSize * half_pixelrange;
    float2 uv     = (((input.uv - 0.5) * 2) * uvsize) + half_pixelrange;

    // Bilinear interpolation weights for InterpolatedBox support.
    float u = input.uv.x;
    float v = input.uv.y;
    float4 weights = float4(
        (1.0 - u) * (1.0 - v),   // TL (pixel 0)
        u * (1.0 - v),           // TR (pixel 1)
        u * v,                   // BR (pixel 2)
        (1.0 - u) * v            // BL (pixel 3)
    );

    // SDF distance to the rounded-box edge. Drives both corner anti-aliasing
    // (alpha) and the SDFGradient color sample, so distance-based fills like the
    // drop shadow fade from the edge instead of sampling one fixed point.
    float cornerRadius = input.cornerRadius * input.transformScale * pixelrange;
    // A shadow's quad extends its shape by the blur on every side.
    float  blurPx = float(quad.Blur()) * input.transformScale;
    float2 shape  = max(uvsize - float2(blurPx * pixelrange), float2(0.0));
    float  dist   = sdf_roundedBox(uv, shape, min(cornerRadius, min(shape.x, shape.y)));

    // In units of the quad's smaller side: its middle is half a unit deep.
    float4 color = getSDFColor(quad.ColorIndex(), uv, uvsize, saturate(-dist * 2.0),
                               input.textureIndex, input.boxSize, weights, quad.uvRect);
    color = applyImageDisplay(color, quad.displayFlags) * quad.tint;

    float alpha = clamp(1.0 - (dist / pixelrange), 0.0, 1.0);
    if (blurPx > 0.0)
    {
        // Gaussian falloff across the edge (sigma = blur / 2, as CSS): 0.5 * erfc(d / (sigma * sqrt 2)).
        float x  = (dist / pixelrange) / (blurPx * 0.70710678);
        float x2 = x * x;
        const float a = 0.147;
        float erfx = sign(x) * sqrt(1.0 - exp(-x2 * (1.27323954 + a * x2) / (1.0 + a * x2)));
        alpha = 0.5 * (1.0 - erfx);
    }

    if (alpha <= (1.0 / 512.0))
        discard;

    color.a *= alpha;

    // Colors are sRGB already, as in the other shaders; premultiply alpha.
    color.a = clamp(color.a, 0, 1);
    color.rgb *= color.a;

    return color * input.opacity;   // premultiplied: the whole color fades
}
