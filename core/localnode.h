#ifndef LOCALNODE_H
#define LOCALNODE_H

#include "connection.h"
#include "fusebackend.h"
#include "virtdisk.h"

#include <functional>

#include <QHostAddress>
#include <QMap>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThreadPool>
#include <QTimer>
#include <QUdpSocket>

using RequestHandler = std::function<QByteArray(u64, QByteArray)>;

// This machine's whole stake in the FileDonkey network, and the three jobs that involves:
// announcing ourselves over UDP and listening for the peers that answer, serving our exported
// directory to the ones that dial in, and mounting a VirtDisk for each one we find.
//
// Nothing here touches a widget. What the UI needs to show, it gets through the signals below.
class LocalNode : public QObject
{
    Q_OBJECT

public:
    explicit LocalNode(QObject *parent = nullptr);
    ~LocalNode();

signals:
    // Forwarded from the FUSEClient of whichever VirtDisk moved the bytes. The counters are
    // per-peer and currently land in one pair of labels; a per-peer view would want peerAdded's
    // machineId alongside them.
    void uploadedChanged(u64 uploaded);
    void downloadedChanged(u64 downloaded);

    // Emitted as peers come and go, for a device list to build on. Nothing consumes them yet -
    // they are here because a headless node with no way to report who it found is not much use
    // to a UI.
    void peerAdded(const Connection &conn);
    void peerRemoved(const QString &machineId);

public slots:
    void onBroadcasting();
    void onConnection();
    void onSocketReadyRead();
    void onSocketDisconnected();
    void onVirtDiskStopped();

private:
    void broadcast();
    void invite(const QHostAddress &address);

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

    QTcpServer *server = nullptr;
    QUdpSocket *broadcaster = nullptr;
    QTimer     *discoveryTimer = nullptr;

    QString machineId;      // our own, so we can tell our announcements from a peer's
    QMap<QString, Connection> connections;
    QMap<OperationType, RequestHandler> fuseHandlers;

    QMap<QTcpSocket*, QByteArray> socketBuffers;
    QMap<QString, VirtDisk*> virtDisks;
    FUSEBackend *fuseBackend = nullptr;

    QThreadPool handlerPool;
};

#endif // LOCALNODE_H
