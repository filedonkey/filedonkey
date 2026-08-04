#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "fusebackend.h"

#include <assert.h>
#include <string.h>

#include <QStringList>
#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkDatagram>
#include <QHostAddress>
#include <QSysInfo>
#include <QDesktopServices>
#include <QNetworkInterface>
#include <QNetworkAddressEntry>

#define THEME_LIGHTNESS_BARRIER 128

#define UDP_PORT    4545
#define TCP_PORT    5454

#define BROADCAST_INTERVAL_MS 5000

using namespace std::placeholders;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , server(new QTcpServer(this))
    , broadcaster(new QUdpSocket(this))
{
    ui->setupUi(this);

    machineId = QString::fromUtf8(QSysInfo::machineUniqueId());

    restoreAction = new QAction(tr("&Restore"), this);
    connect(restoreAction, &QAction::triggered, this, &QWidget::showNormal);

    quitAction = new QAction(tr("&Quit"), this);
    connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    upgradeToProAction = new QAction(tr("&Upgrade to Pro"), this);
    connect(upgradeToProAction, &QAction::triggered, this, &MainWindow::onUpgradeToPro);

    createTrayIcon();

    fuseBackend = new FUSEBackend();
    fuseHandlers.insert(OperationType::readdir,  std::bind(&MainWindow::readdirHandler,  this, _1, _2));
    fuseHandlers.insert(OperationType::read,     std::bind(&MainWindow::readHandler,     this, _1, _2));
    fuseHandlers.insert(OperationType::write,    std::bind(&MainWindow::writeHandler,    this, _1, _2));
    fuseHandlers.insert(OperationType::readlink, std::bind(&MainWindow::readlinkHandler, this, _1, _2));
    fuseHandlers.insert(OperationType::statfs,   std::bind(&MainWindow::statfsHandler,   this, _1, _2));
    fuseHandlers.insert(OperationType::getattr,  std::bind(&MainWindow::getattrHandler,  this, _1, _2));
    fuseHandlers.insert(OperationType::create,   std::bind(&MainWindow::createHandler,   this, _1, _2));
    fuseHandlers.insert(OperationType::unlink,   std::bind(&MainWindow::unlinkHandler,   this, _1, _2));
    fuseHandlers.insert(OperationType::rename,   std::bind(&MainWindow::renameHandler,   this, _1, _2));
    fuseHandlers.insert(OperationType::mkdir,    std::bind(&MainWindow::mkdirHandler,    this, _1, _2));
    fuseHandlers.insert(OperationType::rmdir,    std::bind(&MainWindow::rmdirHandler,    this, _1, _2));
    fuseHandlers.insert(OperationType::truncate, std::bind(&MainWindow::truncateHandler, this, _1, _2));

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
    connect(discoveryTimer, &QTimer::timeout, this, &MainWindow::broadcast);
    discoveryTimer->start(BROADCAST_INTERVAL_MS);

    broadcast();

    ui->statusbar->addWidget(ui->networkWidget);
}

MainWindow::~MainWindow()
{
    handlerPool.clear();
    handlerPool.waitForDone();

    delete ui;
    delete server;
    delete broadcaster;

    // Signal them all first so they wind down in parallel; qDeleteAll then joins each in turn.
    for (VirtDisk *virtDisk : std::as_const(virtDisks)) virtDisk->stop();

    qDeleteAll(virtDisks);
    virtDisks.clear();

    delete fuseBackend;

    connections.clear();
}

