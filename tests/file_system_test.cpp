// MemoryFileSystem: folders and files kept in memory, or copied from the disk as they're
// browsed; moves and renames change only the copy.

#include <gtest/gtest.h>

#include <gui_controls/gui_file_icons.h>
#include <gui_controls/gui_file_system.h>

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
#include <chrono>
#include <fstream>
#include <string>
#include <vector>

namespace tachyon
{
    namespace
    {
        // The names in a folder, sorted, folders marked with a trailing '/'.
        std::vector<std::string> Names(IFileSystem& fs, std::string_view folder)
        {
            std::vector<std::string> names;
            for (const FileEntry& e : fs.List(folder))
                names.push_back(e.isFolder ? e.name + "/" : e.name);
            std::sort(names.begin(), names.end());
            return names;
        }

        TEST(FileSystemTest, PathsJoinAndSplit)
        {
            EXPECT_EQ(JoinPath("", "a"), "a");
            EXPECT_EQ(JoinPath("a/b", "c"), "a/b/c");
            EXPECT_EQ(ParentPath("a/b/c"), "a/b");
            EXPECT_EQ(ParentPath("a"), "");
            EXPECT_EQ(FileName("a/b/c"), "c");
            EXPECT_EQ(FileName("a"), "a");
        }

        // Added folders and files are listed, with the folders on their way.
        TEST(FileSystemTest, WhatsAddedIsListed)
        {
            MemoryFileSystem fs;
            uint64_t generation = fs.Generation();
            EXPECT_TRUE(fs.AddFile("Projects/TachyonUI/readme.txt", 120));
            EXPECT_GT(fs.Generation(), generation) << "a view lists again";
            generation = fs.Generation();
            EXPECT_TRUE(fs.AddFolder("Music"));
            EXPECT_GT(fs.Generation(), generation);
            EXPECT_EQ(Names(fs, ""), (std::vector<std::string>{ "Music/", "Projects/" }));
            EXPECT_EQ(Names(fs, "Projects"), (std::vector<std::string>{ "TachyonUI/" }));
            const std::vector<FileEntry> files = fs.List("Projects/TachyonUI");
            ASSERT_EQ(files.size(), 1u);
            EXPECT_EQ(files[0].name, "readme.txt");
            EXPECT_FALSE(files[0].isFolder);
            EXPECT_EQ(files[0].size, 120u);

            EXPECT_FALSE(fs.AddFile("Projects/TachyonUI/readme.txt")) << "already there";
            EXPECT_FALSE(fs.AddFolder("Projects/TachyonUI/readme.txt/inside")) << "a file in the way";
            EXPECT_FALSE(fs.AddFolder("Projects/TachyonUI/readme.txt")) << "a file, not a folder";
            EXPECT_FALSE(fs.AddFile("")) << "the root";
            EXPECT_TRUE(fs.AddFolder("Music")) << "a folder that's there";
            EXPECT_TRUE(fs.List("Nowhere").empty());
            EXPECT_TRUE(fs.List("Projects/TachyonUI/readme.txt").empty()) << "a file has no entries";
        }

        // A file or folder moves into another folder, with what's in it; not into itself,
        // nor where its name is taken.
        TEST(FileSystemTest, MovesGoIntoFolders)
        {
            MemoryFileSystem fs;
            fs.AddFile("Home/photo.jpg");
            fs.AddFile("Home/Images/old.png");
            fs.AddFile("Home/Projects/Vulkan/shader.frag");
            fs.AddFile("Home/Projects/photo.jpg");
            std::string error;
            uint64_t generation = fs.Generation();

            EXPECT_TRUE(fs.Move("Home/Projects/Vulkan", "Home/Images", error));
            EXPECT_GT(fs.Generation(), generation);
            EXPECT_EQ(Names(fs, "Home/Images"), (std::vector<std::string>{ "Vulkan/", "old.png" }));
            EXPECT_EQ(Names(fs, "Home/Images/Vulkan"), (std::vector<std::string>{ "shader.frag" })) << "with what's in it";
            EXPECT_EQ(Names(fs, "Home/Projects"), (std::vector<std::string>{ "photo.jpg" }));

            generation = fs.Generation();
            EXPECT_TRUE(fs.Move("Home/photo.jpg", "Home", error)) << "where it is";
            EXPECT_EQ(fs.Generation(), generation) << "nothing changed";

            EXPECT_FALSE(fs.Move("Home/photo.jpg", "Home/Projects", error)) << "the name is taken";
            EXPECT_NE(error.find("photo.jpg"), std::string::npos);
            EXPECT_FALSE(fs.Move("Home", "Home/Images/Vulkan", error)) << "into itself";
            EXPECT_FALSE(fs.Move("Home/Images", "Home/Images", error)) << "into itself";
            EXPECT_FALSE(fs.Move("Home/nothing.txt", "Home", error));
            EXPECT_FALSE(fs.Move("Home/photo.jpg", "Home/Nowhere", error));
            EXPECT_FALSE(fs.Move("Home/photo.jpg", "Home/Images/old.png", error)) << "into a file";
            EXPECT_FALSE(fs.Move("", "Home", error)) << "the root";
            EXPECT_EQ(fs.Generation(), generation) << "a refused move changes nothing";

            EXPECT_TRUE(fs.Move("Home/photo.jpg", "", error)) << "to the top";
            EXPECT_EQ(Names(fs, ""), (std::vector<std::string>{ "Home/", "photo.jpg" }));
        }

