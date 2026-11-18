// Boxes: rounded rects with per-edge borders, dashes, and up to four state styles mixed.
#include "all_common.slang"
#include "sdf_color.slang"

struct VSOutput
{
    float4 position : SV_Position;
    nointerpolation int4   styleIndex     : STYLE_INDEX;
    nointerpolation float2 boxSize        : BOX_SIZE;
    nointerpolation int    textureIndex   : TEXTURE_INDEX;
    nointerpolation float  transformScale : TRANSFORM_SCALE; // geometric mean of the content transform's X and Y scale factors
    nointerpolation float4 blendFactors   : BLEND_FACTORS;
    nointerpolation int    colorIndex     : COLOR_INDEX;     // per-element ColorStyle override (0 = use box style's background)
    nointerpolation float2 axisScale      : AXIS_SCALE;      // the transform's X and Y scale (UI units -> pixels)
    nointerpolation float  opacity        : OPACITY;
    float2 uv : UV;
};

[shader("vertex")]
VSOutput vertexMain(VertexInput vertex)
{
    const int vertexIndex = VertexIndex(vertex);
    Box box = LoadPrimitive<Box>(kBoxSlot, vertexIndex / 4);
    float2 uv = QuadCorner(vertexIndex % 4); // the corner, from the index buffer

    // transformed box
    float3 boxMin = float3(float2(box.pos), 1);
    float3 boxMax = boxMin + float3(float2(box.size), 0);
    boxMin = ApplyTransform(box.transform0, box.transform1, boxMin);
    boxMax = ApplyTransform(box.transform0, box.transform1, boxMax);

    float2 pos  = (boxMin.xy / boxMin.z);
    float2 size = (boxMax.xy / boxMax.z) - pos;

    // The drawn box in whole pixels: snapped relative to its parent when the CPU
    // supplies it (gui_pixel_snap.h), else each edge to the nearest pixel (half up).
    // A non-empty box keeps at least one pixel.
    int4   parentSnapped = int4(box.snappedPx);
    bool   hasSnapped    = parentSnapped.z > parentSnapped.x || parentSnapped.w > parentSnapped.y;
    float2 snappedMin    = hasSnapped ? float2(parentSnapped.xy) : floor(pos + 0.5);
    float2 snappedMax    = hasSnapped ? float2(parentSnapped.zw) : floor(pos + size + 0.5);
    snappedMax           = max(snappedMax, snappedMin + float2(size > float2(0)));
    float2 snappedSize   = snappedMax - snappedMin;

    float2 corner   = clamp(lerp(snappedMin, snappedMax, uv), float2(box.clipMin), float2(box.clipMax));
    float2 position = corner + 0.5; // pixel-center alignment

    float2 clipped_uv = (corner - snappedMin) / max(snappedSize, float2(1));

    // Extract the content transform's scale as the geometric mean of X and Y axis lengths.
    // This is 1.0 for identity (no zoom), and equals the zoom factor for uniform scaling.
    float scaleX = length(float2(box.transform0.x, box.transform1.x));
    float scaleY = length(float2(box.transform0.y, box.transform1.y));

    VSOutput output;
    output.uv             = clipped_uv;
    output.boxSize        = snappedSize;
    output.axisScale      = float2(scaleX, scaleY);
    output.transformScale = sqrt(scaleX * scaleY);
    // The state styles mixed, each with its share.
    for (int i = 0; i < 4; i++)
    {
        output.styleIndex[i]   = int(box.styles[i] & 0xFFFFFFu);
        output.blendFactors[i] = float(box.styles[i] >> 24) / 255.0;
    }
    output.textureIndex = box.textureIndex;
    output.colorIndex   = int(box.colorIndex);
    output.position     = PrimitivePosition(position, box.transform0.w);
    output.opacity      = PrimitiveOpacity(box.transform1);
    return output;
}

