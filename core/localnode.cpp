#include "localnode.h"

#include <string.h>

#include <QByteArray>
#include <QDateTime>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAddressEntry>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QPointer>
#include <QSettings>
#include <QSysInfo>

// What both ports are unless the settings page says otherwise, and what they have always been.
#define UDP_PORT    4545
#define TCP_PORT    5454

// The lowest port worth offering. Below this are the ports the system hands out for its own
// services, and on the two platforms that enforce it they cannot be bound without being root.
#define LOWEST_PORT 1024
#define HIGHEST_PORT 65535

// Where what the user changed is kept. Each is absent until they change it, and absent again the
// moment they put it back - see the accessors for what that then answers. Two of the three are the
// settings page's; the discovery port is written by nothing here and is read for whoever sets it by
// hand - see the note on the accessors in localnode.h.
#define MACHINE_NAME_KEY    "network/machineName"
#define DISCOVERY_PORT_KEY  "network/discoveryPort"
#define TRANSFER_PORT_KEY   "network/transferPort"

// Not under network/ with the three above: what this machine shares is its own business, and the
// network settings are about how it is reached.
#define SHARED_ROOT_KEY     "sharing/root"

#define BROADCAST_INTERVAL_MS 5000

// How long a peer whose mount failed may go unheard of before its row is taken down. Three
// announcements' worth: one missed datagram is ordinary on a busy wireless network, and a row
// that vanished on one would be worse than one that lingers for ten seconds longer.
#define FAILED_PEER_TIMEOUT_MS (BROADCAST_INTERVAL_MS * 3)

// How long a manual connect waits for the machine at the typed address to introduce itself before
// giving up on it. It covers the whole attempt, the TCP connect included, because from the user's
// side there is one question - is anything there - and a wrong digit in an address is answered by
// silence rather than by a refusal: nothing replies, and the platform's own connect timeout is
// measured in tens of seconds. Long enough for a machine that is there and slow, short enough that
// a mistyped address does not look like the dialog has hung.
#define MANUAL_CONNECT_TIMEOUT_MS 5000

// The most a hello may be. It is one small JSON object naming a machine; anything beyond this is
// not a FileDonkey on the other end, and the number is here so that a socket answering with a
// length of several gigabytes is dropped rather than believed.
#define MAX_HELLO_SIZE 4096

using namespace std::placeholders;

namespace {

// How a peer's address is written down, without the ::ffff: an IPv4 peer arrives wearing. Both our
// sockets are bound to QHostAddress::Any, which is dual-stack, and a packet from an IPv4 machine
// reaching a dual-stack socket is reported as the IPv4-mapped IPv6 address that stands for it - so
// the row drew ::ffff:192.168.1.5 where the user expects the four numbers on their router.
//
// toIPv4Address() is that mapping run backwards: it answers for a mapped address as readily as for
// a native IPv4 one and says which it had through ok. A peer that really is on IPv6 fails it and
// keeps the address it came with.
QString addressText(const QHostAddress &address)
{
    bool isIPv4 = false;
    const quint32 ipv4 = address.toIPv4Address(&isIPv4);

    return isIPv4 ? QHostAddress(ipv4).toString() : address.toString();
}

// Which of this machine's addresses a packet leaving it would carry, according to the routing
// table. Connecting a UDP socket sends nothing - it only fixes the peer - but that is enough to
// make the OS choose an interface by its routes and bind the socket to that interface's address.
//
// Asked because enumerating interfaces cannot answer it. A machine with VirtualBox or WSL on it
// has host-only adapters carrying private addresses of their own, indistinguishable from the real
// one by anything QNetworkInterface reports - same family, same flags, same type, and often listed
// first, so the first-match answer was VirtualBox's 192.168.56.1 on a machine reachable at
// 192.168.50.253. The default route is what tells the two apart.
//
// The address dialled is never contacted and nothing about it matters beyond being routable and
// off this network. Null when there is no default route at all - a LAN with no way out, which is
// a network this app is expressly meant to work on, so the caller falls back rather than gives up.
QHostAddress routedAddress()
{
    QUdpSocket probe;
    probe.connectToHost(QHostAddress("8.8.8.8"), 53);

    return probe.localAddress();
}

// A port out of the settings, and the default in place of anything that cannot be one. The range is
// checked on the way out as well as on the way in: what is stored was written by a version of this
// app that may have checked something else, or by a hand editing the registry.
int storedPort(const QString &key, int fallback)
{
    const int port = QSettings().value(key, fallback).toInt();
    if (port < LOWEST_PORT || port > HIGHEST_PORT) return fallback;

    return port;
}

// A peer read off the "machine" object of an announcement, whichever way it reached us. The address
// is not in that object and never has been: a machine cannot reliably say where it is - it may have
// several addresses and only one of them is the one we can see it on - so it is taken from the
// packet or the socket it arrived over instead.
Connection connectionFrom(const QJsonObject &machine, const QHostAddress &address)
{
    return Connection {
        .machineId      = machine["id"].toString(),
        .machineName    = machine["name"].toString(),
        .machineAddress = addressText(address),
        .machinePort    = machine["port"].toInteger(),
        .machineOs      = machine["os"].toString(),
    };
}

void storePort(const QString &key, int port, int fallback)
{
    QSettings settings;

    // Nothing kept for the default itself, so that a port this app changes its mind about later is
    // followed by everyone who never chose one of their own.
    if (port == fallback || port < LOWEST_PORT || port > HIGHEST_PORT)
    {
        settings.remove(key);
        return;
    }

    settings.setValue(key, port);
}

} // namespace

