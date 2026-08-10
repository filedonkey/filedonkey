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
    // Forwarded from the FUSEClient of whichever VirtDisk moved the bytes, named so the device list
    // can put them on the right row - the counters have always been per-peer, and until there was a
    // list to show them in there was nowhere for the name to go.
    void peerUploaded(const QString &machineId, u64 uploaded);
    void peerDownloaded(const QString &machineId, u64 downloaded);

    // Emitted as peers come and go, and as their mounts come up, for the window's device list to
    // build on. A peer arrives as peerAdded with its mount already started, then either reaches
    // peerMounted or goes straight to peerRemoved if the mount never came up.
    void peerAdded(const Connection &conn);
    void peerMounted(const QString &machineId, const QString &mountPoint);
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

    // The peer behind a socket that dialled in to our server, once its announcement has reached us.
    // Matched on address, the way onSocketDisconnected does it: a peer dials in from a socket of its
    // own and the address is all that connection has in common with the one it announced itself on.
    QString servedPeerId(QTcpSocket *socket);

    // Adds to what our server has moved for one peer, and reports the new totals.
    void countServed(QTcpSocket *socket, u64 uploaded, u64 downloaded);

    // Adds the two sockets' halves together and emits them. Called whenever either half moves.
    void reportTransfer(const QString &machineId);

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

    // One per socket a peer has dialled in to our server on. Holds the datagram that socket is in
    // the middle of sending us, and - until we can put a name to it - what has crossed it.
    struct ServedPeer
    {
        QByteArray incoming;
        QString    machineId;
        u64        uploaded   = 0;
        u64        downloaded = 0;
    };

    // Everything this machine has moved for one peer, over both of the sockets it involves: the one
    // our VirtDisk dialled out on, whose totals FUSEClient reports, and the one that peer dialled in
    // on, which our own server reads and writes. Both halves have to be counted or these numbers
    // cannot agree with the peer's own - what it calls a download is our upload, and each of the two
    // sockets carries only one direction of the pair.
    struct Transfer
    {
        u64 clientUploaded   = 0;
        u64 clientDownloaded = 0;
        u64 serverUploaded   = 0;
        u64 serverDownloaded = 0;
    };

    QMap<QTcpSocket*, ServedPeer> served;
    QMap<QString, Transfer> transfers;
    QMap<QString, VirtDisk*> virtDisks;
    FUSEBackend *fuseBackend = nullptr;

    QThreadPool handlerPool;
};

#endif // LOCALNODE_H
