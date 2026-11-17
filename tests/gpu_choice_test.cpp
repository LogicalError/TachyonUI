// Choosing the GPU: by kind (discrete, integrated, ...), by its number in the log's list, or
// by part of its name; TACHYON_GPU in place of the app's choice. No GPU is needed: the choice
// is made over a list of names and kinds.

#include <gtest/gtest.h>

#include <gui_core/graphics/gui_gpu_choice.h>

#include <cstdlib>
#include <vector>

namespace tachyon
{
    namespace
    {
        const std::vector<GpuInfo> kGpus = {
            { "NVIDIA GeForce RTX 4070 Ti SUPER", GpuKind::Discrete },
            { "AMD Radeon(TM) Graphics",          GpuKind::Integrated },
            { "llvmpipe (LLVM 17.0.6, 256 bits)", GpuKind::Cpu },
        };

        TEST(GpuChoice, NoPreferenceLeavesItToTheDefault)
        {
            EXPECT_EQ(ChooseGpu(kGpus, ""), std::nullopt);
            EXPECT_EQ(ChooseGpu(kGpus, "  "), std::nullopt);
        }

        TEST(GpuChoice, AKindPicksTheFirstOfThatKind)
        {
            EXPECT_EQ(ChooseGpu(kGpus, "integrated"), 1u);
            EXPECT_EQ(ChooseGpu(kGpus, "Discrete"), 0u);
            EXPECT_EQ(ChooseGpu(kGpus, "cpu"), 2u);
            EXPECT_EQ(ChooseGpu(kGpus, "virtual"), std::nullopt) << "none of that kind";
        }

        TEST(GpuChoice, ANumberIsItsPlaceInTheList)
        {
            EXPECT_EQ(ChooseGpu(kGpus, "1"), 1u);
            EXPECT_EQ(ChooseGpu(kGpus, "0"), 0u);
            EXPECT_EQ(ChooseGpu(kGpus, "3"), std::nullopt) << "past the end";
        }

        TEST(GpuChoice, OtherTextIsPartOfTheName)
        {
            EXPECT_EQ(ChooseGpu(kGpus, "radeon"), 1u);
            EXPECT_EQ(ChooseGpu(kGpus, "RTX 4070"), 0u);
            EXPECT_EQ(ChooseGpu(kGpus, "intel"), std::nullopt);
        }

        void SetEnv(const char* name, const char* value)
        {
#if defined(_WIN32)
            _putenv_s(name, value ? value : "");
#else
            if (value) setenv(name, value, 1); else unsetenv(name);
#endif
        }

        TEST(GpuChoice, TheEnvironmentVariableTakesThePlaceOfTheAppsChoice)
        {
            SetEnv("TACHYON_GPU", "integrated");
            EXPECT_EQ(GpuPreference("discrete"), "integrated");
            SetEnv("TACHYON_GPU", nullptr);
            EXPECT_EQ(GpuPreference("discrete"), "discrete");
            EXPECT_EQ(GpuPreference(""), "");
        }

        TEST(GpuChoice, KindsHaveNames)
        {
            EXPECT_EQ(GpuKindName(GpuKind::Integrated), "integrated");
            EXPECT_EQ(GpuKindName(GpuKind::Discrete), "discrete");
        }
    }
}
