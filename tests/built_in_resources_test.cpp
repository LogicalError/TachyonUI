// The default themes and fonts compiled into the library: found by the end of their
// path, kept in step with the files in workdirectory/, and enough for an app that has no
// resource files at all.

#include <gtest/gtest.h>

#include "headless_gui_app.h"
#include "scoped_temp_dir.h"

#include <gui_core/gui_embedded_files_internal.h>
#include <gui_core/gui_resources.h>
#include <gui_core/graphics/gui_render_primitives.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    using namespace tachyon;

    // A folder that doesn't exist, so nothing can come from disk.
    std::string MissingFolder()
    {
        return (std::filesystem::temp_directory_path() / "tachyon-no-such-resources").generic_string();
    }

    bool Embedded() { return !detail::EmbeddedResourceFiles().empty(); }

    TEST(BuiltInResources, FoundByTheEndOfTheirPath)
    {
        if (!Embedded()) GTEST_SKIP() << "built without TACHYON_EMBED_RESOURCES";
        EXPECT_FALSE(FindBuiltInResource("themes/styles.css").empty());
        EXPECT_FALSE(FindBuiltInResource("C:/app/resources/themes/styles.css").empty());
        EXPECT_FALSE(FindBuiltInResource("/opt/app/themes/../fonts/Inter/Inter-Regular.ttf").empty());
#ifdef _WIN32
        // Backslashes separate folders only on Windows.
        EXPECT_FALSE(FindBuiltInResource("C:\\app\\resources\\themes\\styles.css").empty());
#endif
        EXPECT_FALSE(FindBuiltInResource("fonts/JetBrainsMono/JetBrainsMono-Bold.ttf").empty());
    }

    // Each backend built in has both shaders of every primitive kind, in its own format.
    TEST(BuiltInShaders, EveryKindHasItsPairForEachBackend)
    {
        std::vector<std::string_view> extensions;
        if (TACHYON_VULKAN) extensions.push_back(".spv");
        if (TACHYON_D3D12)  extensions.push_back(".dxil");
        if (TACHYON_SDLGPU) extensions.push_back(".sdl.spv");
#if defined(_WIN32)
        if (TACHYON_SDLGPU) extensions.push_back(".sdl.dxil");
#endif
        if (extensions.empty()) GTEST_SKIP() << "built without a GPU backend";
        for (const PrimitiveSpec& spec : kPrimitiveSpecs)
            for (const std::string_view stage : { ".vert", ".frag" })
                for (const std::string_view extension : extensions)
                {
                    const std::string name = std::string(spec.shaderBaseName) + std::string(stage) + std::string(extension);
                    EXPECT_FALSE(detail::FindEmbeddedFile(detail::EmbeddedShaderFiles(), name).empty()) << name;
                }
    }

    // Direct3D 12 runs only signed DXIL: a container, "DXBC", whose 16-byte hash isn't zero. Both
    // the Direct3D 12 backend and SDL GPU (on Windows) build a DXIL pair per primitive kind.
    TEST(BuiltInShaders, TheDXILIsSigned)
    {
#if defined(_WIN32)
        const size_t backends = size_t(TACHYON_D3D12) + size_t(TACHYON_SDLGPU);
#else
        const size_t backends = size_t(TACHYON_D3D12);
#endif
        if (backends == 0) GTEST_SKIP() << "built without a backend that runs DXIL";
        size_t dxil = 0;
        for (const detail::EmbeddedFile& file : detail::EmbeddedShaderFiles())
        {
            if (!file.key.ends_with(".dxil"))
                continue;
            ++dxil;
            ASSERT_GE(file.data.size(), 20u) << file.key;
            EXPECT_EQ(std::memcmp(file.data.data(), "DXBC", 4), 0) << file.key;
            const auto hash = file.data.subspan(4, 16);
            EXPECT_TRUE(std::any_of(hash.begin(), hash.end(), [](std::byte b) { return b != std::byte{ 0 }; }))
                << file.key << " is unsigned";
        }
        EXPECT_EQ(dxil, std::size(kPrimitiveSpecs) * 2 * backends);
    }

    // The shaders keep their 16-bit fields packed in 32-bit words and do their math in 32 bits,
    // so they run on GPUs without 16-bit shader types: SPIR-V declares no 16- or 8-bit capability.
    TEST(BuiltInShaders, TheSPIRVNeedsNo16Or8BitTypes)
    {
        constexpr uint32_t kMagic = 0x07230203, kOpCapability = 17;
        // Float16, Int16, Int8; 16-bit storage (buffer, uniform, push constant, in/out); 8-bit storage.
        constexpr uint32_t kNarrow[] = { 9, 22, 39, 4433, 4434, 4435, 4436, 4448, 4449, 4450 };
        size_t modules = 0;
        for (const detail::EmbeddedFile& file : detail::EmbeddedShaderFiles())
        {
            if (!file.key.ends_with(".spv"))
                continue;
            ++modules;
            std::vector<uint32_t> words(file.data.size() / 4);
            std::memcpy(words.data(), file.data.data(), words.size() * 4);
            ASSERT_GT(words.size(), 5u) << file.key;
            ASSERT_EQ(words[0], kMagic) << file.key;
            for (size_t i = 5; i < words.size();)
            {
                const uint32_t opcode = words[i] & 0xFFFFu, count = words[i] >> 16;
                ASSERT_GT(count, 0u) << file.key;
                if (opcode == kOpCapability && count == 2)
                    EXPECT_EQ(std::ranges::count(kNarrow, words[i + 1]), 0) << file.key << " declares capability " << words[i + 1];
                i += count;
            }
        }
        if (modules == 0) GTEST_SKIP() << "built without a backend that runs SPIR-V";
    }

    // Nor does the DXIL ask for native 16-bit operations (the "SFI0" part's feature flags).
    TEST(BuiltInShaders, TheDXILNeedsNoNative16BitOps)
    {
        constexpr uint64_t kNative16BitOps = 0x40000;   // D3D_SHADER_REQUIRES_NATIVE_16BIT_OPS
        size_t modules = 0;
        for (const detail::EmbeddedFile& file : detail::EmbeddedShaderFiles())
        {
            if (!file.key.ends_with(".dxil"))
                continue;
            ++modules;
            auto read32 = [&](size_t at) { uint32_t v = 0; std::memcpy(&v, file.data.data() + at, 4); return v; };
            // DXBC, a 16-byte hash, a version, the container's size, the part count, then each
            // part's offset; a part is its four letters, its size and its data.
            ASSERT_GE(file.data.size(), 32u) << file.key;
            const uint32_t parts = read32(28);
            bool found = false;
            for (uint32_t p = 0; p < parts; ++p)
            {
                const uint32_t at = read32(32 + 4 * p);
                ASSERT_LE(size_t(at) + 16, file.data.size()) << file.key;
                if (std::memcmp(file.data.data() + at, "SFI0", 4) != 0)
                    continue;
                uint64_t flags = 0;
                std::memcpy(&flags, file.data.data() + at + 8, 8);
                EXPECT_EQ(flags & kNative16BitOps, 0u) << file.key << " needs native 16-bit operations";
                found = true;
            }
            (void)found;   // a shader that needs nothing extra has no SFI0 part
        }
        if (modules == 0) GTEST_SKIP() << "built without a backend that runs DXIL";
    }

    TEST(BuiltInResources, OnlyAtAFolderBoundary)
    {
        EXPECT_TRUE(FindBuiltInResource("styles.css").empty());
        EXPECT_TRUE(FindBuiltInResource("mythemes/styles.css").empty());
        EXPECT_TRUE(FindBuiltInResource("themes/showcase.css").empty());
    }

    // The tests run in workdirectory/, which holds the files the copies were made from.
    TEST(BuiltInResources, MatchTheFilesTheyWereMadeFrom)
    {
        if (!Embedded()) GTEST_SKIP() << "built without TACHYON_EMBED_RESOURCES";
        for (const detail::EmbeddedFile& file : detail::EmbeddedResourceFiles())
        {
            std::ifstream in(std::string(file.key), std::ios::binary);
            ASSERT_TRUE(in.good()) << file.key;
            const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            ASSERT_EQ(bytes.size(), file.data.size()) << file.key;
            EXPECT_EQ(std::memcmp(bytes.data(), file.data.data(), bytes.size()), 0) << file.key;
        }
    }

    // A file on disk wins over the built-in copy of the same name.
    TEST(BuiltInResources, ReadResourcePrefersTheFileOnDisk)
    {
        if (!Embedded()) GTEST_SKIP() << "built without TACHYON_EMBED_RESOURCES";
        const GUITest::ScopedTempDir temp("tachyon-resource-test");
        const std::filesystem::path folder = temp / "themes";
        std::filesystem::create_directories(folder);
        const std::filesystem::path file = folder / "dark.css";
        {
            std::ofstream out(file, std::ios::binary);
            out << ":root { --accent: #123456; }\n";
        }
        const Result<std::string> read = ReadResource(file.string());
        ASSERT_TRUE(read) << read.GetError().message;
        EXPECT_EQ(*read, ":root { --accent: #123456; }\n");
    }

    TEST(BuiltInResources, ReadResourceReportsAFileWithNoBuiltInCopy)
    {
        const Result<std::string> missing = ReadResource(MissingFolder() + "/themes/showcase.css");
        ASSERT_FALSE(missing);
        EXPECT_EQ(missing.GetError().code, ErrorCode::NotFound);
    }

    TEST(BuiltInResources, ReadResourceFallsBackToTheBuiltInCopy)
    {
        if (!Embedded()) GTEST_SKIP() << "built without TACHYON_EMBED_RESOURCES";
        const Result<std::string> builtIn = ReadResource(MissingFolder() + "/themes/dark.css");
        ASSERT_TRUE(builtIn) << builtIn.GetError().message;
        const Result<std::string> onDisk = ReadResource("themes/dark.css");
        ASSERT_TRUE(onDisk);
        EXPECT_EQ(*builtIn, *onDisk);
    }

    // An app with no resource files loads the default theme: palette, styles.css and the
    // @font-face fonts it names.
    TEST(BuiltInResources, AThemeLoadsWithoutFiles)
    {
        if (!Embedded()) GTEST_SKIP() << "built without TACHYON_EMBED_RESOURCES";
        GUITest::HeadlessGUIApp app(MissingFolder() + "/themes/dark.css");
        ASSERT_TRUE(app.Initialize());
        EXPECT_TRUE(app.theme.FindStyle("button")) << "styles.css is loaded";
        EXPECT_TRUE(app.theme.FindStyle("window"));
        EXPECT_NE(app.theme.GetColor("--accent", color4f{ 0, 0, 0, 0 }).a, 0.0f) << "the palette is loaded";
    }
}