void MainWindow::broadcast()
{
    QUdpSocket broadcaster;
    QJsonObject root;
    QJsonObject machine;

    machine["id"]   = machineId;
    machine["name"] = QSysInfo::machineHostName();
    machine["port"] = server->serverPort();

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

void MainWindow::invite(const QHostAddress &address)
{
    QUdpSocket broadcaster;
    QJsonObject root;
    QJsonObject machine;

    machine["id"]   = machineId;
    machine["name"] = QSysInfo::machineHostName();
    machine["port"] = server->serverPort();

    root["machine"] = machine;

    QByteArray datagram = QJsonDocument(root).toJson(QJsonDocument::Compact);
    broadcaster.writeDatagram(datagram, address, UDP_PORT);
}

void MainWindow::onBroadcasting()
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
            .machineAddress = senderAddress.toString(),
            .machinePort    = machine["port"].toInteger(),
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

        qDebug() << "[MainWindow::onBroadcasting] machine id: "     << newConn.machineId;
        qDebug() << "[MainWindow::onBroadcasting] machine name: "   << newConn.machineName;
        qDebug() << "[MainWindow::onBroadcasting] machine port: "   << newConn.machinePort;
        qDebug() << "[MainWindow::onBroadcasting] sender address: " << newConn.machineAddress;
        qDebug() << "[MainWindow::onBroadcasting] sender port: "    << netDG.senderPort();

        connections.insert(newConn.machineId, newConn);

        VirtDisk *virtDisk = new VirtDisk(newConn);
        virtDisks.insert(newConn.machineId, virtDisk);
        connect(virtDisk, &VirtDisk::stopped, this, &MainWindow::onVirtDiskStopped);
        connect(virtDisk->client, SIGNAL(uploadedChanged(u64)), this, SLOT(onUploaded(u64)));
        connect(virtDisk->client, SIGNAL(downloadedChanged(u64)), this, SLOT(onDownloaded(u64)));
        virtDisk->mount("M:\\");

        invite(senderAddress);
    }
}

void MainWindow::onConnection()
{
    qDebug() << "[MainWindow::onConnection] Connected";
    while (server->hasPendingConnections())
    {
        qDebug() << "[MainWindow::onConnection] Befor next pending connection";
        QTcpSocket *newConnection = server->nextPendingConnection();

        newConnection->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
        newConnection->setSocketOption(QAbstractSocket::LowDelayOption,  1);

        connect(newConnection, SIGNAL(readyRead()), this, SLOT(onSocketReadyRead()));
        connect(newConnection, SIGNAL(disconnected()), this, SLOT(onSocketDisconnected()));
    }
}

void MainWindow::onSocketReadyRead()
{
    QTcpSocket *newConnection = (QTcpSocket*)QObject::sender();
    QByteArray data = newConnection->readAll();

    QByteArray &incoming = socketBuffers[newConnection];
    incoming.append(data);

    while (true)
    {
        if ((u64)incoming.size() < sizeof(DatagramHeader))
            return;

        DatagramHeader header;
        memcpy(&header, incoming.constData(), sizeof(DatagramHeader));

        constexpr u64 MaxDatagramSize = 64 * MiB;
        if (header.datagramSize < sizeof(DatagramHeader) || header.datagramSize > MaxDatagramSize)
        {
            qDebug() << "[MainWindow::onSocketReadyRead] Error: invalid datagram size:" << header.datagramSize;
            return;
        }

        if (header.messageType != MessageType::Request)
        {
            qDebug() << "[MainWindow::onSocketReadyRead] Error: Invalid message type:"
                     << ToString(header.messageType);
            return;
        }

        if (header.protocolVersion != 1)
        {
            qDebug() << "[MainWindow::onSocketReadyRead] Error: Unsupported protocol version:"
                     << header.protocolVersion;
            return;
        }

        if ((u64)incoming.size() < header.datagramSize)
            return;

        if (!fuseHandlers.contains(header.operationType))
        {
            qDebug() << "[MainWindow::onSocketReadyRead] Error: invalid operation type:"
                     << ToString(header.operationType);
            return;
        }

        const qsizetype payloadSize = header.datagramSize - sizeof(DatagramHeader);
        QByteArray payload = incoming.sliced(sizeof(DatagramHeader), payloadSize);

        dispatchRequest(newConnection, header, payload);

        incoming.remove(0, header.datagramSize);
    }
}

