// FileThumbnails: pictures of files read on a thread of their own, kept by path, time and
// size, at most so many.

#include <gtest/gtest.h>

#include <gui_controls/gui_file_thumbnails.h>

#include "gui_text_system_stub.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tachyon
{
    namespace
    {
        using namespace std::chrono_literals;

        // A reader that gives a 2x1 picture of any file but "plain.txt", and remembers what it read
        // and on which thread. "slow.jpg" waits until `open` is set.
        struct FakeShell
        {
            std::mutex                        mutex;
            std::vector<std::string>          read;
            std::vector<std::thread::id>      threads;
            std::atomic<bool>                 open { true };
            std::atomic<bool>                 waiting { false };

            FileThumbnails::Reader Reader()
            {
                return [this](const std::filesystem::path& file, uint32_t pixels, FileThumbnails::Picture& picture) {
                    while (file.filename() == "slow.jpg" && !open)
                    {
                        waiting = true;
                        std::this_thread::sleep_for(1ms);
                    }
                    {
                        std::lock_guard lock(mutex);
                        read.push_back(file.filename().string());
                        threads.push_back(std::this_thread::get_id());
                    }
                    if (file.filename() == "plain.txt")
                        return false;
                    picture.size = uint2{ pixels, pixels / 2 };
                    picture.rgba.assign(size_t(pixels) * (pixels / 2) * 4, 200);
                    return true;
                };
            }
            size_t Reads(const std::string& name)
            {
                std::lock_guard lock(mutex);
                return size_t(std::count(read.begin(), read.end(), name));
            }
        };

        // Waits for everything asked for to be read.
        bool Settle(FileThumbnails& thumbnails)
        {
            for (auto end = std::chrono::steady_clock::now() + 5s; std::chrono::steady_clock::now() < end;)
            {
                if (thumbnails.Reading() == 0)
                    return true;
                std::this_thread::sleep_for(1ms);
            }
            return false;
        }

        const std::filesystem::file_time_type kMonday = std::filesystem::file_time_type{} + 24h;
    }

    TEST(FileThumbnailsTest, KeptByFileTimeAndSize)
    {
        const std::filesystem::path photo = "C:/Photos/cat.jpg";
        EXPECT_EQ(FileThumbnails::Key(photo, kMonday, 48), FileThumbnails::Key(photo, kMonday, 48));
        EXPECT_NE(FileThumbnails::Key(photo, kMonday, 48), FileThumbnails::Key("C:/Photos/dog.jpg", kMonday, 48));
        EXPECT_NE(FileThumbnails::Key(photo, kMonday, 48), FileThumbnails::Key(photo, kMonday + 1s, 48)) << "changed since";
        EXPECT_NE(FileThumbnails::Key(photo, kMonday, 48), FileThumbnails::Key(photo, kMonday, 96)) << "another size";
        EXPECT_EQ(FileThumbnails::Key("C:\\Photos\\cat.jpg", kMonday, 48), FileThumbnails::Key(photo, kMonday, 48))
            << "either separator";
    }

    // None at first: read on another thread, then an icon of the picture's size, read once.
    TEST(FileThumbnailsTest, ReadOffTheUIThread)
    {
        StubTextSystem textSystem;
        FakeShell shell;
        FileThumbnails thumbnails(textSystem, 8, shell.Reader());
        EXPECT_EQ(thumbnails.Get("C:/cat.jpg", kMonday, 32), IconID::Invalid) << "not read yet";
        ASSERT_TRUE(Settle(thumbnails));
        const IconID cat = thumbnails.Get("C:/cat.jpg", kMonday, 32);
        ASSERT_NE(cat, IconID::Invalid);
        const IconMeasurement size = textSystem.MeasureIcon(cat);
        EXPECT_EQ(size.width, 32);
        EXPECT_EQ(size.height, 16);
        EXPECT_EQ(thumbnails.Get("C:/cat.jpg", kMonday, 32), cat);
        EXPECT_EQ(shell.Reads("cat.jpg"), 1u) << "read once";
        ASSERT_EQ(shell.threads.size(), 1u);
        EXPECT_NE(shell.threads[0], std::this_thread::get_id());
    }

    // A file the shell has no thumbnail for stays without one, and isn't asked about again.
    TEST(FileThumbnailsTest, AFileWithoutOneHasNone)
    {
        StubTextSystem textSystem;
        FakeShell shell;
        FileThumbnails thumbnails(textSystem, 8, shell.Reader());
        thumbnails.Get("C:/plain.txt", kMonday, 32);
        ASSERT_TRUE(Settle(thumbnails));
        EXPECT_EQ(thumbnails.Get("C:/plain.txt", kMonday, 32), IconID::Invalid);
        EXPECT_EQ(shell.Reads("plain.txt"), 1u);
        EXPECT_EQ(textSystem.pixelIconCalls, 0);

        EXPECT_EQ(thumbnails.Get({}, kMonday, 32), IconID::Invalid) << "only in memory";
        EXPECT_EQ(thumbnails.Get("C:/cat.jpg", kMonday, 0), IconID::Invalid) << "no size";
        EXPECT_EQ(thumbnails.Count(), 1u) << "nothing to read";

        FileThumbnails none(textSystem, 8, {});
        EXPECT_EQ(none.Get("C:/cat.jpg", kMonday, 32), IconID::Invalid) << "no reader, as off Windows";
        EXPECT_EQ(none.Count(), 0u);
    }

    TEST(FileThumbnailsTest, AChangedFileIsReadAgain)
    {
        StubTextSystem textSystem;
        FakeShell shell;
        FileThumbnails thumbnails(textSystem, 8, shell.Reader());
        thumbnails.Get("C:/cat.jpg", kMonday, 32);
        ASSERT_TRUE(Settle(thumbnails));
        const IconID before = thumbnails.Get("C:/cat.jpg", kMonday, 32);
        thumbnails.Get("C:/cat.jpg", kMonday + 1h, 32);
        ASSERT_TRUE(Settle(thumbnails));
        const IconID after = thumbnails.Get("C:/cat.jpg", kMonday + 1h, 32);
        EXPECT_EQ(shell.Reads("cat.jpg"), 2u);
        ASSERT_NE(after, IconID::Invalid);
        EXPECT_NE(after, before);
    }

    // Past the cap the one asked for longest ago goes, icon and all; asked for again, it's
    // read again.
    TEST(FileThumbnailsTest, AtMostSoMany)
    {
        StubTextSystem textSystem;
        FakeShell shell;
        FileThumbnails thumbnails(textSystem, 2, shell.Reader(), 0ms);
        thumbnails.Get("C:/a.jpg", kMonday, 32);
        thumbnails.Get("C:/b.jpg", kMonday, 32);
        ASSERT_TRUE(Settle(thumbnails));
        const IconID a = thumbnails.Get("C:/a.jpg", kMonday, 32);   // now b was asked for longest ago
        const IconID b = thumbnails.Get("C:/b.jpg", kMonday, 32);
        thumbnails.Get("C:/a.jpg", kMonday, 32);
        ASSERT_NE(a, IconID::Invalid);
        ASSERT_NE(b, IconID::Invalid);

        thumbnails.Get("C:/c.jpg", kMonday, 32);
        EXPECT_EQ(thumbnails.Count(), 2u);
        EXPECT_EQ(textSystem.unregisteredIcons, std::vector<IconID>{ b });
        EXPECT_EQ(thumbnails.Get("C:/a.jpg", kMonday, 32), a) << "kept";
        ASSERT_TRUE(Settle(thumbnails));

        EXPECT_EQ(thumbnails.Get("C:/b.jpg", kMonday, 32), IconID::Invalid) << "let go";
        ASSERT_TRUE(Settle(thumbnails));
        EXPECT_NE(thumbnails.Get("C:/b.jpg", kMonday, 32), IconID::Invalid);
        EXPECT_EQ(shell.Reads("b.jpg"), 2u);
    }

    // More files on screen than are kept: the ones past the cap keep their type icons, and
    // the others aren't read over and over.
    TEST(FileThumbnailsTest, NoneLetGoWhileOnScreen)
    {
        StubTextSystem textSystem;
        FakeShell shell;
        FileThumbnails thumbnails(textSystem, 2, shell.Reader(), std::chrono::hours(1));
        for (int frame = 0; frame < 3; ++frame)
        {
            for (const char* file : { "C:/a.jpg", "C:/b.jpg", "C:/c.jpg" })
                thumbnails.Get(file, kMonday, 32);
            ASSERT_TRUE(Settle(thumbnails));
        }
        EXPECT_NE(thumbnails.Get("C:/a.jpg", kMonday, 32), IconID::Invalid);
        EXPECT_NE(thumbnails.Get("C:/b.jpg", kMonday, 32), IconID::Invalid);
        EXPECT_EQ(thumbnails.Get("C:/c.jpg", kMonday, 32), IconID::Invalid);
        EXPECT_EQ(thumbnails.Count(), 2u);
        EXPECT_EQ(shell.Reads("a.jpg") + shell.Reads("b.jpg") + shell.Reads("c.jpg"), 2u);
        EXPECT_TRUE(textSystem.unregisteredIcons.empty());
    }

    // On screen means asked for lately, not added lately.
    TEST(FileThumbnailsTest, AskingAgainKeepsItOnScreen)
    {
        StubTextSystem textSystem;
        FakeShell shell;
        FileThumbnails thumbnails(textSystem, 2, shell.Reader(), 300ms);
        thumbnails.Get("C:/a.jpg", kMonday, 32);
        thumbnails.Get("C:/b.jpg", kMonday, 32);
        ASSERT_TRUE(Settle(thumbnails));
        std::this_thread::sleep_for(400ms);
        thumbnails.Get("C:/a.jpg", kMonday, 32);
        thumbnails.Get("C:/b.jpg", kMonday, 32);
        EXPECT_EQ(thumbnails.Get("C:/c.jpg", kMonday, 32), IconID::Invalid);
        EXPECT_TRUE(textSystem.unregisteredIcons.empty()) << "a and b are still on screen";
        EXPECT_NE(thumbnails.Get("C:/a.jpg", kMonday, 32), IconID::Invalid);
    }

    // The file asked for last is read first: what's on screen now, not what was scrolled past.
    TEST(FileThumbnailsTest, TheNewestIsReadFirst)
    {
        StubTextSystem textSystem;
        FakeShell shell;
        shell.open = false;
        FileThumbnails thumbnails(textSystem, 8, shell.Reader());
        thumbnails.Get("C:/slow.jpg", kMonday, 32);
        for (auto end = std::chrono::steady_clock::now() + 5s; !shell.waiting && std::chrono::steady_clock::now() < end;)
            std::this_thread::sleep_for(1ms);
        ASSERT_TRUE(shell.waiting);
        thumbnails.Get("C:/old.jpg", kMonday, 32);
        thumbnails.Get("C:/new.jpg", kMonday, 32);
        shell.open = true;
        ASSERT_TRUE(Settle(thumbnails));
        EXPECT_EQ(shell.read, (std::vector<std::string>{ "slow.jpg", "new.jpg", "old.jpg" }));
    }

    // One let go before it was read isn't read: scrolled past, it's no longer wanted.
    TEST(FileThumbnailsTest, OneLetGoIsntRead)
    {
        StubTextSystem textSystem;
        FakeShell shell;
        shell.open = false;
        FileThumbnails thumbnails(textSystem, 1, shell.Reader(), 0ms);
        thumbnails.Get("C:/slow.jpg", kMonday, 32);
        for (auto end = std::chrono::steady_clock::now() + 5s; !shell.waiting && std::chrono::steady_clock::now() < end;)
            std::this_thread::sleep_for(1ms);
        ASSERT_TRUE(shell.waiting) << "the reading thread waits in slow.jpg";
        thumbnails.Get("C:/passed.jpg", kMonday, 32);   // lets slow.jpg go
        thumbnails.Get("C:/kept.jpg", kMonday, 32);     // lets passed.jpg go
        shell.open = true;
        ASSERT_TRUE(Settle(thumbnails));
        EXPECT_EQ(shell.Reads("passed.jpg"), 0u);
        EXPECT_EQ(shell.Reads("kept.jpg"), 1u);
        EXPECT_EQ(thumbnails.Count(), 1u);
        EXPECT_EQ(textSystem.pixelIconCalls, 1) << "slow.jpg, let go while it was read, makes no icon";
        EXPECT_NE(thumbnails.Get("C:/kept.jpg", kMonday, 32), IconID::Invalid);
    }

    TEST(FileThumbnailsTest, IconsGoWithIt)
    {
        StubTextSystem textSystem;
        FakeShell shell;
        IconID cat = IconID::Invalid;
        {
            FileThumbnails thumbnails(textSystem, 8, shell.Reader());
            thumbnails.Get("C:/cat.jpg", kMonday, 32);
            ASSERT_TRUE(Settle(thumbnails));
            cat = thumbnails.Get("C:/cat.jpg", kMonday, 32);
        }
        EXPECT_EQ(textSystem.unregisteredIcons, std::vector<IconID>{ cat });
    }

#ifdef _WIN32
    // The shell's bitmaps are premultiplied; a bitmap with no alpha at all is opaque.
    TEST(FileThumbnailsTest, BitmapPixelsAreStraight)
    {
        // ARGB, rows from the top: an opaque blue over a half see-through red.
        const uint32_t premultiplied[2] = { 0xFF0000FFu, 0x80800000u };
        HBITMAP bitmap = CreateBitmap(1, 2, 1, 32, premultiplied);
        uint32_t width = 0, height = 0;
        std::vector<uint8_t> rgba;
        ASSERT_TRUE(detail::BitmapPixels(bitmap, width, height, rgba));
        DeleteObject(bitmap);
        ASSERT_EQ(width, 1u);
        ASSERT_EQ(height, 2u);
        ASSERT_EQ(rgba.size(), 8u);
        EXPECT_EQ(rgba[2], 255) << "blue";
        EXPECT_EQ(rgba[3], 255);
        EXPECT_EQ(rgba[4], 255) << "red, no longer darkened by its alpha";
        EXPECT_EQ(rgba[7], 128);

        const uint32_t opaque[1] = { 0x0000FF00u };
        bitmap = CreateBitmap(1, 1, 1, 32, opaque);
        ASSERT_TRUE(detail::BitmapPixels(bitmap, width, height, rgba));
        DeleteObject(bitmap);
        EXPECT_EQ(rgba[1], 255) << "green";
        EXPECT_EQ(rgba[3], 255) << "no alpha anywhere: opaque";
    }

    // A picture on the disk: the shell's thumbnail of it, its shape kept. Skipped where the
    // shell makes none (thumbnails turned off).
    TEST(FileThumbnailsTest, TheShellsThumbnailOfAPicture)
    {
        const std::filesystem::path folder = std::filesystem::temp_directory_path() /
            ("tachyon_thumbnail_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(folder);
        const std::filesystem::path picture = folder / "red.bmp";
        {
            // 64x32, 24 bits, all red; rows are 192 bytes, already a multiple of 4.
            constexpr uint32_t w = 64, h = 32, data = w * h * 3;
            uint8_t header[54] = { 'B', 'M' };
            auto put = [&header](size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) header[at + i] = uint8_t(v >> (8 * i)); };
            put(2, 54 + data);
            put(10, 54);
            put(14, 40);
            put(18, w);
            put(22, h);
            header[26] = 1;
            header[28] = 24;
            put(34, data);
            std::ofstream out(picture, std::ios::binary);
            out.write(reinterpret_cast<const char*>(header), sizeof(header));
            const std::vector<uint8_t> red = [] { std::vector<uint8_t> px(data, 0); for (size_t i = 2; i < px.size(); i += 3) px[i] = 255; return px; }();
            out.write(reinterpret_cast<const char*>(red.data()), std::streamsize(red.size()));
        }
        std::ofstream(folder / "plain.nothumbnail") << "x";

        FileThumbnails::Picture thumbnail;
        const FileThumbnails::Reader shell = FileThumbnails::ShellReader();
        ASSERT_TRUE(shell);
        const bool none = shell(folder / "plain.nothumbnail", 32, thumbnail);
        const bool read = shell(picture, 32, thumbnail);
        std::error_code ec;
        std::filesystem::remove_all(folder, ec);
        EXPECT_FALSE(none) << "a type without thumbnails: no icon in its place";
        if (!read)
            GTEST_SKIP() << "the shell made no thumbnail here";

        EXPECT_LE(thumbnail.size.x, 32u);
        EXPECT_GT(thumbnail.size.x, thumbnail.size.y) << "wider than high, like the picture";
        ASSERT_EQ(thumbnail.rgba.size(), size_t(thumbnail.size.x) * thumbnail.size.y * 4);
        const size_t middle = (size_t(thumbnail.size.y / 2) * thumbnail.size.x + thumbnail.size.x / 2) * 4;
        EXPECT_GE(thumbnail.rgba[middle], 240) << "red";
        EXPECT_LE(thumbnail.rgba[middle + 2], 15);
        EXPECT_EQ(thumbnail.rgba[middle + 3], 255);
    }
#endif
}
