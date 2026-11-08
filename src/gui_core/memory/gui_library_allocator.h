#pragma once

#include <gui_core/memory/gui_allocation_hooks.h>

#include <cstddef>

// The memory functions HarfBuzz, skribidi, SheenBidi and nanosvg are built to call in place
// of malloc, calloc, realloc and free. Each block remembers what made it (the C heap or a set
// of hooks), so it is always freed by that, whatever is routed by then.
extern "C"
{
    void* tachyon_library_malloc(size_t size);
    void* tachyon_library_calloc(size_t count, size_t size);
    void* tachyon_library_realloc(void* ptr, size_t size);
    void  tachyon_library_free(void* ptr);
}

namespace tachyon
{
    /// @addtogroup diagnostics
    /// @{

    /// Where the text and SVG libraries (HarfBuzz, skribidi, SheenBidi, nanosvg) get their
    /// memory: the C heap, or a host's allocation hooks while a route is in place.
    namespace LibraryAllocator
    {
        /// A route, as `Route` returns it; null when nothing was routed.
        using RouteHandle = const void*;

        /// From now on the libraries allocate through `hooks`. Returns null when `hooks` is
        /// not set, or when too many different hooks were routed in this process already.
        RouteHandle Route(const AllocationHooks& hooks);

        /// Back to the C heap, if `route` is still the one in place. Blocks made through its
        /// hooks are still freed by them, so the hooks must outlive those blocks.
        void Unroute(RouteHandle route);

        /// Whether the libraries allocate through hooks right now.
        bool IsRouted();

        /// While one lives, what the libraries allocate on this thread comes from the C heap,
        /// even with a route in place. For data they keep until the process ends (HarfBuzz
        /// keeps every language it is given), which hooks may not live to free.
        class HeapScope
        {
        public:
            HeapScope();
            ~HeapScope();
            HeapScope(const HeapScope&) = delete;
            HeapScope& operator=(const HeapScope&) = delete;
        };
    }
    /// @}
}
