#include <gui_core/state/gui_scroll_management.h>

#include <gui_core/elements/gui_element_storage.h>

#include <gui_core/gui_id_manager.h>

#include <assert.h>

namespace tachyon
{
    void ScrollManagement::BeginFrame()
    {
        m_frameIndex++;
        if (m_frameIndex == 0)
            m_frameIndex = 1;
    }

    void ScrollManagement::EndFrame(IDManager& idManager)
    {
        if (m_scrollOffsetHandlesByID.Size() == 0)
            return;

        auto shouldKeep = [&](ID id, ScrollOffsetHandle handle)
        {
            const ScrollOffsetEntry& entry = m_scrollOffsets[(uint32_t)handle];
            if (!entry.inUse)
                return false;
            if (id.GetMode() == IDMode::Static)
                return idManager.IsStaticIDValid(id);
            return entry.lastSeenFrame == m_frameIndex;
        };

        // Most frames keep them all: sweep only when one goes.
        bool anyGoes = false;
        m_scrollOffsetHandlesByID.ForEach([&](ID id, ScrollOffsetHandle handle) { anyGoes = anyGoes || !shouldKeep(id, handle); });
        if (!anyGoes)
            return;

        m_scrollOffsetHandlesByID.EraseIf([&](ID id, ScrollOffsetHandle handle)
        {
            if (shouldKeep(id, handle))
                return false;
            if ((uint32_t)handle < m_scrollOffsets.size())
            {
                m_scrollOffsets[(uint32_t)handle] = ScrollOffsetEntry{};
                m_scrollOffsetFreeList.push_back(handle);
            }
            return true;
        });
    }

    ScrollOffsetHandle ScrollManagement::GetOrCreateScrollOffsetHandle(ID id)
    {
        assert(id != ID::Invalid() && "GetOrCreateScrollOffsetHandle called with invalid ID");

        if (const ScrollOffsetHandle* found = m_scrollOffsetHandlesByID.Find(id))
        {
            m_scrollOffsets[(uint32_t)*found].lastSeenFrame = m_frameIndex;
            return *found;
        }

        // Index 0 is a sentinel so handle 0 == ScrollOffsetHandle::Invalid.
        if (m_scrollOffsets.empty())
            m_scrollOffsets.emplace_back();

        ScrollOffsetHandle handle = ScrollOffsetHandle::Invalid;
        if (!m_scrollOffsetFreeList.empty())
        {
            handle = m_scrollOffsetFreeList.back();
            m_scrollOffsetFreeList.pop_back();
            m_scrollOffsets[(uint32_t)handle] = ScrollOffsetEntry
            {
                .id = id,
                .offset = int2{0, 0},
                .lastSeenFrame = m_frameIndex,
                .inUse = true
            };
        }
        else
        {
            handle = static_cast<ScrollOffsetHandle>(m_scrollOffsets.size());
            m_scrollOffsets.push_back(ScrollOffsetEntry
            {
                .id = id,
                .offset = int2{0, 0},
                .lastSeenFrame = m_frameIndex,
                .inUse = true
            });
        }

        m_scrollOffsetHandlesByID.TryEmplace(id, handle);
        return handle;
    }

    ScrollOffsetHandle ScrollManagement::GetScrollOffsetHandle(ID id)
    {
        if (const ScrollOffsetHandle* found = m_scrollOffsetHandlesByID.Find(id))
        {
            m_scrollOffsets[(uint32_t)*found].lastSeenFrame = m_frameIndex;
            return *found;
        }

        return ScrollOffsetHandle::Invalid;
    }

    bool ScrollManagement::IsScrollOffsetHandleValid(ScrollOffsetHandle handle) const
    {
        return handle != ScrollOffsetHandle::Invalid &&
               (uint32_t)handle < m_scrollOffsets.size() &&
               m_scrollOffsets[(uint32_t)handle].inUse;
    }

    int2 ScrollManagement::GetScrollOffset(ScrollOffsetHandle handle)
    {
        assert(IsScrollOffsetHandleValid(handle) && "Invalid scroll offset handle");
        ScrollOffsetEntry& entry = m_scrollOffsets[(uint32_t)handle];
        entry.lastSeenFrame = m_frameIndex;
        return entry.offset;
    }

    void ScrollManagement::SetScrollOffset(ScrollOffsetHandle handle, int2 offset)
    {
        assert(IsScrollOffsetHandleValid(handle) && "Invalid scroll offset handle");
        ScrollOffsetEntry& entry = m_scrollOffsets[(uint32_t)handle];
        entry.offset = offset;
        entry.lastSeenFrame = m_frameIndex;
    }

