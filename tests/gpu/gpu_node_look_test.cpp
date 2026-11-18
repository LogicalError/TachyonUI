// Nodes, states and frames on a real device, in their own colors: a node's header tinted with
// its color over the node's body, its line under the header stronger; a state filled with its
// color and edged in it; a frame faint with its color, its title bar stronger, edged in it.

#include <gtest/gtest.h>

#include <gui_controls/gui_application_internal.h>

#include "gpu_injected_input.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    using namespace tachyon;

    struct Rgb { int r = 0, g = 0, b = 0; };
    int Distance(Rgb a, Rgb b) { return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b); }
    // `over` at `alpha` over `base`, as 0-255.
    Rgb Mix(color4f base, color4f over, float alpha)
    {
        auto m = [&](float a, float b) { return int(std::lround((a * (1.0f - alpha) + b * alpha) * 255.0f)); };
        return { m(base.r, over.r), m(base.g, over.g), m(base.b, over.b) };
    }

    class NodeLookApp : public GUIApplicationInternals
    {
    public:
        ID windowID = ID::Invalid();
        NodeView view;
        const color4f red  { 0.88f, 0.44f, 0.42f, 1.0f };
        const color4f teal { 0.20f, 0.82f, 0.75f, 1.0f };

        void OnGUI() override
        {
            if (BeginWindow(windowID, "Nodes", theme.mainWindowStyleRef, LayoutOverride{}.Size(900, 600),
                            ElementFlags::WindowRenderToImage))
                RenderNodeView(view);
            EndWindow();
        }

        bool Start(NodeViewKind kind)
        {
            UseInjectedInputOnly(*this);
            if (!Initialize())
                return false;
            windowID = idManager.CreateStaticID();
            view.kind = kind;
            view.showMinimap = false;
            NodeModel node;
            node.stableID = idManager.CreateStaticID();
            node.title = "N";   // short: the header's middle is clear of it
            node.pos = { -100, -60 };
            node.categoryColor = red;
            if (kind == NodeViewKind::Graph)
                node.properties.push_back(NodeProperty{ .output = true });
            view.nodes.push_back(std::move(node));
            if (kind == NodeViewKind::Graph)
                view.frames.push_back(NodeFrame{ .stableID = idManager.CreateStaticID(), .title = "F", .color = teal,
                                                 .pos = { -380, -160 }, .size = { 200, 240 } });
            SetRedrawMode(RedrawMode::Always);
            return true;
        }

        bool Frames(int count)
        {
            for (int i = 0; i < count; i++)
            {
                if (!UpdateFrame() || !RecordFrame()) return false;
                PresentFrame();
            }
            return true;
        }

        IntRect OnScreen(ElementRef ref) { return elementStorage.ElementRectOnScreen(ref); }

        std::vector<uint8_t> pixels;
        int width = 0;
        bool Read()
        {
            const IntRect rect = windowManager->GetWindowRect(windowID);
            width = rect.w;
            pixels.assign(size_t(std::max(rect.w, 0)) * size_t(std::max(rect.h, 0)) * 4, 0);
            return !pixels.empty() && ReadWindowPixels(windowID, pixels);
        }
        Rgb At(int x, int y) const
        {
            const uint8_t* p = pixels.data() + (size_t(y) * size_t(width) + size_t(x)) * 4;   // BGRA
            return { p[2], p[1], p[0] };
        }
        // The pixel nearest `want` in the 5 by 5 around (x, y): past the canvas's grid dots.
        Rgb Nearest(int x, int y, Rgb want) const
        {
            Rgb best = At(x, y);
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx)
                    if (Distance(At(x + dx, y + dy), want) < Distance(best, want)) best = At(x + dx, y + dy);
            return best;
        }
    };

    std::string Show(Rgb c) { return std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b); }

    TEST(GpuNodeLook, ANodesHeaderIsTintedWithItsColor)
    {
        NodeLookApp app;
        if (!app.Start(NodeViewKind::Graph)) { app.Cleanup(); GTEST_SKIP() << "no GPU device available"; }
        ASSERT_TRUE(app.Frames(4));
        ASSERT_TRUE(app.Read());
        const ElementRef node = app.elementStorage.FindElementRefByID(app.view.nodes[0].stableID);
        ASSERT_TRUE(node.IsValid());
        const ElementRef header = app.elementStorage.GetChildrenSpan(node).empty() ? ElementRef{}
                                : app.elementStorage.FindElementRefByID(app.elementStorage.GetChildrenSpan(node)[0].id);
        ASSERT_TRUE(header.IsValid());
        const IntRect h = app.OnScreen(header);
        const IntRect n = app.OnScreen(node);
        const color4f body = app.theme.GetColor("--node-bg");
        const Rgb tint = Mix(body, app.red, app.theme.nodeTints.header);
        const Rgb line = Mix(body, app.red, app.theme.nodeTints.headerLine);
        EXPECT_LT(Distance(app.At(h.x + h.w * 3 / 4, h.y + h.h / 2), tint), 12) << Show(app.At(h.x + h.w * 3 / 4, h.y + h.h / 2));
        EXPECT_LT(Distance(app.At(h.x + h.w * 3 / 4, h.y + h.h - 1), line), 12) << "the line under it, stronger";
        EXPECT_LT(Distance(app.At(n.x + n.w / 2, n.y + n.h - 3), Mix(body, body, 0.0f)), 12) << "the body is the node's own color";
        app.Cleanup();
    }

    TEST(GpuNodeLook, AStateIsFilledAndEdgedInItsColor)
    {
        NodeLookApp app;
        if (!app.Start(NodeViewKind::StateMachine)) { app.Cleanup(); GTEST_SKIP() << "no GPU device available"; }
        ASSERT_TRUE(app.Frames(4));
        ASSERT_TRUE(app.Read());
        const ElementRef state = app.elementStorage.FindElementRefByID(app.view.nodes[0].stableID);
        ASSERT_TRUE(state.IsValid());
        const IntRect s = app.OnScreen(state);
        const color4f body = app.theme.GetColor("--node-bg");
        const Rgb fill = Mix(body, app.red, app.theme.nodeTints.state);
        const Rgb edge = Mix(body, app.red, app.theme.nodeTints.stateBorder);
        EXPECT_LT(Distance(app.At(s.x + s.w * 3 / 4, s.y + s.h / 2), fill), 12) << Show(app.At(s.x + s.w * 3 / 4, s.y + s.h / 2));
        EXPECT_LT(Distance(app.At(s.x + s.w / 2, s.y), edge), 12) << "edged in its color: " << Show(app.At(s.x + s.w / 2, s.y));
        app.Cleanup();
    }

    // The state the machine is in: an ok ring round it in a soft halo, and the time it's been
    // in it in a pill under it.
    TEST(GpuNodeLook, AnActiveStateIsRingedWithItsTimeUnderIt)
    {
        NodeLookApp app;
        if (!app.Start(NodeViewKind::StateMachine)) { app.Cleanup(); GTEST_SKIP() << "no GPU device available"; }
        app.view.activeState = app.view.nodes[0].stableID;
        app.view.activeStateTime = 1.24f;
        ASSERT_TRUE(app.Frames(4));
        ASSERT_TRUE(app.Read());
        const IntRect s = app.OnScreen(app.elementStorage.FindElementRefByID(app.view.nodes[0].stableID));
        const color4f ok = app.theme.GetColor("--ok");
        const Rgb okRgb = Mix(ok, ok, 0.0f);
        const int ring = app.styleManager.Px(3);
        // Its 2px ring sits from 3px out to 1px out, in the state's shadow: the ok green, a
        // little darker.
        const Rgb ringPixel = app.At(s.x - ring + 1, s.y + s.h / 2);
        EXPECT_GT(ringPixel.g - ringPixel.r, (okRgb.g - okRgb.r) / 2) << "the ring: " << Show(ringPixel);
        EXPECT_GT(ringPixel.g, okRgb.g * 3 / 4) << "the ring: " << Show(ringPixel);
        const Rgb canvas = Mix(app.theme.GetColor("--bg-canvas"), ok, 0.0f);
        const Rgb halo = app.At(s.x - ring - app.styleManager.Px(2), s.y + s.h / 2);
        EXPECT_GT(Distance(halo, canvas), 6) << "the halo round it";
        EXPECT_LT(Distance(halo, canvas), Distance(okRgb, canvas) / 2) << "soft";
        // The pill: ink in the ok color under the state's middle.
        int inked = 0;
        for (int y = s.y + s.h + ring; y < s.y + s.h + ring + app.styleManager.Px(24); ++y)
            for (int x = s.x + s.w / 2 - app.styleManager.Px(20); x < s.x + s.w / 2 + app.styleManager.Px(20); ++x)
                if (Distance(app.At(x, y), okRgb) < 120) ++inked;
        EXPECT_GT(inked, 10) << "its time, in the ok color";
        app.Cleanup();
    }

    // A state holding a machine of its own has a second box stacked behind it, up and left.
    TEST(GpuNodeLook, ANestedStateHasABoxStackedBehindIt)
    {
        NodeLookApp app;
        if (!app.Start(NodeViewKind::StateMachine)) { app.Cleanup(); GTEST_SKIP() << "no GPU device available"; }
        app.view.nodes[0].subMachine = true;
        ASSERT_TRUE(app.Frames(4));
        ASSERT_TRUE(app.Read());
        const IntRect s = app.OnScreen(app.elementStorage.FindElementRefByID(app.view.nodes[0].stableID));
        // In the state's shadow, so darker than the state, but red where the canvas is grey.
        const int o = app.styleManager.Px(4);
        const Rgb behind = app.At(s.x - o / 2, s.y + s.h / 2);
        const Rgb edge   = app.At(s.x - o, s.y + s.h / 2);
        const Rgb beside = app.At(s.x - o / 2, s.y + s.h + o);   // past the box's bottom: the canvas
        EXPECT_GT(behind.r - behind.g, 12) << "behind it, its fill: " << Show(behind);
        EXPECT_GT(edge.r - edge.g, 20) << "and its edge, stronger: " << Show(edge);
        EXPECT_LT(std::abs(beside.r - beside.g), 8) << "not past it: " << Show(beside);
        app.Cleanup();
    }

    TEST(GpuNodeLook, AFrameIsFaintWithItsColorItsTitleBarStronger)
    {
        NodeLookApp app;
        if (!app.Start(NodeViewKind::Graph)) { app.Cleanup(); GTEST_SKIP() << "no GPU device available"; }
        ASSERT_TRUE(app.Frames(4));
        ASSERT_TRUE(app.Read());
        // The frame where it is from the node, at 100%: its title bar a row pitch tall (two grid
        // cells), the box going on below it.
        const ElementRef node = app.elementStorage.FindElementRefByID(app.view.nodes[0].stableID);
        ASSERT_TRUE(node.IsValid());
        const IntRect n = app.OnScreen(node);
        const NodeFrame& frame = app.view.frames[0];
        const IntRect t{ n.x + frame.pos.x - app.view.nodes[0].pos.x, n.y + frame.pos.y - app.view.nodes[0].pos.y,
                         frame.size.x, 2 * app.theme.nodeGridSize };
        const IntRect window = app.windowManager->GetWindowRect(app.windowID);
        ASSERT_TRUE(t.x >= 3 && t.y >= 3 && t.x + t.w + 3 <= window.w && t.y + t.h + 83 <= window.h)
            << "in the window: " << t.x << "," << t.y;
        const color4f canvas = app.theme.GetColor("--bg-canvas");
        const Rgb fill   = Mix(canvas, app.teal, app.theme.nodeTints.frameFill);
        // The title bar is over the box's fill.
        const float boxA = app.theme.nodeTints.frameFill;
        const color4f boxColor{ canvas.r + (app.teal.r - canvas.r) * boxA, canvas.g + (app.teal.g - canvas.g) * boxA,
                                canvas.b + (app.teal.b - canvas.b) * boxA, 1.0f };
        const Rgb band   = Mix(boxColor, app.teal, app.theme.nodeTints.frameBand);
        const Rgb border = Mix(canvas, app.teal, app.theme.nodeTints.frameBorder);
        const Rgb inBox = app.Nearest(t.x + t.w / 2, t.y + t.h + 80, fill);
        EXPECT_LT(Distance(inBox, fill), 12) << "faint: " << Show(inBox);
        const Rgb inBand = app.Nearest(t.x + t.w * 3 / 4, t.y + t.h / 2, band);
        EXPECT_LT(Distance(inBand, band), 12) << "the title bar stronger: " << Show(inBand);
        EXPECT_LT(Distance(app.At(t.x, t.y + t.h + 80), border), 12) << "edged in its color: " << Show(app.At(t.x, t.y + t.h + 80));
        app.Cleanup();
    }
}
