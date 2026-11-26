// Shared by every GUI shader: the primitives as the renderer writes them (gui_render_primitives.h),
// the push constants, and the color helpers. Structs read through a buffer address use Slang's
// natural layout, so every padding std430 would add is written out: offsets and sizes match the
// C++ structs.
//
// The C++ structs pack some fields in 16 bits (half, int16_t, uint16_t). Here those are read as
// the 32-bit words holding them, two to a word (the first in the low half), and unpacked with
// 32-bit operations: the shaders need no 16-bit types, which some GPUs (GeForce GTX 900 and 10,
// Radeon RX 400 and 500) can't run. The values are widened before any math in either case.

// Two halves, the first in the low 16 bits, as floats. SPIR-V has an instruction for it that
// needs no 16-bit type, where Slang's f16tof32 goes through one; DXIL's f16tof32 needs none.
float2 UnpackHalf2(uint bits)
{
    __target_switch
    {
    case spirv:
        return spirv_asm { result:$$float2 = OpExtInst glsl450 UnpackHalf2x16 $bits };
    default:
        return f16tof32(uint2(bits & 0xFFFFu, bits >> 16));
    }
}
float4 UnpackHalf4(uint2 bits) { return float4(UnpackHalf2(bits.x), UnpackHalf2(bits.y)); }
// Two uint16_t, the first in the low 16 bits.
uint2 UnpackUint16x2(uint bits) { return uint2(bits & 0xFFFFu, bits >> 16); }
uint4 UnpackUint16x4(uint2 bits) { return uint4(UnpackUint16x2(bits.x), UnpackUint16x2(bits.y)); }
// Two int16_t, the first in the low 16 bits, sign-extended.
int2 UnpackInt16x2(uint bits) { return int2(int(bits << 16) >> 16, int(bits) >> 16); }
int4 UnpackInt16x4(uint2 bits) { return int4(UnpackInt16x2(bits.x), UnpackInt16x2(bits.y)); }

struct ColorStyle
{
    uint2 transformationXBits;   // half4
    uint2 transformationYBits;   // half4
    uint2 colorUVTypeBits;       // uint16_t4 colorUV_type

    int   textureIndex;
    uint  animSpeedBits;         // half2: x = U/gradient scroll speed, y = V scroll speed (textures only)

    float4 TransformationX() { return UnpackHalf4(transformationXBits); }
    float4 TransformationY() { return UnpackHalf4(transformationYBits); }
    uint4  ColorUVType()     { return UnpackUint16x4(colorUVTypeBits); }
    float2 AnimSpeed()       { return UnpackHalf2(animSpeedBits); }
};

struct BoxStyle // aka 'StyleState'
{
    uint2 borderWidthBits;        // uint16_t4
    uint2 cornerRadiusBits;       // uint16_t4
    uint2 borderColorIndexBits;   // int16_t4, per edge: x=top, y=right, z=bottom, w=left
    uint  backgroundAndDashSize;  // int16_t backgroundColorIndex, uint16_t borderDashSize (pixels per dash, 0 = solid)
    uint  dashOffsetAndShadow;    // uint16_t borderDashOffset (pixel start into the pattern), uint16_t shadowIndex (CPU only)

    float4 BorderWidth()          { return float4(UnpackUint16x4(borderWidthBits)); }
    float4 CornerRadius()         { return float4(UnpackUint16x4(cornerRadiusBits)); }
    int4   BorderColorIndex()     { return UnpackInt16x4(borderColorIndexBits); }
    int    BackgroundColorIndex() { return UnpackInt16x2(backgroundAndDashSize).x; }
    float  BorderDashSize()       { return float(backgroundAndDashSize >> 16); }
    float  BorderDashOffset()     { return float(dashOffsetAndShadow & 0xFFFFu); }
};

struct Box
{
    float4   transform0;
    float4   transform1;
    int2     clipMin;
    int2     clipMax;

    int2     pos;
    int2     size;

    int      textureIndex;
    uint     colorIndexBits;   // int16_t colorIndex, then padding: a ColorStyle override (0 = the box style's background); supports gradients
    uint     styles[4];        // up to four state styles mixed: GPU style index in the low 24 bits, share (of 255) in the top 8
    uint2    snappedPxBits;    // int16_t4: window pixels {x0,y0,x1,y1} snapped to the parent; all 0 = snap here

    int  ColorIndex() { return UnpackInt16x2(colorIndexBits).x; }
    int4 SnappedPx()  { return UnpackInt16x4(snappedPxBits); }
};

struct Glyph
{
    float4    transform0;
    float4    transform1;
    int2      clipMin;
    int2      clipMax;

