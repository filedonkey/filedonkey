#ifndef MANUALCONNECTDIALOG_H
#define MANUALCONNECTDIALOG_H

#include <QDialog>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QPushButton;
class QToolButton;
QT_END_NAMESPACE

// The four numbers of an IPv4 address in one box, the way Windows asks for one in the IPv4
// properties of a network adapter: four fields divided by dots, each taking one octet, and the
// whole thing drawn as a single field so it reads as one address rather than four numbers.
//
// It is four QLineEdits rather than one field validated as a whole because that is what makes the
// dots fixed. A single field would have the user typing them, and a mistyped or missing dot is the
// one mistake an address entry should be incapable of.
//
// Everything past that is what a user who has met the Windows control expects of it: a field that
// is full moves the cursor on by itself, a dot moves it on early, backspace at the start of a field
// goes back to the end of the one before, and an address pasted into any of the four spreads itself
// across all of them instead of landing in one.
class IPv4Edit : public QWidget
{
    Q_OBJECT

public:
    explicit IPv4Edit(QWidget *parent = nullptr);

    // The four fields joined by dots, or an empty string while any of them is blank - a partial
    // address is not an address, and there is nothing sensible to fill the gaps with.
    QString address() const;

    // Fills the first three from an address and leaves the fourth empty, which is how the dialog
    // opens: the machine being reached by hand is nearly always on this network, so three of the
    // four numbers are already known and the user has one to type. Anything that is not an IPv4
    // address leaves all four empty.
    //
    // What it fills in is also remembered as what the box started as, for the three below.
    void setPrefixFrom(const QString &address);

    // Whether every field still holds what setPrefixFrom() left there. False the moment anything is
    // typed, the fourth octet included - what the dialog's revert button follows.
    bool isDefault() const;

    // Puts all four back to that state and reopens the cursor where the dialog opened it, so
    // pressing revert leaves the box exactly as it was found.
    void revert();

    // That state written out, for whoever has to name it: "192.168.50." with the empty fourth
    // leaving the trailing dot the prefix is read by. Empty when there was no address to borrow
    // from and the box opened blank - there is then nothing to name.
    QString defaultText() const;

    // Puts the cursor in the first field that has nothing in it, or in the last one when they are
    // all filled. What the dialog calls to open with the cursor where the typing starts.
    void focusFirstEmpty();

signals:
    // Emitted whenever any of the four changes, for the dialog's Connect button to follow.
    void addressChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Which of the four the given field is, or -1 for anything else.
    int indexOf(QObject *field) const;

    // Moves the cursor between the fields. Both are no-ops at the ends of the row, so the callers
    // do not have to check first.
    void focusPrevious(int index);
    void focusNext(int index);

    // An address typed or pasted into one field with dots in it, spread across this one and the
    // ones after it. Returns false when there was nothing to spread, so the caller can go on and
    // treat the change as an ordinary one.
    bool spread(int index, const QString &text);

    // Draws the box as focused while any of the four fields is, since no stylesheet can ask that
    // of a container. Queued behind the focus change that prompts it, because during a FocusOut
    // the field the focus is moving to does not have it yet.
    void refreshFocused();

    QLineEdit *octets[4] = {};

    // What each field was filled with when the box was set up, which is what revert() puts back.
    // The fourth is empty on every path - it is the number the box cannot guess.
    QString defaults[4];
};

// Where a device is reached, for the times nothing found it: an address and a port, and the reason
// the user would be typing them at all. The device list's button opens it - see the footer built in
// DeviceList's constructor - and what comes out goes to LocalNode::connectManually().
//
// It only collects the two numbers. Whether anything is there to answer them is the node's business
// and is reported separately, because the answer takes a moment to arrive and a modal dialog held
// open waiting for it is a dialog that looks stuck.
class ManualConnectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ManualConnectDialog(QWidget *parent = nullptr);

    // Both only meaningful once exec() has returned Accepted - the Connect button is not offered
    // until there is a whole address and a port in range.
    QString address() const;
    int port() const;

protected:
    // Keeps the shadow the size of the visible frame. Unlike the main window, which is a fixed size
    // and sets its own once, this dialog is as tall as whatever is in it - so the size arrives with
    // the first layout pass rather than being known in the constructor.
    void resizeEvent(QResizeEvent *event) override;

    // Puts the dialog in the middle of the window it belongs to, which is where QDialog would put
    // it if this one wore a frame - see the note on the implementation for why it does not.
    void showEvent(QShowEvent *event) override;

private:
    // Everything about the dialog that follows from what is in its two fields: Connect is greyed out
    // until both hold something that could be dialled, and each field's revert button is there only
    // while that field holds something other than what the dialog opened with.
    void refreshControls();

    IPv4Edit    *ipEdit     = nullptr;
    QLineEdit   *portEdit   = nullptr;
    QPushButton *connectBtn = nullptr;

    QToolButton *ipRevert   = nullptr;
    QToolButton *portRevert = nullptr;

    // The blur under the frame - see windowshadow.h. Held only so resizeEvent() can keep it the
    // size of what it sits behind.
    QWidget     *shadowLayer = nullptr;

    // The port the field was filled with, and so the one its revert button puts back. Kept because
    // the setting behind it is read once, when this dialog is built: were it read again on the way
    // back, a port changed on the settings page while the dialog stood open would have revert
    // putting back a number the field never held.
    QString defaultPort;
};

#endif // MANUALCONNECTDIALOG_H