LocalNode::LocalNode(QObject *parent)
    : QObject(parent)
    , server(new QTcpServer(this))
    , broadcaster(new QUdpSocket(this))
{
    machineId = QString::fromUtf8(QSysInfo::machineUniqueId());

    // Both ports read here and nowhere else, so that everything this session does is done on the
    // pair it actually bound. A change made in the settings page while it runs is picked up by the
    // next start - see the note on the accessors, and the one under the two fields.
    udpPort = discoveryPort();

    // The folder every request this node serves is answered under, read here for the same reason
    // the ports are: it is fixed for the life of the backend behind it.
    fuseBackend = new FUSEBackend(sharedRoot().toStdString());
    qDebug() << "[LocalNode] sharing:" << sharedRoot();

    fuseHandlers.insert(OperationType::readdir,  std::bind(&LocalNode::readdirHandler,  this, _1, _2));
    fuseHandlers.insert(OperationType::read,     std::bind(&LocalNode::readHandler,     this, _1, _2));
    fuseHandlers.insert(OperationType::write,    std::bind(&LocalNode::writeHandler,    this, _1, _2));
    fuseHandlers.insert(OperationType::readlink, std::bind(&LocalNode::readlinkHandler, this, _1, _2));
    fuseHandlers.insert(OperationType::statfs,   std::bind(&LocalNode::statfsHandler,   this, _1, _2));
    fuseHandlers.insert(OperationType::getattr,  std::bind(&LocalNode::getattrHandler,  this, _1, _2));
    fuseHandlers.insert(OperationType::create,   std::bind(&LocalNode::createHandler,   this, _1, _2));
    fuseHandlers.insert(OperationType::unlink,   std::bind(&LocalNode::unlinkHandler,   this, _1, _2));
    fuseHandlers.insert(OperationType::rename,   std::bind(&LocalNode::renameHandler,   this, _1, _2));
    fuseHandlers.insert(OperationType::mkdir,    std::bind(&LocalNode::mkdirHandler,    this, _1, _2));
    fuseHandlers.insert(OperationType::rmdir,    std::bind(&LocalNode::rmdirHandler,    this, _1, _2));
    fuseHandlers.insert(OperationType::truncate, std::bind(&LocalNode::truncateHandler, this, _1, _2));

    connect(server, SIGNAL(newConnection()), this, SLOT(onConnection()));
    if (!server->listen(QHostAddress::Any, transferPort()))
    {
        qDebug() << "[Server] Unable to start: " << server->errorString();
    }
    else
    {
        qDebug() << "[Server] Started on port: " << server->serverPort();
    }

    // Bind before announcing ourselves, never after: a peer answers our broadcast with an invite
    // straight away, and until this socket owns the discovery port that reply lands on a port
    // nobody is listening on and is dropped.
    connect(broadcaster, SIGNAL(readyRead()), this, SLOT(onBroadcasting()));
    broadcaster->bind(udpPort, QUdpSocket::ShareAddress);

    // Keep announcing ourselves rather than doing it once at startup. A peer that is still tearing
    // down its side of our previous session - exactly what it is doing when this app has just been
    // restarted - still has us in its connections and ignores the first broadcast, and nothing
    // makes it announce itself again either, so that peer would stay invisible for the rest of the
    // session. Repeating also gives a mount that failed a chance to come back.
    discoveryTimer = new QTimer(this);
    connect(discoveryTimer, &QTimer::timeout, this, &LocalNode::broadcast);
    discoveryTimer->start(BROADCAST_INTERVAL_MS);

    broadcast();
}

LocalNode::~LocalNode()
{
    handlerPool.clear();
    handlerPool.waitForDone();

    delete server;
    delete broadcaster;

    // Signal them all first so they wind down in parallel; qDeleteAll then joins each in turn.
    //
    // Cut ourselves out of what they emit on the way past, both the VirtDisk and the client behind
    // it. Deleting a VirtDisk runs its unmount(), which waits for the mount helper to exit, and
    // that wait pumps the helper's pipes: its stdout arrives as onWorkerOutput() and reaches
    // onUploaded(), which we would forward to a UI that may already be half torn down, and its
    // finished() reaches onVirtDiskStopped(), which removes entries from the very map qDeleteAll
    // is walking - the next step lands on a freed node. Neither has anything to do here:
    // everything is going, not just the one peer that stopped.
    for (VirtDisk *virtDisk : std::as_const(virtDisks))
    {
        disconnect(virtDisk, nullptr, this, nullptr);
        disconnect(virtDisk->client, nullptr, this, nullptr);
        virtDisk->stop();
    }

    qDeleteAll(virtDisks);
    virtDisks.clear();

    delete fuseBackend;

    connections.clear();
}

// The routing table's answer, and only if it has none, a guess: the first IPv4 address on an
// interface with a broadcast address to send to - the same test broadcast() applies, so whatever
// comes back is at least an address we really do announce ourselves on. It is a guess because
// with no default route there is nothing left to rank the interfaces by, and the one listed first
// need not be the one a peer can see us on.
//
// Read whenever it is asked for rather than kept: the answer changes when the machine changes
// network, and there is no signal to hang a cached copy off.
QString LocalNode::localAddress()
{
    const QHostAddress routed = routedAddress();
    if (!routed.isNull()) return routed.toString();

    for (const QNetworkInterface &networkInterface : QNetworkInterface::allInterfaces())
    {
        const QNetworkInterface::InterfaceFlags flags = networkInterface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp))      continue;
        if (!flags.testFlag(QNetworkInterface::IsRunning)) continue;

        for (const QNetworkAddressEntry &addressEntry : networkInterface.addressEntries())
        {
            const bool isIPv4 = addressEntry.ip().protocol() == QAbstractSocket::IPv4Protocol;
            if (!isIPv4 || addressEntry.broadcast().isNull()) continue;

            return addressEntry.ip().toString();
        }
    }

    return QString();
}

// The address above and the port this session actually bound. Split in two because the dialog that
// fills itself with our address has no node to ask for a port and no use for one.
QString LocalNode::localEndpoint() const
{
    const QString address = localAddress();
    if (address.isEmpty()) return QString();

    return QString("%1:%2").arg(address).arg(server->serverPort());
}

QString LocalNode::defaultMachineName()
{
    return QSysInfo::machineHostName();
}

int LocalNode::defaultTransferPort()
{
    return TCP_PORT;
}

QString LocalNode::defaultSharedRoot()
{
    return QString::fromStdString(FUSEBackend::defualtPublicDir());
}

QString LocalNode::sharedRoot()
{
    const QString path = QSettings().value(SHARED_ROOT_KEY).toString();

    return path.isEmpty() ? defaultSharedRoot() : path;
}