    float2    pos;
    float2    size;

    float2    uvMin;
    float2    uvMax;

    float     scale;
    uint      glyphTypeBits;    // uint16_t2 glyph_type

    int       textureIndex;     // Glyph atlas, not to be confused with ColorStyle texture index
    uint      colorAndBlurBits; // int16_t textColorIndex (1-based, 0 = invalid), int16_t shadowBlur
                                // (-1: the glyph itself; 0 and up: a text shadow blurred by this many 1/8 pixels)

    uint2 GlyphType()      { return UnpackUint16x2(glyphTypeBits); }
    int   TextColorIndex() { return UnpackInt16x2(colorAndBlurBits).x; }
    int   ShadowBlur()     { return UnpackInt16x2(colorAndBlurBits).y; }
};

struct Dot
{
    float4 transform0;
    float4 transform1;
    int2   clipMin;
    int2   clipMax;

    float4 color;
    float  size;
    float  _padSizeToPos;  // std430 puts the float2 below at 8 bytes
    // float2 (not uint16_t2): allows negative / >65535 positions and
    // sub-pixel precision, matching the C++ Dot struct.
    float2 pos;
};

struct Line
{
    float4   transform0;
    float4   transform1;
    int2     clipMin;
    int2     clipMax;

    int      colorIndex;
    float    lineWidth;
    float    dashSize;
    float    dashOffset;
    float    gradientOffset;
    float    gradientScale;

    uint     p0Bits, p1Bits, p2Bits, p3Bits;   // int16_t2 each
    int2     _padTo96;     // std430 rounds the struct up to 16 bytes

    float2 P0() { return float2(UnpackInt16x2(p0Bits)); }
    float2 P1() { return float2(UnpackInt16x2(p1Bits)); }
    float2 P2() { return float2(UnpackInt16x2(p2Bits)); }
    float2 P3() { return float2(UnpackInt16x2(p3Bits)); }
};

struct Triangle
{
    float4  transform0;
    float4  transform1;
    int2    clipMin;
    int2    clipMax;

    float2  p0;
    float2  p1;
    float2  p2;

    int     textureIndex;

    uint    colorAndEdgesBits;   // int16_t colorIndex, int16_t hardEdges (bit 0: p0-p1, 1: p1-p2, 2: p2-p0, drawn without a soft edge)

    int ColorIndex() { return UnpackInt16x2(colorAndEdgesBits).x; }
    int HardEdges()  { return UnpackInt16x2(colorAndEdgesBits).y; }
};

struct Quad
{
    float4  transform0;
    float4  transform1;
    int2    clipMin;
    int2    clipMax;

    int2    pos;
    int2    size;

    int     textureIndex;
    uint    colorAndBlurBits;   // int16_t colorIndex, int16_t blur (pixels; a shadow: the rect includes the blur on every side)
    float   cornerRadius;
    uint    displayFlags; // ImageDisplay: 3 bits a channel's source (red first), bit 12 sRGB-encode, bit 31 on
    float4  uvRect;       // the texture's part a StretchedTexture stretches over the quad: min.xy, max.zw
    float4  tint;         // multiplies the color

    int ColorIndex() { return UnpackInt16x2(colorAndBlurBits).x; }
    int Blur()       { return UnpackInt16x2(colorAndBlurBits).y; }
};

static const int kBoxSlot      = 0;
static const int kGlyphSlot    = 1;
static const int kDotSlot      = 2;
static const int kLineSlot     = 3;
static const int kTriangleSlot = 4;

#if defined(TACHYON_D3D12)
// Direct3D 12: the matrix, the time and the draw's first vertex as root constants (b0); the
// primitive and style buffers as root SRVs (t0 to t6), a GPU virtual address each, in place of
// Vulkan's buffer addresses.
struct PushConstants
{
    float4 worldColumns[4];
    float  elapsedTime;   // seconds since app start
    uint   firstVertex;   // the draw's first vertex: SV_VertexID starts at 0
};
ConstantBuffer<PushConstants> pc : register(b0);

ByteAddressBuffer boxBuffer      : register(t0);
ByteAddressBuffer glyphBuffer    : register(t1);
ByteAddressBuffer dotBuffer      : register(t2);
ByteAddressBuffer lineBuffer     : register(t3);
ByteAddressBuffer triangleBuffer : register(t4);
ByteAddressBuffer boxStyles      : register(t5);
ByteAddressBuffer colorStyles    : register(t6);

