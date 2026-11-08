// The library allocator: where HarfBuzz, skribidi, SheenBidi and nanosvg get their memory.
// The C heap by default; a host's hooks while a route is in place. Each block is freed by
// what made it, whatever is routed by then.

#include <gtest/gtest.h>

#include <gui_core/memory/gui_library_allocator.h>
#include <gui_core/memory/gui_tracking_allocator.h>
#include <gui_core/text/skribidi/gui_text_system_skribidi.h>

#include <harfbuzz/hb.h>
#include <SheenBidi/SheenBidi.h>
#include <skribidi/skb_common.h>

#include <cstring>
#include <string>

// nanosvg, as gui_core builds it.
extern "C"
{
    struct NSVGimage;
    NSVGimage* nsvgParse(char* input, const char* units, float dpi);
    void nsvgDelete(NSVGimage* image);
}

namespace
{
    using namespace tachyon;

    // Routes `hooks` for its lifetime.
    class ScopedRoute
    {
    public:
        explicit ScopedRoute(const AllocationHooks& hooks) : m_route(LibraryAllocator::Route(hooks)) {}
        ~ScopedRoute() { LibraryAllocator::Unroute(m_route); }
        ScopedRoute(const ScopedRoute&) = delete;
        ScopedRoute& operator=(const ScopedRoute&) = delete;
        LibraryAllocator::RouteHandle Handle() const { return m_route; }

    private:
        LibraryAllocator::RouteHandle m_route;
    };

    // Lives as long as the process: a library may keep a block made through its hooks
    // (HarfBuzz caches some) and free it at exit.
    TrackingAllocator& Tracker()
    {
        static TrackingAllocator* tracker = new TrackingAllocator();
        return *tracker;
    }

    TEST(LibraryAllocator, WithoutARouteItUsesTheCHeap)
    {
        TrackingAllocator tracker;
        EXPECT_EQ(LibraryAllocator::Route(AllocationHooks{}), nullptr) << "hooks not set: no route";
        EXPECT_FALSE(LibraryAllocator::IsRouted());

        void* ptr = tachyon_library_malloc(64);
        ASSERT_NE(ptr, nullptr);
        std::memset(ptr, 0xab, 64);
        ptr = tachyon_library_realloc(ptr, 4096);
        ASSERT_NE(ptr, nullptr);
        EXPECT_EQ(static_cast<unsigned char*>(ptr)[63], 0xab) << "kept by realloc";
        tachyon_library_free(ptr);
        tachyon_library_free(nullptr);
        EXPECT_EQ(tracker.TotalAllocations(), 0u);
    }

