#pragma once

#include <gui_core/style/gui_style_text.h>      // IconID

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Telling the user something without a dialog: toasts in a window's corner, banners at the
// top of a panel.

namespace tachyon
{
    /// @addtogroup controls
    /// @{

    /// How serious a notification is: its icon and color.
    enum class Severity : uint8_t { Info, Success, Warning, Error };

    /// A text button on a toast; the first is drawn in the link color.
    struct ToastAction {
        std::string           label;
        std::function<void()> onClick;   ///< then the toast goes
    };

    /// What a toast says (ShowToast). Change it later through FindToast.
    struct ToastDesc {
        Severity    severity = Severity::Info;
        std::string title;
        std::string body;
        bool        monospaceBody = false;     ///< code or file locations
        std::optional<float> progress;         ///< 0..1: a progress bar under the body
        std::vector<ToastAction> actions;
        float       age = 0.0f;                ///< seconds since it happened, when it's shown: its time says so
    };

    /// A toast shown by ShowToast; 0 is none.
    using ToastID = uint32_t;

    /// How a banner's action is drawn.
    enum class BannerActionKind : uint8_t { Primary, Secondary, Link };

    /// A banner's button or link, right-aligned after its message.
    struct BannerAction {
        std::string_view      label;
        std::function<void()> onClick;
        BannerActionKind      kind = BannerActionKind::Secondary;
    };

    /// @}
}
