#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "connection.h"
#include "virtdisk.h"

#include <functional>

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

using RequestHandler = std::function<QByteArray(QByteArray)>;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void changeEvent(QEvent *event) override;

public slots:
    void onBroadcasting();
    void onConnection();
    void onSocketReadyRead();
    void onUpgradeToPro();
    void onUploaded(u64 uploaded);
    void onDownloaded(u64 downloaded);

private:
    void broadcast();
    void createTrayIcon();
    void setTryaIcon();

    QByteArray readdirHandler(QByteArray payload);
    QByteArray readHandler(QByteArray payload);
    QByteArray writeHandler(QByteArray payload);
    QByteArray readlinkHandler(QByteArray payload);
    QByteArray statfsHandler(QByteArray payload);
    QByteArray getattrHandler(QByteArray payload);

    Ui::MainWindow  *ui = nullptr;
    QAction         *restoreAction;
    QAction         *quitAction;
    QAction         *upgradeToProAction;
    QSystemTrayIcon *trayIcon;
    QMenu           *trayIconMenu;

    QTcpServer     *server = nullptr;
    QUdpSocket     *broadcaster = nullptr;

    QMap<QString, Connection> connections;
    QMap<QString, RequestHandler> fuseHandlers;

    VirtDisk *virtDisk = nullptr;
};
#endif // MAINWINDOW_H
