#include <gui_core/frame/gui_redraw.h>

#include <algorithm>
#include <climits>

namespace tachyon
{
    void RedrawRequests::RequestAt(int64_t nanos)
    {
        std::lock_guard lock(m_mutex);
        if (std::find(m_at.begin(), m_at.end(), nanos) == m_at.end())
            m_at.push_back(nanos);
    }

    bool RedrawRequests::IsDue(int64_t nowNanos) const
    {
        return m_now || NextAt() <= nowNanos;
    }

    int64_t RedrawRequests::NextAt() const
    {
        std::lock_guard lock(m_mutex);
        return m_at.empty() ? INT64_MAX : *std::min_element(m_at.begin(), m_at.end());
    }

    void RedrawRequests::Answer(int64_t nowNanos)
    {
        m_now = false;
        std::lock_guard lock(m_mutex);
        std::erase_if(m_at, [nowNanos](int64_t at) { return at <= nowNanos; });
    }
}
