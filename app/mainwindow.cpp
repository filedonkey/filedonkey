#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFile>
#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QStyleHints>
#include <QHBoxLayout>
#include <QIcon>
#include <QUrl>
#include <QVBoxLayout>

#define THEME_LIGHTNESS_BARRIER 128

// The strip of window kept empty around the visible frame for the shadow to fall on. Everything
// inside it - title bar, content, status bar - is inset by this much, and the window is grown to
// match so the visible part stays the size the .ui file asked for.
#define SHADOW_MARGIN   18
#define SHADOW_BLUR     28
#define SHADOW_OFFSET_Y 6

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

namespace {

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

    // A graphics effect on a top-level window does not render, so the shadow cannot go on the
    // window itself. It goes on a child instead: a plain widget the size and shape of the visible
    // frame, stacked behind everything and painted the same colour as the corners it sits under.
    // Its own body is covered by the title bar, the content and the status bar - all that is ever
    // seen of it is the blur it throws into the margin outside.
    shadowLayer = new QWidget(this);
    shadowLayer->setObjectName("windowShadow");
    shadowLayer->setAttribute(Qt::WA_StyledBackground, true);
    shadowLayer->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect(shadowLayer);
    shadow->setBlurRadius(SHADOW_BLUR);
    shadow->setOffset(0, SHADOW_OFFSET_Y);
    shadow->setColor(QColor(0, 0, 0, 160));
    shadowLayer->setGraphicsEffect(shadow);

    // setupUi() has already made the central widget, so this one is the youngest sibling and
    // would otherwise sit on top of the lot.
    shadowLayer->lower();

    setContentsMargins(SHADOW_MARGIN, SHADOW_MARGIN, SHADOW_MARGIN, SHADOW_MARGIN);

    // Fixed, and bigger than the .ui asked for by exactly what the margins take - so the frame
    // the user sees is still that size rather than that size minus the shadow. Fixing it here is
    // what makes the window unresizable: min and max become the same, so the window manager has
    // nothing to drag and the caption offers no maximise.
    setFixedSize(size() + QSize(2 * SHADOW_MARGIN, 2 * SHADOW_MARGIN));

    // Once, rather than from a resizeEvent: the window is the only thing that could have moved it
    // and it can no longer change size.
    shadowLayer->setGeometry(contentsRect());

    titleBar = new TitleBar(this);
    titleBar->setTitle(windowTitle());
    setMenuWidget(titleBar);

    // Asked once, before anything decides what closing the window should mean.
    trayAvailable = QSystemTrayIcon::isSystemTrayAvailable();
    qDebug() << "[MainWindow] system tray available:" << trayAvailable;

    // Closing a window never ends the application on its own - closeEvent() decides, and only it.
    // Hiding the last window would otherwise be indistinguishable from closing it, and the Hide
    // the no-tray dialog offers would quit instead.
    qApp->setQuitOnLastWindowClosed(false);

    deviceListAction = new QAction(QIcon(":/assets/device_list.svg"), tr("&Device List"), this);
    connect(deviceListAction, &QAction::triggered, this, &MainWindow::restoreWindow);

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
    connect(settingsAction, &QAction::triggered, this, &MainWindow::onUpgradeToPro);

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

    QVBoxLayout *contentLayout = new QVBoxLayout(ui->centralwidget);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(deviceList);

    node = new LocalNode(this);

    // Straight to the list: nothing here has anything to add to them, and MainWindow keeping a
    // shadow copy of who is connected would only be a second thing to get out of step.
    connect(node, &LocalNode::peerAdded,      deviceList, &DeviceList::onPeerAdded);
    connect(node, &LocalNode::peerMounted,    deviceList, &DeviceList::onPeerMounted);
    connect(node, &LocalNode::peerUploaded,   deviceList, &DeviceList::onPeerUploaded);
    connect(node, &LocalNode::peerDownloaded, deviceList, &DeviceList::onPeerDownloaded);
    connect(node, &LocalNode::peerRemoved,    deviceList, &DeviceList::onPeerRemoved);

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

void MainWindow::onUpgradeToPro()
{
    QString link = "https://filedonkey.app";
    QDesktopServices::openUrl(QUrl(link));
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
        hide();
        announceStillRunning();
        return;
    }

    switch (askWhatCloseMeans())
    {
        case CloseChoice::Cancel:
            break;

        case CloseChoice::Hide:
            hide();
            break;

        case CloseChoice::Quit:
            qApp->quit();
            break;
    }
}

MainWindow::CloseChoice MainWindow::askWhatCloseMeans()
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("FileDonkey"));
    box.setText(tr("Close the window, or quit FileDonkey?"));
    box.setInformativeText(tr(
        "There is no system tray on this desktop, so FileDonkey has nowhere to sit while it runs.\n\n"
        "Quitting unmounts every connected device and drops all connections.\n\n"
        "Hiding keeps them running. Start FileDonkey again to bring this window back."));

    // AcceptRole and DestructiveRole rather than fixed positions: each platform orders its
    // buttons its own way, and the roles are what let it.
    QPushButton *hideButton = box.addButton(tr("Hide"), QMessageBox::AcceptRole);
    QPushButton *quitButton = box.addButton(tr("Quit"), QMessageBox::DestructiveRole);
    QPushButton *cancelButton = box.addButton(QMessageBox::Cancel);

    // Named so the stylesheet can pick it out and give it the error red - it is the one button
    // here that loses work.
    quitButton->setObjectName("dialogQuitBtn");

    box.setDefaultButton(hideButton);
    box.setEscapeButton(cancelButton);

    box.exec();

    if (box.clickedButton() == quitButton) return CloseChoice::Quit;
    if (box.clickedButton() == hideButton) return CloseChoice::Hide;

    // Covers Cancel, Escape, and the window being dismissed by the desktop.
    return CloseChoice::Cancel;
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

    trayIcon = new QSystemTrayIcon(this);
    setTryaIcon();
    trayIcon->setToolTip("FileDonkey");
    trayIcon->setContextMenu(trayIconMenu);

    // A left click on the icon is what everyone tries first, so it does the same as Restore:
    // brings the window back if it is away, and pulls it to the front if it is merely buried.
    // Trigger only - the right click belongs to the context menu, and DoubleClick would arrive
    // after a Trigger anyway on the platforms that send both.
    connect(trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
        {
            restoreWindow();
        }
    });

    trayIcon->show();
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
