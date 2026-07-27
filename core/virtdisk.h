#ifndef VIRTDISK_H
#define VIRTDISK_H

#include "connection.h"
#include "fuseclient.h"

#include <QString>
#include <thread>

// #include <fuse.h>

class VirtDisk : public QObject
{
    Q_OBJECT

public:
    VirtDisk(const Connection& conn);
    ~VirtDisk();

    void mount(const QString &mountPoint);

    FUSEClient *client;

    struct fuse *f;
    const char *mountpoint = "M:";

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
