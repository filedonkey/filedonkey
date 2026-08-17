#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "windowshadow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFile>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QStackedWidget>
#include <QStyleHints>
#include <QHBoxLayout>
#include <QIcon>
#include <QUrl>
#include <QVBoxLayout>

#define THEME_LIGHTNESS_BARRIER 128

// SHADOW_MARGIN, and the widget that throws the blur into it, come from windowshadow.h - this
// window and the manual connect dialog are both frameless and both need the same one.

// APP_VERSION and APP_STAGE come from app.pro, which is where they are written down: it is the
// file Windows' VERSIONINFO block is filled from, and a number kept here as well would be a second
// copy to forget. Not defaulted if they are missing - a build with no version in it should fail to
// compile rather than ship calling itself something made up here.
//
// How far in from the window edge that label sits. The summary at the other end is inset by its
// own layout margin and the spacing after its dot; matching the sum here is what makes the two
// ends of the bar look level - see summaryWidget()'s layout in devicelist.cpp.
#define STATUS_EDGE_INSET 9

// The rule between this machine's address and the device count, and the room left in front of it.
// Shorter than the bar so it reads as a divider between two lines rather than a second border.
#define STATUS_SEPARATOR_HEIGHT 12
#define STATUS_SEPARATOR_GAP    9

// The state dot in front of a device in the tray menu: the icon a menu item is given, and the dot
// drawn in the middle of it. The icon is the size the styles ask for a small icon in; the dot is a
// touch bigger than the 6px the rows carry, which is what keeps it reading as a dot next to 13px
// menu text rather than disappearing into it.
#define TRAY_DOT_ICON 16
#define TRAY_DOT_SIZE 8

namespace {

// Amber while the mount is coming up, green once it is up - the two colours in the #deviceDot
// rules in filedonkey.qss, and the same thing the row and the status bar say. Written out here
// because a menu item's icon is a picture: it is not a widget, so no stylesheet rule reaches it,
// and changing a colour means changing it in both places.
#define TRAY_DOT_MOUNTING QColor(0xE0, 0xA3, 0x3C)
#define TRAY_DOT_MOUNTED  QColor(0x4E, 0xC9, 0x7A)

// Drawn at the display's scale factor rather than at 16 square and left to be blown up, so the
// circle keeps its edge on a HiDPI screen. The painter works in the icon's own coordinates either
// way - that is what setDevicePixelRatio() buys.
QIcon trayDot(const QColor &colour)
{
    const qreal ratio = qApp->devicePixelRatio();

    QPixmap pixmap(QSize(TRAY_DOT_ICON, TRAY_DOT_ICON) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);

    const int inset = (TRAY_DOT_ICON - TRAY_DOT_SIZE) / 2;
    painter.drawEllipse(inset, inset, TRAY_DOT_SIZE, TRAY_DOT_SIZE);

    return QIcon(pixmap);
}

// Applied to the whole application, not to this window, so that the tray menu and the tray
// tooltip are covered too - both are top-level windows of their own and a window-level sheet
// never reaches them.
void applyStyleSheet()
{
    QFile sheet(":/assets/filedonkey.qss");
    if (!sheet.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qDebug() << "[applyStyleSheet] could not open the stylesheet:" << sheet.errorString();
        return;
    }

    qApp->setStyleSheet(QString::fromUtf8(sheet.readAll()));
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    applyStyleSheet();

    ui->setupUi(this);

    // Off with the native title bar, on with ours. setMenuWidget() puts the bar in the slot the
    // menu bar had, spanning the full width above the central widget, and deletes the empty
    // QMenuBar the .ui file brings with it.
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);

    // Rounded corners need somewhere for the corner to go. Without this the window is an opaque
    // rectangle and a radius only rounds the colour inside it, leaving the square corner behind;
    // with it, the pixels the radius cuts away are genuinely absent and the desktop shows through.
    // What the window looks like is then entirely up to the three widgets stacked inside it - see
    // the border and radius rules on #titleBar, #centralwidget and QStatusBar in the stylesheet.
    setAttribute(Qt::WA_TranslucentBackground);

    // The blur that falls into the margin below. Built after setupUi(), which has already made the
    // central widget - windowShadow() lowers what it builds, so this one being the youngest sibling
    // does not leave it sitting on top of the lot.
    shadowLayer = windowShadow(this);

    setContentsMargins(SHADOW_MARGIN, SHADOW_MARGIN, SHADOW_MARGIN, SHADOW_MARGIN);