// The `index`-th primitive of type T in slot `slot`. Callers pass a constant slot, so the
// switch folds away.
T LoadPrimitive<T>(int slot, int index)
{
    const uint offset = uint(index) * sizeof(T);
    switch (slot)
    {
    case kBoxSlot:   return boxBuffer.Load<T>(offset);
    case kGlyphSlot: return glyphBuffer.Load<T>(offset);
    case kDotSlot:   return dotBuffer.Load<T>(offset);
    case kLineSlot:  return lineBuffer.Load<T>(offset);
    default:         return triangleBuffer.Load<T>(offset);
    }
}
BoxStyle   LoadBoxStyle(int index)   { return boxStyles.Load<BoxStyle>(uint(index) * sizeof(BoxStyle)); }
ColorStyle LoadColorStyle(int index) { return colorStyles.Load<ColorStyle>(uint(index) * sizeof(ColorStyle)); }

struct VertexInput { uint id : SV_VertexID; };
int VertexIndex(VertexInput input) { return input.id + int(pc.firstVertex); }
#elif defined(TACHYON_METAL)
// Metal: as Direct3D 12's, the matrix, the time and the draw's first vertex as constants, and the
// primitive and style buffers bound per draw, each at the offset its data starts. slangc numbers
// Metal's buffers in the order they're declared, here and in sdf_color.slang: the backend binds
// them by those numbers (metal_device.h, namespace binding), so keep the order.
struct PushConstants
{
    float4 worldColumns[4];
    float  elapsedTime;   // seconds since app start
    uint   firstVertex;   // the draw's first vertex: SV_VertexID starts at 0
    uint2  _pad;
};
ConstantBuffer<PushConstants> pc;   // buffer(0)

ByteAddressBuffer boxBuffer;        // buffer(1)
ByteAddressBuffer glyphBuffer;      // buffer(2)
ByteAddressBuffer dotBuffer;        // buffer(3)
ByteAddressBuffer lineBuffer;       // buffer(4)
ByteAddressBuffer triangleBuffer;   // buffer(5)
ByteAddressBuffer boxStyles;        // buffer(6)
ByteAddressBuffer colorStyles;      // buffer(7)

// The `index`-th primitive of type T in slot `slot`. Callers pass a constant slot, so the
// switch folds away.
T LoadPrimitive<T>(int slot, int index)
{
    const uint offset = uint(index) * sizeof(T);
    switch (slot)
    {
    case kBoxSlot:   return boxBuffer.Load<T>(offset);
    case kGlyphSlot: return glyphBuffer.Load<T>(offset);
    case kDotSlot:   return dotBuffer.Load<T>(offset);
    case kLineSlot:  return lineBuffer.Load<T>(offset);
    default:         return triangleBuffer.Load<T>(offset);
    }
}
BoxStyle   LoadBoxStyle(int index)   { return boxStyles.Load<BoxStyle>(uint(index) * sizeof(BoxStyle)); }
ColorStyle LoadColorStyle(int index) { return colorStyles.Load<ColorStyle>(uint(index) * sizeof(ColorStyle)); }

struct VertexInput { uint id : SV_VertexID; };
int VertexIndex(VertexInput input) { return input.id + int(pc.firstVertex); }
#elif defined(TACHYON_SDLGPU)
// SDL GPU: each stage's resources where SDL puts them, the vertex stage's in set 0 / space0
// (its uniforms set 1 / space1), the fragment stage's in set 2 / space2 (set 3 / space3). The
// 16 images come first (sdf_color.slang), then the primitive and style buffers, each bound
// whole: a primitive slot's data starts at its byte offset.
#if defined(TACHYON_SDLGPU_FRAGMENT)
#define SDL_RESOURCES 2
#define SDL_RESOURCE_SPACE space2
#define SDL_UNIFORMS 3
#define SDL_UNIFORM_SPACE space3
#else
#define SDL_RESOURCES 0
#define SDL_RESOURCE_SPACE space0
#define SDL_UNIFORMS 1
#define SDL_UNIFORM_SPACE space1
#endif
struct PushConstants
{
    float4 worldColumns[4];
    float  elapsedTime;          // seconds since app start
    uint   firstVertex;          // the draw's first vertex: SV_VertexID starts at 0
    uint2  _pad;
    uint4  primitiveOffsets[2];  // bytes into each slot's buffer: box, glyph, dot, line, triangle
    uint4  imageSlots[4];        // the image slot each of the draw's 16 images is; ~0 for none
};
#if defined(TACHYON_SDLGPU_METAL)
// Metal: SDL binds a stage's uniform buffers first, then its storage buffers, each [[buffer]]
// counting up from 0. slangc numbers the buffers in the order they're declared (a register
// would put them all at 0): the uniforms at buffer(0), the storage buffers at buffer(1) to (7).
ConstantBuffer<PushConstants> pc;
ByteAddressBuffer boxBuffer;
ByteAddressBuffer glyphBuffer;
ByteAddressBuffer dotBuffer;
ByteAddressBuffer lineBuffer;
ByteAddressBuffer triangleBuffer;
ByteAddressBuffer boxStyles;
ByteAddressBuffer colorStyles;
#else
[[vk::binding(0, SDL_UNIFORMS)]] ConstantBuffer<PushConstants> pc : register(b0, SDL_UNIFORM_SPACE);

