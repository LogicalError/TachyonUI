// The focus an image window's host gives it, against the OS's: a menu opened from an image
// window is an OS window, and the OS giving it the focus or taking it away again must not
// read as the focus moving, or the open menu closes.

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <gui_core/windows/gui_image_window_backend.h>
#include <gui_core/windows/gui_window_flags.h>

#include "mock_window_backend.h"

namespace tachyon
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;
    using ::testing::ReturnPointee;
    using GUITest::MockWindowBackend;

    class ImageWindowBackendFocusTest : public ::testing::Test
    {
    protected:
        NiceMock<MockWindowBackend> os;
        ImageWindowBackend          backend{ os };
        uint32_t                    osFocusChanges = 0;
        OSWindowHandle              osActive       = nullptr;
        intptr_t                    nextHandle     = 0;

        void SetUp() override
        {
            ON_CALL(os, GetFocusChangeCount()).WillByDefault(ReturnPointee(&osFocusChanges));
            ON_CALL(os, GetActiveWindow()).WillByDefault(ReturnPointee(&osActive));
            ON_CALL(os, CreateWindow(_, testing::A<IntRect>(), _, _))
                .WillByDefault([this](std::string_view, IntRect, WindowFlags, OSWindowHandle) {
                    return reinterpret_cast<OSWindowHandle>(++nextHandle);
                });
            ON_CALL(os, DestroyWindow(_)).WillByDefault(Return(true));
        }

        // The OS moved its focus to `window`, or to another app's (null).
        void OSFocus(OSWindowHandle window)
        {
            osActive = window;
            ++osFocusChanges;
        }
    };

    TEST_F(ImageWindowBackendFocusTest, TheOSFocusOnAMenuOfAnImageWindowIsNotAFocusChange)
    {
        const OSWindowHandle image = backend.CreateWindow("image", IntRect{ 0, 0, 900, 600 }, WindowFlags::RenderToImage, nullptr);
        backend.SetFocus(image);
        const uint32_t before = backend.GetFocusChangeCount();

        const OSWindowHandle osMenu = backend.CreateWindow("menu", IntRect{ 400, 300, 200, 100 }, WindowFlags::PopupMenu, image);
        OSFocus(osMenu);
        EXPECT_EQ(backend.GetFocusChangeCount(), before) << "the OS activated the menu";
        EXPECT_EQ(backend.GetActiveWindow(), image);

        OSFocus(nullptr);
        EXPECT_EQ(backend.GetFocusChangeCount(), before) << "another app took the focus from the menu";
    }

    TEST_F(ImageWindowBackendFocusTest, TheOSFocusOnAnotherOSWindowIsAFocusChange)
    {
        const OSWindowHandle image = backend.CreateWindow("image", IntRect{ 0, 0, 900, 600 }, WindowFlags::RenderToImage, nullptr);
        const OSWindowHandle osMain = backend.CreateWindow("main", IntRect{ 0, 0, 800, 600 }, WindowFlags::None, nullptr);
        backend.SetFocus(image);
        const uint32_t before = backend.GetFocusChangeCount();

        OSFocus(osMain);
        EXPECT_EQ(backend.GetFocusChangeCount(), before + 1) << "the app's own OS window took the focus";
        OSFocus(nullptr);
        EXPECT_EQ(backend.GetFocusChangeCount(), before + 2) << "and lost it";
    }

    TEST_F(ImageWindowBackendFocusTest, WithoutTheHostsFocusEveryOSFocusChangeCounts)
    {
        const OSWindowHandle image = backend.CreateWindow("image", IntRect{ 0, 0, 900, 600 }, WindowFlags::RenderToImage, nullptr);
        const uint32_t before = backend.GetFocusChangeCount();

        const OSWindowHandle osMenu = backend.CreateWindow("menu", IntRect{ 400, 300, 200, 100 }, WindowFlags::PopupMenu, image);
        OSFocus(osMenu);
        EXPECT_EQ(backend.GetFocusChangeCount(), before + 1);
    }

    TEST_F(ImageWindowBackendFocusTest, TheHostMovingTheFocusCounts)
    {
        const OSWindowHandle image = backend.CreateWindow("image", IntRect{ 0, 0, 900, 600 }, WindowFlags::RenderToImage, nullptr);
        const uint32_t before = backend.GetFocusChangeCount();
        backend.SetFocus(image);
        EXPECT_EQ(backend.GetFocusChangeCount(), before + 1);
        backend.SetFocus(nullptr);
        EXPECT_EQ(backend.GetFocusChangeCount(), before + 2);
    }
}
