#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include <QWidget>

class QLineEdit;

// The window's second page, behind the title bar's Settings tab. One setting so far: the name this
// machine announces itself under, which every peer's device list shows it by.
//
// It keeps nothing of its own. What the field holds is read from and written back to the settings
// through LocalNode, so the node reads the name the user typed without this page having to reach it.
class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(QWidget *parent = nullptr);

protected:
    // The tab being switched away from, or the window being put away, counts as being done with the
    // field: neither necessarily takes focus off it, so neither would reach editingFinished on its
    // own and a name typed and left there would be lost.
    void hideEvent(QHideEvent *event) override;

private:
    // What is in the field, back to the settings - and then back out of them into the field, so an
    // emptied one fills with the host name it has fallen back to rather than sitting blank.
    void commitMachineName();

    QLineEdit *nameEdit = nullptr;
};

#endif // SETTINGSPAGE_H
