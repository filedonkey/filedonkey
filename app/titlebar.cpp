#include "titlebar.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QToolButton>
#include <QWindow>

#define TITLEBAR_HEIGHT 36
#define TITLEBAR_ICON   16

// What Windows 11 uses for its own caption buttons: a 46px-wide button with a 10px glyph in it.
#define CAPTION_BUTTON_WIDTH 46
#define CAPTION_GLYPH        10

namespace {

QToolButton *captionButton(QWidget *parent, const QIcon &icon, const QString &objectName)
{
    QToolButton *button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setIcon(icon);
    button->setIconSize(QSize(CAPTION_GLYPH, CAPTION_GLYPH));
    button->setFocusPolicy(Qt::NoFocus);

    // QCommonStyle only reaches for QIcon::Active - the hover artwork - on a button that is in
    // auto-raise mode. The flag's usual effect, drawing no frame until the pointer is over the
    // button, is invisible here: the stylesheet paints every state's background itself.
    button->setAutoRaise(true);

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

    // The glyphs are drawn from SVG rather than from a font so the three buttons are the same
    // picture on every platform - there is no character in a font we can count on shipping with
    // Windows, macOS and Linux alike that draws Windows' own caption marks. QSvgIconEngine
    // re-renders from the vector at whatever size and device pixel ratio the style asks for, so
    // they stay crisp on scaled and Retina displays with no pixmap-per-DPI bookkeeping here.
    //
    // Colour is baked into each file, one file per state, because an icon cannot pick up the
    // stylesheet's `color:` the way the text glyphs these replaced did. The mode names below are
    // what QStyle asks the icon for: Normal at rest, Active under the pointer, Disabled when the
    // widget is.
    QIcon minimiseIcon;
    minimiseIcon.addFile(":/assets/caption_minimise.svg",       QSize(), QIcon::Normal);
    minimiseIcon.addFile(":/assets/caption_minimise_hover.svg", QSize(), QIcon::Active);

    // Both modes off the one file: the button is permanently disabled, so Disabled is the mode
    // that will actually be drawn, and naming it stops Qt washing the Normal artwork out further
    // to invent one. Normal is registered too so the icon is still right if it is ever enabled.
    QIcon maximiseIcon;
    maximiseIcon.addFile(":/assets/caption_maximise.svg", QSize(), QIcon::Normal);
    maximiseIcon.addFile(":/assets/caption_maximise.svg", QSize(), QIcon::Disabled);

    QIcon closeIcon;
    closeIcon.addFile(":/assets/caption_close.svg",       QSize(), QIcon::Normal);
    closeIcon.addFile(":/assets/caption_close_hover.svg", QSize(), QIcon::Active);

    minimiseBtn = captionButton(this, minimiseIcon, "minimiseBtn");
    maximiseBtn = captionButton(this, maximiseIcon, "maximiseBtn");
    closeBtn    = captionButton(this, closeIcon,    "closeBtn");

    // Shown, never usable: the window has a fixed size. Disabling it is what greys the glyph
    // (Qt draws the Disabled artwork registered above) and what stops Qt sending it hover and
    // press states, so it cannot light up under the pointer the way its neighbours do.
    maximiseBtn->setEnabled(false);

    // QIcon has no mode for "pressed", so the one state the icon modes do not cover is swapped in
    // by hand. Only close needs it: minimise's pressed glyph is the same grey as its idle one, and
    // for both buttons the pressed background still comes from the stylesheet either way.
    const QIcon closePressedIcon(":/assets/caption_close_pressed.svg");

    connect(closeBtn, &QToolButton::pressed,  this, [this, closePressedIcon]() { closeBtn->setIcon(closePressedIcon); });
    connect(closeBtn, &QToolButton::released, this, [this, closeIcon]()        { closeBtn->setIcon(closeIcon); });

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
