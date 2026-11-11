#include <gui_core/input/gui_gesture.h>

#include <gui_core/state/gui_state_manager.h>

namespace tachyon
{
    void TakeFocus(GUIStateManager& gsm, ID element)
    {
        gsm.Interaction().SetFocusID(element);
        gsm.TextEditorFocus().Clear();
    }

    namespace detail
    {
        void TakePress(GUIStateManager& gsm, ID pressed, const PressOptions& options)
        {
            if (options.focus)
                TakeFocus(gsm, options.focusID != ID::Invalid() ? options.focusID : pressed);
            if (options.capture)
                gsm.Interaction().SetCaptureID(pressed);
        }

        void ReleaseCaptureOf(GUIStateManager& gsm, ID element)
        {
            if (gsm.Interaction().GetCaptureID() == element)
                gsm.Interaction().ReleaseCaptureID();
        }
    }
}
