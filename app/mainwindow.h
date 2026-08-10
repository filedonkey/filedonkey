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
    void onUpgradeToPro();
    void onUploaded(u64 uploaded);
    void onDownloaded(u64 downloaded);

    // Brings the window back from hidden or minimised and puts it in front. Wired to the tray's
    // Restore entry and to a second start of the application.
    void restoreWindow();

private:
    void createTrayIcon();
    void setTryaIcon();

    // Shown once, the first time closing the window leaves the app running in the tray. Without
    // it the window simply vanishes and the app looks like it crashed.
    void announceStillRunning();

    enum class CloseChoice { Cancel, Hide, Quit };

    // Asked only where there is no tray, because there the window is the whole application and
    // closing it could mean either thing. Deliberately has no "do not ask again": this dialog is
    // the one place a tray-less desktop can quit from, and remembering Hide would seal it off.
    CloseChoice askWhatCloseMeans();

    Ui::MainWindow  *ui = nullptr;
    QAction         *restoreAction;
    QAction         *quitAction;
    QAction         *upgradeToProAction;
    QSystemTrayIcon *trayIcon;
    QMenu           *trayIconMenu;

    TitleBar        *titleBar = nullptr;
    QWidget         *shadowLayer = nullptr;
    DeviceList      *deviceList = nullptr;

    // Read once at startup. On Linux this can turn true later, when a shell registers its
    // StatusNotifier host after login, and Qt offers no signal for that - so a user who installs
    // GNOME's AppIndicator extension gets the tray-less behaviour until the app is restarted.
    bool             trayAvailable = false;

    LocalNode       *node = nullptr;
};
#endif // MAINWINDOW_H