void LocalNode::setSharedRoot(const QString &path)
{
    QSettings settings;

    // Nothing kept for the folder this app would have picked anyway - the same rule the name and the
    // ports are stored by, and it is what keeps a machine following the default if that default ever
    // changes.
    if (path.isEmpty() || path == defaultSharedRoot())
    {
        settings.remove(SHARED_ROOT_KEY);
        return;
    }

    settings.setValue(SHARED_ROOT_KEY, path);
}

QString LocalNode::machineName()
{
    const QString name = QSettings().value(MACHINE_NAME_KEY).toString().trimmed();

    return name.isEmpty() ? defaultMachineName() : name;
}

void LocalNode::setMachineName(const QString &name)
{
    QSettings settings;

    // Nothing stored for either of the two names that mean "whatever this machine calls itself":
    // an empty one, and the host name the field is filled with while the user has chosen nothing.
    // Storing the second would announce the same name today and the wrong one the day the machine
    // is renamed - and the field is committed whenever the settings tab is left, so a user who
    // never touched it would be the one it happened to.
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == defaultMachineName())
    {
        settings.remove(MACHINE_NAME_KEY);
        return;
    }

    settings.setValue(MACHINE_NAME_KEY, trimmed);
}

int LocalNode::discoveryPort()
{
    return storedPort(DISCOVERY_PORT_KEY, UDP_PORT);
}

int LocalNode::transferPort()
{
    return storedPort(TRANSFER_PORT_KEY, TCP_PORT);
}

void LocalNode::setTransferPort(int port)
{
    storePort(TRANSFER_PORT_KEY, port, TCP_PORT);
}

QByteArray LocalNode::machineDatagram() const
{
    QJsonObject root;
    QJsonObject machine;

    machine["id"]   = machineId;
    machine["name"] = machineName();
    machine["port"] = server->serverPort();

    // Carried for the sake of the peer's device list, which badges each row with the platform it
    // found. Nothing in the protocol turns on it, so a peer that does not send it is not a peer we
    // refuse to talk to.
    machine["os"]   = QSysInfo::productType();

    root["machine"] = machine;

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

void LocalNode::broadcast()
{
    sweepFailedPeers();

    QUdpSocket broadcaster;
    QByteArray datagram = machineDatagram();

    for (auto networkInterface : QNetworkInterface::allInterfaces())
    {
        for (auto addressEntry : networkInterface.addressEntries())
        {
            QHostAddress host = addressEntry.broadcast();
            bool isIPv4Protocol = addressEntry.ip().protocol() == QAbstractSocket::IPv4Protocol;
            bool isHostAddressValid = host.toString().isEmpty() == false;
            if (isIPv4Protocol && isHostAddressValid)
            {
                broadcaster.writeDatagram(datagram, host, udpPort);
            }
        }
    }
}

// Takes down the rows of failed peers that are no longer there. A peer whose mount failed is kept
// so that its row can say why and offer another go, and nothing else would ever remove it: it has
// no VirtDisk left to stop.
//
// The backstop, not the usual way. A peer whose own mount of us was up announces its going by that
// socket dropping, and onSocketDisconnected() takes the row down there and then. This is for the
// peers that never had one - the ones that could not mount us either, or never got as far as
// dialling - where nothing arrives to be noticed and all there is to go on is silence.
//
// Which is judged on two counts, because either alone is wrong. A peer that is still announcing
// itself is plainly still running, whatever went wrong with our mount of it. So is one whose own
// mount of us is up: on a network that drops broadcasts - the one the manual connect exists for -
// that socket is the only sign of it there will ever be.
void LocalNode::sweepFailedPeers()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    for (auto it = failedPeers.begin(); it != failedPeers.end(); )
    {
        const QString peerId = it.key();

        if (now - it.value() < FAILED_PEER_TIMEOUT_MS || isPeerServed(peerId))
        {
            ++it;
            continue;
        }

        qDebug() << "[sweepFailedPeers] failed peer has gone quiet, dropping:" << peerId;

        it = failedPeers.erase(it);

        connections.remove(peerId);
        transfers.remove(peerId);

        emit peerRemoved(peerId);
    }
}

// Whether this peer's own mount of us is still up, which is to say whether the socket it dialled
// our server on is still connected. Matched on address, the way servedPeerId() and
// onSocketDisconnected() do it - a peer dials in on a socket of its own and the address is all
// that connection has in common with the one it announced itself on.
bool LocalNode::isPeerServed(const QString &machineId) const
{
    const auto conn = connections.constFind(machineId);
    if (conn == connections.constEnd()) return false;

    const QHostAddress peerAddress(conn->machineAddress);

    for (auto it = served.constBegin(); it != served.constEnd(); ++it)
    {
        if (it.key()->state() != QAbstractSocket::ConnectedState) continue;
        if (QHostAddress(it.key()->peerAddress()).isEqual(peerAddress)) return true;
    }

    return false;
}

void LocalNode::invite(const QHostAddress &address)
{
    QUdpSocket broadcaster;
    broadcaster.writeDatagram(machineDatagram(), address, udpPort);
}

void LocalNode::onBroadcasting()
{
    while (broadcaster->hasPendingDatagrams())
    {
        QNetworkDatagram netDG = broadcaster->receiveDatagram();
        QByteArray datagram = netDG.data();
        QHostAddress senderAddress = netDG.senderAddress();

        QJsonDocument doc = QJsonDocument::fromJson(datagram);
        const Connection newConn = connectionFrom(doc["machine"].toObject(), senderAddress);

        // Our own announcements come straight back to us - we are bound to the port we broadcast
        // to, and ShareAddress lets them through - and without this we mount ourselves, handing
        // our own exported home directory back to us on a drive letter. Ordering the bind after
        // the first broadcast used to hide this; it stops working the moment we broadcast again.
        //
        // continue, not return, for both of these: a peer we already know is the common case now
        // that we re-broadcast, and returning here would abandon every datagram still queued behind
        // this one - including the invite of a peer we have never seen. addPeer() tests the same two
        // things again for the sake of the callers that reach it another way; they are here as well
        // because they decide whether this datagram is worth another word about.
        if (newConn.machineId == machineId) continue;

        // Still there, whatever went wrong with our mount of it. Noted before the check below
        // sends this datagram away, since a peer we already have is exactly what a failed one is.
        const auto failed = failedPeers.find(newConn.machineId);
        if (failed != failedPeers.end()) *failed = QDateTime::currentMSecsSinceEpoch();

        if (connections.contains(newConn.machineId)) continue;

        qDebug() << "[LocalNode::onBroadcasting] machine id: "     << newConn.machineId;
        qDebug() << "[LocalNode::onBroadcasting] machine name: "   << newConn.machineName;
        qDebug() << "[LocalNode::onBroadcasting] machine port: "   << newConn.machinePort;
        qDebug() << "[LocalNode::onBroadcasting] sender address: " << newConn.machineAddress;
        qDebug() << "[LocalNode::onBroadcasting] sender port: "    << netDG.senderPort();

        if (!addPeer(newConn)) continue;

        invite(senderAddress);
    }
}

