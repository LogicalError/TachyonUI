#include <gui_core/windows/sdl/gui_sdl_clipboard.h>

#include <SDL3/SDL.h>

#include <cstring>

namespace tachyon
{
    namespace
    {
        constexpr const char* kPng = "image/png";
        constexpr const char* kBmp = "image/bmp";

        // An image put on the clipboard, and its bytes in the format last asked for (SDL copies
        // them). SDL keeps it until something else goes on the clipboard (ForgetImage).
        struct OfferedImage
        {
            ClipboardImage       image;
            std::vector<uint8_t> png;
            std::vector<uint8_t> bmp;
        };

        const void* SDLCALL ProvideImage(void* userdata, const char* mimeType, size_t* size)
        {
            auto* offered = static_cast<OfferedImage*>(userdata);
            *size = 0;
            if (offered == nullptr || mimeType == nullptr)
                return nullptr;
            std::vector<uint8_t>* bytes = std::strcmp(mimeType, kPng) == 0 ? &offered->png
                                        : std::strcmp(mimeType, kBmp) == 0 ? &offered->bmp
                                                                          : nullptr;
            if (bytes == nullptr)
                return nullptr;
            *bytes = EncodeClipboardImage(offered->image, mimeType);
            *size = bytes->size();
            return bytes->empty() ? nullptr : bytes->data();
        }

        void SDLCALL ForgetImage(void* userdata) { delete static_cast<OfferedImage*>(userdata); }
    }

    std::vector<uint8_t> EncodeClipboardImage(const ClipboardImage& image, std::string_view mimeType)
    {
        const bool png = mimeType == kPng;
        if (image.Empty() || (!png && mimeType != kBmp) || image.width > 0x7FFFFFFFu / 4 || image.height > 0x7FFFFFFFu)
            return {};
        SDL_Surface* surface = SDL_CreateSurfaceFrom(int(image.width), int(image.height), SDL_PIXELFORMAT_RGBA32,
                                                     const_cast<uint8_t*>(image.rgba.data()), int(image.width * 4));
        if (surface == nullptr)
            return {};
        std::vector<uint8_t> bytes;
        if (SDL_IOStream* io = SDL_IOFromDynamicMem())
        {
            if (png ? SDL_SavePNG_IO(surface, io, false) : SDL_SaveBMP_IO(surface, io, false))
            {
                const Sint64 size = SDL_GetIOSize(io);
                const auto* data = static_cast<const uint8_t*>(
                    SDL_GetPointerProperty(SDL_GetIOProperties(io), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr));
                if (data != nullptr && size > 0)
                    bytes.assign(data, data + size);
            }
            SDL_CloseIO(io);
        }
        SDL_DestroySurface(surface);
        return bytes;
    }

    ClipboardImage DecodeClipboardImage(std::span<const uint8_t> bytes, std::string_view mimeType)
    {
        const bool png = mimeType == kPng;
        if (bytes.empty() || (!png && mimeType != kBmp))
            return {};
        SDL_IOStream* io = SDL_IOFromConstMem(bytes.data(), bytes.size());
        if (io == nullptr)
            return {};
        SDL_Surface* loaded = png ? SDL_LoadPNG_IO(io, true) : SDL_LoadBMP_IO(io, true);
        if (loaded == nullptr)
            return {};
        SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(loaded);
        if (rgba == nullptr)
            return {};
        ClipboardImage image;
        if (rgba->w > 0 && rgba->h > 0 && rgba->pixels != nullptr)
        {
            image.width  = uint32_t(rgba->w);
            image.height = uint32_t(rgba->h);
            const size_t row = size_t(image.width) * 4;
            image.rgba.resize(row * image.height);
            for (uint32_t y = 0; y < image.height; ++y)
                std::memcpy(image.rgba.data() + y * row, static_cast<const uint8_t*>(rgba->pixels) + size_t(y) * size_t(rgba->pitch), row);
        }
        SDL_DestroySurface(rgba);
        return image;
    }

    std::string SDLClipboard::Get() const
    {
        // SDL_GetClipboardText returns a heap-allocated copy that must be freed.
        char* raw = SDL_GetClipboardText();
        if (raw == nullptr)
            return {};
        std::string out(raw);
        SDL_free(raw);
        return out;
    }

    void SDLClipboard::Set(std::string_view text)
    {
        std::string buffer(text);
        SDL_SetClipboardText(buffer.c_str());
    }

    bool SDLClipboard::HasImage() const
    {
        return SDL_HasClipboardData(kPng) || SDL_HasClipboardData(kBmp);
    }

    ClipboardImage SDLClipboard::GetImage() const
    {
        // PNG first: it keeps the alpha, where an application gives it.
        for (const char* mimeType : { kPng, kBmp })
        {
            if (!SDL_HasClipboardData(mimeType))
                continue;
            size_t size = 0;
            void* data = SDL_GetClipboardData(mimeType, &size);
            if (data == nullptr)
                continue;
            ClipboardImage image = DecodeClipboardImage(std::span<const uint8_t>(static_cast<const uint8_t*>(data), size), mimeType);
            SDL_free(data);
            if (!image.Empty())
                return image;
        }
        return {};
    }

    bool SDLClipboard::SetImage(const ClipboardImage& image)
    {
        if (image.Empty() || (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0)
            return false;
        // SDL owns it from here, and lets go of it through ForgetImage, also when it fails.
        auto* offered = new OfferedImage{ image, {}, {} };
        const char* mimeTypes[] = { kPng, kBmp };
        return SDL_SetClipboardData(ProvideImage, ForgetImage, offered, mimeTypes, 2);
    }
} // namespace tachyon
