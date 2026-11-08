#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

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

    /// Where a piece of work is.
    enum class TaskState : uint8_t
    {
        Pending,  ///< not started: a step still to come
        Running,  ///< under way
        Paused,   ///< stopped for now, in the warning color
        Failed,   ///< stopped for good, in the error color
        Done,     ///< finished
    };

    /// A piece of work, as TaskProgress, ProgressSteps, StatusProgress and BackgroundTasks
    /// show it. The texts are only read during the call.
    struct ProgressTask
    {
        std::string_view name;              ///< "Importing character.fbx"
        float            value = -1.0f;     ///< 0 to 1 done; below 0 when not known, shown as a sliding segment
        TaskState        state = TaskState::Running;
        std::string_view note;              ///< beside the name: "2.1 s", "1,240 / 3,800"; empty shows the percent while running
        std::string_view detail;            ///< under the bar: "12.4 MB of 19.3 MB"; once failed, why
        std::function<void()> onCancel;     ///< set: a cancel button while running or paused
        std::function<void()> onRetry;      ///< set: a Retry button once failed
    };
    /// @}
}
