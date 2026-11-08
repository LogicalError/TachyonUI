#pragma once

#include <gui_core/text/gui_clipboard.h>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace tachyon
{
    /// An image as the bytes of a file of `mimeType` ("image/png" or "image/bmp"); empty when
    /// the image is, or the type is another.
    std::vector<uint8_t> EncodeClipboardImage(const ClipboardImage& image, std::string_view mimeType);
    /// The image in the bytes of a file of `mimeType` ("image/png" or "image/bmp"); an empty
    /// one when they can't be read.
    ClipboardImage DecodeClipboardImage(std::span<const uint8_t> bytes, std::string_view mimeType);

    /// @addtogroup windows
    /// @{

    /// The system clipboard, through SDL. Images go as PNG and BMP: another application takes
    /// the one it reads (on Windows, BMP is the system's own image format).
    class SDLClipboard : public IClipboard
    {
    public:
        std::string Get() const override;
        void        Set(std::string_view text) override;

        bool           HasImage() const override;
        ClipboardImage GetImage() const override;
        bool           SetImage(const ClipboardImage& image) override;
    };
    /// @}
}
