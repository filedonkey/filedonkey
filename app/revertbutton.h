#ifndef REVERTBUTTON_H
#define REVERTBUTTON_H

#include <QIcon>
#include <QSize>
#include <QSizePolicy>
#include <QString>
#include <QToolButton>

// The button at the end of a row that puts a field back to the value it started with. Square, and a
// touch smaller than the field it sits beside so it reads as something offered rather than as a
// second control of the same weight. The glyph is drawn under the arrow's own ends at this size,
// which is why it is not the full button.
#define REVERT_BUTTON_SIZE 22
#define REVERT_GLYPH       14

// Header-only and shared, the way ElidedLabel is - and for the same reason. It was the settings
// page's alone until the manual connect dialog grew two fields worth putting back, and that dialog
// wants not merely a button like it but the same button: the two are dressed by one stylesheet rule
// (QToolButton#revertBtn) and a size kept in two places would drift from the artwork that fills it.
//
// What "the value it started with" means is the caller's business, and the two callers mean
// different things by it: on the settings page it is the value this app ships with, in the dialog it
// is what that dialog opened holding. Neither is knowable from here, so nothing about it is decided
// here - the tooltip arrives already written, and the button is built hidden because only the caller
// knows when there is something to undo.
inline QToolButton *revertButton(QWidget *parent, const QString &tooltip)
{
    QToolButton *button = new QToolButton(parent);
    button->setObjectName("revertBtn");
    button->setIcon(QIcon(":/assets/reverse.svg"));
    button->setIconSize(QSize(REVERT_GLYPH, REVERT_GLYPH));
    button->setFixedSize(REVERT_BUTTON_SIZE, REVERT_BUTTON_SIZE);
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(tooltip);
    button->hide();

    // Never takes the focus. The row belongs to the field, and a button that could be tabbed into
    // would put a focus ring on the one control in the window that has no use for one.
    button->setFocusPolicy(Qt::NoFocus);

    // Its place in the row is kept while it is hidden. Without that the field beside it would grow
    // and shrink by the button's width as the button came and went, and a name being typed on the
    // settings page would jump about under the cursor on the first character that differed from the
    // host name. The dialog's rows end in a stretch and would not move either way, but a button that
    // behaved differently on the two pages would be a second thing to remember.
    QSizePolicy policy = button->sizePolicy();
    policy.setRetainSizeWhenHidden(true);
    button->setSizePolicy(policy);

    return button;
}

#endif // REVERTBUTTON_H
