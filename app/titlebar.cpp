#include "titlebar.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QToolButton>
#include <QWindow>

#define TITLEBAR_HEIGHT 36
#define TITLEBAR_ICON   16

// What Windows 11 uses for its own caption buttons.
#define CAPTION_BUTTON_WIDTH 46

namespace {

QToolButton *captionButton(QWidget *parent, const QString &glyph, const QString &objectName)
{
    QToolButton *button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setText(glyph);
    button->setFocusPolicy(Qt::NoFocus);

    // Fixed across, expanding down. The width is the one Windows uses; the height is left to the
    // layout so the button fills the bar without a number here that would have to be kept in step
    // with TITLEBAR_HEIGHT and the bar's borders by hand.
    button->setFixedWidth(CAPTION_BUTTON_WIDTH);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    return button;
}

} // namespace

TitleBar::TitleBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("titleBar");
    setFixedHeight(TITLEBAR_HEIGHT);

    // A QWidget subclass draws no stylesheet background unless it is asked to. Without this the
    // bar is transparent and the window behind it shows through.
    setAttribute(Qt::WA_StyledBackground, true);

    iconLbl = new QLabel(this);
    iconLbl->setObjectName("titleBarIcon");
    iconLbl->setPixmap(QIcon(":/assets/filedonkey_app_icon.ico").pixmap(TITLEBAR_ICON, TITLEBAR_ICON));

    titleLbl = new QLabel(this);
    titleLbl->setObjectName("titleBarTitle");

    minimiseBtn = captionButton(this, "–", "minimiseBtn");   // en dash
    maximiseBtn = captionButton(this, "□", "maximiseBtn");   // white square
    closeBtn    = captionButton(this, "×", "closeBtn");      // multiplication sign

    // Shown, never usable: the window has a fixed size. Disabling it is what greys the glyph
    // (see the :disabled rule in the stylesheet) and what stops Qt sending it hover and press
    // states, so it cannot light up under the pointer the way its neighbours do.
    maximiseBtn->setEnabled(false);

    connect(minimiseBtn, &QToolButton::clicked, this, [this]() { window()->showMinimized(); });
    connect(closeBtn,    &QToolButton::clicked, this, [this]() { window()->close(); });

    QHBoxLayout *layout = new QHBoxLayout(this);

    // No margin on the right and no spacing anywhere: the caption buttons have to touch each
    // other and reach the window edge. The one gap that is wanted, between the icon and the
    // title, is added on its own below. Zero top and bottom is what lets the buttons run the
    // full height of the bar.
    layout->setContentsMargins(10, 0, 0, 0);
    layout->setSpacing(0);

    layout->addWidget(iconLbl);
    layout->addSpacing(8);
    layout->addWidget(titleLbl);
    layout->addStretch(1);
    layout->addWidget(minimiseBtn);
    layout->addWidget(maximiseBtn);
    layout->addWidget(closeBtn);
}

void TitleBar::setTitle(const QString &title)
{
    titleLbl->setText(title);
}

void TitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
    {
        QWidget::mousePressEvent(event);
        return;
    }

    // Hand the drag to the window manager rather than moving the window ourselves on every mouse
    // move. It costs the same one line and brings the platform's own behaviour with it - snapping
    // to screen edges on Windows, the drag cursor, and the shake-to-minimise gesture - none of
    // which a hand-rolled move would have.
    if (QWindow *handle = window()->windowHandle())
    {
        handle->startSystemMove();
    }
}
