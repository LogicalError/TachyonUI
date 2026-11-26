// Lines: straight or a cubic curve (as two quadratics), with a width, dashes and a gradient along it.
#include "all_common.slang"
#include "sdf_color.slang"

struct VSOutput
{
    float4 position : SV_Position;
    nointerpolation int    colorIndex     : COLOR_INDEX;
    nointerpolation float2 p0             : P0;          // control points in the quad's own pixels, like uv
    nointerpolation float2 p1             : P1;
    nointerpolation float2 p2             : P2;
    nointerpolation float2 p3             : P3;
    nointerpolation float2 lineSize       : LINE_SIZE;
    nointerpolation float  lineWidth      : LINE_WIDTH;  // screen pixels
    nointerpolation float  dashSize       : DASH_SIZE;
    nointerpolation float  dashOffset     : DASH_OFFSET;
    nointerpolation float  gradientOffset : GRADIENT_OFFSET;
    nointerpolation float  gradientScale  : GRADIENT_SCALE;
    nointerpolation float  opacity        : OPACITY;
    float2 uv : UV;
};

[shader("vertex")]
VSOutput vertexMain(VertexInput vertex)
{
    const int vertexIndex = VertexIndex(vertex);
    Line line = LoadPrimitive<Line>(kLineSlot, vertexIndex / 4);
    float2 uv = QuadCorner(vertexIndex % 4); // the corner, from the index buffer

    float outerLineWidth = max(0, line.lineWidth);

    float scaleX = length(float2(line.transform0.x, line.transform1.x));
    float scaleY = length(float2(line.transform0.y, line.transform1.y));
    float scale  = sqrt(scaleX * scaleY);

    // Transform all four control points to screen space, then AABB.
    // AABB-after-transform is the correct screen bounding box under any affine
    // transform; transform-after-AABB would tilt for non-axis-aligned cases.
    const float2 lp0 = line.P0(), lp1 = line.P1(), lp2 = line.P2(), lp3 = line.P3();
    float3 p0_t = ApplyTransform(line.transform0, line.transform1, float3(lp0, 1));
    float3 p1_t = ApplyTransform(line.transform0, line.transform1, float3(lp1, 1));
    float3 p2_t = ApplyTransform(line.transform0, line.transform1, float3(lp2, 1));
    float3 p3_t = ApplyTransform(line.transform0, line.transform1, float3(lp3, 1));
    float2 p0s = p0_t.xy / p0_t.z;
    float2 p1s = p1_t.xy / p1_t.z;
    float2 p2s = p2_t.xy / p2_t.z;
    float2 p3s = p3_t.xy / p3_t.z;

    // Snap axis-aligned (horizontal/vertical) lines to the perpendicular
    // pixel boundary. Half-pixel offset for odd effective widths (1, 3, 5 px)
    // centers the stroke on a pixel; integer for even widths puts the edges
    // on pixel boundaries. Bezier curves render unsnapped at sub-pixel.
    // Axis-aligned only when all four control points share the coordinate (2-point
    // lines arrive as p0,p0,p1,p1); symmetric curves can match on endpoints and
    // midpoints alone and must not be flattened.
    bool axisX = (lp0.y == lp1.y) && (lp1.y == lp2.y) && (lp2.y == lp3.y);
    bool axisY = (lp0.x == lp1.x) && (lp1.x == lp2.x) && (lp2.x == lp3.x);
    if (axisX || axisY)
    {
        float effW       = max(line.lineWidth * scale, 1.0);
        float snapOffset = (glslMod(floor(effW), 2.0) == 1.0) ? 0.5 : 0.0;
        if (axisX) {
            float sy = floor(p0s.y) + snapOffset;
            p0s.y = sy; p1s.y = sy; p2s.y = sy; p3s.y = sy;
        } else {
            float sx = floor(p0s.x) + snapOffset;
            p0s.x = sx; p1s.x = sx; p2s.x = sx; p3s.x = sx;
        }
    }

    float2 lineMinS = min(min(p0s, p1s), min(p2s, p3s));
    float2 lineMaxS = max(max(p0s, p1s), max(p2s, p3s));

    float2 pos  = lineMinS - (outerLineWidth * 0.5);
    float2 size = (lineMaxS - pos) + outerLineWidth;

    float2 clipMin = max(float2(line.clipMin), pos);
    float2 clipMax = min(float2(line.clipMax), pos + size);

    // clip the position in (0,1) space
    float2 clipped_uv = clamp(uv * size, clipMin - pos, clipMax - pos);
    float2 position   = (pos + clipped_uv) + 0.5; // pixel-center alignment

    VSOutput output;
    output.uv       = clipped_uv;
    output.lineSize = size;
    // Endpoints in quad-local screen space - same space as uv - so the
    // fragment shader's curve/line distance math stays consistent.
    output.p0 = p0s - pos;
    output.p1 = p1s - pos;
    output.p2 = p2s - pos;
    output.p3 = p3s - pos;
    output.dashSize       = line.dashSize;
    output.lineWidth      = line.lineWidth;
    output.dashOffset     = line.dashOffset;
    output.colorIndex     = line.colorIndex;
    output.gradientOffset = line.gradientOffset;
    output.gradientScale  = line.gradientScale;
    output.position       = PrimitivePosition(position, line.transform0.w);
    output.opacity        = PrimitiveOpacity(line.transform1);
    return output;
}

