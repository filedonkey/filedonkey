#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QDesktopServices>
#include <QLocale>
#include <QUrl>

#define THEME_LIGHTNESS_BARRIER 128

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    restoreAction = new QAction(tr("&Restore"), this);
    connect(restoreAction, &QAction::triggered, this, &QWidget::showNormal);

    quitAction = new QAction(tr("&Quit"), this);
    connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    upgradeToProAction = new QAction(tr("&Upgrade to Pro"), this);
    connect(upgradeToProAction, &QAction::triggered, this, &MainWindow::onUpgradeToPro);

    createTrayIcon();

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

void MainWindow::setTryaIcon()
{
    auto bg = palette().color(QPalette::Active, QPalette::Window);
    qDebug() << "[MainWindow::setTryaIcon] lightness:" << bg.lightness();

    if (bg.lightness() < THEME_LIGHTNESS_BARRIER)
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
