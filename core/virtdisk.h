#ifndef VIRTDISK_H
#define VIRTDISK_H

#include "connection.h"
#include "fuseclient.h"
#include "timer.h"

#include <QString>

#include <thread>

class VirtDisk : public QObject
{
    Q_OBJECT

public:
    VirtDisk(const Connection& conn);
    ~VirtDisk();

    void mount(const QString &mountPoint);

    FUSEClient *client;

    struct fuse *f;
    std::string mountpoint;
    Timer timer;

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