    // Fixed, and bigger than the .ui asked for by exactly what the margins take - so the frame
    // the user sees is still that size rather than that size minus the shadow. Fixing it here is
    // what makes the window unresizable: min and max become the same, so the window manager has
    // nothing to drag and the caption offers no maximise.
    setFixedSize(size() + QSize(2 * SHADOW_MARGIN, 2 * SHADOW_MARGIN));

    // Once, rather than from a resizeEvent: the window is the only thing that could have moved it
    // and it can no longer change size.
    shadowLayer->setGeometry(contentsRect());

    titleBar = new TitleBar(TitleBar::Kind::Window, this);
    titleBar->setTitle(windowTitle());
    setMenuWidget(titleBar);

    // Asked once, before anything decides what closing the window should mean.
    trayAvailable = QSystemTrayIcon::isSystemTrayAvailable();
    qDebug() << "[MainWindow] system tray available:" << trayAvailable;

    // Closing a window never ends the application on its own - closeEvent() decides, and only it.
    // Hiding the last window would otherwise be indistinguishable from closing it, and the Hide
    // the no-tray dialog offers would quit instead.
    qApp->setQuitOnLastWindowClosed(false);

    // The tray's two view entries name the same pair the title bar's tabs do, and mean the same
    // thing: bring the window back, and put it on that view. Restoring without choosing a tab
    // would leave whichever page was up last, so an entry named Settings could open the device
    // list - which is what the Settings entry did before there was a settings page to open.
    deviceListAction = new QAction(QIcon(":/assets/device_list.svg"), tr("&Device List"), this);
    connect(deviceListAction, &QAction::triggered, this, [this]() {
        restoreWindow();
        titleBar->setCurrentTab(TitleBar::Tab::DeviceList);
    });

#if defined(Q_OS_MACOS)
    // Clicking the Dock icon of an app with no window open should bring the window back, the way
    // every mac app behaves. Qt surfaces that as the application going active - only act on it
    // while the window is actually away, or every Cmd-Tab would yank it to the front.
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive && isHidden())
        {
            restoreWindow();
        }
    });
