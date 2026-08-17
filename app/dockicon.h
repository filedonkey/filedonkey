#ifndef DOCKICON_H
#define DOCKICON_H

#include <QtGlobal>

// Whether the application shows up in the Dock, and with it in Cmd-Tab and in the menu bar.
//
// The window is what earns a Dock icon here, not the process: FileDonkey lives in the tray, and
// closing the window leaves the node running with its mounts up - an icon left sitting in the Dock
// for that says the application is open in the ordinary sense, and clicking it is the ordinary way
// back to a window that closing was meant to put away.
//
// So it is called wherever the window goes away or comes back - see MainWindow::hideWindow() and
// MainWindow::restoreWindow() - rather than set once at startup. Saying it once, with LSUIElement
// in the bundle's Info.plist, is the other way to have no Dock icon, but it takes the icon away
// while the window is up too, which is not what a window on screen should look like.
//
// macOS alone. Windows and Linux have nothing in the taskbar for a window-less process to leave
// behind, so there the call is a no-op and callers need no #ifdef of their own.
#if defined(Q_OS_MACOS)
void setDockIconVisible(bool visible);
#else
inline void setDockIconVisible(bool) {}
#endif

#endif // DOCKICON_H
