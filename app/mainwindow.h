#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "closechoicedialog.h"
#include "core.h"
#include "devicelist.h"
#include "localnode.h"
#include "settingspage.h"
#include "titlebar.h"

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
class QStackedWidget;
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

public slots:
    // Brings the window back from hidden or minimised and puts it in front. Wired to the tray's
    // view entries, which each pick a tab after calling this, and to a second start of the
    // application, which picks none - it wants the window back as the user left it.
    void restoreWindow();

private:
    void createTrayIcon();
    void setTryaIcon();

    // Rebuilds the device section at the top of the tray menu from the device list, and is wired
    // to the menu's own aboutToShow - so what it shows is read the moment it is asked for rather
    // than kept up to date by a signal of its own.
    void refreshTrayDevices();

    // Puts the window away and resets it to the device list. Both of the ways closeEvent() can
    // decide to keep the application running go through this rather than calling hide() directly.
    void hideWindow();

    // The right-hand end of the status bar in front of the device count: this machine's own
    // address, and the rule that separates the two.
    QWidget *endpointWidget(const QString &endpoint);

    // Shown once, the first time closing the window leaves the app running in the tray. Without
    // it the window simply vanishes and the app looks like it crashed.
    void announceStillRunning();

    // Wired to LocalNode::manualConnectFailed. The dialog that took the address is closed by the
    // time an answer - or the lack of one - comes back, so this is where the news lands.
    void reportManualConnectFailed(const QString &address, const QString &reason);

    // Asked only where there is no tray, because there the window is the whole application and
    // closing it could mean either thing. The choice is CloseChoiceDialog's own enum rather than
    // one of ours: it is the dialog that offers the three answers, and a second copy of them here
    // would be a second thing to keep in step.
    CloseChoiceDialog::Choice askWhatCloseMeans();

    Ui::MainWindow  *ui = nullptr;
    QAction         *deviceListAction;
    QAction         *quitAction;
    QAction         *settingsAction;
    QSystemTrayIcon *trayIcon;
    QMenu           *trayIconMenu;

    // Everything refreshTrayDevices() put in the menu last time: one action per device and the
    // separator that closes the section. Held so the next pass can take them out again - the
    // entries below it are the menu's own and are never touched.
    QList<QAction *> trayDeviceActions;

    TitleBar        *titleBar = nullptr;
    QWidget         *shadowLayer = nullptr;

    // The window's content, one page per tab in the title bar.
    QStackedWidget  *contentStack = nullptr;
    DeviceList      *deviceList = nullptr;
    SettingsPage    *settingsPage = nullptr;

    // Read once at startup. On Linux this can turn true later, when a shell registers its
    // StatusNotifier host after login, and Qt offers no signal for that - so a user who installs
    // GNOME's AppIndicator extension gets the tray-less behaviour until the app is restarted.
    bool             trayAvailable = false;

    LocalNode       *node = nullptr;
};
#endif // MAINWINDOW_H
