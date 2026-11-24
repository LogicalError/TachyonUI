#include <gui_core/windows/sdl/gui_window_backend_sdl_cocoa.h>

#import <AppKit/AppKit.h>

namespace tachyon::cocoa
{
    namespace
    {
        // The floating windows, held weakly: a window closed leaves the table by itself.
        NSHashTable<NSWindow*>* FloatingWindows()
        {
            static NSHashTable<NSWindow*>* windows = [NSHashTable weakObjectsHashTable];
            return windows;
        }

        NSWindowLevel FloatingLevel()
        {
            return NSApp.isActive ? NSFloatingWindowLevel : NSNormalWindowLevel;
        }

        // Once: the floating windows' level follows the app's activation.
        void ObserveActivation()
        {
            static bool observing = false;
            if (observing)
                return;
            observing = true;
            NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
            const auto relevel = ^(NSNotification*) {
                const NSWindowLevel level = FloatingLevel();
                for (NSWindow* window in FloatingWindows())
                    window.level = level;
            };
            [center addObserverForName:NSApplicationDidBecomeActiveNotification object:nil queue:nil usingBlock:relevel];
            [center addObserverForName:NSApplicationDidResignActiveNotification object:nil queue:nil usingBlock:relevel];
        }
    }

    void FloatAboveAppWindows(void* nsWindow)
    {
        NSWindow* window = (__bridge NSWindow*)nsWindow;
        if (window == nil)
            return;
        ObserveActivation();
        [FloatingWindows() addObject:window];
        window.level             = FloatingLevel();
        window.hidesOnDeactivate = NO;
        // Like the owner's other windows: on its Space, not on every one.
        window.collectionBehavior = (window.collectionBehavior & ~NSWindowCollectionBehaviorCanJoinAllSpaces)
                                  | NSWindowCollectionBehaviorFullScreenAuxiliary;
    }
}
