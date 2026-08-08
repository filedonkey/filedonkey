#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "core.h"
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

public slots:
    void onUpgradeToPro();
    void onUploaded(u64 uploaded);
    void onDownloaded(u64 downloaded);

private:
    void createTrayIcon();
    void setTryaIcon();

    Ui::MainWindow  *ui = nullptr;
    QAction         *restoreAction;
    QAction         *quitAction;
    QAction         *upgradeToProAction;
    QSystemTrayIcon *trayIcon;
    QMenu           *trayIconMenu;

    TitleBar        *titleBar = nullptr;
    QWidget         *shadowLayer = nullptr;

    LocalNode       *node = nullptr;
};
#endif // MAINWINDOW_H
