// Occluders: opaque rects that write depth first, so what's drawn under them is skipped.
#include "all_common.slang"

struct Occluder
{
    float4 transform0;
    float4 transform1;
    int2   clipMin;
    int2   clipMax;

    int2   pos;
    int2   size;
};

[shader("vertex")]
float4 vertexMain(VertexInput vertex) : SV_Position
{
    const int vertexIndex = VertexIndex(vertex);
    // Occluders share the box slot of the push constants.
    Occluder occluder = LoadPrimitive<Occluder>(kBoxSlot, vertexIndex / 4);
    float2 corner = QuadCorner(vertexIndex % 4); // the corner, from the index buffer

    // The rect under the primitive's transform, kept inside its clip rect. Only a
    // pixel's center inside the rect is covered, so its edge pixels aren't.
    float3 rectMin = ApplyTransform(occluder.transform0, occluder.transform1, float3(float2(occluder.pos), 1));
    float3 rectMax = ApplyTransform(occluder.transform0, occluder.transform1, float3(float2(occluder.pos + occluder.size), 1));
    float2 lo = min(rectMin.xy / rectMin.z, rectMax.xy / rectMax.z);
    float2 hi = max(rectMin.xy / rectMin.z, rectMax.xy / rectMax.z);
    lo = max(lo, float2(occluder.clipMin));
    hi = max(min(hi, float2(occluder.clipMax)), lo);

    return PrimitivePosition(lerp(lo, hi, corner), occluder.transform0.w);
}

// Writes depth only: the pipeline masks the color out. Magenta, should it ever not.
[shader("fragment")]
float4 fragmentMain() : SV_Target
{
    return float4(1, 0, 1, 1);
}
