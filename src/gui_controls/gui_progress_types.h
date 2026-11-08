#pragma once

#include <cstdint>

namespace tachyon
{
    /// @addtogroup app
    /// @{

    /// How large a Spinner is.
    enum class SpinnerSize : uint8_t
    {
        Small,    ///< 12px, in a status bar or beside a line of small text
        Medium,   ///< 16px, beside a line of text
        Large,    ///< 24px, alone in an empty panel
    };
    /// @}
}
