#ifndef APPDIALOG_H
#define APPDIALOG_H

#include <QDialog>

QT_BEGIN_NAMESPACE
class QHBoxLayout;
class QVBoxLayout;
QT_END_NAMESPACE

// The dialog's inset from its own edges - what the page inside it keeps clear, and what the bar at
// the foot keeps clear at each end so the buttons finish under the rows they act on.
#define DIALOG_MARGIN 18

// What that bar keeps above and below its buttons. Less than the page keeps around its rows: the
// bar is as tall as what is on it and no taller.
#define DIALOG_BAR_PADDING 14

// Between one button and the next. Tight, because a dialog's buttons are one group - wider and they
// would read as unrelated buttons that happen to share an edge.
#define DIALOG_BUTTON_GAP 8

// A dialog wearing this application's own window frame instead of the system's: the same title bar
// the main window has, the same border and rounded corners, the same shadow under it.
//
// It exists because there are two of these now - the manual connect dialog and the question asked
// when the window is closed on a desktop with no tray - and every line of that frame was the same
// in both. What differs between two dialogs is what is written on them, so that is all a subclass
// is left to do: fill pageLayout() with its rows and buttonLayout() with its buttons.
//
// Nothing here is virtual and there is nothing to override. A subclass builds itself in its own
// constructor, into the two layouts below, and this class is finished by the time that starts.
class AppDialog : public QDialog
{
    Q_OBJECT

public:
    // The width is of the frame the user sees, not of the window: the shadow's gutter is added on
    // top of it here, so a caller asks for the width it wants the dialog to look.
    //
    // Only the width is fixed. The height is whatever the subclass's rows come to.
    AppDialog(const QString &title, int width, QWidget *parent = nullptr);

protected:
    // The page above the bar, which is where a subclass puts everything that is read or filled in.
    // Already inset by DIALOG_MARGIN and spaced at zero - a dialog's rows are set apart by what it
    // puts between them rather than by one gap applied everywhere.
    QVBoxLayout *pageLayout() const { return page; }

    // The bar at the foot. Already holds the stretch that pushes what follows to the right-hand
    // end, so a subclass adds its buttons in the order they should read, left to right.
    QHBoxLayout *buttonLayout() const { return buttons; }

    // The width a wrapped label added to the page will actually be given.
    //
    // Worth asking for rather than assuming, because a label that wraps reports its height for a
    // width and the layout asks at the width it is about to hand over - and the two only agree
    // while every margin between them is known to the layout. The 1px border down each side of the
    // page is not: a stylesheet border is outside the box the layout hands out. Left to work it
    // out, a paragraph is measured a line shorter than it draws and comes out clipped at both ends.
    // Hand this to setFixedWidth() on the label and the question does not arise.
    int textWidth() const;

    // Keeps the shadow the size of the visible frame. The dialog is as tall as whatever a subclass
    // put in it, so the size arrives with the first layout pass rather than being known here.
    void resizeEvent(QResizeEvent *event) override;

    // Puts the dialog in the middle of the window that opened it.
    //
    // QDialog places itself on its parent already, and this is here because of how: adjustPosition()
    // centres the dialog and then lifts it by what it takes to be the height of a title bar and the
    // thickness of a frame, so that a framed dialog's contents rather than its caption end up on the
    // parent's centre line. These dialogs have neither - the bar is a widget inside them like any
    // other - so that correction is applied to a window with nothing to correct for, and the dialog
    // opens up and to the left of where it was asked to go.
    void showEvent(QShowEvent *event) override;

private:
    QWidget     *shadowLayer = nullptr;
    QVBoxLayout *page        = nullptr;
    QHBoxLayout *buttons     = nullptr;

    // What the constructor was asked for, kept for textWidth().
    int          frameWidth  = 0;
};

#endif // APPDIALOG_H
