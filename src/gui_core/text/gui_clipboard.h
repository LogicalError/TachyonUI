#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tachyon
{
    /// @addtogroup text
    /// @{

    /// An image on the clipboard: RGBA, 8 bits a channel, alpha not premultiplied, rows from
    /// the top. Its pixels go to GraphicsContext::CreateImage as ImageFormat::R8G8B8A8.
    struct ClipboardImage
    {
        uint32_t             width  = 0;
        uint32_t             height = 0;
        std::vector<uint8_t> rgba;   ///< width * height * 4 bytes

        /// No image: no size, or not as many bytes as the size needs.
        bool Empty() const { return width == 0 || height == 0 || rgba.size() != size_t(width) * height * 4; }
    };

    /// The clipboard text editors copy to and paste from, and images go through;
    /// GUIApplication::SetClipboard sets one (default: SDL's, or LocalClipboard without SDL).
    /// The clipboard holds one thing: setting text drops an image, and the other way round.
    class IClipboard
    {
    public:
        virtual ~IClipboard() = default;

        /// The clipboard's text, UTF-8; empty when it holds none.
        virtual std::string Get() const = 0;
        /// Puts UTF-8 text on the clipboard.
        virtual void Set(std::string_view text) = 0;

        /// Whether the clipboard holds an image. A clipboard without images says no.
        virtual bool HasImage() const { return false; }
        /// The clipboard's image; an empty one when it holds none, or it can't be read.
        virtual ClipboardImage GetImage() const { return {}; }
        /// Puts an image on the clipboard. False when it's empty, or this clipboard can't
        /// hold images; the clipboard then stays as it was.
        virtual bool SetImage(const ClipboardImage& image) { (void)image; return false; }
    };

    /// A clipboard inside the process, not shared with other applications.
    class LocalClipboard final : public IClipboard
    {
    public:
        std::string Get() const override { return m_text; }
        void Set(std::string_view text) override { m_text = text; m_image = {}; }

        bool HasImage() const override { return !m_image.Empty(); }
        ClipboardImage GetImage() const override { return m_image; }
        bool SetImage(const ClipboardImage& image) override
        {
            if (image.Empty())
                return false;
            m_image = image;
            m_text.clear();
            return true;
        }

    private:
        std::string    m_text;
        ClipboardImage m_image;
    };
    /// @}
}
