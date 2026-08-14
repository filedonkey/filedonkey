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

    // Where a peer reaches this machine, written the way its device row writes ours: address then
    // port, and empty while no interface carries one. For the status bar - nothing in the protocol
    // asks us, every peer learns it from the datagrams we send.
    QString localEndpoint() const;

    // What this machine calls itself in every announcement it sends, and the settings page's way of
    // changing it. Stored in QSettings, with the machine's own host name as the answer while the
    // user has set nothing - which is what every announcement carried before there was a field to
    // set it in.
    //
    // Static and read from the settings on each call rather than kept in a member: the page that
    // writes it is built before the node exists, and re-reading is what lets the next broadcast
    // carry a name the user has just typed with no signal wired between the two. A peer that has
    // already found us keeps the name it was given, though - it writes a machine down once, when it
    // first hears from it, so a rename only reaches it when the connection is made again.
    static QString machineName();

    // Empty puts it back to following the host name. Whitespace at either end is dropped: it cannot
    // be seen in the field it was typed in and would be a second name in every peer's list.
    static void setMachineName(const QString &name);

    // What each of these answers while the user has chosen nothing. Asked by the settings page,
    // which offers to put a field back to its default and has to know when it is already there -
    // and which should not be the second place in this project that writes 5454 down.
    static QString defaultMachineName();
    static int defaultTransferPort();

    // The two ports this machine uses: the UDP port announcements are broadcast to and answered on,
    // and the TCP port the server serving our exported directory listens on. Both stored the same
    // way the name is, with the numbers this app has always used as the answer while nothing is
    // stored, and both read once in the constructor - a port changed while the application runs
    // reaches nothing until it is started again, because the sockets are bound by then.
    static int discoveryPort();
    static int transferPort();

    // Only the transfer port can be set, and only it has a field on the settings page. A peer is
    // told which port to dial - every announcement carries it - so one machine can move it and the
    // rest follow. The discovery port is the opposite: it is the port a broadcast is sent to as
    // well as the one it is heard on, so a machine that moves it alone is announcing itself into a
    // port no one is listening on and hearing nothing back, with no sign of it beyond an empty
    // device list. It stays readable from the settings for the rare network that has to move it on
    // every machine at once, and is offered nowhere.
    //
    // Out of range, or the number this app defaults to, and nothing is kept - the same as never
    // having set one.
    static void setTransferPort(int port);

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

    // The discovery port this session bound, kept because it is also the port every announcement is
    // sent to and the two must be the same one. Read from the settings once, in the constructor:
    // reading it again at each broadcast would have a change made mid-session announcing us on a
    // port nothing is listening on, ours included. The transfer port needs no twin - the server
    // knows what it bound, and serverPort() is what the announcements carry.
    quint16 udpPort = 0;

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