#endif

    quitAction = new QAction(QIcon(":/assets/quit.svg"), tr("&Quit"), this);
    connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    settingsAction = new QAction(QIcon(":/assets/settings.svg"), tr("&Settings"), this);
    connect(settingsAction, &QAction::triggered, this, [this]() {
        restoreWindow();
        titleBar->setCurrentTab(TitleBar::Tab::Settings);
    });

    createTrayIcon();

    // QStatusBar puts a QSizeGrip in its bottom-right corner unless told not to, and the grip
    // carries a diagonal resize cursor of its own. On a fixed-size window that is the one place
    // left that still offers to resize it - and it would drag the window bigger if it could.
    ui->statusbar->setSizeGripEnabled(false);

    // The window's content, and all of it. Built here rather than in the .ui file because every row
    // in it is made as a peer arrives, so there is nothing for Designer to hold - only the layout
    // that gives it the whole central widget. After setFixedSize() above, deliberately: the size in
    // the .ui file is the one the window keeps, and the list is built to fit whatever it gets.
    deviceList = new DeviceList(ui->centralwidget);

    // The other page. Nothing connects it to the node below: what it changes it writes to the
    // settings, and the node reads it from there on its way into the next broadcast.
    settingsPage = new SettingsPage(ui->centralwidget);

    // The pages the tabs choose between. A stack rather than show()/hide() on the list: it keeps
    // one widget's worth of space reserved whichever page is up, so switching to the empty pane
    // cannot let the window's contents collapse.
    contentStack = new QStackedWidget(ui->centralwidget);
    contentStack->setObjectName("contentStack");
    contentStack->addWidget(deviceList);
    contentStack->addWidget(settingsPage);

    // What the bar's tabs open, and what the bar then calls it. The window is named after the view
    // it is showing rather than after itself, so the title says the same thing the selected tab
    // does - which is worth having when the tabs are icons and carry no label of their own.
    //
    // Through setWindowTitle() rather than straight to the bar so the taskbar entry and the
    // Alt-Tab card follow too: the caption is off, but that name is still shown in both, and a
    // window listed as the device list while it is showing the settings would be the odd one out.
    //
    // Nothing to run once at the end of the constructor: the bar has already checked the Device
    // List tab, the .ui file's window title already names that view, and addWidget() put its page
    // in the stack first - so all three start out saying the same thing.
    connect(titleBar, &TitleBar::tabSelected, this, [this](TitleBar::Tab tab) {
        const bool settings = (tab == TitleBar::Tab::Settings);

        // The cast is what gives the two arms of the conditional a type in common: they are
        // unrelated widget classes, and the stack wants either of them as a QWidget.
        contentStack->setCurrentWidget(settings ? (QWidget *)settingsPage : (QWidget *)deviceList);

        setWindowTitle(settings ? tr("FileDonkey Settings") : tr("FileDonkey Device List"));
        titleBar->setTitle(windowTitle());
    });

    QVBoxLayout *contentLayout = new QVBoxLayout(ui->centralwidget);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(contentStack);

    node = new LocalNode(this);

    // Straight to the list: nothing here has anything to add to them, and MainWindow keeping a
    // shadow copy of who is connected would only be a second thing to get out of step.
    connect(node, &LocalNode::peerAdded,      deviceList, &DeviceList::onPeerAdded);
    connect(node, &LocalNode::peerMounted,    deviceList, &DeviceList::onPeerMounted);
    connect(node, &LocalNode::peerUploaded,   deviceList, &DeviceList::onPeerUploaded);
    connect(node, &LocalNode::peerDownloaded, deviceList, &DeviceList::onPeerDownloaded);
    connect(node, &LocalNode::peerRemoved,    deviceList, &DeviceList::onPeerRemoved);

    // The one thing that travels the other way. The list collects an address and knows nothing to
    // do with it; the node dials it and, when nothing answers, says so back through here. A peer
    // that does answer needs no wiring of its own - it arrives as peerAdded above, like any other.
    connect(deviceList, &DeviceList::manualConnectRequested, node, &LocalNode::connectManually);

    // Queued, unlike every other connection here. What it reaches opens a modal message box, and a
    // modal box runs an event loop of its own - delivered directly it would start that loop inside
    // the socket callback the failure was noticed in, with the socket the node had just handed to
    // deleteLater() being freed under the very call still returning through it. Queued, the stack
    // is back at the event loop before the box goes up.
    connect(node, &LocalNode::manualConnectFailed, this, &MainWindow::reportManualConnectFailed,
            Qt::QueuedConnection);

    // The left-hand end of the status bar. Neither half goes through tr(): a version number is the
    // same string in every language, and lupdate cannot see a stage that arrives as a macro anyway.
    QLabel *versionLbl = new QLabel(QString("v%1 (%2)").arg(APP_VERSION, APP_STAGE), this);
    versionLbl->setObjectName("versionLbl");
    versionLbl->setContentsMargins(STATUS_EDGE_INSET, 0, 0, 0);

    // A normal widget, which is what puts it at the left-hand end: QStatusBar lays these out from
    // the left and reserves the right for permanent ones. Nothing here ever calls showMessage(), so
    // there is no message to be pushed aside by - or to cover - the label.
    ui->statusbar->addWidget(versionLbl);

    // This machine's own address, at the right-hand end in front of the count: the rows say where
    // every other machine is, and this is the one address they cannot say. Read once, here, and
    // left as it was found - the node re-reads it on every call, but a machine that changes network
    // mid-session has no way to tell this label about it, and no signal to add one to.
    //
    // Skipped entirely when there is no address to show, rather than drawn empty behind a rule that
    // then separates the count from nothing.
    const QString endpoint = node->localEndpoint();
    if (!endpoint.isEmpty())
    {
        ui->statusbar->addPermanentWidget(endpointWidget(endpoint));
    }

    // The other end, and the rest of the bar. It used to carry the transfer counters, which have
    // gone to the rows that earned them - a device moves its own bytes, and one pair of labels down
    // here could only ever show whichever of them moved some last.
    ui->statusbar->addPermanentWidget(deviceList->summaryWidget());
}

// The address and the rule that keeps it off the device count, as one widget: permanent widgets
// are spaced by the status bar's own layout, and only what shares a layout of ours can be given
// the gap the rule wants on either side of it.
QWidget *MainWindow::endpointWidget(const QString &endpoint)
{
    QLabel *endpointLbl = new QLabel(endpoint, this);
    endpointLbl->setObjectName("endpointLbl");

    // A plain widget rather than a QFrame VLine: a frame's line is drawn by the native style in
    // the style's own colours, and this one has to be the same grey as the window's border.
    QWidget *separator = new QWidget(this);
    separator->setObjectName("statusSeparator");
    separator->setAttribute(Qt::WA_StyledBackground, true);
    separator->setFixedSize(1, STATUS_SEPARATOR_HEIGHT);

    QWidget *box = new QWidget(this);
    box->setObjectName("statusEndpoint");

    QHBoxLayout *layout = new QHBoxLayout(box);
    layout->setContentsMargins(0, 5, 0, 5);
    layout->setSpacing(STATUS_SEPARATOR_GAP);
    layout->addWidget(endpointLbl, 0, Qt::AlignVCenter);
    layout->addWidget(separator, 0, Qt::AlignVCenter);

    return box;
}

