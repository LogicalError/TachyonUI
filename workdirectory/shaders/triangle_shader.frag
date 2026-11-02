#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_GOOGLE_include_directive : require

// Depth is only tested here (occluders wrote it), so test it before shading.
layout(early_fragment_tests) in;

#include "triangle_common.glsl"

layout (location = 0) in flat TriSurfaceData inSurface;
layout (location = 4) in TriFragmentData inData;
layout (location = 16) in flat float inOpacity;

layout(set = 0, binding = 0) uniform sampler2D allTextures[];
layout (location = 0) out vec4 outFragColor;

float distanceToSegment(vec2 p, vec2 a, vec2 b)
{
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

#include "sdf_color.glsl"

void main()
{
    Triangle triangle = PushConstants.triangleBuffer.triangles[inSurface.triangleIndex];

    vec2 p0 = vec2(triangle.p0);
    vec2 p1 = vec2(triangle.p1);
    vec2 p2 = vec2(triangle.p2);

    // Match the vertex shader: the interior test runs in transformed (screen)
    // space, since inData.position is the transformed quad position.
    mat3x3 transform = transpose(mat3x3(triangle.transform0.xyz, triangle.transform1.xyz, vec3(0,0,1)));
    vec3 p0_t = transform * vec3(p0, 1);
    vec3 p1_t = transform * vec3(p1, 1);
    vec3 p2_t = transform * vec3(p2, 1);
    p0 = p0_t.xy / p0_t.z;
    p1 = p1_t.xy / p1_t.z;
    p2 = p2_t.xy / p2_t.z;
    vec2 p = inData.position;

    float area = cross2(p1 - p0, p2 - p0);
    if (abs(area) < 0.0001)
        discard;

    float w0 = cross2(p1 - p0, p - p0) / area;
    float w1 = cross2(p2 - p1, p - p1) / area;
    float w2 = cross2(p0 - p2, p - p2) / area;

    // A hard edge (shared with a neighbour) cuts straight; only the others are soft.
    // (A pixel's middle right on it is kept on both sides, despite rounding.)
    int hard = int(triangle.hardEdges);
    const float onEdge = -1e-4;
    if (((hard & 1) != 0 && w0 < onEdge) || ((hard & 2) != 0 && w1 < onEdge) || ((hard & 4) != 0 && w2 < onEdge))
        discard;
    // The weights are over the signed area, so inside is all three at or above zero; a hard
    // edge passed above counts as inside.
    bool inside = ((hard & 1) != 0 || w0 >= 0.0) && ((hard & 2) != 0 || w1 >= 0.0) && ((hard & 4) != 0 || w2 >= 0.0);
    float edgeDistance = 1e6;
    if ((hard & 1) == 0) edgeDistance = min(edgeDistance, distanceToSegment(p, p0, p1));
    if ((hard & 2) == 0) edgeDistance = min(edgeDistance, distanceToSegment(p, p1, p2));
    if ((hard & 4) == 0) edgeDistance = min(edgeDistance, distanceToSegment(p, p2, p0));

    float signedDistance = inside ? -edgeDistance : edgeDistance;
    // Half a pixel either side of the edge, as lines and circles: the shape keeps its size.
    float opacity = saturate(0.5 - signedDistance);
    if (opacity <= (1.0 / 512.0))
        discard;

    vec2 triHalfSize = max(inSurface.triSize * 0.5, vec2(1.0, 1.0));
    vec2 localPos = (p - inSurface.triMin) - triHalfSize;


    vec2 boxSize = max(max(p0,p1),p2) - min(min(p0, p1), p2);

    vec4 weights = vec4(w0, w1, w2, (w0+w1+w2)/3);

    // The incircle's radius is the deepest a point gets: twice the area over the perimeter.
    float perimeter = length(p1 - p0) + length(p2 - p1) + length(p0 - p2);
    float inradius  = abs(area) / max(perimeter, 0.0001);
    float depth     = saturate(-signedDistance / max(inradius, 0.0001));
    vec4 color = getSDFColor(int(triangle.colorIndex), localPos, triHalfSize, depth, inSurface.textureIndex, boxSize, weights);

    color.a *= opacity;
    if (color.a <= (1.0 / 512.0))
        discard;
        
    color.a = clamp(color.a, 0, 1);

    // pre-multiply alpha
    color.rgb *= color.a;

    outFragColor = color * inOpacity;   // premultiplied: the whole color fades
}