        // A rename keeps the file in its folder; a name taken, empty or with a separator is
        // refused.
        TEST(FileSystemTest, RenamesStayInTheFolder)
        {
            MemoryFileSystem fs;
            fs.AddFile("Docs/todo.txt");
            fs.AddFile("Docs/notes.txt");
            std::string error;
            uint64_t generation = fs.Generation();
            EXPECT_TRUE(fs.Rename("Docs/todo.txt", "done.txt", error));
            EXPECT_GT(fs.Generation(), generation);
            EXPECT_EQ(Names(fs, "Docs"), (std::vector<std::string>{ "done.txt", "notes.txt" }));
            EXPECT_TRUE(fs.Rename("Docs", "Documents", error));
            EXPECT_EQ(Names(fs, "Documents"), (std::vector<std::string>{ "done.txt", "notes.txt" }));

            generation = fs.Generation();
            EXPECT_TRUE(fs.Rename("Documents/notes.txt", "notes.txt", error)) << "its own name";
            EXPECT_EQ(fs.Generation(), generation);
            for (const char* bad : { "", ".", "..", "a/b", "a\\b", "done.txt" })
            {
                EXPECT_FALSE(fs.Rename("Documents/notes.txt", bad, error)) << bad;
                EXPECT_FALSE(error.empty()) << bad;
            }
            EXPECT_FALSE(fs.Rename("Documents/nothing.txt", "x.txt", error));
            EXPECT_FALSE(fs.Rename("", "x", error)) << "the root";
            EXPECT_EQ(fs.Generation(), generation);
        }

        // A copy of a disk folder: read as it's browsed, changed only in memory.
        class DiskFileSystemTest : public ::testing::Test
        {
        protected:
            std::filesystem::path root;

            void SetUp() override
            {
                root = std::filesystem::temp_directory_path() /
                       ("tachyon_fs_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
                std::filesystem::create_directories(root / "Images" / "Deep");
                std::filesystem::create_directories(root / "Docs");
                std::ofstream(root / "Images" / "photo.jpg") << "12345";
                std::ofstream(root / "Docs" / "todo.txt") << "x";
                std::ofstream(root / "Images" / "Deep" / "far.png") << "";
            }
            void TearDown() override
            {
                std::error_code ec;
                std::filesystem::remove_all(root, ec);
            }
        };

        TEST_F(DiskFileSystemTest, ItReadsTheDiskAsItsBrowsed)
        {
            MemoryFileSystem fs(root);
            EXPECT_EQ(Names(fs, ""), (std::vector<std::string>{ "Docs/", "Images/" }));
            const std::vector<FileEntry> images = fs.List("Images");
            const auto photo = std::find_if(images.begin(), images.end(), [](const FileEntry& e) { return e.name == "photo.jpg"; });
            ASSERT_NE(photo, images.end());
            EXPECT_EQ(photo->size, 5u);
            EXPECT_EQ(Names(fs, "Images/Deep"), (std::vector<std::string>{ "far.png" }));

            // Added to the disk after a folder was read: not seen; before: seen.
            std::ofstream(root / "Docs" / "later.txt") << "";
            EXPECT_EQ(Names(fs, "Docs"), (std::vector<std::string>{ "later.txt", "todo.txt" })) << "read now";
            std::ofstream(root / "Docs" / "after.txt") << "";
            EXPECT_EQ(Names(fs, "Docs"), (std::vector<std::string>{ "later.txt", "todo.txt" })) << "read once";
        }

        TEST_F(DiskFileSystemTest, ChangesStayInMemory)
        {
            MemoryFileSystem fs(root);
            std::string error;
            // Into a folder not read yet: it's read first, so its own entries are there too.
            EXPECT_TRUE(fs.Move("Docs/todo.txt", "Images/Deep", error)) << error;
            EXPECT_EQ(Names(fs, "Images/Deep"), (std::vector<std::string>{ "far.png", "todo.txt" }));
            EXPECT_TRUE(fs.Rename("Images", "Pictures", error)) << error;
            EXPECT_EQ(Names(fs, ""), (std::vector<std::string>{ "Docs/", "Pictures/" }));
            EXPECT_EQ(Names(fs, "Pictures"), (std::vector<std::string>{ "Deep/", "photo.jpg" }));

            // The disk is as it was.
            EXPECT_TRUE(std::filesystem::exists(root / "Docs" / "todo.txt"));
            EXPECT_TRUE(std::filesystem::exists(root / "Images"));
            EXPECT_FALSE(std::filesystem::exists(root / "Pictures"));
        }

