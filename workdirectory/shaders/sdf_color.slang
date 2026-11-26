// Shared color resolution for SDF-based rendering: the bindless images and the color styles.

#if defined(TACHYON_D3D12)
// Every registered image, by its ImageIndex: its texture in the descriptor heap, and which of
// the static samplers it uses (0 nearest repeat, 1 linear repeat, 2 linear clamp to edge), one
// uint per image.
ByteAddressBuffer imageSamplers : register(t7);
// Static samplers, so three of them by name: an array of static samplers can't be indexed.
SamplerState      nearestRepeat : register(s0);
SamplerState      linearRepeat  : register(s1);
SamplerState      linearClamp   : register(s2);

Texture2D Image(uint index)
{
    const DescriptorHandle<Texture2D> handle = DescriptorHandle<Texture2D>(uint2(NonUniformResourceIndex(index), 0));
    return handle;
}

float2 ImageSize(uint index)
{
    uint width, height;
    Image(index).GetDimensions(width, height);
    return float2(width, height);
}

// The base level: images have no other (as on Vulkan, whose samplers read level 0 alone), and
// it needs no derivatives in the branches.
float4 SampleImage(uint index, float2 uv)
{
    const Texture2D image   = Image(index);
    const uint      sampler = imageSamplers.Load(index * 4);
    if (sampler == 1)
        return image.SampleLevel(linearRepeat, uv, 0);
    if (sampler == 2)
        return image.SampleLevel(linearClamp, uv, 0);
    return image.SampleLevel(nearestRepeat, uv, 0);
}
float4 FetchImage(uint index, int2 texel) { return Image(index).Load(int3(texel, 0)); }
#elif defined(TACHYON_METAL)
// Every registered image, by its ImageIndex: which sampler it uses (0 nearest repeat, 1 linear
// repeat, 2 linear clamp to edge), one uint per image, and its texture, an MTLResourceID per
// image (Metal 3's bindless textures). Declared after all_common.slang's buffers: buffer(8) and
// buffer(9); the samplers are sampler(0) to sampler(2).
// Metal takes no pointer to a texture, only to a struct holding one: an argument buffer.
struct ImageEntry { DescriptorHandle<Texture2D> texture; };   // an MTLResourceID, 8 bytes
ByteAddressBuffer            imageSamplers;
StructuredBuffer<ImageEntry> images;
SamplerState nearestRepeat;
SamplerState linearRepeat;
SamplerState linearClamp;

// Metal indexes the handles as they are: no NonUniformResourceIndex.
Texture2D Image(uint index) { return images[index].texture; }

float2 ImageSize(uint index)
{
    uint width, height;
    Image(index).GetDimensions(width, height);
    return float2(width, height);
}

// The base level, as on the other APIs.
float4 SampleImage(uint index, float2 uv)
{
    const Texture2D image   = Image(index);
    const uint      sampler = imageSamplers.Load(index * 4);
    if (sampler == 1)
        return image.SampleLevel(linearRepeat, uv, 0);
    if (sampler == 2)
        return image.SampleLevel(linearClamp, uv, 0);
    return image.SampleLevel(nearestRepeat, uv, 0);
}
float4 FetchImage(uint index, int2 texel) { return Image(index).Load(int3(texel, 0)); }
#elif defined(TACHYON_SDLGPU)
// SDL GPU binds 16 images a draw, each its own binding (set or space as all_common.slang says),
// with its own sampler: the draw's images, whose image slots pc.imageSlots holds. An image
// reached by slot is looked for among them; one that isn't there reads as nothing.
#if defined(TACHYON_SDLGPU_SPIRV)
// SPIR-V: a combined image sampler at binding <n>.
typealias SDLImage = Sampler2D;
#define SDL_IMAGE(n) [[vk::binding(n, SDL_RESOURCES)]] Sampler2D image##n;
#define SDL_GET_IMAGE(n) image##n
#else
// DXIL: the texture at t<n>, its sampler at s<n>, used as one. Metal the same, at texture(n) and
// sampler(n), numbered in the order declared.
struct SDLImage
{
    Texture2D    texture;
    SamplerState sampler;

    float4 SampleLevel(float2 uv, float level) { return texture.SampleLevel(sampler, uv, level); }
    float4 Load(int3 texel) { return texture.Load(texel); }
    void   GetDimensions(out uint width, out uint height) { texture.GetDimensions(width, height); }
};
#if defined(TACHYON_SDLGPU_METAL)
#define SDL_IMAGE(n) \
    Texture2D    imageTexture##n; \
    SamplerState imageSampler##n;
#else
#define SDL_IMAGE(n) \
    Texture2D    imageTexture##n : register(t##n, SDL_RESOURCE_SPACE); \
    SamplerState imageSampler##n : register(s##n, SDL_RESOURCE_SPACE);
#endif
#define SDL_GET_IMAGE(n) SDLImage(imageTexture##n, imageSampler##n)
#endif
SDL_IMAGE(0)  SDL_IMAGE(1)  SDL_IMAGE(2)  SDL_IMAGE(3)  SDL_IMAGE(4)  SDL_IMAGE(5)  SDL_IMAGE(6)  SDL_IMAGE(7)
SDL_IMAGE(8)  SDL_IMAGE(9)  SDL_IMAGE(10) SDL_IMAGE(11) SDL_IMAGE(12) SDL_IMAGE(13) SDL_IMAGE(14) SDL_IMAGE(15)

