#include "dockicon.h"

#import <AppKit/AppKit.h>

// Objective-C++ because the activation policy is an NSApplication property and Qt exposes nothing
// for it. Regular is what an ordinary application runs as - Dock icon, menu bar, a place in
// Cmd-Tab. Accessory is the same process with none of those, which is what the tray icon and the
// mounts want when there is no window to go with them.
void setDockIconVisible(bool visible)
{
    if (!visible)
    {
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        return;
    }

    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    // An application coming back from Accessory is not the active one, and making a window key -
    // which is all Qt's activateWindow() does - does not make it active either: the window would
    // come up behind whatever the user was looking at, with the menu bar still belonging to that.
    // Asking for the application itself is the part only AppKit can be asked for.
    [NSApp activateIgnoringOtherApps:YES];
}