// Shared by the two ways a peer can reach us: a UDP announcement, and a TCP hello from a machine
// somebody typed the address of. Everything past knowing who the peer is and where it lives is the
// same either way, and was this function's body inside onBroadcasting() before there was a second
// caller for it.
bool LocalNode::addPeer(const Connection &conn)
{
    // Nothing in the "machine" object we could work with - not a FileDonkey on the other end, or
    // one that answered with something we cannot read.
    if (conn.machineId.isEmpty()) return false;

    // Ourselves. Over UDP that is our own broadcast coming back; over TCP it is a user typing this
    // machine's own address into the manual connect dialog. Both would mount our exported folder
    // onto our own drive letter.
    if (conn.machineId == machineId) return false;

    if (connections.contains(conn.machineId)) return false;

    connections.insert(conn.machineId, conn);

    startMount(conn);

    emit peerAdded(conn);

    return true;
}

void LocalNode::startMount(const Connection &conn)
{
    VirtDisk *virtDisk = new VirtDisk(conn);
    virtDisks.insert(conn.machineId, virtDisk);
    connect(virtDisk, &VirtDisk::stopped, this, &LocalNode::onVirtDiskStopped);

    // VirtDisk reports the mount point and nothing else about who it belongs to - it serves one
    // peer and has never needed to say which. Carry the id in the lambda rather than looking it
    // up in virtDisks afterwards, so this still names the right peer if the map has moved on.
    connect(virtDisk, &VirtDisk::mounted, this, [this, id = conn.machineId](const QString &mountPoint) {
        emit peerMounted(id, mountPoint);
    });

    // Half of what this peer's row shows - what our own mount of it has moved. The other half is
    // counted by our server, on the socket this peer dialled in on, and the two are added
    // together in reportTransfer().
    //
    // By value rather than by reading the client back: these are emitted from the fuse thread
    // and delivered on this one, so the number has to travel with the signal. The function
    // pointer form matters too - the string form named the argument "u64", which is no type Qt
    // knows, and a queued delivery of it is dropped at the boundary with a warning.
    connect(virtDisk->client, &FUSEClient::uploadedChanged, this, [this, id = conn.machineId](u64 uploaded) {
        transfers[id].clientUploaded = uploaded;
        reportTransfer(id);
    });

    connect(virtDisk->client, &FUSEClient::downloadedChanged, this, [this, id = conn.machineId](u64 downloaded) {
        transfers[id].clientDownloaded = downloaded;
        reportTransfer(id);
    });

    virtDisk->mount("M:\\");
}

// Another go at a peer whose mount failed. Everything needed for it is still here: onVirtDiskStopped
// keeps such a peer in connections rather than forgetting it, which is both what stops the next
// broadcast retrying by itself and what leaves this with an address to dial.
void LocalNode::retryMount(const QString &machineId)
{
    // Mounted, or still coming up. Only a peer with no VirtDisk of its own is in the state this is
    // for - see the note on peerMountFailed.
    if (virtDisks.contains(machineId)) return;

    const auto conn = connections.constFind(machineId);
    if (conn == connections.constEnd()) return;

    qDebug() << "[LocalNode::retryMount] mounting again:" << conn->machineName;

    // Out of the failed set before the mount starts, or a sweep landing between here and the next
    // announcement would take down the row of a peer that is busy mounting. It goes back in if
    // this attempt fails too.
    failedPeers.remove(machineId);

    startMount(*conn);
}

