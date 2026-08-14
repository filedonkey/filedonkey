#ifndef AUTOSTART_H
#define AUTOSTART_H

// Whether the desktop starts FileDonkey when the user signs in, and how that is asked for. Each
// platform keeps the request somewhere of its own - a value under the Run key on Windows, a launch
// agent on macOS, a .desktop file in the autostart directory on Linux - and this is all three of
// them behind one question.
//
// What is registered starts the application with this argument, which brings it up with no window on
// a desktop that has a tray to sit in. Written down here rather than twice: the command is built
// below and read in main(), and a flag the two spelled differently would start a window at every
// sign-in with nothing to say why.
#define TRAY_ARGUMENT "--tray"

namespace Autostart
{

// Read rather than remembered: the entry is the user's as much as ours, and it can be taken away
// from outside the application - by a startup manager, or by another copy of FileDonkey registering
// itself from a different directory.
bool isEnabled();

// Asks for it, or takes the request back. Nothing is returned: whether it took is answered by
// isEnabled(), which the settings page reads straight after so that a refusal shows as the switch
// going back where it was rather than as a promise nothing kept.
void setEnabled(bool enabled);

} // namespace Autostart

#endif // AUTOSTART_H
