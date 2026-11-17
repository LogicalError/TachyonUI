// Circles: a ring of a line's width, dashed and graded as lines are.
#include "all_common.slang"
#include "sdf_color.slang"

struct Circle
{
    float4 transform0;
    float4 transform1;
    int2   clipMin;
    int2   clipMax;

    float2 center;
    float  radius;
    float  lineWidth;
    int    colorIndex;
    float  dashSize;
    float  dashOffset;
    float  gradientOffset;
    float  gradientScale;
    float3 _padTo96;     // std430 rounds the struct up to 16 bytes
};

struct VSOutput
{
    float4 position : SV_Position;
    nointerpolation int    colorIndex     : COLOR_INDEX;
    nointerpolation float2 center         : CENTER;     // in the quad's own pixels, like uv
    nointerpolation float2 quadSize       : QUAD_SIZE;
    nointerpolation float  radius         : RADIUS;     // screen pixels
    nointerpolation float  lineWidth      : LINE_WIDTH; // screen pixels
    nointerpolation float  dashSize       : DASH_SIZE;
    nointerpolation float  dashOffset     : DASH_OFFSET;
    nointerpolation float  gradientOffset : GRADIENT_OFFSET;
    nointerpolation float  gradientScale  : GRADIENT_SCALE;
    nointerpolation float  opacity        : OPACITY;
    float2 uv : UV;
};

static const float kTau = 6.28318530718;

[shader("vertex")]
VSOutput vertexMain(int vertexIndex : SV_VulkanVertexID)
{
    // Circles share the line slot of the push constants.
    Circle circle = LoadPrimitive<Circle>(kLineSlot, vertexIndex / 4);
    float2 corner = QuadCorner(vertexIndex % 4); // the corner, from the index buffer

    // The center and radius in screen pixels; the radius follows the transform's
    // uniform scale, the line width stays in screen pixels, as for lines.
    float3 center_t = ApplyTransform(circle.transform0, circle.transform1, float3(circle.center, 1));
    float2 center   = center_t.xy / center_t.z;
    float  scaleX   = length(float2(circle.transform0.x, circle.transform1.x));
    float  scaleY   = length(float2(circle.transform0.y, circle.transform1.y));
    float  radius   = circle.radius * sqrt(scaleX * scaleY);

    // The square around the ring, a pixel wider for its anti-aliased edge.
    float  reach = radius + max(0.0, circle.lineWidth) * 0.5 + 1.0;
    float2 pos   = center - reach;
    float2 size  = float2(reach * 2.0);

    float2 clipMin = max(float2(circle.clipMin), pos);
    float2 clipMax = min(float2(circle.clipMax), pos + size);

    float2 clipped_uv = clamp(corner * size, clipMin - pos, clipMax - pos);
    float2 position   = (pos + clipped_uv) + 0.5; // pixel-center alignment

    VSOutput output;
    output.uv             = clipped_uv;
    output.colorIndex     = circle.colorIndex;
    output.center         = center - pos;
    output.quadSize       = size;
    output.radius         = radius;
    output.lineWidth      = circle.lineWidth;
    output.dashSize       = circle.dashSize;
    output.dashOffset     = circle.dashOffset;
    output.gradientOffset = circle.gradientOffset;
    output.gradientScale  = circle.gradientScale;
    output.position       = PrimitivePosition(position, circle.transform0.w);
    output.opacity        = PrimitiveOpacity(circle.transform1);
    return output;
}

// Depth is only tested here (occluders wrote it), so test it before shading.
[earlydepthstencil]
[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float2 fromCenter = input.uv - input.center;
    float  distance   = abs(length(fromCenter) - input.radius);

    // As lines: the edge half a pixel past half the width, so a 1-px ring is one opaque
    // pixel with sub-pixel AA on each side.
    float lineWidth = max(0.0, input.lineWidth);
    float opacity   = saturate(0.5 + (lineWidth * 0.5) - distance);

    // How far round, clockwise from the right (+x towards +y): 0 to 1.
    float around = atan2(fromCenter.y, fromCenter.x) / kTau;
    if (around < 0.0)
        around += 1.0;

    float dashSize = input.dashSize;
    if (dashSize > 1)
    {
        float pixel_smooth_range = 0.5;
        float along  = around * kTau * input.radius;
        float offset = along + dashSize + input.dashOffset;
        float pos    = glslMod(offset, (2 * dashSize));
        float m      = abs(pos - dashSize) - (0.5 * dashSize);
        opacity *= saturate((pixel_smooth_range + m) / (2 * pixel_smooth_range));
    }

    // The gradient runs once around, mapped the way lines map it along their length.
    float  lineT = input.gradientOffset + around * input.gradientScale;
    float2 size  = input.quadSize;
    float2 cpos  = float2(input.uv.x - size.x * 0.5, (lineT - 0.5) * size.y);
    float  depth = saturate(1.0 - distance / max(lineWidth * 0.5, 0.5));
    float4 color = getSDFColor(input.colorIndex, cpos, size * 0.5, depth, 0, size, float4(0));

    color.a *= opacity;
    if (color.a <= (1.0 / 512))
        discard;

    color.a = clamp(color.a, 0, 1);
    color.rgb *= color.a;
    return color * input.opacity;   // premultiplied: the whole color fades
}