float2 Bprime(float2 P0, float2 P1, float2 P2, float t)
{
    return 2.0 * (1.0 - t) * (P1 - P0) + 2.0 * t * (P2 - P1);
}

float arcLengthEstimate(float2 P0, float2 P1, float2 P2, float t)
{
    float t_half = t * 0.5;

    float len0 = length(Bprime(P0, P1, P2, 0.0));
    float len1 = length(Bprime(P0, P1, P2, t_half));
    float len2 = length(Bprime(P0, P1, P2, t));

    return (t / 6.0) * (len0 + 4.0 * len1 + len2);
}

// Depth is only tested here (occluders wrote it), so test it before shading.
[earlydepthstencil]
[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float2 uv = input.uv;

    float2 P0 = input.p0;
    float2 P1 = input.p1;
    float2 P3 = input.p2;
    float2 P4 = input.p3;

    float distance, t, totalLen;

    // Check if all four control points are collinear - covers both the degenerate
    // 2-point case (P1==P0, P3==P4) and any manually-placed straight-line bezier.
    float2 v1   = P1 - P0;
    float2 v2   = P4 - P0;
    float cross = abs(v1.x * v2.y - v1.y * v2.x);

    if (cross < 0.1)
    {
        // Straight line: exact perpendicular segment distance.
        float2 dir = P4 - P0;
        totalLen = length(dir);
        if (totalLen < 0.001)
        {
            distance = length(uv - P0);
            t = 0.0;
        }
        else
        {
            float2 dirN = dir / totalLen;
            float along = clamp(dot(uv - P0, dirN), 0.0, totalLen);
            distance = length((uv - P0) - dirN * along);
            t = along;
        }
    }
    else
    {
        // Curved line: split into two quadratic Bezier segments.
        float2 P2 = (P1 + P3) * 0.5;
        float t1, t2;

        float d1 = sdBezierExact(uv, P0, P1, P2, t1);
        float d2 = sdBezierExact(uv, P2, P3, P4, t2);

        float totalLen1 = arcLengthEstimate(P0, P1, P2, 1.0);
        float totalLen2 = arcLengthEstimate(P2, P3, P4, 1.0);
        totalLen = totalLen1 + totalLen2;

        t = (d1 < d2) ?
                arcLengthEstimate(P0, P1, P2, t1) :
                (totalLen1 + arcLengthEstimate(P2, P3, P4, t2));
        t = clamp(t, 0.0, totalLen);

        distance = min(d1, d2);
    }

    // AA falloff convention: extend the SDF cutoff by half a pixel so a
    // 1-px line renders as one fully-opaque pixel with sub-pixel AA on
    // each side. lineWidth is in screen pixels (the SDF distance is
    // screen-space), so a 1-px line stays 1 screen px wide at any zoom.
    float lineWidth = max(0.0, input.lineWidth);
    float opacity   = saturate(0.5 + (lineWidth * 0.5) - distance);

    // apply dash
    float dashSize = input.dashSize;
    if (dashSize > 1)
    {
        float pixel_smooth_range = 0.5;

        float offset = t + dashSize + input.dashOffset;

        float pos = glslMod(offset, (2 * dashSize));
        float m   = abs(pos - dashSize) - (0.5 * dashSize);
        opacity *= saturate((pixel_smooth_range + m) / (2 * pixel_smooth_range));
    }

    // Map arc-length fraction into the Y axis of the colour lookup so gradients
    // follow the curve rather than the bounding-box axes. gradientOffset/Scale
    // let callers stitch multiple segments into one sweep.
    float lineT = (totalLen > 0.0) ? (t / totalLen) : 0.0;
    lineT = input.gradientOffset + lineT * input.gradientScale;
    float2 halfSize = input.lineSize * 0.5;
    float2 cpos     = float2(uv.x - input.lineSize.x * 0.5,
                             (lineT - 0.5) * input.lineSize.y);
    float  depth    = saturate(1.0 - distance / max(lineWidth * 0.5, 0.5));
    float4 color    = getSDFColor(input.colorIndex, cpos, halfSize, depth, 0, input.lineSize, float4(0));

    // make everything outside the line invisible
    color.a *= opacity;

    if (color.a <= (1.0 / 512))
        discard;

    color.a = clamp(color.a, 0, 1);

    // pre-multiply alpha
    color.rgb *= color.a;

    return color * input.opacity;   // premultiplied: the whole color fades
}
