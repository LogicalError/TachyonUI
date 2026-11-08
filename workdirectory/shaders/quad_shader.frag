#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_GOOGLE_include_directive : require

// Depth is only tested here (occluders wrote it), so test it before shading.
layout(early_fragment_tests) in;

#include "quad_common.glsl"

layout (location = 0) in flat QuadSurfaceData inSurface;
layout (location = 5) in QuadFragmentData inData;
layout (location = 16) in flat float inOpacity;

layout(set = 0, binding = 0) uniform sampler2D allTextures[];
layout (location = 0) out vec4 outFragColor;

#include "sdf_color.glsl"

// A channel of `c` (0 red, 1 green, 2 blue, 3 alpha), or a constant (4 zero, 5 one).
float imageChannel(vec4 c, uint source)
{
    return source == 0u ? c.r : source == 1u ? c.g : source == 2u ? c.b : source == 3u ? c.a : source == 4u ? 0.0 : 1.0;
}

// ImageDisplay: each channel from its source, then sRGB-encoded when asked.
vec4 applyImageDisplay(vec4 c, uint flags)
{
    if ((flags & 0x80000000u) == 0u)
        return c;
    vec4 shown = vec4(imageChannel(c, flags & 7u), imageChannel(c, (flags >> 3) & 7u),
                      imageChannel(c, (flags >> 6) & 7u), imageChannel(c, (flags >> 9) & 7u));
    if ((flags & 0x1000u) != 0u)
        shown.rgb = linearToSrgb(clamp(shown.rgb, 0.0, 1.0));
    return shown;
}

// Rounded box SDF (uniform radius on all corners)
float sdf_roundedBox(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main()
{
    Quad quad = Quads(PushConstants.triangleBuffer).quads[inSurface.quadIndex];

    float pixelrange = 1.0 / min(inSurface.boxSize.x, inSurface.boxSize.y);
    float half_pixelrange = pixelrange * 0.5;
    vec2 uvsize = inSurface.boxSize * half_pixelrange;
    vec2 uv     = (((inData.UV - 0.5) * 2) * uvsize) + half_pixelrange;

    // Bilinear interpolation weights for InterpolatedBox support.
    float u = inData.UV.x;
    float v = inData.UV.y;
    vec4 weights = vec4(
        (1.0 - u) * (1.0 - v),   // TL (pixel 0)
        u * (1.0 - v),            // TR (pixel 1)
        u * v,                    // BR (pixel 2)
        (1.0 - u) * v             // BL (pixel 3)
    );

    // SDF distance to the rounded-box edge. Drives both corner anti-aliasing
    // (alpha) and the SDFGradient color sample, so distance-based fills like the
    // drop shadow fade from the edge instead of sampling one fixed point.
    float cornerRadius = inSurface.cornerRadius * inSurface.transformScale * pixelrange;
    // A shadow's quad extends its shape by the blur on every side.
    float blurPx = float(quad.blur) * inSurface.transformScale;
    vec2  shape  = max(uvsize - vec2(blurPx * pixelrange), vec2(0.0));
    float dist = sdf_roundedBox(uv, shape, min(cornerRadius, min(shape.x, shape.y)));

    // In units of the quad's smaller side: its middle is half a unit deep.
    vec4 color = getSDFColor(int(quad.colorIndex), uv, uvsize, saturate(-dist * 2.0),
                             inSurface.textureIndex, inSurface.boxSize, weights, quad.uvRect);
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

    outFragColor = color * inOpacity;   // premultiplied: the whole color fades
}
