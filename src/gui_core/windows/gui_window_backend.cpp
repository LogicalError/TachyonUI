#include <gui_core/windows/gui_window_backend.h>

#include <algorithm>
#include <chrono>
#include <thread>

namespace tachyon
{
    void IWindowBackend::WaitForEvents(float timeoutSecs)
    {
        // Without a way to wait for the OS: a short sleep, after which the app looks again.
        const float secs = std::min(timeoutSecs, 1.0f / 60.0f);
        if (secs > 0.0f)
            std::this_thread::sleep_for(std::chrono::duration<float>(secs));
    }
}