[[vk::binding(16, SDL_RESOURCES)]] ByteAddressBuffer boxBuffer      : register(t16, SDL_RESOURCE_SPACE);
[[vk::binding(17, SDL_RESOURCES)]] ByteAddressBuffer glyphBuffer    : register(t17, SDL_RESOURCE_SPACE);
[[vk::binding(18, SDL_RESOURCES)]] ByteAddressBuffer dotBuffer      : register(t18, SDL_RESOURCE_SPACE);
[[vk::binding(19, SDL_RESOURCES)]] ByteAddressBuffer lineBuffer     : register(t19, SDL_RESOURCE_SPACE);
[[vk::binding(20, SDL_RESOURCES)]] ByteAddressBuffer triangleBuffer : register(t20, SDL_RESOURCE_SPACE);
[[vk::binding(21, SDL_RESOURCES)]] ByteAddressBuffer boxStyles      : register(t21, SDL_RESOURCE_SPACE);
[[vk::binding(22, SDL_RESOURCES)]] ByteAddressBuffer colorStyles    : register(t22, SDL_RESOURCE_SPACE);
#endif

// The `index`-th primitive of type T in slot `slot`, from where the slot's data starts.
T LoadPrimitive<T>(int slot, int index)
{
    const uint offset = pc.primitiveOffsets[slot >> 2][slot & 3] + uint(index) * sizeof(T);
    switch (slot)
    {
    case kBoxSlot:   return boxBuffer.Load<T>(offset);
    case kGlyphSlot: return glyphBuffer.Load<T>(offset);
    case kDotSlot:   return dotBuffer.Load<T>(offset);
    case kLineSlot:  return lineBuffer.Load<T>(offset);
    default:         return triangleBuffer.Load<T>(offset);
    }
}
BoxStyle   LoadBoxStyle(int index)   { return boxStyles.Load<BoxStyle>(uint(index) * sizeof(BoxStyle)); }
ColorStyle LoadColorStyle(int index) { return colorStyles.Load<ColorStyle>(uint(index) * sizeof(ColorStyle)); }