// Which of the draw's images is `index`, or -1.
int BoundImage(uint index)
{
    [unroll]
    for (int i = 0; i < 16; ++i)
        if (pc.imageSlots[i >> 2][i & 3] == index)
            return i;
    return -1;
}

// `expression` on image `i` of the draw (`image`): a case for each, as the images can't be
// indexed.
#define SDL_IMAGE_CASE(n, expression) case n: { SDLImage image = SDL_GET_IMAGE(n); return expression; }
#define SDL_ON_IMAGE(i, expression, otherwise) \
    switch (i) \
    { \
    SDL_IMAGE_CASE(0, expression)  SDL_IMAGE_CASE(1, expression)  SDL_IMAGE_CASE(2, expression)  SDL_IMAGE_CASE(3, expression) \
    SDL_IMAGE_CASE(4, expression)  SDL_IMAGE_CASE(5, expression)  SDL_IMAGE_CASE(6, expression)  SDL_IMAGE_CASE(7, expression) \
    SDL_IMAGE_CASE(8, expression)  SDL_IMAGE_CASE(9, expression)  SDL_IMAGE_CASE(10, expression) SDL_IMAGE_CASE(11, expression) \
    SDL_IMAGE_CASE(12, expression) SDL_IMAGE_CASE(13, expression) SDL_IMAGE_CASE(14, expression) SDL_IMAGE_CASE(15, expression) \
    default: return otherwise; \
    }

float2 SizeOf(SDLImage image)
{
    uint width, height;
    image.GetDimensions(width, height);
    return float2(width, height);
}

// Four texels in a row, from `texel` right, the last no further right than `lastX`.
float4x4 LoadRow(SDLImage image, int2 texel, int lastX)
{
    return float4x4(image.Load(int3(texel, 0)), image.Load(int3(texel.x + 1, texel.y, 0)),
                    image.Load(int3(texel.x + 2, texel.y, 0)), image.Load(int3(min(texel.x + 3, lastX), texel.y, 0)));
}

float2 ImageSize(uint index) { SDL_ON_IMAGE(BoundImage(index), SizeOf(image), float2(1, 1)) }
// The base level, as on the other APIs.
float4 SampleImage(uint index, float2 uv) { SDL_ON_IMAGE(BoundImage(index), image.SampleLevel(uv, 0), float4(0, 0, 0, 0)) }
float4 FetchImage(uint index, int2 texel) { SDL_ON_IMAGE(BoundImage(index), image.Load(int3(texel, 0)), float4(0, 0, 0, 0)) }
float4x4 FetchImageRow(uint index, int2 texel, int lastX) { SDL_ON_IMAGE(BoundImage(index), LoadRow(image, texel, lastX), float4x4(0)) }
#else
// Every registered image, by its ImageIndex (set 0, binding 0), with its own sampler.
[[vk::binding(0, 0)]] Sampler2D allTextures[];

float2 ImageSize(uint index)
{
    uint width, height;
    allTextures[NonUniformResourceIndex(index)].GetDimensions(width, height);
    return float2(width, height);
}

float4 SampleImage(uint index, float2 uv) { return allTextures[NonUniformResourceIndex(index)].Sample(uv); }
float4 FetchImage(uint index, int2 texel) { return allTextures[NonUniformResourceIndex(index)].Load(int3(texel, 0)); }
#endif

#if !defined(TACHYON_SDLGPU)
// Four texels in a row, from `texel` right, the last no further right than `lastX`. SDL GPU has
// its own, one case for each bound image rather than four.
float4x4 FetchImageRow(uint index, int2 texel, int lastX)
{
    return float4x4(FetchImage(index, texel), FetchImage(index, int2(texel.x + 1, texel.y)),
                    FetchImage(index, int2(texel.x + 2, texel.y)), FetchImage(index, int2(min(texel.x + 3, lastX), texel.y)));
}
#endif

float cross2(float2 a, float2 b)
{
    return a.x * b.y - a.y * b.x;
}

