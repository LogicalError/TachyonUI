// A tooltip on a real device: its arrow, a bordered square on its corner, points at its
// target's text (into a label's from its start, into end-aligned text from its end, at the
// middle of a button's), or at the middle of a target without text or one under a canvas's
// pan and zoom; it comes to a point one pixel wide, and has nothing drawn past that point.
//
// Measured against what's beside the arrow on the same row, not against "nothing": where
// the window can't be see-through its empty part reads back opaque, and a shadow may lie
// under the arrow. Where the tooltip's box never shows up in what its window reads back,
// it skips.

#include <gtest/gtest.h>

#include <gui_controls/gui_application_internal.h>

#include "gpu_injected_input.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef CreateWindow   // IWindowBackend::CreateWindow
#endif

namespace
{
    using namespace tachyon;

    class TooltipApp : public GUIApplicationInternals
    {
    public:
        ID windowID = ID::Invalid(), fieldID = ID::Invalid();
        IntRect fieldRect{}, tooltipLaid{}, boxLaid{}, tooltipRect{};
        int align = 2;            // the target's text: 0 at its start (a label), 1 at its end, 2 centered (a button),
                                  // 3 none, 4 a label in a panned and zoomed canvas
        IntRect onScreen{};       // the target on the screen
        IntRect textContent{};    // ... its padding box's content, laid out
        int32_t textWidth = 0;    // ... and its text, whole
        std::vector<uint8_t> pixels;
        bool drawn = false;
        int frame = 0;

        void OnGUI() override
        {
            if (BeginWindow(windowID, "Tooltip", theme.windowStyleRef, LayoutOverride{}.Size(420, 160),
                            ElementFlags::WindowRenderToImage))
            {
                BeginVertical(LayoutOverride{}.FlexGrow(1));
                if (align == 2)
                    Button(fieldID, "Editable text...", [] {}, LayoutOverride{}.Size(360, 26));
                else if (align == 3)
                    RegisterElement(Element{ .id = fieldID, .styleRef = theme.emptyHorzStyleRef }, LayoutOverride{}.Size(360, 26));
                else if (align == 4)
                {
                    const ElementRef canvas = RegisterElement(Element{ .id = idManager.AllocateDynamicID("canvas"), .styleRef = theme.emptyVertStyleRef },
                                                              LayoutOverride{}.Size(400, 100));
                    elementStorage.SetContentTransform(canvas, float3x2::Translate(40, 4) * float3x2::Scale(1.5f));
                    VERIFY(elementStorage.BeginChildren());
                    Label(fieldID, "Editable text...", theme.labelStyleRef, LayoutOverride{}.Size(200, 26));
                    VERIFY(elementStorage.EndChildren());
                }
                else
                    Label(fieldID, "Editable text...", align == 0 ? theme.labelStyleRef : theme.GetStyle("table-cell-number"),
                          LayoutOverride{}.Size(360, 26));
                SetTooltip(fieldID, "Short");
                EndVertical();
            }
            EndWindow();
            // Once laid out, the pointer goes to the field's middle.
            if (frame == 2)
                InjectMouseMove(windowID, int2{ onScreen.x + onScreen.w / 2, onScreen.y + onScreen.h / 2 });
            // Its first drawing with the box in it: a popup may show a few frames late.
            if (frame >= 8 && !drawn)
            {
                tooltipRect = windowManager->GetWindowRect(tooltipWindowID);
                pixels.resize(size_t(tooltipRect.w) * tooltipRect.h * 4);
                drawn = ReadWindowPixels(tooltipWindowID, pixels) && BoxStandsOut();
            }
            if (++frame >= 30 || (drawn && frame > 10))
                applicationQuit = true;
        }

        // Whether the box reads back different from the window's empty top row, a few pixels
        // into it, anywhere along it.
        bool BoxStandsOut() const
        {
            const int left = boxLaid.x - tooltipLaid.x, y = boxLaid.y - tooltipLaid.y + 3;
            if (boxLaid.w <= 0 || y >= tooltipRect.h)
                return false;
            for (int x = std::max(left, 0); x < std::min(left + boxLaid.w, tooltipRect.w); ++x)
            {
                const uint8_t* a = pixels.data() + (size_t(y) * tooltipRect.w + size_t(x)) * 4;
                const uint8_t* b = pixels.data() + size_t(x) * 4;
                for (int c = 0; c < 4; ++c)
                    if (std::abs(int(a[c]) - int(b[c])) > 16)
                        return true;
            }
            return false;
        }