void LocalNode::connectManually(const QString &address, int port)
{
    const QHostAddress host(address);
    if (host.isNull())
    {
        emit manualConnectFailed(address, tr("%1 is not an address.").arg(address));
        return;
    }

    QTcpSocket *socket = new QTcpSocket(this);

    // Covers the whole attempt rather than the connect alone - see MANUAL_CONNECT_TIMEOUT_MS. A
    // child of the socket, so it goes when the socket does and there is one thing to clean up.
    QTimer *timer = new QTimer(socket);
    timer->setSingleShot(true);

    // The one way out, whichever of the four ends the attempt: an answer, a refusal, a timeout, or
    // an address that turns out to be this machine. Cutting our own connections first is what stops
    // the ones that have not fired yet from reporting a second thing about the same attempt - the
    // timeout in particular is still armed when a refusal arrives.
    auto finish = [this, socket, timer]() {
        timer->stop();
        disconnect(socket, nullptr, this, nullptr);
        disconnect(timer,  nullptr, this, nullptr);

        // Not abort(): this is also the path a finished handshake leaves by, and close() lets what
        // is already written go out before the socket is taken down.
        socket->close();
        socket->deleteLater();
    };

    auto fail = [this, address, finish](const QString &reason) {
        finish();
        emit manualConnectFailed(address, reason);
    };

    connect(timer, &QTimer::timeout, this, [fail]() {
        fail(tr("Nothing answered. Check the address, and that FileDonkey is running there."));
    });

    // Our own announcement, over TCP and addressed to one machine - the same object a broadcast
    // carries, so the peer learns exactly what it would have learned from one. It answers with its
    // own, which is what we are waiting for below, and takes us on from ours: neither side has to
    // be the one that started this for both to end up mounted.
    connect(socket, &QTcpSocket::connected, this, [this, socket]() {
        const QByteArray body = machineDatagram();

        DatagramHeader header(MessageType::Request, OperationType::hello);
        header.datagramSize += body.size();

        socket->write(QByteArray((const char *)&header, sizeof(DatagramHeader)));
        socket->write(body);
    });

    connect(socket, &QTcpSocket::errorOccurred, this, [socket, fail](QAbstractSocket::SocketError) {
        fail(socket->errorString());
    });

    // Peeked rather than buffered: a hello is answered with one small datagram and nothing follows
    // it, so the socket's own read buffer is the only buffer this needs - we look at the header
    // where it lies and take nothing out until the whole of it has arrived.
    connect(socket, &QTcpSocket::readyRead, this, [this, socket, address, finish, fail]() {
        if (socket->bytesAvailable() < (qint64)sizeof(DatagramHeader)) return;

        DatagramHeader header;
        socket->peek((char *)&header, sizeof(DatagramHeader));

        if (header.datagramSize < sizeof(DatagramHeader) || header.datagramSize > MAX_HELLO_SIZE)
        {
            fail(tr("The machine at %1 is not answering as FileDonkey.").arg(address));
            return;
        }

        if (socket->bytesAvailable() < (qint64)header.datagramSize) return;

        const QByteArray datagram = socket->read(header.datagramSize);
        const QByteArray payload  = datagram.sliced(sizeof(DatagramHeader));

        const QJsonDocument doc = QJsonDocument::fromJson(payload);
        const Connection conn = connectionFrom(doc["machine"].toObject(), socket->peerAddress());

        // Before the mount, and before anything else can arrive on it: the handshake is over, and
        // the peer knows to expect this socket to go - see ServedPeer::handshake. What we mount it
        // over is the VirtDisk's own socket, dialled by addPeer() below.
        finish();

        if (conn.machineId == machineId)
        {
            emit manualConnectFailed(address, tr("%1 is this PC.").arg(address));
            return;
        }

        if (conn.machineId.isEmpty())
        {
            emit manualConnectFailed(address, tr("The machine at %1 is not answering as FileDonkey.").arg(address));
            return;
        }

        qDebug() << "[LocalNode::connectManually] machine id: "   << conn.machineId;
        qDebug() << "[LocalNode::connectManually] machine name: " << conn.machineName;
        qDebug() << "[LocalNode::connectManually] machine port: " << conn.machinePort;
        qDebug() << "[LocalNode::connectManually] address: "      << conn.machineAddress;

        // False here is a machine already in the list, which is not a failure and needs no word
        // said about it: the row the user was after is on screen, put there by whichever route
        // found it first.
        addPeer(conn);
    });

    timer->start(MANUAL_CONNECT_TIMEOUT_MS);
    socket->connectToHost(host, port);
}

void LocalNode::handleHello(QTcpSocket *socket, const DatagramHeader &header, const QByteArray &payload)
{
    // Answered before we do anything with what it said, so the machine waiting on the other end is
    // not held up by a mount coming up on this one.
    const QByteArray body = machineDatagram();

    DatagramHeader reply(MessageType::Response, OperationType::hello, header.requestId);
    reply.datagramSize += body.size();

    socket->write(QByteArray((const char *)&reply, sizeof(DatagramHeader)));
    socket->write(body);
    socket->flush();

    // Not counted through countServed(), unlike every other byte this server writes. These crossed
    // before there was a peer to put them against, and the socket they crossed on is about to go -
    // see below - taking the tally held on it with them. A few hundred bytes, once.
    served[socket].handshake = true;

    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    const Connection conn = connectionFrom(doc["machine"].toObject(), socket->peerAddress());

    qDebug() << "[LocalNode::handleHello] machine id: "   << conn.machineId;
    qDebug() << "[LocalNode::handleHello] machine name: " << conn.machineName;
    qDebug() << "[LocalNode::handleHello] machine port: " << conn.machinePort;
    qDebug() << "[LocalNode::handleHello] address: "      << conn.machineAddress;

    addPeer(conn);
}

void LocalNode::onConnection()
{
    qDebug() << "[LocalNode::onConnection] Connected";
    while (server->hasPendingConnections())
    {
        qDebug() << "[LocalNode::onConnection] Befor next pending connection";
        QTcpSocket *newConnection = server->nextPendingConnection();

        newConnection->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
        newConnection->setSocketOption(QAbstractSocket::LowDelayOption,  1);

        connect(newConnection, SIGNAL(readyRead()), this, SLOT(onSocketReadyRead()));
        connect(newConnection, SIGNAL(disconnected()), this, SLOT(onSocketDisconnected()));
    }
}

void LocalNode::onSocketReadyRead()
{
    QTcpSocket *newConnection = (QTcpSocket*)QObject::sender();
    QByteArray data = newConnection->readAll();

    QByteArray &incoming = served[newConnection].incoming;
    incoming.append(data);

    // Counted off the wire, before any of it has been understood: whatever these bytes turn out to
    // say, they are bytes this peer has sent us.
    countServed(newConnection, 0, data.size());

    while (true)
    {
        if ((u64)incoming.size() < sizeof(DatagramHeader))
            return;

        DatagramHeader header;
        memcpy(&header, incoming.constData(), sizeof(DatagramHeader));

        constexpr u64 MaxDatagramSize = 64 * MiB;
        if (header.datagramSize < sizeof(DatagramHeader) || header.datagramSize > MaxDatagramSize)
        {
            qDebug() << "[LocalNode::onSocketReadyRead] Error: invalid datagram size:" << header.datagramSize;
            return;
        }

        if (header.messageType != MessageType::Request)
        {
            qDebug() << "[LocalNode::onSocketReadyRead] Error: Invalid message type:"
                     << ToString(header.messageType);
            return;
        }

        if (header.protocolVersion != 1)
        {
            qDebug() << "[LocalNode::onSocketReadyRead] Error: Unsupported protocol version:"
                     << header.protocolVersion;
            return;
        }

        if ((u64)incoming.size() < header.datagramSize)
            return;

        // hello is the one operation with no entry in that map, and it is checked for first so the
        // test below still means what it says. It is answered here, on this thread, rather than
        // handed to the pool: it takes on a peer - see handleHello() - which touches the maps every
        // slot in this file reads, and it needs the socket itself to know where the peer is.
        const bool isHello = (header.operationType == OperationType::hello);

        // And bye, which is answered here for the same reason and is even less work: it changes
        // one flag on this socket and says nothing back. See handleBye().
        const bool isBye = (header.operationType == OperationType::bye);

        if (!isHello && !isBye && !fuseHandlers.contains(header.operationType))
        {
            qDebug() << "[LocalNode::onSocketReadyRead] Error: invalid operation type:"
                     << ToString(header.operationType);
            return;
        }

        const qsizetype payloadSize = header.datagramSize - sizeof(DatagramHeader);
        QByteArray payload = incoming.sliced(sizeof(DatagramHeader), payloadSize);

        // Taken out of the buffer before either is run, not after: handleHello() below reaches back
        // into served[] for this same socket, and leaving the datagram in place while it does would
        // have this loop's next pass read it a second time.
        incoming.remove(0, header.datagramSize);

        if      (isHello) handleHello(newConnection, header, payload);
        else if (isBye)   handleBye(newConnection);
        else              dispatchRequest(newConnection, header, payload);
    }
}

