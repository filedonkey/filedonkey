#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "connection.h"
#include "virtdisk.h"
#include "fusebackend.h"

#include <functional>

#include <QMainWindow>
#include <QMap>
#include <QPointer>
#include <QTcpServer>
#include <QThreadPool>
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

using RequestHandler = std::function<QByteArray(u64, QByteArray)>;

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
    void onSocketDisconnected();
    void onVirtDiskStopped();
    void onUpgradeToPro();
    void onUploaded(u64 uploaded);
    void onDownloaded(u64 downloaded);

private:
    void broadcast();
    void invite(const QHostAddress &address);
    void createTrayIcon();
    void setTryaIcon();

    void dispatchRequest(QTcpSocket *socket, const DatagramHeader &header, const QByteArray &payload);

    QByteArray readdirHandler(u64 requestId, QByteArray payload);
    QByteArray readHandler(u64 requestId, QByteArray payload);
    QByteArray writeHandler(u64 requestId, QByteArray payload);
    QByteArray readlinkHandler(u64 requestId, QByteArray payload);
    QByteArray statfsHandler(u64 requestId, QByteArray payload);
    QByteArray getattrHandler(u64 requestId, QByteArray payload);
    QByteArray createHandler(u64 requestId, QByteArray payload);
    QByteArray unlinkHandler(u64 requestId, QByteArray payload);
    QByteArray renameHandler(u64 requestId, QByteArray payload);
    QByteArray mkdirHandler(u64 requestId, QByteArray payload);
    QByteArray rmdirHandler(u64 requestId, QByteArray payload);
    QByteArray truncateHandler(u64 requestId, QByteArray payload);

    Ui::MainWindow  *ui = nullptr;
    QAction         *restoreAction;
    QAction         *quitAction;
    QAction         *upgradeToProAction;
    QSystemTrayIcon *trayIcon;
    QMenu           *trayIconMenu;

    QTcpServer     *server = nullptr;
    QUdpSocket     *broadcaster = nullptr;

    QMap<QString, Connection> connections;
    QMap<OperationType, RequestHandler> fuseHandlers;

    QMap<QTcpSocket*, QByteArray> socketBuffers;
    QMap<QString, VirtDisk*> virtDisks;
    FUSEBackend *fuseBackend = nullptr;

    QThreadPool handlerPool;
};
#endif // MAINWINDOW_H