// Resolve a ColorStyle index to a color.
// colorStyleIndex: 1-based index (0 = invalid/transparent).
// pos, size: position and half-size in local space (for gradient mapping).
// edgeDepth: how deep the point is inside its shape, 0 on the edge and 1 at the deepest
//   point (the middle of a box, a ring's stroke or a triangle's incircle); SDFGradient uses it.
// textureIndex: override texture index (1-based), or 0 to use ColorStyle's own texture.
// boxSize: original box pixel size (for RepeatedTexture UV).
// uvRect: the texture's part a StretchedTexture stretches over the shape (min.xy, max.zw).
float4 getSDFColor(int colorStyleIndex, float2 pos, float2 size, float edgeDepth, int textureIndex, float2 boxSize, float4 weights,
                   float4 uvRect = float4(0, 0, 1, 1))
{
    if (colorStyleIndex < 1)
        return float4(0); // default to transparent if no color style is set

    ColorStyle colorStyle = LoadColorStyle(colorStyleIndex - 1);

    textureIndex = textureIndex >= 1 ? max(0, textureIndex - 1) : max(0, colorStyle.textureIndex - 1);

    // Its UV transform's rows: transformationX.xyz, transformationY.xyz, (0, 0, 1).
    const float4 uvRow0 = colorStyle.TransformationX();
    const float4 uvRow1 = colorStyle.TransformationY();
    const uint4  colorUV_type = colorStyle.ColorUVType();
    uint2 colorPixelPos = colorUV_type.xy;
    uint colorType = colorUV_type.z; // type

    // colorUV_type.w holds the 0-based physical bindless slot of the gradient-atlas
    // page; every atlas fetch for this style uses it.
    uint gradientPageIdx = colorUV_type.w;

    // The one image the type reads, and its size: the texture for the texture types, the gradient
    // page for the rest. Each type works out where to read it, then it's read in one place: on SDL
    // GPU every image read is a case for each bound image, so there are as few as there can be.
    const uint   imageIndex = (colorType == 1 || colorType == 2) ? textureIndex : gradientPageIdx;
    const float2 imageSize  = ImageSize(imageIndex);
    float2 colorPos = float2(colorPixelPos) / imageSize;

    float2 animOffset = colorStyle.AnimSpeed() * pc.elapsedTime;

    if (colorType == 7 || colorType == 8) // InterpolatedTriangle, InterpolatedBox
    {
        // The corners' colors side by side: a triangle's three, a box's four (top-left,
        // top-right, bottom-left, bottom-right).
        const float4x4 c = FetchImageRow(gradientPageIdx, int2(colorPixelPos), int(imageSize.x) - 1);
        float4 color = (c[0] * weights.x) +
                       (c[1] * weights.y) +
                       (c[2] * weights.z);
        if (colorType == 8)
            color += c[3] * weights.w;
        return color;
    }

    float2 uv;
    if (colorType == 1) // StretchedTexture
    {
        uv = lerp(uvRect.xy, uvRect.zw, frac((pos / (size * 2)) + 0.5 + animOffset));
    }

    else if (colorType == 2) // RepeatedTexture
    {
        float2 boxUV = ((pos / size) + 1.0) * 0.5 * boxSize;
        uv = (boxUV / imageSize) + animOffset;
    }

    else if (colorType == 3) // LinearGradient
    {
        const float2 gradient_vector = float2(0.0, 1.0);
        float2 normalizedUV = (pos / (size * 2)) + 0.5;
        float3 transformedUV = ApplyTransform(uvRow0, uvRow1, float3(normalizedUV, 1));
        normalizedUV = transformedUV.xy / transformedUV.z;
        float gradient = min(abs(dot(gradient_vector, normalizedUV)), 1 - (1.0 / imageSize.x));
        float animGradient = clamp(frac(gradient + animOffset.x), 0, 1);
        uv = float2(animGradient, colorPos.y);
    }

    else if (colorType == 4) // CircularAngularGradient
    {
        float3 transformedUV = ApplyTransform(uvRow0, uvRow1, float3(pos, 1));
        pos = transformedUV.xy / transformedUV.z;
        float gradient = min(length(pos), 1 - (1.0 / imageSize.x));
        float animGradient = frac(gradient + animOffset.x);
        uv = float2(animGradient, colorPos.y);
    }

    else if (colorType == 5) // EllipsoidAngularGradient
    {
        float2 normalizedUV = pos / size;
        float3 transformedUV = ApplyTransform(uvRow0, uvRow1, float3(normalizedUV, 1));
        normalizedUV = transformedUV.xy / transformedUV.z;
        float gradient = min(length(normalizedUV), 1 - (1.0 / imageSize.x));
        float animGradient = frac(gradient + animOffset.x);
        uv = float2(animGradient, colorPos.y);
    }

    else if (colorType == 6) // RadialGradient
    {
        float3 transformedUV = ApplyTransform(uvRow0, uvRow1, float3(pos, 1));
        pos = transformedUV.xy / transformedUV.z;
        float2 from = normalize(pos);
        const float2 to = float2(0, 1);

        float angle = acos(clamp(dot(from, to), -1.0, 1.0)) / (2 * 3.1415926535897932384626433832795);
        float side = (from.x * to.y) - (from.y * to.x);
        float signed_angle = glslMod((((side < 0) ? -1 : 1) * angle) + 1, 1);

        float gradient = min(abs(signed_angle), 1 - (1.0 / imageSize.x));
        float animGradient = frac(gradient + animOffset.x);
        uv = float2(animGradient, colorPos.y);
    }

    else if (colorType == 9) // SDFGradient: the ramp by edge depth, mapped by the first row
    {
        float t = ApplyTransform(uvRow0, uvRow1, float3(edgeDepth, 0, 1)).x;
        uv = float2(clamp(t * imageSize.x, 0, imageSize.x - 1) / imageSize.x, colorPos.y);
    }

    else // SolidColor (default)
    {
        uv = colorPos;
    }
    return SampleImage(imageIndex, uv);
}
