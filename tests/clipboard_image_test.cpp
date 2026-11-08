// Images on the clipboard: IClipboard's GetImage / SetImage, LocalClipboard's, and SDL's (as
// PNG and BMP). The SDL test runs on SDL's offscreen video driver, whose clipboard stays in
// the process, so the system clipboard is never touched.

#include <gtest/gtest.h>

#include <gui_core/text/gui_clipboard.h>
#include <gui_core/windows/sdl/gui_sdl_clipboard.h>

#include <SDL3/SDL.h>

#include <cstring>
#include <string>

namespace
{
    using namespace tachyon;

    // A 3 x 2 image, every pixel different, alpha too.
    ClipboardImage Sample()
    {
        ClipboardImage image{ 3, 2, {} };
        for (uint32_t i = 0; i < 6; ++i)
            image.rgba.insert(image.rgba.end(), { uint8_t(40 * i), uint8_t(255 - 30 * i), uint8_t(7 + i), uint8_t(255 - 40 * i) });
        return image;
    }

    TEST(ClipboardImageTest, AnImageIsEmptyWithoutPixelsForItsSize)
    {
        EXPECT_TRUE(ClipboardImage{}.Empty());
        EXPECT_FALSE(Sample().Empty());
        ClipboardImage cut = Sample();
        cut.rgba.pop_back();
        EXPECT_TRUE(cut.Empty()) << "one byte short";
        ClipboardImage flat = Sample();
        flat.height = 0;
        EXPECT_TRUE(flat.Empty());
    }

    // A clipboard holds one thing: text or an image.
    TEST(ClipboardImageTest, ALocalClipboardHoldsTextOrAnImage)
    {
        LocalClipboard clipboard;
        EXPECT_FALSE(clipboard.HasImage());
        EXPECT_TRUE(clipboard.GetImage().Empty());

        clipboard.Set("words");
        ASSERT_TRUE(clipboard.SetImage(Sample()));
        EXPECT_TRUE(clipboard.HasImage());
        EXPECT_EQ(clipboard.GetImage().rgba, Sample().rgba);
        EXPECT_EQ(clipboard.GetImage().width, 3u);
        EXPECT_EQ(clipboard.Get(), "") << "the image took the text's place";

        ClipboardImage cut = Sample();
        cut.rgba.resize(4);
        EXPECT_FALSE(clipboard.SetImage(cut));
        EXPECT_FALSE(clipboard.SetImage({}));
        EXPECT_EQ(clipboard.GetImage().rgba, Sample().rgba) << "a refused image changes nothing";

        clipboard.Set("more words");
        EXPECT_FALSE(clipboard.HasImage()) << "the text took the image's place";
        EXPECT_EQ(clipboard.Get(), "more words");
    }

    // A clipboard of text only has no images, and takes none.
    TEST(ClipboardImageTest, AClipboardWithoutImagesTakesNone)
    {
        struct TextOnly final : IClipboard
        {
            std::string text;
            std::string Get() const override { return text; }
            void Set(std::string_view t) override { text = t; }
        } clipboard;
        EXPECT_FALSE(clipboard.SetImage(Sample()));
        EXPECT_FALSE(clipboard.HasImage());
        EXPECT_TRUE(clipboard.GetImage().Empty());
    }

    // PNG and BMP keep every pixel, alpha too.
    TEST(ClipboardImageTest, PngAndBmpKeepTheImage)
    {
        for (const char* type : { "image/png", "image/bmp" })
        {
            const std::vector<uint8_t> bytes = EncodeClipboardImage(Sample(), type);
            ASSERT_FALSE(bytes.empty()) << type;
            const ClipboardImage back = DecodeClipboardImage(bytes, type);
            EXPECT_EQ(back.width, 3u) << type;
            EXPECT_EQ(back.height, 2u) << type;
            EXPECT_EQ(back.rgba, Sample().rgba) << type;
        }
        const std::vector<uint8_t> png = EncodeClipboardImage(Sample(), "image/png");
        EXPECT_EQ(std::memcmp(png.data(), "\x89PNG", 4), 0);
        const std::vector<uint8_t> bmp = EncodeClipboardImage(Sample(), "image/bmp");
        EXPECT_EQ(std::memcmp(bmp.data(), "BM", 2), 0);
    }