struct VertexInput { uint id : SV_VertexID; };
int VertexIndex(VertexInput input) { return input.id + int(pc.firstVertex); }
#else
// Vulkan: the push constants (GPUDrawPushConstants), 128 bytes. The world matrix is its four
// columns, as the C++ side stores it; a primitive kind's buffer is at its PrimitiveSpec::pushSlot.
struct PushConstants
{
    float4   worldColumns[4];
    uint64_t primitiveAddresses[5];  // boxes, glyphs, dots, lines, triangles (slots shared by other kinds)
    uint64_t boxStyleAddress;
    uint64_t colorStyleAddress;
    float    elapsedTime;   // seconds since app start
    float    _timePad;      // pad to 128 bytes
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

// The `index`-th primitive of type T in push-constant slot `slot`.
T LoadPrimitive<T>(int slot, int index) { return ((T*)pc.primitiveAddresses[slot])[index]; }
BoxStyle   LoadBoxStyle(int index)   { return ((BoxStyle*)pc.boxStyleAddress)[index]; }
ColorStyle LoadColorStyle(int index) { return ((ColorStyle*)pc.colorStyleAddress)[index]; }

// The vertex index with the draw's vertex offset in it, which the draws start each kind's
// primitives at.
struct VertexInput { int id : SV_VulkanVertexID; };
int VertexIndex(VertexInput input) { return input.id; }
#endif

// GLSL's mod: x - y * floor(x / y), which keeps the sign of y (fmod keeps x's).
float  glslMod(float x, float y)   { return x - y * floor(x / y); }

// A point through a primitive's 2D transform: rows transform0.xyz, transform1.xyz, (0, 0, 1).
float3 ApplyTransform(float4 transform0, float4 transform1, float3 v)
{
    return float3(dot(transform0.xyz, v), dot(transform1.xyz, v), v.z);
}

float srgbToLinear(float cs) {
    return (cs <= 0.04045f) ? (cs / 12.92f) : pow((cs + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float cl) {
    return (cl <= 0.0031308f) ? (12.92f * cl) : (1.055f * pow(cl, 1.0f / 2.4f) - 0.055f);
}

float3 srgbToLinear(float3 srgb) {
    return select(srgb > float3(0.04045), pow((srgb + 0.055) / 1.055, float3(2.4)), srgb / 12.92);
}

float3 linearToSrgb(float3 linear) {
    return select(linear > float3(0.0031308), 1.055 * pow(linear, float3(1.0 / 2.4)) - 0.055, 12.92 * linear);
}

// The same curve for alpha looks better.
float4 srgbToLinear(float4 srgb) {
    return float4(srgbToLinear(srgb.rgb), srgb.a);
}

float4 linearToSrgb(float4 linear) {
    return float4(linearToSrgb(linear.rgb), linear.a);
}

// ---- Oklab (perceptual: equal steps look equal) -----------------------------
float3 cbrt3(float3 v) { return sign(v) * pow(abs(v), float3(1.0 / 3.0)); }

float3 linearSrgbToOklab(float3 c)
{
    float3 lms = float3(0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b,
                        0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b,
                        0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b);
    float3 l = cbrt3(lms);
    return float3(0.2104542553 * l.x + 0.7936177850 * l.y - 0.0040720468 * l.z,
                  1.9779984951 * l.x - 2.4285922050 * l.y + 0.4505937099 * l.z,
                  0.0259040371 * l.x + 0.7827717662 * l.y - 0.8086757660 * l.z);
}

float3 oklabToLinearSrgb(float3 lab)
{
    float3 l = float3(lab.x + 0.3963377774 * lab.y + 0.2158037573 * lab.z,
                      lab.x - 0.1055613458 * lab.y - 0.0638541728 * lab.z,
                      lab.x - 0.0894841775 * lab.y - 1.2914855480 * lab.z);
    l = l * l * l;
    return float3( 4.0767416621 * l.x - 3.3077115913 * l.y + 0.2309699292 * l.z,
                  -1.2684380046 * l.x + 2.6097574011 * l.y - 0.3413193965 * l.z,
                  -0.0041960863 * l.x - 0.7034186147 * l.y + 1.7076147010 * l.z);
}

// Two colors whose hues differ (both have some chroma, and their hues are more than
// 20 degrees apart) blend in Oklab.
static const float kHueChroma = 0.03;
static const float kHueCos    = 0.9397; // cos(20 degrees)

// A state's color `t` of the way from `from` to `to`, both straight (not premultiplied)
// sRGB. Hues that differ blend in Oklab, where the middle keeps the ends' lightness and
// saturation (in sRGB it goes dark and grey); others blend in sRGB. A transparent end
// adds no color, only its alpha.
float4 BlendStateColor(float4 from, float4 to, float t)
{
    float alpha = lerp(from.a, to.a, t);
    float wFrom = (1.0 - t) * from.a;
    float wTo   = t * to.a;
    if (wFrom + wTo <= 1e-5)
        return float4(0.0);
    float3 labFrom = linearSrgbToOklab(srgbToLinear(from.rgb));
    float3 labTo   = linearSrgbToOklab(srgbToLinear(to.rgb));
    float cFrom = length(labFrom.yz);
    float cTo   = length(labTo.yz);
    bool  hues  = cFrom > kHueChroma && cTo > kHueChroma && dot(labFrom.yz, labTo.yz) < kHueCos * cFrom * cTo;
    float3 rgb;
    if (hues)
        rgb = linearToSrgb(clamp(oklabToLinearSrgb((wFrom * labFrom + wTo * labTo) / (wFrom + wTo)), 0.0, 1.0));
    else
        rgb = (wFrom * from.rgb + wTo * to.rgb) / (wFrom + wTo);
    return float4(rgb, alpha);
}

// A vertex at window pixel `position` of the primitive drawn `order`-th (its transform0.w):
// the world matrix maps the order to depth, later nearer, and past the depth range all
// stay at the nearest depth.
float4 PrimitivePosition(float2 position, float order)
{
    float4 p = pc.worldColumns[0] * position.x + pc.worldColumns[1] * position.y
             + pc.worldColumns[2] * order      + pc.worldColumns[3];
    p.z = min(p.z, p.w);
    return p;
}

// How opaque the primitive is, from its transform1.w (how transparent it is, so 0 is opaque).
float PrimitiveOpacity(float4 transform1)
{
    return 1.0 - transform1.w;
}

// A quad's corner from its vertex: horizontal index / 2, vertical 1 - index % 2 (0 or 1 each).
float2 QuadCorner(int vertexIndex)
{
    int index = vertexIndex % 4;
    return float2(index / 2, 1 - (index % 2));
}

#include "bezier_sdf.slang"