        bool Start(float scale)
        {
            SetUIScale(scale);
            UseInjectedInputOnly(*this);
            if (!Initialize())
                return false;
            windowID = idManager.CreateStaticID();
            fieldID  = idManager.CreateStaticID();
            tooltipManager.delaySeconds = 0.0f;
            m_frame.OnPostLayout([this](FrameContext&)
            {
                if (const ElementRef ref = elementStorage.FindElementRefByID(fieldID); ref.IsValid())
                {
                    const ElementRect* rect = elementStorage.GetElementRectPtr(ref);
                    fieldRect   = rect->rect;
                    onScreen    = elementStorage.ElementRectOnScreen(ref);
                    textContent = rect->layoutStyle->GetPaddingBoxContentRect(rect->rect);
                    const TextID text = elementStorage.GetElementUnsafe(ref).textID;
                    textWidth   = text != TextID::Invalid ? textSystem->GetTextMeasurement(text).maxWidth : 0;
                }
                // The tooltip's window, and its box: the window's first child.
                if (const ElementRef win = elementStorage.FindElementRefByID(tooltipWindowID); win.IsValid())
                {
                    tooltipLaid = elementStorage.GetElementRectPtr(win)->rect;
                    const auto children = elementStorage.GetChildrenSpan(win);
                    if (!children.empty())
                        if (const ElementRef box = elementStorage.FindElementRefByID(children[0].id); box.IsValid())
                            boxLaid = elementStorage.GetElementRectPtr(box)->rect;
                }
            });
            return true;
        }
    };

#ifdef _WIN32
    // A tooltip's window lets the pointer through: over it, the pointer is still over what's
    // under it, which keeps it hovered (or the tooltip would close and open again under the
    // pointer), and drops from other applications reach that too.
    TEST(GpuTooltipWindow, ThePointerGoesThroughATooltip)
    {
        TooltipApp app;
        if (!app.Start(1.0f))
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        ASSERT_TRUE(app.Run());
        ASSERT_GT(app.boxLaid.w, 0) << "the tooltip didn't show";
        const OSWindowHandle tooltip = app.windowManager->GetWindowHandle(app.tooltipWindowID);
        ASSERT_NE(tooltip, nullptr);
        const HWND hwnd = static_cast<HWND>(app.windowBackend->GetNativeHandle(tooltip));
        ASSERT_NE(hwnd, nullptr);
        const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        EXPECT_NE(exStyle & WS_EX_TRANSPARENT, 0) << "passed over by the OS's hit test";
        EXPECT_NE(exStyle & WS_EX_LAYERED, 0);
        app.Cleanup();

        // A window that takes input doesn't.
        IWindowBackend* backend = nullptr;
        TooltipApp other;
        if (other.Initialize())
        {
            backend = other.windowBackend;
            const OSWindowHandle plain = backend->CreateWindow("gpu-test-plain", int2{ 100, 60 },
                                                               WindowFlags::Hidden | WindowFlags::Transparent | WindowFlags::NotFocusable, nullptr);
            if (plain != nullptr)
            {
                const HWND plainHwnd = static_cast<HWND>(backend->GetNativeHandle(plain));
                EXPECT_EQ(GetWindowLongPtrW(plainHwnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT, 0) << "without NoInput";
                backend->DestroyWindow(plain);
            }
        }
        other.Cleanup();
    }
#endif

    // At 100% and at a zoom that puts the arrow between pixels unless it's snapped to one; for
    // text at its start, its end and centered.
    struct TooltipCase { float scale; int align; };
    class GpuTooltip : public ::testing::TestWithParam<TooltipCase> {};

    TEST_P(GpuTooltip, TheArrowPointsAtTheTextAndComesToAPoint)
    {
        const float scale = GetParam().scale;
        TooltipApp app;
        app.align = GetParam().align;
        if (!app.Start(scale))
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        ASSERT_TRUE(app.Run());
        ASSERT_GT(app.boxLaid.w, 0) << "the tooltip didn't show";
        if (!app.drawn)
        {
            app.Cleanup();
            GTEST_SKIP() << "the tooltip's box never shows up in what its window reads back here";
        }

        const IntRect r = app.tooltipRect;
        const uint8_t* px = app.pixels.data();
        // How far apart two pixels are: the most any channel (alpha too) differs.
        auto apart = [&](int x0, int y0, int x1, int y1) {
            const uint8_t* a = px + (size_t(y0) * r.w + size_t(x0)) * 4;
            const uint8_t* b = px + (size_t(y1) * r.w + size_t(x1)) * 4;
            int most = 0;
            for (int c = 0; c < 4; ++c)
                most = std::max(most, std::abs(int(a[c]) - int(b[c])));
            return most;
        };

        // The box in the window's pixels; the point is the arrow's height above its top.
        const int boxLeft = app.boxLaid.x - app.tooltipLaid.x;
        const int boxTop  = app.boxLaid.y - app.tooltipLaid.y;
        const int tip     = boxTop - int(std::round(6.0f * scale));
        ASSERT_GT(tip, 0) << "room above the point";
        const int side = int(std::round(12.0f * scale));   // beside the arrow on the same row
        // How much a pixel of the arrow stands out from what's beside it on its row, against
        // how much the box's fill does.
        int peak = boxLeft + side;
        for (int x = boxLeft + side; x < boxLeft + app.boxLaid.w - side; ++x)
            if (apart(x, tip, x - side, tip) > apart(peak, tip, peak - side, tip)) peak = x;
        const float full = float(apart(peak, boxTop + 3, peak, 0));
        ASSERT_GT(full, 16.0f);
        auto cover = [&](int x, int y) { return float(apart(x, y, peak - side, y)) / full; };

        // At the target's text: as far into it as the box's own text is from the point (the
        // box's text starts or ends where the target's does), at most its middle; at the middle
        // of centered text.
        const LayoutStyle* box = app.styleManager.GetLayoutStyle(app.theme.tooltipStyleRef.layoutStyleIndex);
        ASSERT_NE(box, nullptr);
        const float fromEdge = 17.5f * scale;
        const int   shown    = std::min(app.textWidth, app.textContent.w);
        const float start    = float(app.align == 0 ? app.textContent.x : app.align == 1 ? app.textContent.Max(0) - shown
                                                                                         : app.textContent.x + (app.textContent.w - shown) / 2);
        const float middle   = start + float(shown) * 0.5f;
        const float at = app.align == 0 ? std::min(start + fromEdge - float(box->borderWidths.left + box->padding.left), middle)
                       : app.align == 1 ? std::max(start + float(shown) - fromEdge + float(box->borderWidths.right + box->padding.right), middle)
                       : app.align == 2 ? middle
                                        : float(app.onScreen.x) + float(app.onScreen.w) * 0.5f;   // its middle on the screen
        if (app.align == 4)
            ASSERT_NE(app.onScreen, app.fieldRect) << "the canvas is panned and zoomed";
        EXPECT_NEAR(float(peak), std::floor(at) - float(app.tooltipLaid.x), 1.0f);
        // One pixel wide: strong in the middle, faint either side, nothing above it.
        EXPECT_GT(cover(peak, tip), 0.6f);
        EXPECT_LT(cover(peak - 1, tip), 0.35f);
        EXPECT_LT(cover(peak + 1, tip), 0.35f);
        EXPECT_LT(cover(peak, tip - 1), 0.15f);
        // Even on both sides a row down.
        EXPECT_NEAR(cover(peak - 2, tip + 1), cover(peak + 2, tip + 1), 0.08f);
        EXPECT_NEAR(cover(peak - 1, tip + 1), cover(peak + 1, tip + 1), 0.08f);
        app.Cleanup();
    }

    INSTANTIATE_TEST_SUITE_P(Scales, GpuTooltip, ::testing::Values(TooltipCase{ 1.0f, 0 }, TooltipCase{ 1.0f, 1 }, TooltipCase{ 1.0f, 2 },
                                                                   TooltipCase{ 1.0f, 3 }, TooltipCase{ 1.0f, 4 },
                                                                   TooltipCase{ 1.25f, 0 }, TooltipCase{ 1.25f, 1 }, TooltipCase{ 1.25f, 2 },
                                                                   TooltipCase{ 1.25f, 3 }, TooltipCase{ 1.25f, 4 }));
}
