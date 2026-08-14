#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "core.h"
#include "devicelist.h"
#include "localnode.h"
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

    // Puts the window away and resets it to the device list. Both of the ways closeEvent() can
    // decide to keep the application running go through this rather than calling hide() directly.
    void hideWindow();

    // The right-hand end of the status bar in front of the device count: this machine's own
    // address, and the rule that separates the two.
    QWidget *endpointWidget(const QString &endpoint);

    // Shown once, the first time closing the window leaves the app running in the tray. Without
    // it the window simply vanishes and the app looks like it crashed.
    void announceStillRunning();

    enum class CloseChoice { Cancel, Hide, Quit };

    // Asked only where there is no tray, because there the window is the whole application and
    // closing it could mean either thing. Deliberately has no "do not ask again": this dialog is
    // the one place a tray-less desktop can quit from, and remembering Hide would seal it off.
    CloseChoice askWhatCloseMeans();

    Ui::MainWindow  *ui = nullptr;
    QAction         *deviceListAction;
    QAction         *quitAction;
    QAction         *settingsAction;
    QSystemTrayIcon *trayIcon;
    QMenu           *trayIconMenu;

    TitleBar        *titleBar = nullptr;
    QWidget         *shadowLayer = nullptr;

    // The window's content, one page per tab in the title bar.
    QStackedWidget  *contentStack = nullptr;
    DeviceList      *deviceList = nullptr;
    QWidget         *settingsPage = nullptr;

    // Read once at startup. On Linux this can turn true later, when a shell registers its
    // StatusNotifier host after login, and Qt offers no signal for that - so a user who installs
    // GNOME's AppIndicator extension gets the tray-less behaviour until the app is restarted.
    bool             trayAvailable = false;

    LocalNode       *node = nullptr;
};
#endif // MAINWINDOW_H