// The peer is closing this socket and staying where it is - its mount of us failed, or it is about
// to try again. Nothing to answer: it is not waiting on us, and by the time this is read it has
// written everything it means to write.
//
// All it leaves behind is the flag, which onSocketDisconnected() reads a moment later. Our mount of
// this peer has nothing to do with the socket it dialled us on, and that is exactly what this says.
void LocalNode::handleBye(QTcpSocket *socket)
{
    qDebug() << "[LocalNode::handleBye] peer is closing its socket and staying:"
             << socket->peerAddress().toString();

    served[socket].graceful = true;
}

void LocalNode::dispatchRequest(QTcpSocket *socket, const DatagramHeader &header, const QByteArray &payload)
{
    RequestHandler handler = fuseHandlers[header.operationType];
    u64 requestId = header.requestId;

    QPointer<QTcpSocket> guard(socket);

    handlerPool.start([this, guard, handler, requestId, payload]() {
        QByteArray response = handler(requestId, payload);

        QMetaObject::invokeMethod(this, [this, guard, response]() {
            if (!guard) return;

            guard->write(response);
            guard->flush();

            // The other direction over the same socket, and the half the peer sees as its download.
            countServed(guard, response.size(), 0);
        }, Qt::QueuedConnection);
    });
}

QString LocalNode::servedPeerId(QTcpSocket *socket)
{
    auto it = served.find(socket);
    if (it == served.end()) return QString();
    if (!it->machineId.isEmpty()) return it->machineId;

    // A peer's requests can beat its announcement to us: it dials in the moment it hears our
    // broadcast, and the invite naming it comes back over UDP separately. Until that lands there is
    // nothing to match on, and countServed holds the bytes on the socket rather than dropping them.
    const QHostAddress peerAddress = socket->peerAddress();

    for (auto conn = connections.constBegin(); conn != connections.constEnd(); ++conn)
    {
        if (!QHostAddress(conn->machineAddress).isEqual(peerAddress)) continue;

        it->machineId = conn.key();

        // Whatever crossed while it was still a stranger belongs to it after all.
        Transfer &transfer = transfers[it->machineId];
        transfer.serverUploaded   += it->uploaded;
        transfer.serverDownloaded += it->downloaded;

        it->uploaded   = 0;
        it->downloaded = 0;

        return it->machineId;
    }

    return QString();
}

void LocalNode::countServed(QTcpSocket *socket, u64 uploaded, u64 downloaded)
{
    auto it = served.find(socket);
    if (it == served.end()) return;

    const QString machineId = servedPeerId(socket);
    if (machineId.isEmpty())
    {
        it->uploaded   += uploaded;
        it->downloaded += downloaded;
        return;
    }

    Transfer &transfer = transfers[machineId];
    transfer.serverUploaded   += uploaded;
    transfer.serverDownloaded += downloaded;

    reportTransfer(machineId);
}

void LocalNode::reportTransfer(const QString &machineId)
{
    const Transfer transfer = transfers.value(machineId);

    emit peerUploaded(machineId, transfer.clientUploaded + transfer.serverUploaded);
    emit peerDownloaded(machineId, transfer.clientDownloaded + transfer.serverDownloaded);
}

void LocalNode::onSocketDisconnected()
{
    QTcpSocket *socket = (QTcpSocket*)QObject::sender();
    if (!socket) {
        qDebug() << "[onSocketDisconnected] socket ptr is null" << (u64)socket;
        return;
    }

    qDebug() << "[onSocketDisconnected] disconnect socket:" << (u64)socket;

    // A socket that carried nothing but a hello is not this peer's client connection and says
    // nothing about the peer by going: the machine that dialled in with one drops it the moment it
    // has our answer, and its VirtDisk comes back on a socket of its own. Without this that FIN
    // would be read below as the peer leaving, and would stop the mount handleHello() had just
    // started - every manual connect would undo itself a moment after it worked.
    //
    // Neither does one the peer told us it was closing. That is a peer whose own mount of us could
    // not be brought up, and it is still running and still serving: our mount of it is fine and
    // must be left alone - see OperationType::bye.
    const auto closing = served.constFind(socket);
    if (closing != served.constEnd() && (closing->handshake || closing->graceful))
    {
        qDebug() << "[onSocketDisconnected] socket closed on purpose, peer kept";

        served.remove(socket);
        socket->deleteLater();
        return;
    }

    // This socket is the peer's client talking to our server. Its drop is noticed straight away
    // because it lives on this thread's event loop. Our own VirtDisk talks to that peer over a
    // separate socket owned by the fuse thread, which has no event loop, so nothing there
    // notices until a Fetch times out. Hand the news over rather than waiting those 5 seconds.
    QHostAddress peerAddress = socket->peerAddress();

    for (auto it = connections.constBegin(); it != connections.constEnd(); ++it)
    {
        if (!QHostAddress(it->machineAddress).isEqual(peerAddress)) continue;

        VirtDisk *virtDisk = virtDisks.value(it.key(), nullptr);
        if (virtDisk)
        {
            qDebug() << "[onSocketDisconnected] stopping virtdisk for:" << it->machineName;
            virtDisk->stop();   // returns at once; onVirtDiskStopped() does the cleanup
            break;
        }

        // No VirtDisk means a peer we kept because our mount of it failed - see
        // onVirtDiskStopped(). Its row is still up offering another go, and this socket, the one
        // its own mount of us ran over, was the last sign it was there.
        //
        // Reaching here at all is what says it has gone. A peer that is merely closing this socket
        // and staying - which is what its mount failing looks like, and what every press of its
        // Retry does - says so first, and that is answered above and never gets this far. So the
        // row goes now rather than waiting for the silence to be noticed: a Retry against a
        // machine that is not listening could only fail, and one that comes back is found again by
        // the next broadcast.
        const QString peerId = it.key();

        qDebug() << "[onSocketDisconnected] failed peer has gone:" << it->machineName;

        connections.remove(peerId);
        failedPeers.remove(peerId);
        transfers.remove(peerId);

        emit peerRemoved(peerId);
        break;
    }

    served.remove(socket);
    socket->deleteLater();
}

