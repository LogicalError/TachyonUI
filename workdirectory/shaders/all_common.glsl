layout(buffer_reference, std430)
struct ColorStyle
{
    f16vec4 transformationX;
    f16vec4 transformationY;

    u16vec4 colorUV_type;

    int     textureIndex;
    f16vec2 animSpeed;      // x = U/gradient scroll speed, y = V scroll speed (textures only)
};

layout(buffer_reference, std430)
struct BoxStyle // aka 'StyleState'
{
	u16vec4  borderWidth;
	u16vec4  cornerRadius;

    i16vec4  borderColorIndex;    // per-edge: x=top, y=right, z=bottom, w=left
    int16_t  backgroundColorIndex;
    uint16_t borderDashSize;      // pixels per dash (0 = solid)
    uint16_t borderDashOffset;    // pixel start offset into dash pattern
    uint16_t shadowIndex;         // CPU only
};

layout(buffer_reference, std430)
struct Box
{
    vec4       transform0;
    vec4       transform1;
    ivec2      clipMin;
    ivec2      clipMax;

    ivec2      pos;
    ivec2      size;

    int        textureIndex;
    int16_t    colorIndex;   // Per-element ColorStyle override (0 = use box style's background); supports gradients.
    int16_t    _pad3;
    uint       styles[4];    // up to four state styles mixed: GPU style index in the low 24 bits, share (of 255) in the top 8
    i16vec4    snappedPx;    // window pixels {x0,y0,x1,y1} snapped to the parent; all 0 = snap here
};

layout(buffer_reference, std430)
struct Glyph
{
    vec4       transform0;
    vec4       transform1;
    ivec2      clipMin;
    ivec2      clipMax;
               
    vec2       pos;
    vec2       size;
     
    vec2       uvMin;
    vec2       uvMax;
               
    float      scale;
    u16vec2    glyph_type;
               
    int        textureIndex; // Glyph atlas, not to be confused with ColorStyle texture index
    int16_t    textColorIndex; // ColorStyle index for text color (1-based, 0 = invalid)
    int16_t    shadowBlur;     // -1: the glyph itself; 0 and up: a text shadow blurred by this many 1/8 pixels
}; 

layout(buffer_reference, std430)
struct Dot
{
    vec4       transform0;
    vec4       transform1;
    ivec2      clipMin;
    ivec2      clipMax;

	vec4       color;
    float      size;
    // vec2 (not u16vec2): allows negative / >65535 positions and
    // sub-pixel precision, matching the C++ Dot struct.
    vec2       pos;
};

layout(buffer_reference, std430)
struct Line
{
    vec4       transform0;
    vec4       transform1;
    ivec2      clipMin;
    ivec2      clipMax;
    
    int        colorIndex;
    float      lineWidth;
    float      dashSize;
    float      dashOffset;
    float      gradientOffset;
    float      gradientScale;

    i16vec2    p0, p1, p2, p3;
};

layout(buffer_reference, std430)
struct Triangle
{
    vec4       transform0;
    vec4       transform1;               
    ivec2      clipMin;
    ivec2      clipMax;

    vec2       p0;
    vec2       p1;
    vec2       p2;
    
    int        textureIndex;

    int16_t    colorIndex;
    int16_t    hardEdges;   // bit 0: p0-p1, 1: p1-p2, 2: p2-p0, drawn without a soft edge
};

layout(buffer_reference, std430)
struct Quad
{
    vec4       transform0;
    vec4       transform1;
    ivec2      clipMin;
    ivec2      clipMax;

    ivec2      pos;
    ivec2      size;

    int        textureIndex;
    int16_t    colorIndex;
    int16_t    blur;         // pixels; a shadow: the rect includes the blur on every side
    float      cornerRadius;
    uint       displayFlags; // ImageDisplay: 3 bits a channel's source (red first), bit 12 sRGB-encode, bit 31 on
    vec4       uvRect;       // the texture's part a StretchedTexture stretches over the quad: min.xy, max.zw
    vec4       tint;         // multiplies the color
};

layout(buffer_reference, std430) readonly buffer Boxes  { Box   boxes []; };
layout(buffer_reference, std430) readonly buffer Dots   { Dot   dots  []; };
layout(buffer_reference, std430) readonly buffer Lines  { Line  lines []; };
layout(buffer_reference, std430) readonly buffer Triangles { Triangle triangles []; };
layout(buffer_reference, std430) readonly buffer Quads  { Quad  quads []; };
layout(buffer_reference, std430) readonly buffer Glyphs { Glyph glyphs[]; };
layout(buffer_reference, std430) readonly buffer BoxStyles { BoxStyle boxStyles[]; };
layout(buffer_reference, std430) readonly buffer ColorStyles { ColorStyle colorStyles[]; };

