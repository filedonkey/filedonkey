#include "closechoicedialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

// Wider than the manual connect dialog, which is a form and needs no more width than its fields
// take. This one is three paragraphs, and at the narrower width they run to a column of short lines
// that is taller than the thing it is explaining.
#define CLOSE_DIALOG_WIDTH 420

// Between the question and the paragraph that answers it. Close enough that the two read as one
// block - the paragraph is what the question means, not a separate remark.
#define CLOSE_HEADING_GAP 10

namespace {

// A button on the bar, in the secondary weight the dialog's Cancel is drawn in. The one that is
// answered by pressing Return is given the primary fill instead - see the caller.
QPushButton *choiceButton(QWidget *parent, const QString &text, const QString &objectName)
{
    QPushButton *button = new QPushButton(text, parent);
    button->setObjectName(objectName);
    button->setCursor(Qt::PointingHandCursor);

    return button;
}

} // namespace

CloseChoiceDialog::CloseChoiceDialog(QWidget *parent)
    : AppDialog(tr("Close FileDonkey"), CLOSE_DIALOG_WIDTH, parent)
{
    setObjectName("closeChoiceDialog");

    // The question, in the one line the user is meant to read first.
    QLabel *heading = new QLabel(tr("Close the window, or quit FileDonkey?"), this);
    heading->setObjectName("dialogHeading");
    heading->setWordWrap(true);
    heading->setFixedWidth(textWidth());

    // What each answer costs. The first sentence is why this is being asked at all - on every other
    // desktop the window simply goes away - and the two after it are what the buttons below do,
    // said before they are pressed rather than found out afterwards.
    QLabel *body = new QLabel(tr("There is no system tray on this desktop, so FileDonkey has "
                                 "nowhere to sit while it runs.\n\nQuitting unmounts every "
                                 "connected device and drops all connections.\n\nHiding keeps them "
                                 "running. Start FileDonkey again to bring this window back."), this);
    body->setObjectName("dialogIntro");
    body->setWordWrap(true);

    // Measured at the width it is actually handed - see AppDialog::textWidth() for what a wrapped
    // label left to work that out for itself does instead.
    body->setFixedWidth(textWidth());

    // Quit first, and so furthest from where the eye finishes: it is the one answer here that loses
    // work, and it should not sit against the button the user is most likely to be reaching for.
    // Its red is in the stylesheet and arrives only under the pointer - unarmed it sits with its
    // neighbours, so the dialog does not open looking like an alarm.
    QPushButton *quitBtn   = choiceButton(this, tr("Quit"),   "dialogQuitBtn");
    QPushButton *cancelBtn = choiceButton(this, tr("Cancel"), "dialogCancelBtn");
    QPushButton *hideBtn   = choiceButton(this, tr("Hide"),   "dialogHideBtn");

    // The safe answer, and the one Return presses. It takes the primary fill for the same reason
    // Connect does in the other dialog: it is what this dialog is for.
    hideBtn->setDefault(true);

    // Each button records what it means and then closes the dialog. accept() for the two that
    // answer it and reject() for Cancel, so that Escape and the title bar's close - which reach
    // reject() on their own - come out as Cancel without being wired to anything.
    connect(quitBtn, &QPushButton::clicked, this, [this]() {
        picked = Choice::Quit;
        accept();
    });

    connect(hideBtn, &QPushButton::clicked, this, [this]() {
        picked = Choice::Hide;
        accept();
    });

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    // In the order they read, left to right. The stretch that pushes them to the right-hand end is
    // already in this layout - see AppDialog::buttonLayout().
    buttonLayout()->addWidget(quitBtn);
    buttonLayout()->addWidget(cancelBtn);
    buttonLayout()->addWidget(hideBtn);

    pageLayout()->addWidget(heading);
    pageLayout()->addSpacing(CLOSE_HEADING_GAP);
    pageLayout()->addWidget(body);
}
