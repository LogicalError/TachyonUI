#include <gui_core/memory/gui_library_allocator.h>

#include <gui_core/diagnostics/gui_logging.h>

#include <harfbuzz/hb.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>

static constexpr tachyon::Diagnostics::LogCategory kLogMemory = tachyon::Diagnostics::MakeLogCategory("memory");

namespace
{
    using tachyon::AllocationHooks;

    // A set of hooks blocks can point back to. Kept for the whole process, since a block
    // made through them can be freed long after its route ended.
    struct Record
    {
        AllocationHooks hooks;
    };

    constexpr size_t kMaxRoutes = 16;
    Record s_records[kMaxRoutes];
    size_t s_recordCount = 0;   // under RecordMutex()
    std::atomic<const Record*> s_current{ nullptr };
    thread_local int s_heapScopes = 0;   // HeapScopes open on this thread

    // What allocates now on this thread: the routed hooks, or null for the C heap.
    const Record* Current()
    {
        return s_heapScopes > 0 ? nullptr : s_current.load(std::memory_order_acquire);
    }

    std::mutex& RecordMutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    constexpr size_t kAlignment = alignof(std::max_align_t) > 16 ? alignof(std::max_align_t) : 16;

    // In front of every block: what the caller asked for, and what made it (null: the C heap).
    struct alignas(kAlignment) Header
    {
        size_t        size;
        const Record* origin;
    };

    Header* HeaderOf(void* ptr)
    {
        return static_cast<Header*>(ptr) - 1;
    }

    void* Allocate(size_t size, const Record* origin)
    {
        if (size > SIZE_MAX - sizeof(Header))
            return nullptr;
        const size_t total = sizeof(Header) + size;
        void* base = origin != nullptr ? origin->hooks.allocate(total, kAlignment, origin->hooks.user)
                                       : std::malloc(total);
        if (base == nullptr)
            return nullptr;
        Header* header = static_cast<Header*>(base);
        header->size   = size;
        header->origin = origin;
        return header + 1;
    }

    void Release(Header* header)
    {
        if (header->origin != nullptr)
            header->origin->hooks.free(header, header->origin->hooks.user);
        else
            std::free(header);
    }

    // HarfBuzz makes some data once per process and frees it at exit: made here, on the C
    // heap, it never reaches hooks that may be gone by then.
    void PrepareProcessCaches()
    {
        hb_language_get_default();
        hb_unicode_funcs_get_default();
        hb_shape_list_shapers();
        hb_font_t* font = hb_font_create(hb_face_get_empty());   // with the OpenType font functions
        hb_buffer_t* buffer = hb_buffer_create();
        hb_buffer_add_utf8(buffer, "a", 1, 0, 1);
        hb_buffer_guess_segment_properties(buffer);
        hb_shape(font, buffer, nullptr, 0);
        hb_buffer_destroy(buffer);
        hb_font_destroy(font);
        // HarfBuzz keeps every language it is given; these skribidi asks for when breaking words.
        for (const char* language : { "ja", "zh-hant", "zh-hans", "th" })
            hb_language_from_string(language, -1);
    }

    bool SameHooks(const AllocationHooks& a, const AllocationHooks& b)
    {
        return a.allocate == b.allocate && a.reallocate == b.reallocate && a.free == b.free && a.user == b.user;
    }
}

extern "C" void* tachyon_library_malloc(size_t size)
{
    return Allocate(size, Current());
}

extern "C" void* tachyon_library_calloc(size_t count, size_t size)
{
    if (size != 0 && count > SIZE_MAX / size)
        return nullptr;
    void* ptr = tachyon_library_malloc(count * size);
    if (ptr != nullptr)
        std::memset(ptr, 0, count * size);
    return ptr;
}

extern "C" void* tachyon_library_realloc(void* ptr, size_t size)
{
    const Record* current = Current();
    if (ptr == nullptr)
        return Allocate(size, current);
    if (size > SIZE_MAX - sizeof(Header))
        return nullptr;

    Header* old = HeaderOf(ptr);
    if (old->origin == current)
    {
        const size_t total = sizeof(Header) + size;
        void* base = current != nullptr ? current->hooks.reallocate(old, total, kAlignment, current->hooks.user)
                                        : std::realloc(old, total);
        if (base == nullptr)
            return nullptr;
        Header* header = static_cast<Header*>(base);
        header->size = size;
        return header + 1;
    }

    // Made by what was routed before: moved to what is routed now.
    void* moved = Allocate(size, current);
    if (moved == nullptr)
        return nullptr;
    std::memcpy(moved, ptr, std::min(old->size, size));
    Release(old);
    return moved;
}

extern "C" void tachyon_library_free(void* ptr)
{
    if (ptr != nullptr)
        Release(HeaderOf(ptr));
}

namespace tachyon::LibraryAllocator
{
    RouteHandle Route(const AllocationHooks& hooks)
    {
        if (!hooks.IsSet())
            return nullptr;
        std::lock_guard lock(RecordMutex());
        if (s_recordCount == 0 && s_current.load(std::memory_order_acquire) == nullptr)
            PrepareProcessCaches();
        const Record* record = nullptr;
        for (size_t i = 0; i < s_recordCount && record == nullptr; ++i)
            if (SameHooks(s_records[i].hooks, hooks))
                record = &s_records[i];
        if (record == nullptr)
        {
            if (s_recordCount == kMaxRoutes)
            {
                TACHYON_LOG_WARNING(kLogMemory, "{} different allocation hooks were routed already; the text libraries stay on their current allocator", kMaxRoutes);
                return nullptr;
            }
            s_records[s_recordCount].hooks = hooks;
            record = &s_records[s_recordCount++];
        }
        s_current.store(record, std::memory_order_release);
        return record;
    }

    void Unroute(RouteHandle route)
    {
        if (route == nullptr)
            return;
        const Record* expected = static_cast<const Record*>(route);
        s_current.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    }

    bool IsRouted()
    {
        return s_current.load(std::memory_order_acquire) != nullptr;
    }

    HeapScope::HeapScope()  { ++s_heapScopes; }
    HeapScope::~HeapScope() { --s_heapScopes; }
}