MainWindow::~MainWindow()
{
    // Before ui, not after. Tearing the node down waits for each mount to come down, and that wait
    // pumps the mount helper's pipes on macOS - transfer totals arrive as peerUploaded() and land
    // in the device list, which writes to a label. Let the child destructor take the node and it
    // runs after delete ui, writing to a label that is gone.
    delete node;
    node = nullptr;

    delete ui;
}

// Both routes out of closeEvent() come through here, so that however the window is put away it is
// put away the same: on the device list, which is the view it opens on and the one the whole
// application is about. Without it a user who closed the window while reading the settings would
// find the settings still up the next time they opened it, whatever they did to open it with.
//
// The tab is switched after hide() rather than before, so the change is made off screen instead of
// flashing in the instant before the window goes.
void MainWindow::hideWindow()
{
    hide();
    titleBar->setCurrentTab(TitleBar::Tab::DeviceList);
}

void MainWindow::restoreWindow()
{
    // showNormal() on its own leaves a hidden window behind whatever has focus, and a minimised
    // one comes back minimised. Clearing the state first, then all three calls, is what actually
    // puts it in front on every platform.
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);

    show();
    raise();
    activateWindow();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Always refused. Whether the application lives on is decided below and acted on directly,
    // never by letting the close through and hoping it does the right thing.
    event->ignore();

    if (trayAvailable)
    {
        // The node keeps running, mounts and all, and the tray is how it stays reachable.
        hideWindow();
        announceStillRunning();
        return;
    }

    switch (askWhatCloseMeans())
    {
        case CloseChoiceDialog::Choice::Cancel:
            break;

        case CloseChoiceDialog::Choice::Hide:
            hideWindow();
            break;

        case CloseChoiceDialog::Choice::Quit:
            qApp->quit();
            break;
    }
}

// Why a typed address came to nothing. Shown from here rather than from the dialog that collected
// it, which is long closed: the attempt takes seconds - a TCP connect to an address with nothing on
// it answers with silence, not a refusal - and a modal dialog held open for them is one that looks
// stuck. Nothing is shown when it works; the row appearing in the list is what says so.
void MainWindow::reportManualConnectFailed(const QString &address, const QString &reason)
{
    // Brought back first. The window may well have been put away while the attempt was running, and
    // a message box on its own with no window behind it says nothing about who is asking.
    restoreWindow();
    titleBar->setCurrentTab(TitleBar::Tab::DeviceList);

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("FileDonkey"));
    box.setText(tr("Could not connect to %1.").arg(address));
    box.setInformativeText(reason);
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
}

// The question itself, its wording and the order of its buttons are the dialog's - see
// closechoicedialog.h. All that is left here is asking it.
//
// It is a window of this application's own rather than a QMessageBox, which is what it used to be:
// a message box wears the system's frame and the system's button layout, and beside a window that
// draws its own it looked like a dialog from a different program.
CloseChoiceDialog::Choice MainWindow::askWhatCloseMeans()
{
    CloseChoiceDialog dialog(this);
    dialog.exec();

    // Read off the dialog rather than from exec()'s own result: two of the three answers accept it
    // and only the dialog knows which of them was pressed. Cancel covers the rest - the Cancel
    // button, Escape, and the close button in the title bar.
    return dialog.choice();
}

void MainWindow::announceStillRunning()
{
    QSettings settings;
    if (settings.value("tray/closeNoticeShown", false).toBool()) return;

    trayIcon->showMessage(tr("FileDonkey is still running"),
                          tr("Your devices stay mounted. Open it again from the tray icon, "
                             "or quit from the same menu."),
                          QSystemTrayIcon::Information,
                          6000);

    settings.setValue("tray/closeNoticeShown", true);
}

