#pragma once

// Library internal, macOS only: what the SDL window backend does to its NSWindows that SDL
// doesn't do for it.

namespace tachyon::cocoa
{
    /// Keeps `nsWindow` (an NSWindow) above the app's other windows while the app is active, and
    /// at the normal level while another app is: a floating panel's behavior, for a window with
    /// an owner that the user places (a torn-off panel). It's used in place of an SDL parent,
    /// which on macOS makes the window a child window that moves with its owner.
    void FloatAboveAppWindows(void* nsWindow);
}
