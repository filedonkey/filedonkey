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
#define CAPTION_GLYPH        12

// Short of the bar's full height so it reads as a rule between two groups rather than as another
// window border, and clear of the caption buttons so their hover fills never touch it.
#define TITLEBAR_SEPARATOR_HEIGHT 18
#define TITLEBAR_SEPARATOR_MARGIN 9

// The view tabs. Inset from the top of the bar but flush with its bottom - the shape of a tab
// rather than of a pill, which is what says the page below is the one this opens. The bar is 36
// and its own top border and bottom hairline take one each, so 28 here leaves a 6px run of bar
// above each tab and none below it. The icon is a little bigger than a caption glyph because it is
// a picture to be read rather than a mark to be aimed at.
#define TITLEBAR_TAB_WIDTH  30
#define TITLEBAR_TAB_HEIGHT 32
#define TITLEBAR_TAB_ICON   16

// Enough to keep the two from reading as one wide tab, and no more.
#define TITLEBAR_TAB_GAP 2

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

// A tab in the bar: icon only, checkable, and one of a set only one of which can be checked at a
// time. Auto-exclusive rather than a QButtonGroup - the buttons are siblings, which is all
// QAbstractButton needs to run the exclusivity itself, and it brings the behaviour a group would
// have to be told to want: clicking the tab that is already current leaves it checked instead of
// turning it off and leaving the window on a view no tab claims.
QToolButton *tabButton(QWidget *parent, const QIcon &icon, const QString &objectName, const QString &tooltip)
{
    QToolButton *button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setIcon(icon);
    button->setIconSize(QSize(TITLEBAR_TAB_ICON, TITLEBAR_TAB_ICON));
    button->setFocusPolicy(Qt::NoFocus);

    // The whole label these have. Without it an icon-only button says nothing about which view it
    // opens until it has been clicked once.
    button->setToolTip(tooltip);

    // Set here rather than in the stylesheet because Qt Style Sheets have no `cursor` property -
    // it is one of the CSS properties QStyleSheetStyle does not implement. The caption buttons
    // keep the plain arrow: those are window furniture, and Windows draws its own with an arrow.
    button->setCursor(Qt::PointingHandCursor);

    button->setCheckable(true);
    button->setAutoExclusive(true);

    // Same reason as the caption buttons: auto-raise is what makes QCommonStyle ask the icon for
    // its Active - hover - artwork at all.
    button->setAutoRaise(true);

    // Fixed both ways, unlike a caption button: the height is part of the look here rather than
    // something the layout should stretch, and the bar it sits in has a fixed height anyway.
    button->setFixedSize(TITLEBAR_TAB_WIDTH, TITLEBAR_TAB_HEIGHT);

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

    // The two views the window can show, as tabs at the inner end of the bar. Three files per tab
    // for the same reason the caption glyphs have several: the artwork cannot pick up a colour
    // from the stylesheet, so each fill the tab can sit on needs a drawing that reads against it.
    // Idle is the same #A9AEB6 the tray menu's copies of these icons use, hover is the near-white
    // the rest of the window's text is in, and selected is the bar's own background - the current
    // tab is a light fill, so its glyph is the one that goes dark.
    //
    // On and Off are the checked and unchecked states - a checked QToolButton is what QStyle
    // fetches On for. Active is the pointer being over the button.
    //
    // All four combinations are named, including the one that looks redundant - the current tab
    // with the pointer on it. QSvgIconEngine has one fallback and only one: a combination it was
    // not given falls all the way back to Normal/Off, not to the nearest neighbour, so leaving
    // Active/On out would put the idle grey glyph on the light fill the moment the pointer
    // touched the tab that was already current.
    QIcon deviceListIcon;
    deviceListIcon.addFile(":/assets/device_list.svg",          QSize(), QIcon::Normal, QIcon::Off);
    deviceListIcon.addFile(":/assets/device_list_hover.svg",    QSize(), QIcon::Active, QIcon::Off);
    deviceListIcon.addFile(":/assets/device_list_selected.svg", QSize(), QIcon::Normal, QIcon::On);
    deviceListIcon.addFile(":/assets/device_list_selected.svg", QSize(), QIcon::Active, QIcon::On);

    QIcon settingsIcon;
    settingsIcon.addFile(":/assets/settings.svg",          QSize(), QIcon::Normal, QIcon::Off);
    settingsIcon.addFile(":/assets/settings_hover.svg",    QSize(), QIcon::Active, QIcon::Off);
    settingsIcon.addFile(":/assets/settings_selected.svg", QSize(), QIcon::Normal, QIcon::On);
    settingsIcon.addFile(":/assets/settings_selected.svg", QSize(), QIcon::Active, QIcon::On);

    deviceListTab = tabButton(this, deviceListIcon, "deviceListTab", tr("Device List"));
    settingsTab   = tabButton(this, settingsIcon,   "settingsTab",   tr("Settings"));

    // The view the window opens on. Straight to the button rather than through setCurrentTab(),
    // which would announce a choice to a window that has not connected to hear it yet.
    deviceListTab->setChecked(true);

    connect(deviceListTab, &QToolButton::clicked, this, [this]() { emit tabSelected(Tab::DeviceList); });
    connect(settingsTab,   &QToolButton::clicked, this, [this]() { emit tabSelected(Tab::Settings); });

    QHBoxLayout *layout = new QHBoxLayout(this);

    // No margin on the right and no spacing anywhere: the caption buttons have to touch each
    // other and reach the window edge. The one gap that is wanted, between the icon and the
    // title, is added on its own below. Zero top and bottom is what lets the buttons run the
    // full height of the bar.
    layout->setContentsMargins(10, 0, 0, 0);
    layout->setSpacing(0);

    // A plain widget rather than a QFrame VLine, for the same reason as the status bar's rule: a
    // frame's line is drawn by the native style in the style's own colours, and this one has to be
    // the grey the rest of the window is drawn in.
    QWidget *separator = new QWidget(this);
    separator->setObjectName("titleBarSeparator");
    separator->setAttribute(Qt::WA_StyledBackground, true);
    separator->setFixedSize(1, TITLEBAR_SEPARATOR_HEIGHT);

    layout->addWidget(iconLbl);
    layout->addSpacing(8);
    layout->addWidget(titleLbl);
    layout->addStretch(1);
    // Bottom rather than centre: the layout's cell stops at the bar's bottom hairline, so aligning
    // there puts each tab's square bottom edge directly on the rule it meets - and leaves the
    // window's outline unbroken, which a tab drawn over the hairline would not.
    layout->addWidget(deviceListTab, 0, Qt::AlignBottom);
    layout->addSpacing(TITLEBAR_TAB_GAP);
    layout->addWidget(settingsTab, 0, Qt::AlignBottom);
    layout->addSpacing(TITLEBAR_SEPARATOR_MARGIN);
    layout->addWidget(separator, 0, Qt::AlignVCenter);
    layout->addSpacing(TITLEBAR_SEPARATOR_MARGIN);
    layout->addWidget(minimiseBtn);
    layout->addWidget(maximiseBtn);
    layout->addWidget(closeBtn);
}

void TitleBar::setTitle(const QString &title)
{
    titleLbl->setText(title);
}

// Emits tabSelected() as a click on the tab would, so that a choice made from outside - the tray
// menu is the only caller so far - reaches the window down the one path a choice made in here
// does. Nothing listening turns round and calls this again, so the signal cannot come back.
void TitleBar::setCurrentTab(Tab tab)
{
    // Checking the button is enough to uncheck the other: they are auto-exclusive.
    (tab == Tab::Settings ? settingsTab : deviceListTab)->setChecked(true);

    emit tabSelected(tab);
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
