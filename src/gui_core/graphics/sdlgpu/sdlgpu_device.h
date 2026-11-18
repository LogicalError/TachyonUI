#pragma once

// Inside the SDL GPU backend: the device, and what its graphics context and window contexts
// share.

#include <gui_core/graphics/sdlgpu/sdlgpu_graphics_context.h>

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_log.h>

#include <string>

namespace tachyon::sdlgpu
{
    struct SDLGPUGraphicsContext::Impl
    {
        DebugConfig    debug;
        std::string    driverAsked;    ///< SetDriver; empty, SDL's choice
        bool           lowPower = false;
        WindowResolver resolveWindow = nullptr;

        SDL_GPUDevice* device = nullptr;
        std::string    driver;         ///< the one in use
        std::string    deviceName;

        /// SDL's log output while the debug callback takes SDL GPU's messages.
        SDL_LogOutputFunction previousLog = nullptr;
        void*                 previousLogData = nullptr;
        bool                  logHooked = false;
    };
}
