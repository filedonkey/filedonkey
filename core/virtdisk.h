#ifndef VIRTDISK_H
#define VIRTDISK_H

#include "connection.h"
#include "fuseclient.h"

#include <QString>
#include <QTcpSocket>

#include <thread>

class VirtDisk : public QObject
{
    Q_OBJECT

public:
    VirtDisk(const Connection& conn);
    ~VirtDisk();

    void mount(const QString &mountPoint);

    // Asks the fuse loop to exit and returns immediately. Unlike unmount() this never joins the
    // thread, so it is safe to call from the GUI thread while the fuse thread sits in a Fetch
    // that has not timed out yet. Wait for stopped() before destroying the VirtDisk.
    void stop();

    FUSEClient *client;

    QTcpSocket *socket = nullptr;

    struct fuse *f = nullptr;
    std::string mountpoint;

signals:
    // Emitted from the fuse thread once fuse_loop has returned and the mount is torn down.
    void stopped();

public slots:
    void onSocketDisconnected();
    void onSocketStateChanged(QAbstractSocket::SocketState socketState);

private:
    void unmount();

    QString mountPoint;
    Connection conn;
    std::thread thread;
};

#endif // VIRTDISK_H