float sdf_corner(float2 p, float2 b, float radius)
{
    float2 q = (p - b) + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

float sdf_corner_inner(float2 p, float2 b, float radius, float vert_width, float horz_width)
{
    float maxOffset = max(vert_width, horz_width);
    float2 offset = float2(max(0, maxOffset - horz_width), max(0, maxOffset - vert_width));
    return sdf_corner(p - offset, b, radius) + maxOffset;
}

float sdf_rounded_inner(float2 p, float2 b, float4 cornerRadius, float4 borderWidth)
{
    float tl = cornerRadius.x; // top-left corner radius
    float tr = cornerRadius.y; // top-right corner radius
    float br = cornerRadius.z; // bottom-right corner radius
    float bl = cornerRadius.w; // bottom-left corner radius

    float topBorderWidth    = borderWidth.x;
    float rightBorderWidth  = borderWidth.y;
    float bottomBorderWidth = borderWidth.z;
    float leftBorderWidth   = borderWidth.w;

    return max(
                max(
                    sdf_corner_inner( float2(-p.x,  p.y), b, tl, topBorderWidth, leftBorderWidth),
                    sdf_corner_inner( float2( p.x,  p.y), b, tr, topBorderWidth, rightBorderWidth)
                    ),
                max(
                    sdf_corner_inner( float2(-p.x, -p.y), b, bl, bottomBorderWidth, leftBorderWidth),
                    sdf_corner_inner( float2( p.x, -p.y), b, br, bottomBorderWidth, rightBorderWidth)
                ));
}

float sdf_rounded(float2 p, float2 b, float4 cornerRadius)
{
    float tl = cornerRadius.x; // top-left corner radius
    float tr = cornerRadius.y; // top-right corner radius
    float br = cornerRadius.z; // bottom-right corner radius
    float bl = cornerRadius.w; // bottom-left corner radius
    return max(
                max(
                    sdf_corner( float2( p.x,  p.y), b, tr),
                    sdf_corner( float2(-p.x,  p.y), b, tl)
                    ),
                max(
                    sdf_corner( float2( p.x, -p.y), b, br),
                    sdf_corner( float2(-p.x, -p.y), b, bl)
                ));
}

struct CurrentStyle
{
    float4 borderColor;
    float4 backgroundColor;

    float4 cornerRadius;
    float4 borderWidth;

    float dashSize;
    float dashOffset;
};

void AddPreStyle(inout CurrentStyle currentStyle, float weight, int styleIndex)
{
    if (styleIndex < 0) return;
    BoxStyle boxStyle = LoadBoxStyle(styleIndex);

    currentStyle.cornerRadius += weight * float4(boxStyle.cornerRadius);
    currentStyle.borderWidth  += weight * float4(boxStyle.borderWidth);
    currentStyle.dashSize     += weight * float(boxStyle.borderDashSize);
    currentStyle.dashOffset   += weight * float(boxStyle.borderDashOffset);
}

// A state's border and background colors at this fragment, straight (not premultiplied).
void StateColors(float2 uv, float2 sdfUv, float2 uvsize, float edgeDepth, int styleIndex, float4 weights,
                 int textureIndex, float2 boxSize, out float4 border, out float4 background)
{
    border = float4(0);
    background = float4(0);
    if (styleIndex < 0) return;
    BoxStyle boxStyle = LoadBoxStyle(styleIndex);

    // Select border color based on which edge the fragment is closest to.
    // Regions are divided by diagonals from the center (same convention as CSS):
    //   uv.y > 0 = top, uv.y < 0 = bottom, uv.x > 0 = right, uv.x < 0 = left.
    int16_t edgeColorIndex;
    float2 side = sdfUv / uvsize; // -1..1 from the center, y up
    if (abs(side.y) >= abs(side.x))
        edgeColorIndex = (side.y >= 0.0) ? boxStyle.borderColorIndex.x   // top
                                         : boxStyle.borderColorIndex.z;  // bottom
    else
        edgeColorIndex = (side.x > 0.0)  ? boxStyle.borderColorIndex.y   // right
                                         : boxStyle.borderColorIndex.w;  // left

    border     = getSDFColor(int(edgeColorIndex), uv, uvsize, edgeDepth, textureIndex, boxSize, weights);
    background = getSDFColor(int(boxStyle.backgroundColorIndex), uv, uvsize, edgeDepth, textureIndex, boxSize, weights);
}

// Depth is only tested here (occluders wrote it), so test it before shading.
[earlydepthstencil]
[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float  pixelrange = 1.0 / min(input.boxSize.x, input.boxSize.y);
    float  half_pixelrange = pixelrange * 0.5;
    float2 uvsize = input.boxSize * half_pixelrange;
    float2 uv     = (((input.uv - 0.5) * 2) * uvsize) + half_pixelrange; // y down, as on screen
    float2 sdfUv  = float2(uv.x, -uv.y); // the SDF helpers use y up (top = +y)

    float transformed_pixelrange = input.transformScale * pixelrange;

    CurrentStyle currentStyle;
    currentStyle.borderColor     = float4(0);
    currentStyle.backgroundColor = float4(0);
    currentStyle.cornerRadius    = float4(0);
    currentStyle.borderWidth     = float4(0);
    currentStyle.dashSize        = 0.0;
    currentStyle.dashOffset      = 0.0;

    // Blend geometry (cornerRadius, borderWidth) across the states mixed, by their shares.
    // When not transitioning only slot 0 has a share.
    for (int i = 0; i < 4; i++)
        if (input.blendFactors[i] > 0.0)
            AddPreStyle(currentStyle, input.blendFactors[i], input.styleIndex[i]);

    // Corner radii scale with the transform. Border widths are whole pixels, the
    // same everywhere: children snap relative to their parent's snapped edges by
    // the same rounding, so content against a border meets its inner edge.
    currentStyle.cornerRadius *= transformed_pixelrange;
    {
        float4 bw = currentStyle.borderWidth; // top, right, bottom, left in UI units
        float2 s  = input.axisScale;
        float4 px = floor(bw * float4(s.y, s.x, s.y, s.x) + 0.5);
        px = select(bw > float4(0.0), max(px, float4(1.0)), float4(0.0));
        currentStyle.borderWidth = px * pixelrange;
    }

    // sdf distance for rounded box
    float outer_distance, inner_distance, outer_opacity;

    outer_distance = sdf_rounded(sdfUv, uvsize, currentStyle.cornerRadius);

    outer_opacity = clamp(1 - (outer_distance / pixelrange), 0, 1);
    if (outer_opacity <= (1.0 / 512))
        discard;

    // Shift the inner-SDF edge in by half a pixel only for edges with a border, so a
    // 1-pixel border is one solid pixel. Zero-width edges must not shift, or they show
    // a phantom half-opacity border.
    float4 borderSnap = select(currentStyle.borderWidth > float4(0.0), float4(half_pixelrange), float4(0.0));
    inner_distance    = sdf_rounded_inner(sdfUv, uvsize, currentStyle.cornerRadius,
                                          currentStyle.borderWidth + borderSnap);

    float inner_factor  = clamp(1 - (inner_distance / pixelrange), 0, 1);
    float inner_opacity = min(outer_opacity, inner_factor);

    // Border share of the fragment's covered area. (1 - inner_opacity) would also
    // count the outer AA falloff and fringe edges with border color.
    float border_opacity = max(outer_opacity - inner_opacity, 0.0);
    float border_factor  = (outer_opacity > 1e-5) ? (border_opacity / outer_opacity) : 0.0;

    // Dashed border: modulate border_factor by a dash pattern along the box perimeter.
    if (currentStyle.dashSize > 0.5) {
        // Determine axis-aligned edge and compute pixel position along that edge.
        float2 side = abs(sdfUv / uvsize); // 0..1 from center on each axis
        float edgePos = (side.y >= side.x)
            ? input.uv.x * input.boxSize.x   // top or bottom: horizontal position
            : input.uv.y * input.boxSize.y;  // left or right: vertical position
        float dashPos  = glslMod(edgePos + currentStyle.dashOffset, 2.0 * currentStyle.dashSize);
        float dashMask = abs(dashPos - currentStyle.dashSize) - 0.5 * currentStyle.dashSize;
        const float dashSmooth = 0.5;
        border_factor *= saturate((dashMask + dashSmooth) / (2.0 * dashSmooth));
    }

    float u = input.uv.x;
    float v = input.uv.y;
    float4 weights = float4(
        (1.0 - u) * (1.0 - v),   // TL (pixel 0)
        u * (1.0 - v),           // TR (pixel 1)
        u * v,                   // BR (pixel 2)
        (1.0 - u) * v            // BL (pixel 3)
    );

    // Distances are in units of the box's smaller side, so its middle is half a unit deep.
    float edgeDepth = saturate(-outer_distance * 2.0);

    // Blend colors across the states mixed (BlendStateColor), each into what the ones before
    // made by its share of all so far; one state alone as it is.
    float shareSoFar = 0.0;
    for (int i = 0; i < 4; i++)
    {
        float share = input.blendFactors[i];
        if (share <= 0.0)
            continue;
        float4 border, background;
        StateColors(uv, sdfUv, uvsize, edgeDepth, input.styleIndex[i], weights, input.textureIndex, input.boxSize,
                    border, background);
        if (shareSoFar <= 0.0)
        {
            currentStyle.borderColor     = border;
            currentStyle.backgroundColor = background;
        }
        else
        {
            float t = share / (shareSoFar + share);
            currentStyle.borderColor     = BlendStateColor(currentStyle.borderColor, border, t);
            currentStyle.backgroundColor = BlendStateColor(currentStyle.backgroundColor, background, t);
        }
        shareSoFar += share;
    }

    // Per-element ColorStyle override: replaces the box style's background entirely so
    // a swatch element can paint a solid color or a gradient via colorIndex.
    if (input.colorIndex != 0)
        currentStyle.backgroundColor = getSDFColor(input.colorIndex, uv, uvsize, edgeDepth,
                                                   input.textureIndex, input.boxSize, weights);

    // Mix border and background. Opaque colors mix in linear light; a translucent one
    // mixes premultiplied in sRGB, the space the result composites in, so where it meets
    // the border (rounded corners) it never shows more color than either side.
    float4 color;
    if (currentStyle.backgroundColor.a >= 1.0 && currentStyle.borderColor.a >= 1.0)
    {
        color = lerp(srgbToLinear(currentStyle.backgroundColor), srgbToLinear(currentStyle.borderColor), border_factor);
        color.rgb = linearToSrgb(color.rgb);
    }
    else
    {
        const float4 background_color = float4(currentStyle.backgroundColor.rgb * currentStyle.backgroundColor.a, currentStyle.backgroundColor.a);
        const float4 border_color     = float4(currentStyle.borderColor.rgb * currentStyle.borderColor.a, currentStyle.borderColor.a);
        color = lerp(background_color, border_color, border_factor);
        if (color.a > 1e-5)
            color.rgb /= color.a;
    }

    // make everything outside the rounded box invisible
    color.a *= outer_opacity;

    if (color.a <= 1e-5)
        discard;

    color.a = clamp(color.a, 0, 1);

    // pre-multiply alpha
    color.rgb *= color.a;

    return color * input.opacity;   // premultiplied: the whole color fades
}