void MainWindow::dispatchRequest(QTcpSocket *socket, const DatagramHeader &header, const QByteArray &payload)
{
    RequestHandler handler = fuseHandlers[header.operationType];
    u64 requestId = header.requestId;

    QPointer<QTcpSocket> guard(socket);

    handlerPool.start([this, guard, handler, requestId, payload]() {
        QByteArray response = handler(requestId, payload);

        QMetaObject::invokeMethod(this, [guard, response]() {
            if (!guard) return;

            guard->write(response);
            guard->flush();
        }, Qt::QueuedConnection);
    });
}

void MainWindow::onSocketDisconnected()
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

    socketBuffers.remove(socket);
    socket->deleteLater();
}

void MainWindow::onVirtDiskStopped()
{
    VirtDisk *virtDisk = qobject_cast<VirtDisk *>(QObject::sender());
    if (!virtDisk) return;

    QString machineId = virtDisks.key(virtDisk);

    qDebug() << "[onVirtDiskStopped] virtdisk stopped for machine:" << machineId;

    virtDisks.remove(machineId);

    // Forget the peer too, otherwise onBroadcasting's contains() check would refuse to mount it
    // again when it comes back.
    connections.remove(machineId);

    virtDisk->deleteLater();
}

QByteArray MainWindow::readdirHandler(u64 requestId, QByteArray payload)
{
    const char *path = payload.data();
    qDebug() << "[MainWindow::readdirHandler] fuse readdir path:" << path;
    Ref<ReaddirResult> result = fuseBackend->FD_readdir(path);
    qDebug() << "[MainWindow::readdirHandler] result status:" << result->status;

    DatagramHeader header(MessageType::Response, OperationType::readdir, requestId);
    header.datagramSize += sizeof(ReaddirResult) + result->dataSize;

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(ReaddirResult));
    response.append((char *)result->findData, result->dataSize);

    return response;
}

QByteArray MainWindow::readHandler(u64 requestId, QByteArray payload)
{
    u64 size = *(u64 *)(payload.data());
    i64 offset = *(i64 *)(payload.sliced(sizeof(u64)).data());
    QByteArray path = payload.sliced(sizeof(u64) + sizeof(i64));
    qDebug() << "[MainWindow::readHandler] incoming size:" << size;
    qDebug() << "[MainWindow::readHandler] incoming offset:" << offset;
    qDebug() << "[MainWindow::readHandler] incoming path:" << path.data();
    Ref<ReadResult> result = fuseBackend->FD_read(path.data(), size, offset);
    qDebug() << "[MainWindow::readHandler] result status:" << result->status;

    DatagramHeader header(MessageType::Response, OperationType::read, requestId);
    header.datagramSize += sizeof(ReadResult) + result->size;

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(ReadResult));
    response.append((char *)result->data, result->size);

    return response;
}

QByteArray MainWindow::writeHandler(u64 requestId, QByteArray payload)
{
    qDebug() << "[MainWindow::writeHandler] incoming payload length:" << payload.length();
    u64 size = *(u64 *)(payload.data());
    qDebug() << "[MainWindow::writeHandler] incoming size:" << size;
    i64 offset = *(i64 *)(payload.sliced(sizeof(u64)).data());
    qDebug() << "[MainWindow::writeHandler] incoming offset:" << offset;
    u64 pathLength = *(u64 *)(payload.sliced(sizeof(u64) + sizeof(i64)).data());
    qDebug() << "[MainWindow::writeHandler] incoming path length:" << pathLength;
    QByteArray path = payload.sliced(sizeof(u64) + sizeof(i64) + sizeof(u64));
    qDebug() << "[MainWindow::writeHandler] incoming path:" << path.data();
    QByteArray buf = payload.sliced(sizeof(u64) + sizeof(i64) + sizeof(u64) + pathLength);
    qDebug() << "[MainWindow::writeHandler] incoming buff length:" << buf.length();

    Ref<StatusResult> result = fuseBackend->FD_write(path.data(), buf.data(), buf.length() /* size */, offset);
    qDebug() << "[MainWindow::writeHandler] result status:" << result->status;

    DatagramHeader header(MessageType::Response, OperationType::write, requestId);
    header.datagramSize += sizeof(StatusResult);

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(StatusResult));

    return response;
}

