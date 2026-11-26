// Triangles: drawn in their bounding quad, soft-edged except along edges shared with a neighbour.
#include "all_common.slang"
#include "sdf_color.slang"

struct VSOutput
{
    float4 position : SV_Position;
    nointerpolation float2 triMin        : TRI_MIN;
    nointerpolation float2 triSize       : TRI_SIZE;
    nointerpolation int    triangleIndex : TRIANGLE_INDEX;
    nointerpolation int    textureIndex  : TEXTURE_INDEX;
    nointerpolation float  opacity       : OPACITY;
    float2 windowPos : WINDOW_POS; // the fragment's window pixel, as the corners are computed
};

// The triangle's corners through its transform, in window pixels.
void TransformedCorners(Triangle triangle, out float2 p0, out float2 p1, out float2 p2)
{
    float3 p0_t = ApplyTransform(triangle.transform0, triangle.transform1, float3(triangle.p0, 1));
    float3 p1_t = ApplyTransform(triangle.transform0, triangle.transform1, float3(triangle.p1, 1));
    float3 p2_t = ApplyTransform(triangle.transform0, triangle.transform1, float3(triangle.p2, 1));
    p0 = p0_t.xy / p0_t.z;
    p1 = p1_t.xy / p1_t.z;
    p2 = p2_t.xy / p2_t.z;
}

[shader("vertex")]
VSOutput vertexMain(VertexInput vertex)
{
    const int vertexIndex = VertexIndex(vertex);
    int triangleIndex = vertexIndex / 4; // the bounding quad; the fragment shader tests the interior
    Triangle triangle = LoadPrimitive<Triangle>(kTriangleSlot, triangleIndex);
    float2 uv = QuadCorner(vertexIndex % 4); // the corner, from the index buffer

    // Apply the renderer's content transform, like the box/line/dot shaders.
    float2 p0, p1, p2;
    TransformedCorners(triangle, p0, p1, p2);

    float2 triMin  = min(min(p0, p1), p2);
    float2 triMax  = max(max(p0, p1), p2);
    float2 triSize = max(triMax - triMin, float2(1, 1));

    // The quad drawn: a pixel past the triangle all round, for the soft edge outside it.
    float2 quadMin  = triMin - 1.0;
    float2 quadSize = (triMax + 1.0) - quadMin;

    float2 clipMin = max(float2(triangle.clipMin), quadMin);
    float2 clipMax = min(float2(triangle.clipMax), quadMin + quadSize);

    float2 clipped_uv = clamp(uv * quadSize, clipMin - quadMin, clipMax - quadMin);
    float2 position   = quadMin + clipped_uv + 0.5;

    VSOutput output;
    output.triMin        = triMin;
    output.triSize       = triSize;
    output.triangleIndex = triangleIndex;
    output.textureIndex  = triangle.textureIndex;
    output.windowPos     = position;
    output.position      = PrimitivePosition(position, triangle.transform0.w);
    output.opacity       = PrimitiveOpacity(triangle.transform1);
    return output;
}

float distanceToSegment(float2 p, float2 a, float2 b)
{
    float2 pa = p - a;
    float2 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

// Depth is only tested here (occluders wrote it), so test it before shading.
[earlydepthstencil]
[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    Triangle triangle = LoadPrimitive<Triangle>(kTriangleSlot, input.triangleIndex);

    // Match the vertex shader: the interior test runs in transformed (screen)
    // space, since windowPos is the transformed quad position.
    float2 p0, p1, p2;
    TransformedCorners(triangle, p0, p1, p2);
    float2 p = input.windowPos;

    float area = cross2(p1 - p0, p2 - p0);
    if (abs(area) < 0.0001)
        discard;

    float w0 = cross2(p1 - p0, p - p0) / area;
    float w1 = cross2(p2 - p1, p - p1) / area;
    float w2 = cross2(p0 - p2, p - p2) / area;

    // A hard edge (shared with a neighbour) cuts straight; only the others are soft.
    // (A pixel's middle right on it is kept on both sides, despite rounding.)
    int hard = triangle.HardEdges();
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

    float2 triHalfSize = max(input.triSize * 0.5, float2(1.0, 1.0));
    float2 localPos    = (p - input.triMin) - triHalfSize;

    float2 boxSize = max(max(p0, p1), p2) - min(min(p0, p1), p2);

    float4 weights = float4(w0, w1, w2, (w0 + w1 + w2) / 3);

    // The incircle's radius is the deepest a point gets: twice the area over the perimeter.
    float perimeter = length(p1 - p0) + length(p2 - p1) + length(p0 - p2);
    float inradius  = abs(area) / max(perimeter, 0.0001);
    float depth     = saturate(-signedDistance / max(inradius, 0.0001));
    float4 color = getSDFColor(triangle.ColorIndex(), localPos, triHalfSize, depth, input.textureIndex, boxSize, weights);

    color.a *= opacity;
    if (color.a <= (1.0 / 512.0))
        discard;

    color.a = clamp(color.a, 0, 1);

    // pre-multiply alpha
    color.rgb *= color.a;

    return color * input.opacity;   // premultiplied: the whole color fades
}