    bool ScrollManagement::MakeRectVisibleInScroll(ScrollOffsetHandle scrollHandle, int2 rectMin, int2 rectMax, int2 containerSize, ScrollAxis axis)
    {
        if (!IsScrollOffsetHandleValid(scrollHandle))
            return false;

        int2 offset = GetScrollOffset(scrollHandle);
        bool scrolled = false;

        // Offset is a content translation (typically <= 0).
        const int2 visibleMin = int2{ -offset.x, -offset.y };
        const int2 visibleMax = visibleMin + containerSize;

        // A rect larger than the container that is already partly visible is left alone;
        // scrolling would only change which part is clipped.
        auto isOversizedAndPartiallyVisible = [](int rMin, int rMax, int vMin, int vMax, int containerAxis)
        {
            const int rectAxis = rMax - rMin;
            if (rectAxis <= containerAxis)
                return false;
            return rMax > vMin && rMin < vMax;
        };

        if ((axis & ScrollAxis::X) != ScrollAxis::None &&
            !isOversizedAndPartiallyVisible(rectMin.x, rectMax.x, visibleMin.x, visibleMax.x, containerSize.x))
        {
            if (rectMin.x < visibleMin.x)
            {
                offset.x = -rectMin.x;
                scrolled = true;
            }
            else if (rectMax.x > visibleMax.x)
            {
                offset.x = -(rectMax.x - containerSize.x);
                scrolled = true;
            }
        }

        if ((axis & ScrollAxis::Y) != ScrollAxis::None &&
            !isOversizedAndPartiallyVisible(rectMin.y, rectMax.y, visibleMin.y, visibleMax.y, containerSize.y))
        {
            if (rectMin.y < visibleMin.y)
            {
                offset.y = -rectMin.y;
                scrolled = true;
            }
            else if (rectMax.y > visibleMax.y)
            {
                offset.y = -(rectMax.y - containerSize.y);
                scrolled = true;
            }
        }

		SetScrollOffset(scrollHandle, offset);
        return scrolled;
    }

    bool ScrollManagement::MakeRectVisibleHierarchically(ElementStorage& elementStorage,
                                                         int2 targetScreenMin,
                                                         int2 targetScreenMax,
                                                         const ElementRef& elementRef,
                                                         ScrollAxis axis)
    {
        if (axis == ScrollAxis::None)
            return false;
        if (!elementStorage.IsValidRef(elementRef))
            return false;

        bool anyScrolled = false;
        bool reachedWindow = false;
        ElementRef currentRef = elementRef;

        while (elementStorage.IsValidRef(currentRef))
        {
            ElementRef parentRef = elementStorage.GetParentRef(currentRef);
            if (!elementStorage.IsValidRef(parentRef))
                break;

            const Element* parentElement = elementStorage.GetElementPtr(parentRef);
            const ElementRect* parentRect = elementStorage.GetElementRectPtr(parentRef);
            if (parentElement == nullptr || parentRect == nullptr)
                break;

            // Scroll within the first window but don't cross into an outer one, which owns a
            // different viewport (e.g. the main window behind a popup menu).
            const bool isWindow = (parentElement->flags & ElementFlags::IsWindow) != ElementFlags::None;
            if (isWindow)
            {
                if (reachedWindow)
                    break;
                reachedWindow = true;
            }

            ScrollOffsetHandle scrollHandle = GetScrollOffsetHandle(parentElement->id);
            if (scrollHandle != ScrollOffsetHandle::Invalid)
            {
                // screen = innerRect.position + content_pos + scrollOffset
                const int2 scrollOffset = GetScrollOffset(scrollHandle);
                const int2 contentMin = targetScreenMin - parentRect->innerRect.position - scrollOffset;
                const int2 contentMax = targetScreenMax - parentRect->innerRect.position - scrollOffset;

                anyScrolled |= MakeRectVisibleInScroll(scrollHandle,
                                                       contentMin,
                                                       contentMax,
                                                       parentRect->innerRect.size,
                                                       axis);

                // The target moves on screen with this scroll; outer ancestors
                // must reveal it where it is now.
                const int2 moved = GetScrollOffset(scrollHandle) - scrollOffset;
                targetScreenMin += moved;
                targetScreenMax += moved;
            }

            currentRef = parentRef;
        }

        return anyScrolled;
    }

    bool ScrollManagement::MakeElementVisibleHierarchically(ElementStorage& elementStorage, const ElementRef& elementRef, ScrollAxis axis)
    {
        const ElementRect* elementRect = elementStorage.GetElementRectPtr(elementRef);
        if (elementRect == nullptr)
            return false;

        return MakeRectVisibleHierarchically(elementStorage, elementRect->rect, elementRef, axis);
    }

    bool ScrollManagement::MakeTextCursorVisibleInTextBox(ElementStorage& elementStorage,
                                                          const ElementRef& textEditorRef,
                                                          int2 cursorPos,
                                                          int cursorHeight)
    {
        if (!elementStorage.IsValidRef(textEditorRef))
            return false;

        const Element* textEditorElement = elementStorage.GetElementPtr(textEditorRef);
        if (textEditorElement == nullptr)
            return false;
        
        ElementRect& textEditorRect = elementStorage.GetElementRectUnsafe(textEditorRef);
        int2 styleOffset = textEditorRect.innerRect.position - textEditorRect.rect.position;


        int2 cursorRectMinInElement = textEditorRect.rect.position + styleOffset + cursorPos;
        int2 cursorRectMaxInElement = textEditorRect.rect.position + styleOffset + cursorPos + int2{ 1, cursorHeight };

        bool scrolled = false;
        ScrollOffsetHandle scrollHandle = GetScrollOffsetHandle(textEditorElement->id);
        if (scrollHandle != ScrollOffsetHandle::Invalid)
        {
            scrolled = MakeRectVisibleInScroll(scrollHandle,
                                               cursorPos,
                                               cursorPos + int2{1, cursorHeight},
                                               textEditorRect.innerRect.size);
        }

        if (scrollHandle != ScrollOffsetHandle::Invalid)
        {
            int2 cursorOffset = GetScrollOffset(scrollHandle);
            cursorRectMinInElement += cursorOffset;
            cursorRectMaxInElement += cursorOffset;
        }

        // Reveal the cursor itself, not just the editor bounds, in ancestors.
        scrolled |= MakeRectVisibleHierarchically(elementStorage,
                                                  cursorRectMinInElement,
                                                  cursorRectMaxInElement,
                                                  textEditorRef);
        return scrolled;
    }
}