    TEST(ClipboardImageTest, WhatCantBeReadOrWrittenIsEmpty)
    {
        EXPECT_TRUE(EncodeClipboardImage({}, "image/png").empty());
        EXPECT_TRUE(EncodeClipboardImage(Sample(), "image/jpeg").empty()) << "another type";
        ClipboardImage cut = Sample();
        cut.rgba.resize(5);
        EXPECT_TRUE(EncodeClipboardImage(cut, "image/bmp").empty());

        const std::vector<uint8_t> png = EncodeClipboardImage(Sample(), "image/png");
        EXPECT_TRUE(DecodeClipboardImage(png, "image/bmp").Empty()) << "PNG bytes read as BMP";
        EXPECT_TRUE(DecodeClipboardImage(png, "text/plain").Empty());
        EXPECT_TRUE(DecodeClipboardImage(EncodeClipboardImage(Sample(), "image/bmp"), "text/plain").Empty()) << "BMP bytes of another type";
        EXPECT_TRUE(DecodeClipboardImage({}, "image/png").Empty());
        const std::vector<uint8_t> half(png.begin(), png.begin() + png.size() / 2);
        EXPECT_TRUE(DecodeClipboardImage(half, "image/png").Empty()) << "cut off";
        const uint8_t junk[] = { 'B', 'M', 1, 2, 3, 4, 5, 6, 7, 8 };
        EXPECT_TRUE(DecodeClipboardImage(junk, "image/bmp").Empty());
    }

    // SDL's clipboard: an image put on it comes back, offered as PNG and BMP, and text takes
    // its place. On the offscreen driver only, so the system clipboard is left alone.
    TEST(ClipboardImageTest, SDLsClipboardTakesAnImage)
    {
        if (SDL_WasInit(SDL_INIT_VIDEO) != 0)
            GTEST_SKIP() << "SDL's video is running already, maybe not offscreen";
        const std::string before = SDL_GetHint(SDL_HINT_VIDEO_DRIVER) ? SDL_GetHint(SDL_HINT_VIDEO_DRIVER) : "";
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        const bool started = SDL_InitSubSystem(SDL_INIT_VIDEO);
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, before.empty() ? nullptr : before.c_str());
        if (!started)
            GTEST_SKIP() << "no offscreen video driver: " << SDL_GetError();
        if (SDL_GetCurrentVideoDriver() == nullptr || std::strcmp(SDL_GetCurrentVideoDriver(), "offscreen") != 0)
        {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            GTEST_SKIP() << "not on the offscreen driver";
        }

        SDLClipboard clipboard;
        clipboard.Set("words");
        EXPECT_FALSE(clipboard.HasImage());
        EXPECT_TRUE(clipboard.GetImage().Empty());
        EXPECT_FALSE(clipboard.SetImage({}));

        ASSERT_TRUE(clipboard.SetImage(Sample()));
        EXPECT_TRUE(clipboard.HasImage());
        EXPECT_TRUE(SDL_HasClipboardData("image/png"));
        EXPECT_TRUE(SDL_HasClipboardData("image/bmp"));
        const ClipboardImage back = clipboard.GetImage();
        EXPECT_EQ(back.width, 3u);
        EXPECT_EQ(back.rgba, Sample().rgba);
        size_t size = 0;
        void* bmp = SDL_GetClipboardData("image/bmp", &size);
        ASSERT_NE(bmp, nullptr);
        EXPECT_EQ(DecodeClipboardImage(std::span<const uint8_t>(static_cast<const uint8_t*>(bmp), size), "image/bmp").rgba, Sample().rgba)
            << "the BMP another application would take";
        SDL_free(bmp);

        clipboard.Set("words again");
        EXPECT_FALSE(clipboard.HasImage()) << "text takes the image's place";

        // Another application's image, as PNG only: read. As PNG it can't be read, with a BMP
        // beside it: the BMP is read.
        struct Offer { const char* type; std::vector<uint8_t> bytes; };
        static std::vector<Offer> offers;
        const auto provide = [](void*, const char* type, size_t* size) -> const void* {
            for (const Offer& offer : offers)
                if (std::strcmp(offer.type, type) == 0) { *size = offer.bytes.size(); return offer.bytes.data(); }
            *size = 0;
            return nullptr;
        };
        offers = { { "image/png", EncodeClipboardImage(Sample(), "image/png") } };
        const char* pngOnly[] = { "image/png" };
        ASSERT_TRUE(SDL_SetClipboardData(provide, nullptr, nullptr, pngOnly, 1));
        EXPECT_TRUE(clipboard.HasImage()) << "PNG only";
        EXPECT_EQ(clipboard.GetImage().rgba, Sample().rgba);

        offers = { { "image/png", { 1, 2, 3 } }, { "image/bmp", EncodeClipboardImage(Sample(), "image/bmp") } };
        const char* both[] = { "image/png", "image/bmp" };
        ASSERT_TRUE(SDL_SetClipboardData(provide, nullptr, nullptr, both, 2));
        EXPECT_EQ(clipboard.GetImage().rgba, Sample().rgba) << "the BMP, the PNG unreadable";
        SDL_ClearClipboardData();
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
}