QByteArray MainWindow::readlinkHandler(u64 requestId, QByteArray payload)
{
    u64 size = *(u64 *)(payload.data());
    QByteArray path = payload.sliced(sizeof(u64));
    qDebug() << "[MainWindow::readlinkHandler] incoming size:" << size;
    qDebug() << "[MainWindow::readlinkHandler] incoming path:" << path.data();
    Ref<ReadlinkResult> result = fuseBackend->FD_readlink(path.data(), size);
    qDebug() << "[MainWindow::readlinkHandler] result status:" << result->status;

    DatagramHeader header(MessageType::Response, OperationType::readlink, requestId);
    header.datagramSize += sizeof(ReadlinkResult) + result->size;

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(ReadlinkResult));
    response.append((char *)result->data, result->size);

    return response;
}

QByteArray MainWindow::statfsHandler(u64 requestId, QByteArray payload)
{
    const char *path = payload.data();
    qDebug() << "[MainWindow::statfsHandler] fuse statfs path:" << path;
    Ref<StatfsResult> result = fuseBackend->FD_statfs(path);
    qDebug() << "[MainWindow::statfsHandler] result status:" << result->status;

    DatagramHeader header(MessageType::Response, OperationType::statfs, requestId);
    header.datagramSize += sizeof(StatfsResult);

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(StatfsResult));

    return response;
}

QByteArray MainWindow::getattrHandler(u64 requestId, QByteArray payload)
{
    const char *path = payload.data();
    qDebug() << "[MainWindow::getattrHandler] fuse getattr path:" << path;
    Ref<GetattrResult> result = fuseBackend->FD_getattr(path);
    qDebug() << "[MainWindow::getattrHandler] result status:" << result->status;

    DatagramHeader header(MessageType::Response, OperationType::getattr, requestId);
    header.datagramSize += sizeof(GetattrResult);

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(GetattrResult));

    return response;
}

QByteArray MainWindow::createHandler(u64 requestId, QByteArray payload)
{
    u32 mode = *(u32 *)(payload.data());
    i32 flags = *(i32 *)(payload.sliced(sizeof(u32)).data());
    QByteArray path = payload.sliced(sizeof(u32) + sizeof(i32));
    qDebug() << "[MainWindow::createHandler] incoming mode:" << mode;
    qDebug() << "[MainWindow::createHandler] incoming flags:" << flags;
    qDebug() << "[MainWindow::createHandler] incoming path:" << path.data();
    Ref<StatusResult> result = fuseBackend->FD_create(path.data(), mode, flags);
    qDebug() << "[MainWindow::createHandler] result status:" << result->status;

    DatagramHeader header(MessageType::Response, OperationType::create, requestId);
    header.datagramSize += sizeof(StatusResult);

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(StatusResult));

    return response;
}

QByteArray MainWindow::unlinkHandler(u64 requestId, QByteArray payload)
{
    const char *path = payload.data();
    qDebug() << "[MainWindow::unlinkHandler] fuse unlink path:" << path;
    Ref<StatusResult> result = fuseBackend->FD_unlink(path);
    qDebug() << "[MainWindow::unlinkHandler] result status:" << result->status;

    DatagramHeader header(MessageType::Response, OperationType::unlink, requestId);
    header.datagramSize += sizeof(StatusResult);

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(StatusResult));

    return response;
}

QByteArray MainWindow::renameHandler(u64 requestId, QByteArray payload)
{
    const char *oldpath = payload.data();
    const char *newpath = payload.data() + strlen(oldpath) + 1;

    qDebug() << "[MainWindow::renameHandler] fuse rename from:" << oldpath << "to:" << newpath;

    Ref<StatusResult> result = fuseBackend->FD_rename(oldpath, newpath);
    qDebug() << "[MainWindow::renameHandler] result status:" << result->status;

    DatagramHeader header(MessageType::Response, OperationType::rename, requestId);
    header.datagramSize += sizeof(StatusResult);

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(StatusResult));

    return response;
}

