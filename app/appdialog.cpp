#include "appdialog.h"

#include "titlebar.h"
#include "windowshadow.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

// The 1px border down each side of #dialogContent, which the page's width has to be measured
// without - see textWidth() in appdialog.h. Subtracted by hand for the same reason the device
// list's badge subtracts its own: a stylesheet border is outside the box the layout hands out, and
// no number here can be told about it. See BADGE_SIZE in devicelist.cpp.
#define DIALOG_PAGE_BORDERS 2

AppDialog::AppDialog(const QString &title, int width, QWidget *parent)
    : QDialog(parent)
    , frameWidth(width)
{
    setWindowTitle(title);

    // Off with the system's frame and on with ours, the way the main window does it - see the top
    // of mainwindow.cpp, which this follows step for step. The bar at the top of the layout below
    // is what replaces the caption, and it is the same TitleBar the window wears.
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);

    // Rounded corners need somewhere for the corner to go. Without this the dialog is an opaque
    // rectangle and a radius only rounds the colour inside it, leaving the square corner behind.
    setAttribute(Qt::WA_TranslucentBackground);

    shadowLayer = windowShadow(this);

    // The strip the blur falls into. Everything the dialog draws is inside it, which is why the
    // layout below keeps no margins of its own.
    setContentsMargins(SHADOW_MARGIN, SHADOW_MARGIN, SHADOW_MARGIN, SHADOW_MARGIN);

    // Wider than the frame by exactly what that margin takes, so what the user sees is still the
    // width the caller asked for.
    setFixedWidth(width + 2 * SHADOW_MARGIN);

    // The application icon, this dialog's name, and a close button. No tabs, no minimise, no
    // maximise - see TitleBar::Kind. Its close does what a Cancel does: window()->close() on a
    // QDialog is a reject, so the two ways out cannot come to mean different things.
    TitleBar *titleBar = new TitleBar(TitleBar::Kind::Dialog, this);
    titleBar->setTitle(title);

    // Everything above the bar, on a widget of its own so that it and not the dialog carries the
    // margins - the bar has to reach the dialog's three lower edges, and a margin on the dialog
    // would hold it off all of them.
    QWidget *content = new QWidget(this);
    content->setObjectName("dialogContent");

    page = new QVBoxLayout(content);
    page->setContentsMargins(DIALOG_MARGIN, DIALOG_MARGIN, DIALOG_MARGIN, DIALOG_MARGIN);
    page->setSpacing(0);

    // The buttons and the strip they sit on, which is a widget rather than a bare layout because it
    // carries a fill and a border of its own - the same pair the window's status bar carries, and
    // it closes this dialog the way that bar closes the window.
    //
    // It spans the whole width, edge to edge, which is why the dialog's own layout below keeps no
    // margins: a bar inset from the sides would read as a panel floating in the dialog rather than
    // as its floor. The inset the buttons need is the bar's own, and it is the margin the page
    // above keeps, so they end under the rows they act on.
    QWidget *bar = new QWidget(this);
    bar->setObjectName("dialogButtonBar");
    bar->setAttribute(Qt::WA_StyledBackground, true);

    buttons = new QHBoxLayout(bar);
    buttons->setContentsMargins(DIALOG_MARGIN, DIALOG_BAR_PADDING, DIALOG_MARGIN, DIALOG_BAR_PADDING);
    buttons->setSpacing(DIALOG_BUTTON_GAP);

    // Added here rather than left to each subclass: the buttons belong at the right-hand end in
    // every dialog this application has, and one of them forgetting it would be the odd one out.
    buttons->addStretch(1);

    // The three widgets the frame is drawn by, top to bottom: the bar owns the upper two corners,
    // the bar at the foot owns the lower two, and the page between them carries the straight run of
    // border down each side. No margins here - the dialog's own are the shadow's gutter, and
    // anything inset from them would leave the frame floating inside the window.
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(titleBar);
    layout->addWidget(content);
    layout->addWidget(bar);
}

int AppDialog::textWidth() const
{
    return frameWidth - 2 * DIALOG_MARGIN - DIALOG_PAGE_BORDERS;
}

void AppDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);

    if (shadowLayer) shadowLayer->setGeometry(contentsRect());
}

// After QDialog::showEvent, which is where the adjustment described in appdialog.h is made, and
// before the window is put on screen, which is the call that follows this event: the dialog is
// placed once and never seen moving.
//
// Both windows carry the same SHADOW_MARGIN of empty gutter around their visible frames - the one
// number in windowshadow.h, so they cannot differ - which is what lets this centre the windows and
// have the frames inside them come out centred too.
//
// Nothing is clamped to the screen. These dialogs are smaller than the window they centre on in
// both directions, so wherever that window can be seen, so can they.
void AppDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);

    const QWidget *owner = parentWidget() ? parentWidget()->window() : nullptr;
    if (!owner) return;

    move(owner->geometry().center() - QPoint(width() / 2, height() / 2));
}
