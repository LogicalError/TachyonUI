// LayeredFades: the stand-in windows that show a popup while it fades. On Windows each is a
// layered, click-through window that never takes the focus, moved and removed on request;
// elsewhere there are none. Shown far off-screen, so nothing flashes on the desktop.

#include <gtest/gtest.h>

#include <gui_core/windows/gui_layered_fade.h>

#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace tachyon
{
    namespace
    {
#ifdef _WIN32
        // The stand-in windows there are now.
        std::vector<HWND> StandIns()
        {
            std::vector<HWND> found;
            for (HWND h = FindWindowExW(nullptr, nullptr, L"TachyonLayeredFade", nullptr); h != nullptr;
                 h = FindWindowExW(nullptr, h, L"TachyonLayeredFade", nullptr))
                found.push_back(h);
            return found;
        }
#endif

        TEST(LayeredFades, ShowMoveAndEnd)
        {
            LayeredFades fades;
            const int2 size{ 40, 30 };
            const std::vector<uint8_t> bgra(size_t(size.x) * size.y * 4, 200);
            const IntRect at{ -20000, -20000, size.x, size.y };
            const uint32_t fade = fades.Show(bgra, size, at, 0.5f);
#ifdef _WIN32
            ASSERT_NE(fade, 0u);
            EXPECT_EQ(fades.Count(), 1u);
            const std::vector<HWND> windows = StandIns();
            ASSERT_EQ(windows.size(), 1u);
            const HWND hwnd = windows[0];
            EXPECT_TRUE(IsWindowVisible(hwnd));
            const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            EXPECT_TRUE(ex & WS_EX_LAYERED);
            EXPECT_TRUE(ex & WS_EX_TRANSPARENT) << "clicks go through";
            EXPECT_TRUE(ex & WS_EX_NOACTIVATE) << "never takes the focus";
            RECT r{};
            GetWindowRect(hwnd, &r);
            EXPECT_EQ(r.left, at.x);
            EXPECT_EQ(r.top, at.y);
            EXPECT_EQ(r.right - r.left, size.x);

            fades.Move(fade, IntRect{ at.x + 7, at.y + 3, size.x, size.y }, 1.0f);
            GetWindowRect(hwnd, &r);
            EXPECT_EQ(r.left, at.x + 7);
            EXPECT_EQ(r.top, at.y + 3);

            fades.End(fade);
            EXPECT_EQ(fades.Count(), 0u);
            EXPECT_FALSE(IsWindow(hwnd)) << "gone";
            fades.End(fade);   // again: ignored
#else
            EXPECT_EQ(fade, 0u) << "no stand-ins here";
#endif
        }

        TEST(LayeredFades, NothingForAnEmptyPicture)
        {
            LayeredFades fades;
            EXPECT_EQ(fades.Show({}, int2{ 10, 10 }, IntRect{ 0, 0, 10, 10 }, 1.0f), 0u);
            const std::vector<uint8_t> small(16, 0);
            EXPECT_EQ(fades.Show(small, int2{ 10, 10 }, IntRect{ 0, 0, 10, 10 }, 1.0f), 0u) << "too few pixels";
            EXPECT_EQ(fades.Count(), 0u);
        }

        // Its stand-ins go with it.
        TEST(LayeredFades, EndsAllWhenDestroyed)
        {
#ifdef _WIN32
            const std::vector<uint8_t> bgra(16 * 16 * 4, 255);
            {
                LayeredFades fades;
                fades.Show(bgra, int2{ 16, 16 }, IntRect{ -20000, -20000, 16, 16 }, 1.0f);
                fades.Show(bgra, int2{ 16, 16 }, IntRect{ -20000, -19000, 16, 16 }, 1.0f);
                EXPECT_EQ(StandIns().size(), 2u);
            }
            EXPECT_TRUE(StandIns().empty());
#else
            GTEST_SKIP() << "no stand-ins here";
#endif
        }
    }
}