void LocalNode::onVirtDiskStopped(const QString &reason)
{
    VirtDisk *virtDisk = qobject_cast<VirtDisk *>(QObject::sender());
    if (!virtDisk) return;

    QString peerId = virtDisks.key(virtDisk);

    qDebug() << "[onVirtDiskStopped] virtdisk stopped for machine:" << peerId
             << "reason:" << (reason.isEmpty() ? QString("teardown") : reason);

    virtDisks.remove(peerId);

    // The mount never came up. Keep the peer exactly where it is: what is left in connections is
    // what has onBroadcasting() ignore its next announcement, which is the whole of the fix for
    // two machines unmounting each other every five seconds forever, and it is what retryMount()
    // reads when the user asks for another go. The transfers stay too - this peer's own mount of
    // us may well be up and moving bytes across the socket it dialled our server on.
    //
    // Nothing here decides the row is now unreachable. It stays in the list saying why, until the
    // user retries or the socket above tells us the machine has gone.
    if (!reason.isEmpty())
    {
        // Stamped now rather than when it was last heard from, so a peer gets the full grace
        // period from the moment its mount failed rather than from whenever the last broadcast
        // happened to land.
        failedPeers.insert(peerId, QDateTime::currentMSecsSinceEpoch());

        emit peerMountFailed(peerId, reason);

        virtDisk->deleteLater();
        return;
    }

    // Forget the peer too, otherwise onBroadcasting's contains() check would refuse to mount it
    // again when it comes back. The failed set is keyed on the same peers connections holds and
    // has to go the same way, or a sweep would find a machine that has already been let go of.
    connections.remove(peerId);
    failedPeers.remove(peerId);

    transfers.remove(peerId);

    // The socket this peer dialled in on is usually gone by now - its drop is what brought us here -
    // but it need not be: the window closing brings a mount down while that socket is still up.
    // Unbind it so its bytes do not land on the fresh total the next mount of this machine starts
    // from.
    for (auto it = served.begin(); it != served.end(); ++it)
    {
        if (it->machineId != peerId) continue;

        it->machineId.clear();
        it->uploaded   = 0;
        it->downloaded = 0;
    }

    emit peerRemoved(peerId);

    virtDisk->deleteLater();
}

QByteArray LocalNode::readdirHandler(u64 requestId, QByteArray payload)
{
    const char *path = payload.data();
    qDebug() << "[LocalNode::readdirHandler] fuse readdir path:" << path;
    Ref<ReaddirResult> result = fuseBackend->FD_readdir(path);
    qDebug() << "[LocalNode::readdirHandler] result status:" << result->status;

    DatagramHeader header(MessageType::Response, OperationType::readdir, requestId);
    header.datagramSize += sizeof(ReaddirResult) + result->dataSize;

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(ReaddirResult));
    response.append((char *)result->findData, result->dataSize);

    return response;
}

QByteArray LocalNode::readHandler(u64 requestId, QByteArray payload)
{
    u64 size = *(u64 *)(payload.data());
    i64 offset = *(i64 *)(payload.sliced(sizeof(u64)).data());
    QByteArray path = payload.sliced(sizeof(u64) + sizeof(i64));
    qDebug() << "[LocalNode::readHandler] incoming size:" << size;
    qDebug() << "[LocalNode::readHandler] incoming offset:" << offset;
    qDebug() << "[LocalNode::readHandler] incoming path:" << path.data();
    Ref<ReadResult> result = fuseBackend->FD_read(path.data(), size, offset);
    qDebug() << "[LocalNode::readHandler] result status:" << result->status;

    DatagramHeader header(MessageType::Response, OperationType::read, requestId);
    header.datagramSize += sizeof(ReadResult) + result->size;

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(ReadResult));
    response.append((char *)result->data, result->size);

    return response;
}

QByteArray LocalNode::writeHandler(u64 requestId, QByteArray payload)
{
    qDebug() << "[LocalNode::writeHandler] incoming payload length:" << payload.length();
    u64 size = *(u64 *)(payload.data());
    qDebug() << "[LocalNode::writeHandler] incoming size:" << size;
    i64 offset = *(i64 *)(payload.sliced(sizeof(u64)).data());
    qDebug() << "[LocalNode::writeHandler] incoming offset:" << offset;
    u64 pathLength = *(u64 *)(payload.sliced(sizeof(u64) + sizeof(i64)).data());
    qDebug() << "[LocalNode::writeHandler] incoming path length:" << pathLength;
    QByteArray path = payload.sliced(sizeof(u64) + sizeof(i64) + sizeof(u64));
    qDebug() << "[LocalNode::writeHandler] incoming path:" << path.data();
    QByteArray buf = payload.sliced(sizeof(u64) + sizeof(i64) + sizeof(u64) + pathLength);
    qDebug() << "[LocalNode::writeHandler] incoming buff length:" << buf.length();

    Ref<StatusResult> result = fuseBackend->FD_write(path.data(), buf.data(), buf.length() /* size */, offset);
    qDebug() << "[LocalNode::writeHandler] result status:" << result->status;

    DatagramHeader header(MessageType::Response, OperationType::write, requestId);
    header.datagramSize += sizeof(StatusResult);

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(StatusResult));

    return response;
}

