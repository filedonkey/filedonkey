#include "localnode.h"

#include <string.h>

#include <QByteArray>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAddressEntry>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QPointer>
#include <QSysInfo>

#define UDP_PORT    4545
#define TCP_PORT    5454

#define BROADCAST_INTERVAL_MS 5000

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

} // namespace

LocalNode::LocalNode(QObject *parent)
    : QObject(parent)
    , server(new QTcpServer(this))
    , broadcaster(new QUdpSocket(this))
{
    machineId = QString::fromUtf8(QSysInfo::machineUniqueId());

    fuseBackend = new FUSEBackend();
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
    if (!server->listen(QHostAddress::Any, TCP_PORT))
    {
        qDebug() << "[Server] Unable to start: " << server->errorString();
    }
    else
    {
        qDebug() << "[Server] Started on port: " << server->serverPort();
    }

    // Bind before announcing ourselves, never after: a peer answers our broadcast with an invite
    // straight away, and until this socket owns UDP_PORT that reply lands on a port nobody is
    // listening on and is dropped.
    connect(broadcaster, SIGNAL(readyRead()), this, SLOT(onBroadcasting()));
    broadcaster->bind(UDP_PORT, QUdpSocket::ShareAddress);

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

void LocalNode::broadcast()
{
    QUdpSocket broadcaster;
    QJsonObject root;
    QJsonObject machine;

    machine["id"]   = machineId;
    machine["name"] = QSysInfo::machineHostName();
    machine["port"] = server->serverPort();

    // Carried for the sake of the peer's device list, which badges each row with the platform it
    // found. Nothing in the protocol turns on it, so a peer that does not send it is not a peer we
    // refuse to talk to.
    machine["os"]   = QSysInfo::productType();

    root["machine"] = machine;

    QByteArray datagram = QJsonDocument(root).toJson(QJsonDocument::Compact);

    for (auto networkInterface : QNetworkInterface::allInterfaces())
    {
        for (auto addressEntry : networkInterface.addressEntries())
        {
            QHostAddress host = addressEntry.broadcast();
            bool isIPv4Protocol = addressEntry.ip().protocol() == QAbstractSocket::IPv4Protocol;
            bool isHostAddressValid = host.toString().isEmpty() == false;
            if (isIPv4Protocol && isHostAddressValid)
            {
                broadcaster.writeDatagram(datagram, host, UDP_PORT);
            }
        }
    }
}

void LocalNode::invite(const QHostAddress &address)
{
    QUdpSocket broadcaster;
    QJsonObject root;
    QJsonObject machine;

    machine["id"]   = machineId;
    machine["name"] = QSysInfo::machineHostName();
    machine["port"] = server->serverPort();
    machine["os"]   = QSysInfo::productType();

    root["machine"] = machine;

    QByteArray datagram = QJsonDocument(root).toJson(QJsonDocument::Compact);
    broadcaster.writeDatagram(datagram, address, UDP_PORT);
}

void LocalNode::onBroadcasting()
{
    while (broadcaster->hasPendingDatagrams())
    {
        QNetworkDatagram netDG = broadcaster->receiveDatagram();
        QByteArray datagram = netDG.data();
        QHostAddress senderAddress = netDG.senderAddress();

        QJsonDocument doc = QJsonDocument::fromJson(datagram);
        QJsonObject machine = doc["machine"].toObject();

        Connection newConn = {
            .machineId      = machine["id"].toString(),
            .machineName    = machine["name"].toString(),
            .machineAddress = addressText(senderAddress),
            .machinePort    = machine["port"].toInteger(),
            .machineOs      = machine["os"].toString(),
        };

        // Our own announcements come straight back to us - we are bound to the port we broadcast
        // to, and ShareAddress lets them through - and without this we mount ourselves, handing
        // our own exported home directory back to us on a drive letter. Ordering the bind after
        // the first broadcast used to hide this; it stops working the moment we broadcast again.
        if (newConn.machineId == machineId) continue;

        // continue, not return: a peer we already know is the common case now that we re-broadcast,
        // and returning here would abandon every datagram still queued behind this one - including
        // the invite of a peer we have never seen.
        if (connections.contains(newConn.machineId)) continue;

        qDebug() << "[LocalNode::onBroadcasting] machine id: "     << newConn.machineId;
        qDebug() << "[LocalNode::onBroadcasting] machine name: "   << newConn.machineName;
        qDebug() << "[LocalNode::onBroadcasting] machine port: "   << newConn.machinePort;
        qDebug() << "[LocalNode::onBroadcasting] sender address: " << newConn.machineAddress;
        qDebug() << "[LocalNode::onBroadcasting] sender port: "    << netDG.senderPort();

        connections.insert(newConn.machineId, newConn);

        VirtDisk *virtDisk = new VirtDisk(newConn);
        virtDisks.insert(newConn.machineId, virtDisk);
        connect(virtDisk, &VirtDisk::stopped, this, &LocalNode::onVirtDiskStopped);

        // VirtDisk reports the mount point and nothing else about who it belongs to - it serves one
        // peer and has never needed to say which. Carry the id in the lambda rather than looking it
        // up in virtDisks afterwards, so this still names the right peer if the map has moved on.
        connect(virtDisk, &VirtDisk::mounted, this, [this, id = newConn.machineId](const QString &mountPoint) {
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
        connect(virtDisk->client, &FUSEClient::uploadedChanged, this, [this, id = newConn.machineId](u64 uploaded) {
            transfers[id].clientUploaded = uploaded;
            reportTransfer(id);
        });

        connect(virtDisk->client, &FUSEClient::downloadedChanged, this, [this, id = newConn.machineId](u64 downloaded) {
            transfers[id].clientDownloaded = downloaded;
            reportTransfer(id);
        });
        virtDisk->mount("M:\\");

        emit peerAdded(newConn);

        invite(senderAddress);
    }
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

        if (!fuseHandlers.contains(header.operationType))
        {
            qDebug() << "[LocalNode::onSocketReadyRead] Error: invalid operation type:"
                     << ToString(header.operationType);
            return;
        }

        const qsizetype payloadSize = header.datagramSize - sizeof(DatagramHeader);
        QByteArray payload = incoming.sliced(sizeof(DatagramHeader), payloadSize);

        dispatchRequest(newConnection, header, payload);

        incoming.remove(0, header.datagramSize);
    }
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
        }
        break;
    }

    served.remove(socket);
    socket->deleteLater();
}

void LocalNode::onVirtDiskStopped()
{
    VirtDisk *virtDisk = qobject_cast<VirtDisk *>(QObject::sender());
    if (!virtDisk) return;

    QString peerId = virtDisks.key(virtDisk);

    qDebug() << "[onVirtDiskStopped] virtdisk stopped for machine:" << peerId;

    virtDisks.remove(peerId);

    // Forget the peer too, otherwise onBroadcasting's contains() check would refuse to mount it
    // again when it comes back.
    connections.remove(peerId);

    transfers.remove(peerId);

    // The socket this peer dialled in on is usually gone by now - its drop is what brought us here -
    // but it need not be: a mount can fail on its own. Unbind it so its bytes do not land on the
    // fresh total the next mount of this machine starts from.
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