void MainWindow::createTrayIcon()
{
    trayIconMenu = new QMenu(this);
    // trayIconMenu->addAction(minimizeAction);
    // trayIconMenu->addAction(maximizeAction);
    trayIconMenu->addAction(deviceListAction);
    trayIconMenu->addAction(settingsAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);

    // The devices go in above all of this, and are put there as the menu opens rather than kept in
    // step by a signal: a menu nobody is looking at has nothing to be right about, and the peers
    // come and go while it is closed. Safe this early even though the device list is built after
    // this call - nothing can drop the menu before the constructor has finished.
    connect(trayIconMenu, &QMenu::aboutToShow, this, &MainWindow::refreshTrayDevices);

    trayIcon = new QSystemTrayIcon(this);
    setTryaIcon();
    trayIcon->setToolTip("FileDonkey");
    trayIcon->setContextMenu(trayIconMenu);

#if defined(Q_OS_WIN)
    // On Windows a left click on the icon is what everyone tries first, so it does the same as the
    // Device List entry in the menu behind it: brings the window back, or pulls it to the front if
    // it is merely buried, and puts it on the device list either way. Through the action rather
    // than by repeating what it does, so the click and the entry cannot come to mean different
    // things.
    //
    // Windows only - on macOS and the common Linux tray implementations a left click is expected to
    // drop the menu rather than open a window.
    //
    // Trigger only - the right click belongs to the context menu, and DoubleClick would arrive
    // after a Trigger anyway on the platforms that send both.
    connect(trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
        {
            deviceListAction->trigger();
        }
    });
#endif

    trayIcon->show();
}

// The device section: one entry per connected machine, named as its row names it and carrying the
// same state dot, above a separator that keeps them off the menu's own entries. Nothing is left
// behind when there are none - no heading with nothing under it, and no separator with nothing
// above it, so a menu with no devices is the menu as it was before there were any.
void MainWindow::refreshTrayDevices()
{
    // Whatever the last pass put there. Deleting an action takes it out of every menu holding it,
    // so there is nothing else to undo - and these are ours alone.
    qDeleteAll(trayDeviceActions);
    trayDeviceActions.clear();

    for (const DeviceList::Device &device : deviceList->devices())
    {
        const bool mounted = !device.mountPoint.isEmpty();

        // An ampersand in a machine name would be eaten as a mnemonic and underline the letter
        // after it. Doubling it is how a QAction is told the name means it literally.
        QString name = device.name;
        name.replace('&', "&&");

        QAction *action = new QAction(trayDot(mounted ? TRAY_DOT_MOUNTED : TRAY_DOT_MOUNTING),
                                      name, this);

        // What clicking a row does, where there is something to click through to: a mounted device
        // opens in the desktop's file manager. One still coming up has nowhere to go yet, so it
        // brings the window up on the list instead - which is where its progress is shown. Not
        // disabled for that: a greyed entry would take the colour out of the dot that is the whole
        // point of it being there.
        if (mounted)
        {
            const QString mountPoint = device.mountPoint;
            connect(action, &QAction::triggered, this, [mountPoint]() {
                QDesktopServices::openUrl(QUrl::fromLocalFile(mountPoint));
            });
        }
        else
        {
            connect(action, &QAction::triggered, deviceListAction, &QAction::trigger);
        }

        trayIconMenu->insertAction(deviceListAction, action);
        trayDeviceActions.append(action);
    }

    if (trayDeviceActions.isEmpty()) return;

    trayDeviceActions.append(trayIconMenu->insertSeparator(deviceListAction));
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange)
    {
        setTryaIcon();
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);

    // The fixed size means this should only ever run once, on the way up. It is here for the case
    // it does not: a tiling utility resizing us from outside goes around Qt's size constraints,
    // and without this the shadow would stay the size the window used to be.
    if (shadowLayer)
    {
        shadowLayer->setGeometry(contentsRect());
    }
}

void MainWindow::setTryaIcon()
{
    // Ask the system, not ourselves. This window is now dark whatever the OS is doing, so our own
    // palette would answer "dark" forever and we would hand a light icon to a light taskbar. The
    // tray sits in the taskbar, so the taskbar's theme is the one that decides.
    const Qt::ColorScheme scheme = qApp->styleHints()->colorScheme();

    bool systemIsDark = (scheme == Qt::ColorScheme::Dark);
    if (scheme == Qt::ColorScheme::Unknown)
    {
        // Nothing to ask on this platform - fall back to reading a palette off the desktop.
        auto bg = qApp->palette().color(QPalette::Active, QPalette::Window);
        systemIsDark = bg.lightness() < THEME_LIGHTNESS_BARRIER;
    }

    qDebug() << "[MainWindow::setTryaIcon] system colour scheme dark:" << systemIsDark;

    if (systemIsDark)
    {
        // QIcon::setThemeName(LIGHT_THEME);
        trayIcon->setIcon(QIcon(":/assets/filedonkey_tray_icon_light.ico"));
    }
    else
    {
        // QIcon::setThemeName(DARK_THEME);
        trayIcon->setIcon(QIcon(":/assets/filedonkey_tray_icon_dark.ico"));
    }
}