layout( push_constant ) uniform constants
{	
	mat4   worldMatrix;
	Boxes  boxBuffer;
	Glyphs glyphBuffer;
	Dots   dotsBuffer;
	Lines  lineBuffer;
	Triangles triangleBuffer;

	BoxStyles boxStyleBuffer;
	ColorStyles colorStyleBuffer;
	float elapsedTime;  // seconds since app start
	float _timePad;     // pad to 128 bytes
} PushConstants;



float srgbToLinear(float cs) {
    return (cs <= 0.04045f) ? (cs / 12.92f) : pow((cs + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float cl) {
    return (cl <= 0.0031308f) ? (12.92f * cl) : (1.055f * pow(cl, 1.0f / 2.4f) - 0.055f);
}

vec3 srgbToLinear(vec3 srgb) {
    return mix(srgb / 12.92, pow((srgb + 0.055) / 1.055, vec3(2.4)), greaterThan(srgb, vec3(0.04045)));
}

vec3 linearToSrgb(vec3 linear) {
    return mix(12.92 * linear, 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055, greaterThan(linear, vec3(0.0031308)));
}

 // does this make sense to use same curve for alpha? 
 // it seems to look better...

vec4 srgbToLinear(vec4 srgb) {
    return vec4(srgbToLinear(srgb.rgb), srgb.a);
}

vec4 linearToSrgb(vec4 linear) {
    return vec4(linearToSrgb(linear.rgb), linear.a);
}

float saturate(float val) { return clamp(val,0.0,1.0); }

// ---- Oklab (perceptual: equal steps look equal) -----------------------------
vec3 cbrt3(vec3 v) { return sign(v) * pow(abs(v), vec3(1.0 / 3.0)); }

vec3 linearSrgbToOklab(vec3 c)
{
    vec3 lms = vec3(0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b,
                    0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b,
                    0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b);
    vec3 l = cbrt3(lms);
    return vec3(0.2104542553 * l.x + 0.7936177850 * l.y - 0.0040720468 * l.z,
                1.9779984951 * l.x - 2.4285922050 * l.y + 0.4505937099 * l.z,
                0.0259040371 * l.x + 0.7827717662 * l.y - 0.8086757660 * l.z);
}

vec3 oklabToLinearSrgb(vec3 lab)
{
    vec3 l = vec3(lab.x + 0.3963377774 * lab.y + 0.2158037573 * lab.z,
                  lab.x - 0.1055613458 * lab.y - 0.0638541728 * lab.z,
                  lab.x - 0.0894841775 * lab.y - 1.2914855480 * lab.z);
    l = l * l * l;
    return vec3( 4.0767416621 * l.x - 3.3077115913 * l.y + 0.2309699292 * l.z,
                -1.2684380046 * l.x + 2.6097574011 * l.y - 0.3413193965 * l.z,
                -0.0041960863 * l.x - 0.7034186147 * l.y + 1.7076147010 * l.z);
}

// Two colors whose hues differ (both have some chroma, and their hues are more than
// 20 degrees apart) blend in Oklab.
const float kHueChroma = 0.03;
const float kHueCos    = 0.9397; // cos(20 degrees)

// A state's color `t` of the way from `from` to `to`, both straight (not premultiplied)
// sRGB. Hues that differ blend in Oklab, where the middle keeps the ends' lightness and
// saturation (in sRGB it goes dark and grey); others blend in sRGB. A transparent end
// adds no color, only its alpha.
vec4 BlendStateColor(vec4 from, vec4 to, float t)
{
    float alpha = mix(from.a, to.a, t);
    float wFrom = (1.0 - t) * from.a;
    float wTo   = t * to.a;
    if (wFrom + wTo <= 1e-5)
        return vec4(0.0);
    vec3 labFrom = linearSrgbToOklab(srgbToLinear(from.rgb));
    vec3 labTo   = linearSrgbToOklab(srgbToLinear(to.rgb));
    float cFrom = length(labFrom.yz);
    float cTo   = length(labTo.yz);
    bool  hues  = cFrom > kHueChroma && cTo > kHueChroma && dot(labFrom.yz, labTo.yz) < kHueCos * cFrom * cTo;
    vec3 rgb;
    if (hues)
        rgb = linearToSrgb(clamp(oklabToLinearSrgb((wFrom * labFrom + wTo * labTo) / (wFrom + wTo)), 0.0, 1.0));
    else
        rgb = (wFrom * from.rgb + wTo * to.rgb) / (wFrom + wTo);
    return vec4(rgb, alpha);
}

// A vertex at window pixel `position` of the primitive drawn `order`-th (its transform0.w):
// the world matrix maps the order to depth, later nearer, and past the depth range all
// stay at the nearest depth.
vec4 PrimitivePosition(vec2 position, float order)
{
    vec4 p = PushConstants.worldMatrix * vec4(position, order, 1.0);
    p.z = min(p.z, p.w);
    return p;
}

// How opaque the primitive is, from its transform1.w (how transparent it is, so 0 is opaque).
float PrimitiveOpacity(vec4 transform1)
{
    return 1.0 - transform1.w;
}


#include "bezier_sdf.glsl"