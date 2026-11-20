#pragma once

#include <gui_core/gui_id.h>

#include <cstdint>
#include <iterator>
#include <unordered_map>
#include <utility>

namespace tachyon
{
    /// @addtogroup elements
    /// @{

    /// Per-ID state kept between frames for the elements still built: an entry not written
    /// for kKeepFrames frames goes. For state an immediate-mode control keeps by its ID
    /// (last frame's rect, a measured width), which would otherwise grow by one entry for
    /// every ID ever shown.
    ///
    ///     m_widths[id] = width;               // written while the element is built: kept
    ///     if (const int* w = m_widths.Find(id)) ...
    ///     m_widths.EndFrame();                // once per frame
    template<typename V>
    class RecentIDMap
    {
    public:
        static constexpr uint32_t kKeepFrames  = 300;
        static constexpr uint32_t kSweepPeriod = 64;   ///< frames between sweeps

        /// The value for `id`, made when absent; kept for kKeepFrames more frames.
        V& operator[](ID id)
        {
            Entry& entry = m_entries[id];
            entry.seenFrame = m_frame;
            return entry.value;
        }

        /// Keeps `id`'s entry, when there is one, for kKeepFrames more frames.
        void Touch(ID id)
        {
            if (auto it = m_entries.find(id); it != m_entries.end())
                it->second.seenFrame = m_frame;
        }

        /// The value for `id`, or null. Reading doesn't keep it.
        V* Find(ID id)
        {
            auto it = m_entries.find(id);
            return it != m_entries.end() ? &it->second.value : nullptr;
        }
        const V* Find(ID id) const { return const_cast<RecentIDMap*>(this)->Find(id); }

        void   Erase(ID id) { m_entries.erase(id); }
        void   Clear()      { m_entries.clear(); }
        size_t Size() const { return m_entries.size(); }

        /// Calls `visit(id, value)` for each entry, in no particular order.
        template<typename Visit>
        void ForEach(Visit&& visit)
        {
            for (auto& [id, entry] : m_entries)
                visit(id, entry.value);
        }

        /// Ends a frame. Every kSweepPeriod frames, drops the entries not written for
        /// kKeepFrames frames that `mayDrop(id, value)` agrees to (it can free what the value
        /// holds; return false to keep the entry anyway).
        template<typename MayDrop>
        void EndFrame(MayDrop&& mayDrop)
        {
            if (++m_frame % kSweepPeriod != 0)
                return;
            for (auto it = m_entries.begin(); it != m_entries.end(); )
            {
                if (m_frame - it->second.seenFrame > kKeepFrames && mayDrop(it->first, it->second.value))
                    it = m_entries.erase(it);
                else
                    ++it;
            }
        }
        void EndFrame() { EndFrame([](ID, V&) { return true; }); }

    private:
        struct Entry
        {
            V        value{};
            uint64_t seenFrame = 0;
        };
        std::unordered_map<ID, Entry> m_entries;
        uint64_t                      m_frame = 0;
    };

    /// @}
} // namespace tachyon