QByteArray MainWindow::mkdirHandler(u64 requestId, QByteArray payload)
{
    u32 mode = *(u32 *)(payload.data());
    QByteArray path = payload.sliced(sizeof(u32));
    qDebug() << "[MainWindow::mkdirHandler] incoming mode:" << mode;
    qDebug() << "[MainWindow::mkdirHandler] incoming path:" << path.data();
    Ref<StatusResult> result = fuseBackend->FD_mkdir(path.data(), mode);
    qDebug() << "[MainWindow::mkdirHandler] result status:" << result->status;

    DatagramHeader header(MessageType::Response, OperationType::mkdir, requestId);
    header.datagramSize += sizeof(StatusResult);

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(StatusResult));

    return response;
}

QByteArray MainWindow::rmdirHandler(u64 requestId, QByteArray payload)
{
    const char *path = payload.data();
    qDebug() << "[MainWindow::rmdirHandler] fuse rmdir path:" << path;
    Ref<StatusResult> result = fuseBackend->FD_rmdir(path);
    qDebug() << "[MainWindow::rmdirHandler] result status:" << result->status;

    DatagramHeader header(MessageType::Response, OperationType::rmdir, requestId);
    header.datagramSize += sizeof(StatusResult);

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(StatusResult));

    return response;
}

QByteArray MainWindow::truncateHandler(u64 requestId, QByteArray payload)
{
    i64 size = *(i64 *)(payload.data());
    QByteArray path = payload.sliced(sizeof(i64));
    qDebug() << "[MainWindow::truncateHandler] incoming mode:" << size;
    qDebug() << "[MainWindow::truncateHandler] incoming path:" << path.data();
    Ref<StatusResult> result = fuseBackend->FD_truncate(path.data(), size);
    qDebug() << "[MainWindow::truncateHandler] result status:" << result->status;

    DatagramHeader header(MessageType::Response, OperationType::truncate, requestId);
    header.datagramSize += sizeof(StatusResult);

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(StatusResult));

    return response;
}

void MainWindow::onUpgradeToPro()
{
    QString link = "https://filedonkey.app";
    QDesktopServices::openUrl(QUrl(link));
}

void MainWindow::onUploaded(u64 uploaded)
{
    QLocale locale(QLocale::English, QLocale::UnitedStates);
    this->ui->uploadedLbl->setText(QString("⬆️ %1").arg(locale.formattedDataSize(uploaded)));
}

void MainWindow::onDownloaded(u64 downloaded)
{
    QLocale locale(QLocale::English, QLocale::UnitedStates);
    this->ui->downloadedLbl->setText(QString("⬇️ %1").arg(locale.formattedDataSize(downloaded)));
}

void MainWindow::createTrayIcon()
{
    trayIconMenu = new QMenu(this);
    // trayIconMenu->addAction(minimizeAction);
    // trayIconMenu->addAction(maximizeAction);
    trayIconMenu->addAction(restoreAction);
    trayIconMenu->addAction(upgradeToProAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);

    trayIcon = new QSystemTrayIcon(this);
    setTryaIcon();
    trayIcon->setToolTip("FileDonkey");
    trayIcon->setContextMenu(trayIconMenu);
    trayIcon->show();
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange)
    {
        setTryaIcon();
    }
}

void MainWindow::setTryaIcon()
{
    auto bg = palette().color(QPalette::Active, QPalette::Window);
    qDebug() << "[MainWindow::setTryaIcon] lightness:" << bg.lightness();

    if (bg.lightness() < THEME_LIGHTNESS_BARRIER)
    {
        // QIcon::setThemeName(LIGHT_THEME);
        trayIcon->setIcon(QIcon(":/assets/filedonkey_tray_icon_light.ico"));
    }
    else
    {
        // QIcon::setThemeName(DARK_THEME);
        trayIcon->setIcon(QIcon(":/assets/filedonkey_tray_icon_dark.ico"));
    }
}
