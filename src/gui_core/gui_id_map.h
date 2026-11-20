#pragma once

#include <gui_core/gui_id.h>

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace tachyon
{
    /// @addtogroup elements
    /// @{

    /// A hash map from ID to a small value, for indexes rebuilt or pruned every frame: open
    /// addressing in one array, so adding allocates nothing once the map has grown to the
    /// frame's size, and Clear keeps that room. Entries go only all at once (Clear) or by a
    /// sweep (EraseIf). ID::Invalid() marks a free slot, so it can't be a key.
    template<typename T>
    class IDMap
    {
    public:
        /// The value held for `key`, or null.
        T* Find(ID key)
        {
            if (m_size == 0 || key == ID::Invalid())
                return nullptr;
            for (size_t i = Home(key);; i = (i + 1) & Mask())
            {
                Slot& slot = m_slots[i];
                if (slot.key == key)            return &slot.value;
                if (slot.key == ID::Invalid())  return nullptr;
            }
        }
        const T* Find(ID key) const { return const_cast<IDMap*>(this)->Find(key); }

        /// Adds `key` with `value` unless the key is there already. Returns the value held
        /// for the key and whether it was added; {null, false} for ID::Invalid(), which marks
        /// a free slot and can't be a key.
        std::pair<T*, bool> TryEmplace(ID key, const T& value)
        {
            assert(key != ID::Invalid());
            if (key == ID::Invalid())
                return { nullptr, false };
            if ((m_size + 1) * 2 > m_slots.size())
                Grow();
            for (size_t i = Home(key);; i = (i + 1) & Mask())
            {
                Slot& slot = m_slots[i];
                if (slot.key == key)
                    return { &slot.value, false };
                if (slot.key == ID::Invalid())
                {
                    slot = Slot{ key, value };
                    ++m_size;
                    return { &slot.value, true };
                }
            }
        }

        /// Empties the map, keeping its room, unless that is far more than this fill needed:
        /// after one huge frame, later small ones don't pay to clear (or hold) all of it.
        void Clear()
        {
            if (m_size == 0)
                return;
            if (m_slots.size() > kMinCapacity && m_size * 16 < m_slots.size())
            {
                size_t capacity = kMinCapacity;
                while (capacity < m_size * 4)
                    capacity *= 2;
                std::vector<Slot>(capacity).swap(m_slots);
                m_shift = 64u - uint32_t(std::countr_zero(capacity));
                m_size  = 0;
                return;
            }
            for (Slot& slot : m_slots)
                slot.key = ID::Invalid();
            m_size = 0;
        }

        /// Calls `visit(key, value)` for each entry, in no particular order.
        template<typename Visit>
        void ForEach(Visit&& visit)
        {
            if (m_size == 0)
                return;
            for (Slot& slot : m_slots)
                if (slot.key != ID::Invalid())
                    visit(slot.key, slot.value);
        }

        /// Drops each entry `erase(key, value)` is true for. Once over the table: the entries
        /// kept are set aside and added again, so the probe chains stay whole.
        template<typename Erase>
        void EraseIf(Erase&& erase)
        {
            if (m_size == 0)
                return;
            m_kept.clear();
            for (Slot& slot : m_slots)
                if (slot.key != ID::Invalid())
                {
                    if (!erase(slot.key, slot.value))
                        m_kept.push_back(slot);
                    slot.key = ID::Invalid();
                }
            m_size = 0;
            for (const Slot& slot : m_kept)
                TryEmplace(slot.key, slot.value);
        }

        size_t Size() const     { return m_size; }
        size_t Capacity() const { return m_slots.size(); }

    private:
        struct Slot
        {
            ID key = ID::Invalid();
            T  value {};
        };

        static constexpr size_t kMinCapacity = 64;

        size_t Mask() const { return m_slots.size() - 1; }
        /// Fibonacci hashing: the multiply spreads every key bit into the top bits, which pick
        /// the slot (an ID's low bits are its mode, and a static ID's are a small index).
        size_t Home(ID key) const { return size_t((key.Value() * 0x9E3779B97F4A7C15ull) >> m_shift); }

        void Grow()
        {
            std::vector<Slot> old = std::move(m_slots);
            const size_t capacity = old.empty() ? kMinCapacity : old.size() * 2;
            m_slots.assign(capacity, Slot{});
            m_shift = 64u - uint32_t(std::countr_zero(capacity));
            m_size = 0;
            for (const Slot& slot : old)
                if (slot.key != ID::Invalid())
                    TryEmplace(slot.key, slot.value);
        }

        std::vector<Slot> m_slots;
        std::vector<Slot> m_kept;   ///< EraseIf's, kept for its room
        size_t            m_size  = 0;
        uint32_t          m_shift = 64;
    };

    /// @}
} // namespace tachyon
