#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "connection.h"
#include "virtdisk.h"

#include <QMainWindow>
#include <QMap>
#include <QTcpServer>
#include <QUdpSocket>
#include <QTcpSocket>
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

public slots:
    void onBroadcasting();
    void onConnection();

private:
    void broadcast();
    void establishConnection(const Connection&);
    void sendInitialInfo(const Connection&);
    void createTrayIcon();

    Ui::MainWindow  *ui = nullptr;
    QAction         *restoreAction;
    QAction         *quitAction;
    QSystemTrayIcon *trayIcon;
    QMenu           *trayIconMenu;

    QTcpServer     *server = nullptr;
    QUdpSocket     *broadcaster = nullptr;

    QMap<QString, Connection> connections;

    VirtDisk *virtDisk = nullptr;
};
#endif // MAINWINDOW_H