    TEST(LibraryAllocator, ARouteSendsItsAllocationsThroughTheHooks)
    {
        TrackingAllocator tracker;
        {
            ScopedRoute route(tracker.Hooks());
            ASSERT_NE(route.Handle(), nullptr);
            EXPECT_TRUE(LibraryAllocator::IsRouted());

            char* ptr = static_cast<char*>(tachyon_library_malloc(10));
            ASSERT_NE(ptr, nullptr);
            EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % alignof(std::max_align_t), 0u) << "aligned as malloc's are";
            EXPECT_EQ(tracker.LiveAllocations(), 1u);
            std::memcpy(ptr, "abcdefghi", 10);
            ptr = static_cast<char*>(tachyon_library_realloc(ptr, 100000));
            ASSERT_NE(ptr, nullptr);
            EXPECT_STREQ(ptr, "abcdefghi") << "kept by realloc";
            EXPECT_EQ(tracker.LiveAllocations(), 1u);

            unsigned char* zeroed = static_cast<unsigned char*>(tachyon_library_calloc(16, 4));
            ASSERT_NE(zeroed, nullptr);
            for (int i = 0; i < 64; ++i)
                ASSERT_EQ(zeroed[i], 0) << i;
            EXPECT_EQ(tracker.LiveAllocations(), 2u);
            EXPECT_EQ(tachyon_library_calloc(SIZE_MAX / 4 + 2, 4), nullptr) << "too big: refused, not wrapped to 4 bytes";
            EXPECT_EQ(tachyon_library_malloc(SIZE_MAX - 4), nullptr);

            tachyon_library_free(ptr);
            tachyon_library_free(zeroed);
            EXPECT_EQ(tracker.LiveAllocations(), 0u);
        }
        EXPECT_FALSE(LibraryAllocator::IsRouted()) << "unrouted again";
    }

    // Blocks are freed by what made them, whatever is routed when they are freed or grown.
    TEST(LibraryAllocator, BlocksAreFreedByWhatMadeThem)
    {
        TrackingAllocator tracker;
        void* fromHeap = tachyon_library_malloc(32);
        void* grownIntoHooks = tachyon_library_malloc(8);
        std::memcpy(grownIntoHooks, "1234567", 8);
        void* fromHooks = nullptr;
        void* grownOutOfHooks = nullptr;
        {
            ScopedRoute route(tracker.Hooks());
            tachyon_library_free(fromHeap);
            EXPECT_EQ(tracker.TotalAllocations(), 0u) << "a heap block goes back to the heap";

            grownIntoHooks = tachyon_library_realloc(grownIntoHooks, 64);
            EXPECT_STREQ(static_cast<char*>(grownIntoHooks), "1234567");
            EXPECT_EQ(tracker.LiveAllocations(), 1u) << "grown: moved into the hooks";

            fromHooks = tachyon_library_malloc(16);
            grownOutOfHooks = tachyon_library_malloc(8);
            std::memcpy(grownOutOfHooks, "7654321", 8);
            EXPECT_EQ(tracker.LiveAllocations(), 3u);
        }
        tachyon_library_free(fromHooks);
        EXPECT_EQ(tracker.LiveAllocations(), 2u) << "freed by the hooks that made it";
        grownOutOfHooks = tachyon_library_realloc(grownOutOfHooks, 4096);
        EXPECT_STREQ(static_cast<char*>(grownOutOfHooks), "7654321");
        EXPECT_EQ(tracker.LiveAllocations(), 1u) << "grown: moved to the heap";
        tachyon_library_free(grownOutOfHooks);
        tachyon_library_free(grownIntoHooks);
        EXPECT_EQ(tracker.LiveAllocations(), 0u);
    }

    // A block grown by what made it, then moved: all of its grown size moves with it.
    TEST(LibraryAllocator, AGrownBlockMovesWhole)
    {
        TrackingAllocator tracker;
        unsigned char* ptr = nullptr;
        {
            ScopedRoute route(tracker.Hooks());
            ptr = static_cast<unsigned char*>(tachyon_library_malloc(8));
            ptr = static_cast<unsigned char*>(tachyon_library_realloc(ptr, 64));
            for (int i = 0; i < 64; ++i)
                ptr[i] = static_cast<unsigned char>(i);
        }
        ptr = static_cast<unsigned char*>(tachyon_library_realloc(ptr, 128));
        ASSERT_NE(ptr, nullptr);
        EXPECT_EQ(tracker.LiveAllocations(), 0u) << "moved to the heap";
        for (int i = 0; i < 64; ++i)
            ASSERT_EQ(ptr[i], i) << i;
        tachyon_library_free(ptr);
    }

    // Ending an older route leaves a newer one in place; the same hooks give the same route.
    TEST(LibraryAllocator, EndingAnOlderRouteKeepsTheNewerOne)
    {
        TrackingAllocator first, second;
        const LibraryAllocator::RouteHandle a = LibraryAllocator::Route(first.Hooks());
        const LibraryAllocator::RouteHandle b = LibraryAllocator::Route(second.Hooks());
        ASSERT_NE(a, nullptr);
        ASSERT_NE(b, nullptr);
        EXPECT_NE(a, b);
        LibraryAllocator::Unroute(a);
        EXPECT_TRUE(LibraryAllocator::IsRouted()) << "the second route is still in place";
        void* ptr = tachyon_library_malloc(8);
        EXPECT_EQ(second.LiveAllocations(), 1u);
        EXPECT_EQ(first.TotalAllocations(), 0u);
        tachyon_library_free(ptr);
        LibraryAllocator::Unroute(b);
        EXPECT_FALSE(LibraryAllocator::IsRouted());
        LibraryAllocator::Unroute(nullptr);

        EXPECT_EQ(LibraryAllocator::Route(first.Hooks()), a) << "the same hooks: the same route";
        LibraryAllocator::Unroute(a);
    }

    // In a heap scope this thread allocates on the C heap, route or not; a block made there
    // and grown after it is grown on the heap too.
    TEST(LibraryAllocator, AHeapScopeKeepsAllocationsOnTheHeap)
    {
        TrackingAllocator tracker;
        ScopedRoute route(tracker.Hooks());
        void* kept = nullptr;
        {
            LibraryAllocator::HeapScope heap;
            {
                LibraryAllocator::HeapScope nested;
            }
            kept = tachyon_library_malloc(24);
            kept = tachyon_library_realloc(kept, 48);
            EXPECT_TRUE(LibraryAllocator::IsRouted()) << "the route is still in place";
        }
        EXPECT_EQ(tracker.TotalAllocations(), 0u) << "made on the heap, nested scope closed or not";
        void* routed = tachyon_library_malloc(8);
        EXPECT_EQ(tracker.LiveAllocations(), 1u) << "after the scope: through the hooks again";
        tachyon_library_free(routed);
        tachyon_library_free(kept);
        EXPECT_EQ(tracker.LiveAllocations(), 0u);
    }

    // A text system with hooks routes the libraries while it lives.
    TEST(LibraryAllocator, ATextSystemRoutesWhileItLives)
    {
        TrackingAllocator tracker;
        {
            SkribidiTextSystem withHooks(tracker.Hooks());
            EXPECT_TRUE(LibraryAllocator::IsRouted());
        }
        EXPECT_FALSE(LibraryAllocator::IsRouted());
        {
            SkribidiTextSystem withoutHooks;
            EXPECT_FALSE(LibraryAllocator::IsRouted());
        }
    }

    // HarfBuzz, skribidi, SheenBidi and nanosvg all allocate through a route.
    TEST(LibraryAllocator, TheTextAndSvgLibrariesAllocateThroughIt)
    {
        // HarfBuzz sets up some process-wide data on first use; made now, on the heap.
        hb_buffer_destroy(hb_buffer_create());

        TrackingAllocator& tracker = Tracker();
        ScopedRoute route(tracker.Hooks());
        const size_t before = tracker.TotalAllocations();

        hb_buffer_t* buffer = hb_buffer_create();
        EXPECT_GT(tracker.TotalAllocations(), before) << "HarfBuzz";
        EXPECT_GT(tracker.LiveAllocations(), 0u);
        hb_buffer_destroy(buffer);
        EXPECT_EQ(tracker.LiveAllocations(), 0u);

        size_t made = tracker.TotalAllocations();
        void* skb = skb_malloc(32);
        EXPECT_EQ(tracker.TotalAllocations(), made + 1) << "skribidi";
        skb = skb_realloc(skb, 64);
        skb_free(skb);
        EXPECT_EQ(tracker.LiveAllocations(), 0u);

        made = tracker.TotalAllocations();
        const SBCodepoint text[] = { 'a', 0x05d0, 0x05d1, 'b' };   // Latin and Hebrew
        const SBCodepointSequence sequence{ SBStringEncodingUTF32, text, 4 };
        SBAlgorithmRef bidi = SBAlgorithmCreate(&sequence);
        ASSERT_NE(bidi, nullptr);
        SBParagraphRef paragraph = SBAlgorithmCreateParagraph(bidi, 0, 4, SBLevelDefaultLTR);
        EXPECT_GT(tracker.TotalAllocations(), made) << "SheenBidi";
        SBParagraphRelease(paragraph);
        SBAlgorithmRelease(bidi);
        EXPECT_EQ(tracker.LiveAllocations(), 0u);

        made = tracker.TotalAllocations();
        std::string svg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="8" height="8"><rect width="4" height="4"/></svg>)";
        NSVGimage* image = nsvgParse(svg.data(), "px", 96.0f);
        ASSERT_NE(image, nullptr);
        EXPECT_GT(tracker.TotalAllocations(), made) << "nanosvg";
        nsvgDelete(image);
        EXPECT_EQ(tracker.LiveAllocations(), 0u);
    }
}
