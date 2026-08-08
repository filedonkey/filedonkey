#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QApplication>
#include <QDesktopServices>
#include <QFile>
#include <QGraphicsDropShadowEffect>
#include <QLocale>
#include <QResizeEvent>
#include <QStyleHints>
#include <QUrl>

#define THEME_LIGHTNESS_BARRIER 128

// The strip of window kept empty around the visible frame for the shadow to fall on. Everything
// inside it - title bar, content, status bar - is inset by this much, and the window is grown to
// match so the visible part stays the size the .ui file asked for.
#define SHADOW_MARGIN   18
#define SHADOW_BLUR     28
#define SHADOW_OFFSET_Y 6

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

    // The design tracks its micro-labels out by half a pixel. There is no letter-spacing in
    // QSS, so the one label that wants it gets it here.
    QFont microLabel = ui->networkLbl->font();
    microLabel.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
    ui->networkLbl->setFont(microLabel);

    restoreAction = new QAction(tr("&Restore"), this);
    connect(restoreAction, &QAction::triggered, this, &QWidget::showNormal);

    quitAction = new QAction(tr("&Quit"), this);
    connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    upgradeToProAction = new QAction(tr("&Upgrade to Pro"), this);
    connect(upgradeToProAction, &QAction::triggered, this, &MainWindow::onUpgradeToPro);

    createTrayIcon();

    // QStatusBar puts a QSizeGrip in its bottom-right corner unless told not to, and the grip
    // carries a diagonal resize cursor of its own. On a fixed-size window that is the one place
    // left that still offers to resize it - and it would drag the window bigger if it could.
    ui->statusbar->setSizeGripEnabled(false);

    ui->statusbar->addWidget(ui->networkWidget);

    node = new LocalNode(this);
    connect(node, &LocalNode::uploadedChanged, this, &MainWindow::onUploaded);
    connect(node, &LocalNode::downloadedChanged, this, &MainWindow::onDownloaded);
}

MainWindow::~MainWindow()
{
    // Before ui, not after. Tearing the node down waits for each mount to come down, and that wait
    // pumps the mount helper's pipes on macOS - transfer totals arrive as uploadedChanged() and
    // land in onUploaded(), which writes to a label. Let the child destructor take the node and it
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

void MainWindow::onUploaded(u64 uploaded)
{
    QLocale locale(QLocale::English, QLocale::UnitedStates);
    this->ui->uploadedLbl->setText(QString("⬆️ %1").arg(locale.formattedDataSize(uploaded)));
}

void MainWindow::onDownloaded(u64 downloaded)
{
    QLocale locale(QLocale::English, QLocale::UnitedStates);
    this->ui->downloadedLbl->setText(QString("⬇️ %1").arg(locale.formattedDataSize(downloaded)));
}

void MainWindow::createTrayIcon()
{
    trayIconMenu = new QMenu(this);
    // trayIconMenu->addAction(minimizeAction);
    // trayIconMenu->addAction(maximizeAction);
    trayIconMenu->addAction(restoreAction);
    trayIconMenu->addAction(upgradeToProAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);

    trayIcon = new QSystemTrayIcon(this);
    setTryaIcon();
    trayIcon->setToolTip("FileDonkey");
    trayIcon->setContextMenu(trayIconMenu);
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
