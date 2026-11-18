// Dots: a filled circle, drawn in a triangle around it.
#include "all_common.slang"

static const float kMinThickness = 1.5;

// An equilateral triangle around a circle of radius 1: its corners are twice the
// radius out, at the top and lower right and left.
static const float2 kCorners[3] = { float2(0.0, -2.0), float2(1.7320508, 1.0), float2(-1.7320508, 1.0) };

struct VSOutput
{
    float4 position : SV_Position;
    nointerpolation float4 color   : COLOR;
    nointerpolation float4 clip    : CLIP;    // the clip rect in window pixels: min x, min y, max x, max y
    nointerpolation float  dotSize : DOT_SIZE;
    nointerpolation float  opacity : OPACITY;
    float2 uv        : UV;
    float2 windowPos : WINDOW_POS;             // window pixels before the world matrix, which moves cached draws
};

[shader("vertex")]
VSOutput vertexMain(VertexInput vertex)
{
    const int vertexIndex = VertexIndex(vertex);
    Dot dot = LoadPrimitive<Dot>(kDotSlot, vertexIndex / 3);

    // Apply the renderer's content transform to position and size, like the line shader.
    float3 dotmin3 = ApplyTransform(dot.transform0, dot.transform1, float3(dot.pos - float2(dot.size * 0.5), 1.0));
    float3 dotmax3 = ApplyTransform(dot.transform0, dot.transform1, float3(dot.pos + float2(dot.size * 0.5), 1.0));
    float2 dotmin  = dotmin3.xy / dotmin3.z;
    float2 dotmax  = dotmax3.xy / dotmax3.z;
    float2 center  = (dotmin + dotmax) * 0.5;

    // On-screen size, so the fragment shader's AA edge matches the transformed radius.
    float dotSize = dotmax.x - dotmin.x;
    // The triangle holds the dot and its anti-aliased edge.
    float radius  = max(kMinThickness, dotSize) * 0.5 + 1.0;

    // Corners aren't clamped to the clip rect, which would bend the triangle; the
    // fragment shader clips instead.
    float2 corner = center + kCorners[vertexIndex % 3] * radius;

    VSOutput output;
    output.uv        = corner - center;
    output.windowPos = corner;
    output.dotSize   = dotSize;
    output.color     = dot.color;
    output.clip      = float4(dot.clipMin, dot.clipMax);
    output.position  = PrimitivePosition(corner + 0.5, dot.transform0.w);
    output.opacity   = PrimitiveOpacity(dot.transform1);
    return output;
}

// Depth is only tested here (occluders wrote it), so test it before shading.
[earlydepthstencil]
[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    // Clipped here, not by moving the triangle's corners; in window pixels before the world
    // matrix, as the clip rect is.
    float2 pixel = floor(input.windowPos + 0.5);
    if (any(pixel < input.clip.xy) || any(pixel >= input.clip.zw))
        discard;

    float2 uv = input.uv;

    // sdf distance for dot
    float distance = length(uv);
    float dotSize = input.dotSize;
    float thickness = max(kMinThickness, dotSize);
    float opacity  = clamp((thickness * 0.5) - distance, 0, 1);

    float thickness_leftover = clamp(thickness - dotSize, 0, kMinThickness);
    float thickness_alpha    = 1 - (thickness_leftover / kMinThickness);

    opacity *= thickness_alpha;

    float4 color = input.color;

    // make everything outside the dot invisible
    color.a *= opacity;

    if (opacity <= (1.0 / 512))
        discard;

    color.a = clamp(color.a, 0, 1);

    // pre-multiply alpha
    color.rgb *= color.a;

    return color * input.opacity;   // premultiplied: the whole color fades
}
