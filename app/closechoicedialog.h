#ifndef CLOSECHOICEDIALOG_H
#define CLOSECHOICEDIALOG_H

#include "appdialog.h"

// What closing the window should mean, asked only on a desktop with no system tray.
//
// Everywhere else the question does not arise: the window goes away, the application stays in the
// tray, and the tray is how it is reached again. Without one there is nowhere for it to sit, so
// closing the window could mean either thing and only the user knows which.
//
// Deliberately has no "do not ask again". This dialog is the one place a tray-less desktop can quit
// from, and remembering Hide would seal it off.
//
// It was a QMessageBox until the application grew a window frame of its own. What it asks has not
// changed; where it is asked has - see AppDialog, which is the same frame the manual connect dialog
// wears, so the two questions this application asks look like they come from the same application.
class CloseChoiceDialog : public AppDialog
{
    Q_OBJECT

public:
    // Cancel is what every way of dismissing this without choosing comes back as: the Cancel
    // button, Escape, and the close button in the title bar - which is Cancel by another route, and
    // reaches it the way any QDialog reaches reject().
    enum class Choice { Cancel, Hide, Quit };

    explicit CloseChoiceDialog(QWidget *parent = nullptr);

    // Only meaningful once exec() has returned. Cancel until one of the other two is pressed, so a
    // dialog that was dismissed rather than answered says so without the caller having to read
    // exec()'s own result as well.
    Choice choice() const { return picked; }

private:
    Choice picked = Choice::Cancel;
};

#endif // CLOSECHOICEDIALOG_H
