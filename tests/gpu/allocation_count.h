#pragma once

// Heap allocations made on this thread while a ScopedAllocationCount lives, counted through the
// debug CRT's allocation hook: Debug builds with MSVC's CRT. Elsewhere Supported() is false and
// nothing is counted.

#include <cstdint>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#define TACHYON_TEST_ALLOCATION_HOOK 1
#else
#define TACHYON_TEST_ALLOCATION_HOOK 0
#endif

namespace AllocationCount
{
    /// Allocations this thread makes while one lives aren't counted: a library's own, routed
    /// through its allocator hooks.
    inline thread_local int t_ignored = 0;
    struct Ignored
    {
        Ignored() { ++t_ignored; }
        ~Ignored() { --t_ignored; }
    };

#if TACHYON_TEST_ALLOCATION_HOOK
    inline thread_local uint64_t* t_counter = nullptr;

    inline int Hook(int type, void*, size_t, int block, long, const unsigned char*, int)
    {
        // The CRT's own blocks are its bookkeeping, not the code's.
        if (t_counter != nullptr && t_ignored == 0 && block != _CRT_BLOCK && (type == _HOOK_ALLOC || type == _HOOK_REALLOC))
            ++*t_counter;
        return 1;   // let it through
    }
#endif

    class ScopedAllocationCount
    {
    public:
        static constexpr bool Supported() { return TACHYON_TEST_ALLOCATION_HOOK != 0; }

        ScopedAllocationCount()
        {
#if TACHYON_TEST_ALLOCATION_HOOK
            m_previous = _CrtSetAllocHook(&Hook);
            t_counter  = &m_count;
#endif
        }
        ~ScopedAllocationCount()
        {
#if TACHYON_TEST_ALLOCATION_HOOK
            t_counter = nullptr;
            _CrtSetAllocHook(m_previous);
#endif
        }
        ScopedAllocationCount(const ScopedAllocationCount&)            = delete;
        ScopedAllocationCount& operator=(const ScopedAllocationCount&) = delete;

        uint64_t Count() const { return m_count; }

    private:
        uint64_t m_count = 0;
#if TACHYON_TEST_ALLOCATION_HOOK
        _CRT_ALLOC_HOOK m_previous = nullptr;
#endif
    };
}
