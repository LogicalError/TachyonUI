// Shared color resolution for SDF-based rendering: the bindless images and the color styles.

#if defined(TACHYON_D3D12)
// Every registered image, by its ImageIndex: its texture in the descriptor heap, and which of
// the static samplers it uses (0 nearest repeat, 1 linear repeat, 2 linear clamp to edge), one
// uint per image.
ByteAddressBuffer imageSamplers : register(t7);
SamplerState      samplers[3]   : register(s0);

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

float4 SampleImage(uint index, float2 uv)
{
    const uint sampler = imageSamplers.Load(index * 4);
    return Image(index).Sample(samplers[NonUniformResourceIndex(sampler)], uv);
}
float4 FetchImage(uint index, int2 texel) { return Image(index).Load(int3(texel, 0)); }
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
