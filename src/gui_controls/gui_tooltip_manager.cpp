#include <gui_controls/gui_tooltip_manager.h>

#include <algorithm>

namespace tachyon
{
    void TooltipManager::Dismiss(ID currentFocusedID, bool hasMouseMove)
    {
        m_activeTargetID = ID::Invalid();
        m_isReady = false;
        m_isVisible = false;
        m_triggeredByFocus = false;
        m_lingerID = ID::Invalid();
        m_lastVisibleTime = -1.0e9f;
        // After a dismissal, no hover tooltips until the mouse moves.
        m_suppressHover = !hasMouseMove;
        m_prevFocusedID = currentFocusedID;
    }

    void TooltipManager::BeginFrame(ID currentHoveredID, ID currentFocusedID, float elapsedTimeSecs,
                                    bool hasMouseMove, bool hasMousePress, bool hasKeyboard, bool hasScroll, bool menuIsOpen,
                                    bool buttonHeld)
    {
        m_currentTime = elapsedTimeSecs;
        m_declaredThisFrame = false;
        m_activeAskedLast = m_activeAsked;
        m_activeAsked = false;
        // A press moves focus during that frame's dispatch (after this runs), so
        // the focus change shows up one frame later; it is still mouse-driven.
        const bool pressedLastFrame = m_pressedLastFrame;
        m_pressedLastFrame = hasMousePress;

        // An open menu, and a held button (a drag), have none.
        if (menuIsOpen || buttonHeld)
        {
            Dismiss(currentFocusedID, true);
            m_suppressHover = false;
            return;
        }

        bool anyDismiss = hasMousePress || hasKeyboard || hasScroll;
        if (m_triggeredByFocus)
            anyDismiss = anyDismiss || hasMouseMove; // mouse move also dismisses focus tooltips
        if (anyDismiss)
        {
            Dismiss(currentFocusedID, hasMouseMove);
            return;
        }

        // Mouse movement lifts hover suppression
        if (m_suppressHover && hasMouseMove)
            m_suppressHover = false;

        // Focus changed without a mouse press: keyboard navigation.
        bool focusChangedByKeyboard = (currentFocusedID != m_prevFocusedID &&
                                       currentFocusedID != ID::Invalid() &&
                                       !hasMousePress && !pressedLastFrame);
        m_prevFocusedID = currentFocusedID;

        if (focusChangedByKeyboard)
        {
            // Show the focused element's tooltip right away.
            m_activeTargetID = currentFocusedID;
            m_targetStartTime = elapsedTimeSecs;
            m_isReady = true;
            m_isVisible = false;
            m_triggeredByFocus = true;
            m_suppressHover = false;
            m_lingerID = ID::Invalid();
            return;
        }

        // Hover tracking
        if (m_suppressHover || m_triggeredByFocus)
            return;

        if (currentHoveredID != m_activeTargetID)
        {
            // The one left stays a moment; the next shows at once if one was just showing.
            if (m_isVisible && m_activeTargetID != ID::Invalid())
            {
                m_lingerID = m_activeTargetID;
                m_lingerUntil = elapsedTimeSecs + hideSeconds;
            }
            const bool warm = elapsedTimeSecs - m_lastVisibleTime < warmSeconds;
            m_activeTargetID = currentHoveredID;
            m_targetStartTime = elapsedTimeSecs;
            m_isReady = warm && currentHoveredID != ID::Invalid();
            m_isVisible = false;
            // Asks by the target left don't count against its linger.
            m_activeAskedLast = false;
        }
        else if (m_activeTargetID != ID::Invalid() && !m_isReady)
        {
            if (elapsedTimeSecs - m_targetStartTime >= delaySeconds)
                m_isReady = true;
        }
    }

    bool TooltipManager::ShouldShow(ID targetID)
    {
        if (targetID == ID::Invalid()) return false;
        const bool isActive = targetID == m_activeTargetID ||
            std::find(m_targetAncestors.begin(), m_targetAncestors.end(), targetID) != m_targetAncestors.end();        // Asked for, even when the one left was declared first this frame: it goes next frame.
        if (isActive && m_isReady)
            m_activeAsked = true;
        if (m_declaredThisFrame) return false;
        if (isActive && m_isReady)
        {
            m_declaredThisFrame = true;
            m_isVisible = true;
            m_lingerID = ID::Invalid();
            m_lastVisibleTime = m_currentTime;
            return true;
        }
        // The target just left, a moment more, unless the new one has a tooltip of its own.
        if (targetID == m_lingerID && m_currentTime < m_lingerUntil && !m_activeAskedLast)
        {
            m_declaredThisFrame = true;
            m_lastVisibleTime = m_currentTime;
            return true;
        }
        return false;
    }

    void TooltipManager::EndFrame()
    {
        if (m_isVisible && !m_declaredThisFrame)
        {
            m_isVisible = false;
        }
    }

} // namespace tachyon
