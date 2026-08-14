#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include <functional>

#include <QWidget>

class QCheckBox;
class QLineEdit;
class QToolButton;

class ElidedLabel;

// The window's second page, behind the title bar's Settings tab: the name this machine announces
// itself under, the folder it shares, and the port peers reach it on.
//
// It keeps nothing of its own. What the fields hold is read from and written back to the settings
// through LocalNode, so the node reads what the user typed without this page having to reach it.
class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(QWidget *parent = nullptr);

protected:
    // The tab being switched away from, or the window being put away, counts as being done with a
    // field: neither necessarily takes focus off one, so neither would reach editingFinished on its
    // own and a value typed and left there would be lost.
    void hideEvent(QHideEvent *event) override;

private:
    // What is in the field, back to the settings - and then back out of them into the field, so an
    // emptied one fills with the host name it has fallen back to rather than sitting blank.
    void commitMachineName();

    // The same for a port. A field the validator is not satisfied with - a half-typed port, or an
    // empty one - is put back to the port in force rather than stored. Takes the pair of accessors
    // it works through rather than naming them: there is one port field on the page now, and the
    // second this app grows a use for should not be a copy of this function.
    void commitPort(QLineEdit *edit, void (*store)(int), int (*read)());

    // Ties a field to the button in the third column that puts it back: the button is shown only
    // while the field holds something other than its default, and pressing it types the default in
    // and commits that. Both rows are wired through here so the two cannot come to behave
    // differently.
    void bindRevert(QLineEdit *edit, QToolButton *revert, const QString &defaultText,
                    std::function<void()> commit);

    // Asks the user for a folder and, if they name one, makes it the shared root. Nothing happens
    // when the dialog is dismissed - the row keeps the folder it had.
    void chooseSharedRoot();

    // Stores a folder, writes it into the row, and shows or hides the button that would put it back.
    // Everything that changes the shared root goes through here, so the three cannot fall out of
    // step with each other.
    void showSharedRoot(const QString &path);

    QLineEdit *nameEdit = nullptr;
    QLineEdit *transferEdit = nullptr;

    // The shared root is shown rather than typed: a path is not something to be got right by hand,
    // and the folder dialog behind the button beside it is the only way this row is filled in.
    ElidedLabel *sharedRootLbl = nullptr;
    QToolButton *sharedRootRevert = nullptr;

    // The switch under the rule, which asks the desktop rather than this application for anything:
    // it is read from and written to Autostart, and read back after each write so that a request
    // the platform refused shows as the switch going back rather than as one that lies.
    QCheckBox *autostartBox = nullptr;
};

#endif // SETTINGSPAGE_H
