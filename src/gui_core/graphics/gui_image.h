#pragma once

#include <gui_core/gui_base_types.h>   // float2, uint2, IntRect

#include <stdint.h>

namespace tachyon
{
    /// @addtogroup graphics
    /// @{

    /// The part of an image to draw, in UV: 0,0 is its top-left corner and 1,1 its
    /// bottom-right. The default is the whole image; a `min` past `max` flips it.
    struct UVRect
    {
        float2 min { 0.0f, 0.0f };
        float2 max { 1.0f, 1.0f };

        /// The part of an image `imageSize` pixels big covered by `pixels`, as for a cell of
        /// an atlas.
        static UVRect FromPixels(IntRect pixels, uint2 imageSize)
        {
            const float w = imageSize.x > 0 ? float(imageSize.x) : 1.0f;
            const float h = imageSize.y > 0 ? float(imageSize.y) : 1.0f;
            return { { float(pixels.x) / w, float(pixels.y) / h },
                     { float(pixels.x + pixels.w) / w, float(pixels.y + pixels.h) / h } };
        }

        bool operator==(const UVRect&) const = default;
    };

    /// A channel of an image's pixel, or a constant, for ImageDisplay.
    enum class ImageChannel : uint8_t { Red, Green, Blue, Alpha, Zero, One };

    /// How an image is shown, in this order:
    /// 1. each channel shown taken from one of the image's channels, or a constant
    /// 2. the color sRGB-encoded, for an image that holds linear values
    /// 3. multiplied by `tint`
    ///
    /// The default shows the image as it is.
    struct ImageDisplay
    {
        color4f      tint  { 1.0f, 1.0f, 1.0f, 1.0f };
        ImageChannel red   = ImageChannel::Red;
        ImageChannel green = ImageChannel::Green;
        ImageChannel blue  = ImageChannel::Blue;
        ImageChannel alpha = ImageChannel::Alpha;
        bool         srgbEncode = false;

        /// One channel shown as grey, opaque: a mask, a roughness map, an image's alpha.
        static ImageDisplay Channel(ImageChannel channel)
        {
            ImageDisplay display;
            display.red = display.green = display.blue = channel;
            display.alpha = ImageChannel::One;
            return display;
        }

        /// The channels and the sRGB flag as the shader reads them: 3 bits a channel (red
        /// first), bit 12 sRGB-encode, bit 31 set when any of it differs from the default.
        uint32_t PackedFlags() const
        {
            const uint32_t channels = uint32_t(red) | (uint32_t(green) << 3) | (uint32_t(blue) << 6) | (uint32_t(alpha) << 9);
            const uint32_t flags = channels | (srgbEncode ? (1u << 12) : 0u);
            constexpr uint32_t kIdentity = 0u | (1u << 3) | (2u << 6) | (3u << 9);
            return flags == kIdentity ? 0u : (flags | (1u << 31));
        }

        bool operator==(const ImageDisplay&) const = default;
    };

    /// Pixel formats for GraphicsContext::CreateImage, 8 bits per channel. The _SRGB
    /// formats decode sRGB to linear when sampled.
    enum class ImageFormat
    {
        UNDEFINED,
        R8,
        R8_SRGB,
        R8G8,
        R8G8_SRGB,
        R8G8B8,
        R8G8B8_SRGB,
        R8G8B8A8,
        R8G8B8A8_SRGB
    };

    /// How an image is filtered and addressed when sampled.
    enum class Sampler
    {
        UNDEFINED,

        NearestRepeat,
        LinearRepeat,
        LinearClampToEdge
    };

	/// Index of an image in the graphics context's image set, as the shaders sample it.
	/// Invalid (0) is no image.
    enum ImageIndex : uint32_t { Invalid = 0 };

	/// An image made by GraphicsContext::CreateImage; the backend defines its contents.
	class Image {};

	/// The images the shaders can sample, each at an ImageIndex. Implemented by the backend.
	class ImageSet
	{
	public:
        virtual ~ImageSet() {}
		/// Removes every image, freeing all indices.
		virtual void Clear() = 0;
		/// Adds an image (not owned) and returns its index.
		virtual ImageIndex Add(Image* image) = 0;
		/// Frees the index for reuse; the image itself stays.
		virtual void Remove(ImageIndex index) = 0;
	};
    /// @}
}