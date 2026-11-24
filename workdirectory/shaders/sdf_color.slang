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
// DXIL: the texture at t<n>, its sampler at s<n>, used as one.
struct SDLImage
{
    Texture2D    texture;
    SamplerState sampler;

    float4 SampleLevel(float2 uv, float level) { return texture.SampleLevel(sampler, uv, level); }
    float4 Load(int3 texel) { return texture.Load(texel); }
    void   GetDimensions(out uint width, out uint height) { texture.GetDimensions(width, height); }
};
#define SDL_IMAGE(n) \
    Texture2D    imageTexture##n : register(t##n, SDL_RESOURCE_SPACE); \
    SamplerState imageSampler##n : register(s##n, SDL_RESOURCE_SPACE);
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

float2 ImageSize(uint index) { SDL_ON_IMAGE(BoundImage(index), SizeOf(image), float2(1, 1)) }
// The base level, as on the other APIs.
float4 SampleImage(uint index, float2 uv) { SDL_ON_IMAGE(BoundImage(index), image.SampleLevel(uv, 0), float4(0, 0, 0, 0)) }
float4 FetchImage(uint index, int2 texel) { SDL_ON_IMAGE(BoundImage(index), image.Load(int3(texel, 0)), float4(0, 0, 0, 0)) }
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

float4 getGradientColor(uint gradientPageIdx, float2 texSize, float offset, float gradientIndex)
{
    offset = clamp(offset * texSize.x, 0, texSize.x - 1) / texSize.x;
    return SampleImage(gradientPageIdx, float2(offset, gradientIndex));
}

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
    const float4 uvRow0 = float4(colorStyle.transformationX);
    const float4 uvRow1 = float4(colorStyle.transformationY);
    uint2 colorPixelPos = uint2(colorStyle.colorUV_type.xy);
    uint colorType = colorStyle.colorUV_type.z; // type

    // colorUV_type.w holds the 0-based physical bindless slot of the gradient-atlas
    // page; every atlas fetch for this style uses it.
    uint gradientPageIdx = colorStyle.colorUV_type.w;

    float2 defaultTextureSize = ImageSize(gradientPageIdx);
    float2 colorPos = float2(colorPixelPos) / defaultTextureSize;

    float4 color;

    float2 animOffset = float2(colorStyle.animSpeed) * pc.elapsedTime;

    if (colorType == 1) // StretchedTexture
    {
        float2 normalizedUV = lerp(uvRect.xy, uvRect.zw, frac((pos / (size * 2)) + 0.5 + animOffset));
        color = SampleImage(textureIndex, normalizedUV.xy);
    }

    else if (colorType == 2) // RepeatedTexture
    {
        float2 displayTextureSize = ImageSize(textureIndex);
        float2 uv = ((pos / size) + 1.0) * 0.5 * boxSize;
        float2 repeatedUV = (uv / displayTextureSize) + animOffset;
        color = SampleImage(textureIndex, repeatedUV.xy);
    }

    else if (colorType == 3) // LinearGradient
    {
        const float2 gradient_vector = float2(0.0, 1.0);
        float2 normalizedUV = (pos / (size * 2)) + 0.5;
        float3 transformedUV = ApplyTransform(uvRow0, uvRow1, float3(normalizedUV, 1));
        normalizedUV = transformedUV.xy / transformedUV.z;
        float gradient = min(abs(dot(gradient_vector, normalizedUV)), 1 - (1.0 / defaultTextureSize.x));
        float animGradient = clamp(frac(gradient + animOffset.x), 0, 1);
        color = SampleImage(gradientPageIdx, float2(animGradient, colorPos.y));
    }

    else if (colorType == 4) // CircularAngularGradient
    {
        float3 transformedUV = ApplyTransform(uvRow0, uvRow1, float3(pos, 1));
        pos = transformedUV.xy / transformedUV.z;
        float gradient = min(length(pos), 1 - (1.0 / defaultTextureSize.x));
        float animGradient = frac(gradient + animOffset.x);
        color = SampleImage(gradientPageIdx, float2(animGradient, colorPos.y));
    }

    else if (colorType == 5) // EllipsoidAngularGradient
    {
        float2 normalizedUV = pos / size;
        float3 transformedUV = ApplyTransform(uvRow0, uvRow1, float3(normalizedUV, 1));
        normalizedUV = transformedUV.xy / transformedUV.z;
        float gradient = min(length(normalizedUV), 1 - (1.0 / defaultTextureSize.x));
        float animGradient = frac(gradient + animOffset.x);
        color = SampleImage(gradientPageIdx, float2(animGradient, colorPos.y));
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

        float gradient = min(abs(signed_angle), 1 - (1.0 / defaultTextureSize.x));
        float animGradient = frac(gradient + animOffset.x);
        color = SampleImage(gradientPageIdx, float2(animGradient, colorPos.y));
    }

    else if (colorType == 7) // InterpolatedTriangle
    {
        float4 c0 = FetchImage(gradientPageIdx, int2(colorPixelPos.x + 0, colorPixelPos.y));
        float4 c1 = FetchImage(gradientPageIdx, int2(colorPixelPos.x + 1, colorPixelPos.y));
        float4 c2 = FetchImage(gradientPageIdx, int2(colorPixelPos.x + 2, colorPixelPos.y));

        color = (c0 * weights.x) +
                (c1 * weights.y) +
                (c2 * weights.z);
    }

    else if (colorType == 8) // InterpolatedBox
    {
        float4 c00 = FetchImage(gradientPageIdx, int2(colorPixelPos.x + 0, colorPixelPos.y)); // top-left
        float4 c01 = FetchImage(gradientPageIdx, int2(colorPixelPos.x + 1, colorPixelPos.y)); // top-right
        float4 c11 = FetchImage(gradientPageIdx, int2(colorPixelPos.x + 2, colorPixelPos.y)); // bottom-left
        float4 c10 = FetchImage(gradientPageIdx, int2(colorPixelPos.x + 3, colorPixelPos.y)); // bottom-right

        color = (c00 * weights.x) +
                (c01 * weights.y) +
                (c11 * weights.z) +
                (c10 * weights.w);
    }

    else if (colorType == 9) // SDFGradient: the ramp by edge depth, mapped by the first row
    {
        float t = ApplyTransform(uvRow0, uvRow1, float3(edgeDepth, 0, 1)).x;
        color = getGradientColor(gradientPageIdx, defaultTextureSize, t, colorPos.y);
    }

    else // SolidColor (default)
    {
        color = SampleImage(gradientPageIdx, colorPos);
    }
    return color;
}