        // A folder not read yet is read before a move into it: a name on the disk is taken.
        TEST_F(DiskFileSystemTest, AMoveSeesWhatsOnTheDisk)
        {
            std::ofstream(root / "Docs" / "photo.jpg") << "";
            MemoryFileSystem fs(root);
            std::string error;
            EXPECT_FALSE(fs.Move("Images/photo.jpg", "Docs", error));
            EXPECT_NE(error.find("photo.jpg"), std::string::npos);
            EXPECT_EQ(Names(fs, "Docs"), (std::vector<std::string>{ "photo.jpg", "todo.txt" }));
        }

        TEST_F(DiskFileSystemTest, AFolderThatIsntThereIsEmpty)
        {
            MemoryFileSystem fs(root / "missing");
            EXPECT_TRUE(fs.List("").empty());
        }

        TEST(FileIconsTest, ExtensionsAreLowercaseFromTheLastDot)
        {
            EXPECT_EQ(FileIcons::Extension("photo.JPG"), ".jpg");
            EXPECT_EQ(FileIcons::Extension("archive.tar.Gz"), ".gz");
            EXPECT_EQ(FileIcons::Extension("Makefile"), "");
            EXPECT_EQ(FileIcons::Extension(".gitignore"), "") << "a hidden file's name, not an extension";
            EXPECT_EQ(FileIcons::Extension("ends."), ".");
        }

        // One icon per extension and one for folders, read once.
        TEST(FileIconsTest, OneIconPerExtension)
        {
            StubTextSystem textSystem;
            FileIcons icons(textSystem, 32);
            const IconID text   = icons.Get("notes.txt", false);
            const IconID folder = icons.Get("Docs", true);
#ifdef _WIN32
            const int calls = textSystem.pixelIconCalls;
            icons.Get("more.txt", false);
            icons.Get("More", true);
            EXPECT_EQ(textSystem.pixelIconCalls, calls) << "read once";
            ASSERT_NE(text, IconID::Invalid) << "the shell has an icon for every extension";
            ASSERT_NE(folder, IconID::Invalid);
            EXPECT_NE(text, folder);
            EXPECT_EQ(icons.Get("TODO.TXT", false), text);
            EXPECT_EQ(icons.Get("Images", true), folder);
            const IconID open = icons.OpenFolder();
            EXPECT_NE(open, IconID::Invalid);
            EXPECT_NE(open, folder) << "its own icon, though it may look the same";
            EXPECT_EQ(icons.OpenFolder(), open);
            EXPECT_NE(icons.Get("photo.png", false), text);
            const IconMeasurement size = textSystem.MeasureIcon(text);
            EXPECT_GE(size.width, 32) << "at least the size asked for";
            EXPECT_EQ(size.width, size.height);

            FileIcons small(textSystem, 16);
            EXPECT_NE(small.Get("notes.txt", false), text) << "another size, another icon";
#else
            EXPECT_EQ(text, IconID::Invalid) << "only the Windows shell gives icons";
            EXPECT_EQ(folder, IconID::Invalid);
            EXPECT_EQ(icons.OpenFolder(), IconID::Invalid);
#endif
        }

#ifdef _WIN32
        // An icon of colors and a mask, without alpha of its own: the mask says what shows.
        TEST(FileIconsTest, AnIconWithoutAlphaUsesItsMask)
        {
            // 2x1, BGRA rows from the top: red, then blue; alpha 0 in both.
            const uint32_t colors[2] = { 0x00FF0000u, 0x000000FFu };
            HBITMAP color = CreateBitmap(2, 1, 1, 32, colors);
            // The mask's set bits are where it's transparent: the right pixel. Rows are
            // padded to 16 bits.
            const uint16_t maskRow = 0x40;   // bits 7..6: left 0, right 1
            HBITMAP mask = CreateBitmap(2, 1, 1, 1, &maskRow);
            ICONINFO info{ .fIcon = TRUE, .hbmMask = mask, .hbmColor = color };
            HICON icon = CreateIconIndirect(&info);
            DeleteObject(color);
            DeleteObject(mask);
            ASSERT_NE(icon, nullptr);

            uint32_t width = 0, height = 0;
            std::vector<uint8_t> rgba;
            ASSERT_TRUE(detail::IconPixels(icon, width, height, rgba));
            DestroyIcon(icon);
            ASSERT_EQ(width, 2u);
            ASSERT_EQ(height, 1u);
            ASSERT_EQ(rgba.size(), 8u);
            EXPECT_EQ(rgba[0], 255) << "red";
            EXPECT_EQ(rgba[2], 0);
            EXPECT_EQ(rgba[3], 255) << "shown";
            EXPECT_EQ(rgba[6], 255) << "blue";
            EXPECT_EQ(rgba[7], 0) << "masked out";
        }
#endif
    }
}
