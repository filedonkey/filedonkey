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

    // The address half of it on its own, and empty on the same terms. Asked for by the manual
    // connect dialog, which fills its first three octets with ours: the machine being reached by
    // hand is almost always on this network, and three of the four numbers are then already right.
    //
    // Static because that dialog is built from the device list, which has no node to ask - and it
    // needs nothing of ours to answer, unlike localEndpoint() which also reports the bound port.
    static QString localAddress();

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

    // The directory this machine serves to its peers, and all that any of them can reach: a request
    // naming a path is answered under this one - see normalizePath() in fusebackend.h - so what is
    // outside it does not exist as far as the protocol is concerned.
    //
    // Read once, when the node builds its backend, and handed to it there. A folder chosen while the
    // application runs is served from the next start: the backend answers requests on several
    // threads at once, and the peers already mounted are reading a tree that would move under them.
    //
    // Stored exactly as it was chosen, and not checked against the disk. A drive that is not there
    // this morning makes every request under it fail, which is the right way round - falling back to
    // the default would quietly serve the home directory in place of the folder that went away.
    static QString sharedRoot();
    static void setSharedRoot(const QString &path);

    // What each of these answers while the user has chosen nothing. Asked by the settings page,
    // which offers to put a field back to its default and has to know when it is already there -
    // and which should not be the second place in this project that writes 5454 down.
    static QString defaultMachineName();
    static int defaultTransferPort();
    static QString defaultSharedRoot();

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

    // Takes on a peer that no broadcast found, by dialling the address the user typed and trading
    // announcements over TCP - see OperationType::hello. This is the whole point of the manual
    // route: a network that drops UDP leaves discovery with nothing to work with, while the TCP
    // port every transfer already runs over is by definition open, or the app would be useless on
    // that network anyway.
    //
    // Returns at once. The attempt runs on this thread's event loop and ends in one of three
    // places: peerAdded, manualConnectFailed, or - when that address is already in the list -
    // nothing at all, the device being on screen already.
    void connectManually(const QString &address, int port);

    // Another go at a peer whose mount failed. Nothing tries again on its own - see the note on
    // peerMountFailed - so this is the only way back for such a peer short of restarting the
    // application, and it is what the Retry button on its row is wired to.
    //
    // Does nothing for a machine that is not in that state: one that is mounted or still mounting
    // has a VirtDisk already, and one that has gone away is out of the list altogether.
    void retryMount(const QString &machineId);

signals:
    // Why an address typed into the manual connect dialog came to nothing, in a sentence fit to
    // show - see connectManually(). Only the failures: a peer that answers arrives through
    // peerAdded like any other, and the row appearing is what says it worked.
    void manualConnectFailed(const QString &address, const QString &reason);

    // Forwarded from the FUSEClient of whichever VirtDisk moved the bytes, named so the device list
    // can put them on the right row - the counters have always been per-peer, and until there was a
    // list to show them in there was nowhere for the name to go.
    void peerUploaded(const QString &machineId, u64 uploaded);
    void peerDownloaded(const QString &machineId, u64 downloaded);

    // Emitted as peers come and go, and as their mounts come up, for the window's device list to
    // build on. A peer arrives as peerAdded with its mount already started, and then reaches one
    // of three places: peerMounted, peerMountFailed if the mount could not be brought up, or
    // peerRemoved if it went away before either.
    void peerAdded(const Connection &conn);
    void peerMounted(const QString &machineId, const QString &mountPoint);
    void peerRemoved(const QString &machineId);

    // The mount could not be brought up, with the reason in a sentence fit to show. Unlike
    // peerRemoved this is not the end of the peer: it stays in the list, and stays in connections
    // here, and nothing tries to mount it again until retryMount() is called.
    //
    // That refusal to retry is the point of the signal. Both machines mount each other, and a
    // mount that fails drops the socket the other side reads as us leaving, so it unmounts too;
    // forgetting the peer here - which is what an ordinary teardown does, so it can be found
    // again - had the next broadcast start the whole thing over five seconds later, and every five
    // seconds after that, for as long as the cause of the failure lasted. The peer that could
    // mount now keeps its mount, and this side says why it has none instead of trying forever.
    void peerMountFailed(const QString &machineId, const QString &reason);

public slots:
    void onBroadcasting();
    void onConnection();
    void onSocketReadyRead();
    void onSocketDisconnected();
    void onVirtDiskStopped(const QString &reason);

private:
    void broadcast();

    // Takes down the rows of peers whose mounts failed and which are no longer there to retry
    // against. Run off the broadcast timer, which is the clock it measures against - see
    // FAILED_PEER_TIMEOUT_MS - and needs no timer of its own.
    void sweepFailedPeers();

    // Whether the socket this peer dialled our server on is still up, which is the other way a
    // failed peer can show it is still running when no broadcast reaches us.
    bool isPeerServed(const QString &machineId) const;

    void invite(const QHostAddress &address);

    // What this machine says about itself, as the JSON both routes carry: the two UDP paths above
    // and the TCP handshake below all send this same object, so a field added for one of them
    // cannot go missing on the others.
    QByteArray machineDatagram() const;

    // Everything that follows from learning about a peer, whichever way we learned: write it down,
    // mount it, and say so. False when there was nothing to do - our own announcement come back to
    // us, a machine already in the list, or a reply with no id in it - so a caller can tell a peer
    // it has just taken on from one it already had.
    bool addPeer(const Connection &conn);

    // Builds this peer's VirtDisk, wires what it reports to the signals above, and starts the
    // mount. Split out of addPeer() for retryMount(), which does this and nothing else - the peer
    // is written down already, and the row for it is on screen.
    void startMount(const Connection &conn);

    // A peer has dialled in and introduced itself. Answers with our own announcement and then takes
    // it on, which is what makes the handshake mutual: neither side has to be the one that started
    // it for both to end up mounted.
    void handleHello(QTcpSocket *socket, const DatagramHeader &header, const QByteArray &payload);

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

        // Set when the only thing this socket ever carried was a hello. The machine that dialled in
        // with one drops it as soon as it has our answer and comes back on a socket of its own, so
        // its going away says nothing about the peer - see onSocketDisconnected(), which would
        // otherwise read that FIN as the peer leaving and stop the mount we had just started.
        bool       handshake  = false;
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

    // The peers whose mounts failed, against the last moment each was heard from. A machine is in
    // here exactly while it is in connections with no VirtDisk of its own: kept so its row can say
    // why and offer another go, and not mounted again until the user asks - see peerMountFailed.
    //
    // The time is what lets such a peer be let go of when it stops answering, since nothing else
    // about it will ever change again on its own. See sweepFailedPeers().
    QMap<QString, qint64> failedPeers;
    FUSEBackend *fuseBackend = nullptr;

    QThreadPool handlerPool;
};

#endif // LOCALNODE_H