QByteArray LocalNode::readlinkHandler(u64 requestId, QByteArray payload)
{
    u64 size = *(u64 *)(payload.data());
    QByteArray path = payload.sliced(sizeof(u64));
    qDebug() << "[LocalNode::readlinkHandler] incoming size:" << size;
    qDebug() << "[LocalNode::readlinkHandler] incoming path:" << path.data();
    Ref<ReadlinkResult> result = fuseBackend->FD_readlink(path.data(), size);
    qDebug() << "[LocalNode::readlinkHandler] result status:" << result->status;

    DatagramHeader header(MessageType::Response, OperationType::readlink, requestId);
    header.datagramSize += sizeof(ReadlinkResult) + result->size;

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(ReadlinkResult));
    response.append((char *)result->data, result->size);

    return response;
}

QByteArray LocalNode::statfsHandler(u64 requestId, QByteArray payload)
{
    const char *path = payload.data();
    qDebug() << "[LocalNode::statfsHandler] fuse statfs path:" << path;
    Ref<StatfsResult> result = fuseBackend->FD_statfs(path);
    qDebug() << "[LocalNode::statfsHandler] result status:" << result->status;

    DatagramHeader header(MessageType::Response, OperationType::statfs, requestId);
    header.datagramSize += sizeof(StatfsResult);

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(StatfsResult));

    return response;
}

QByteArray LocalNode::getattrHandler(u64 requestId, QByteArray payload)
{
    const char *path = payload.data();
    qDebug() << "[LocalNode::getattrHandler] fuse getattr path:" << path;
    Ref<GetattrResult> result = fuseBackend->FD_getattr(path);
    qDebug() << "[LocalNode::getattrHandler] result status:" << result->status;

    DatagramHeader header(MessageType::Response, OperationType::getattr, requestId);
    header.datagramSize += sizeof(GetattrResult);

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(GetattrResult));

    return response;
}

QByteArray LocalNode::createHandler(u64 requestId, QByteArray payload)
{
    u32 mode = *(u32 *)(payload.data());
    i32 flags = *(i32 *)(payload.sliced(sizeof(u32)).data());
    QByteArray path = payload.sliced(sizeof(u32) + sizeof(i32));
    qDebug() << "[LocalNode::createHandler] incoming mode:" << mode;
    qDebug() << "[LocalNode::createHandler] incoming flags:" << flags;
    qDebug() << "[LocalNode::createHandler] incoming path:" << path.data();
    Ref<StatusResult> result = fuseBackend->FD_create(path.data(), mode, flags);
    qDebug() << "[LocalNode::createHandler] result status:" << result->status;

    DatagramHeader header(MessageType::Response, OperationType::create, requestId);
    header.datagramSize += sizeof(StatusResult);

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(StatusResult));

    return response;
}

QByteArray LocalNode::unlinkHandler(u64 requestId, QByteArray payload)
{
    const char *path = payload.data();
    qDebug() << "[LocalNode::unlinkHandler] fuse unlink path:" << path;
    Ref<StatusResult> result = fuseBackend->FD_unlink(path);
    qDebug() << "[LocalNode::unlinkHandler] result status:" << result->status;

    DatagramHeader header(MessageType::Response, OperationType::unlink, requestId);
    header.datagramSize += sizeof(StatusResult);

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(StatusResult));

    return response;
}

QByteArray LocalNode::renameHandler(u64 requestId, QByteArray payload)
{
    const char *oldpath = payload.data();
    const char *newpath = payload.data() + strlen(oldpath) + 1;

    qDebug() << "[LocalNode::renameHandler] fuse rename from:" << oldpath << "to:" << newpath;

    Ref<StatusResult> result = fuseBackend->FD_rename(oldpath, newpath);
    qDebug() << "[LocalNode::renameHandler] result status:" << result->status;

    DatagramHeader header(MessageType::Response, OperationType::rename, requestId);
    header.datagramSize += sizeof(StatusResult);

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(StatusResult));

    return response;
}

QByteArray LocalNode::mkdirHandler(u64 requestId, QByteArray payload)
{
    u32 mode = *(u32 *)(payload.data());
    QByteArray path = payload.sliced(sizeof(u32));
    qDebug() << "[LocalNode::mkdirHandler] incoming mode:" << mode;
    qDebug() << "[LocalNode::mkdirHandler] incoming path:" << path.data();
    Ref<StatusResult> result = fuseBackend->FD_mkdir(path.data(), mode);
    qDebug() << "[LocalNode::mkdirHandler] result status:" << result->status;

    DatagramHeader header(MessageType::Response, OperationType::mkdir, requestId);
    header.datagramSize += sizeof(StatusResult);

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(StatusResult));

    return response;
}

QByteArray LocalNode::rmdirHandler(u64 requestId, QByteArray payload)
{
    const char *path = payload.data();
    qDebug() << "[LocalNode::rmdirHandler] fuse rmdir path:" << path;
    Ref<StatusResult> result = fuseBackend->FD_rmdir(path);
    qDebug() << "[LocalNode::rmdirHandler] result status:" << result->status;

    DatagramHeader header(MessageType::Response, OperationType::rmdir, requestId);
    header.datagramSize += sizeof(StatusResult);

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(StatusResult));

    return response;
}

QByteArray LocalNode::truncateHandler(u64 requestId, QByteArray payload)
{
    i64 size = *(i64 *)(payload.data());
    QByteArray path = payload.sliced(sizeof(i64));
    qDebug() << "[LocalNode::truncateHandler] incoming mode:" << size;
    qDebug() << "[LocalNode::truncateHandler] incoming path:" << path.data();
    Ref<StatusResult> result = fuseBackend->FD_truncate(path.data(), size);
    qDebug() << "[LocalNode::truncateHandler] result status:" << result->status;

    DatagramHeader header(MessageType::Response, OperationType::truncate, requestId);
    header.datagramSize += sizeof(StatusResult);

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(StatusResult));

    return response;
}
