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

using namespace std::placeholders;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , server(new QTcpServer(this))
    , broadcaster(new QUdpSocket(this))
{
    ui->setupUi(this);

    restoreAction = new QAction(tr("&Restore"), this);
    connect(restoreAction, &QAction::triggered, this, &QWidget::showNormal);

    quitAction = new QAction(tr("&Quit"), this);
    connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    upgradeToProAction = new QAction(tr("&Upgrade to Pro"), this);
    connect(upgradeToProAction, &QAction::triggered, this, &MainWindow::onUpgradeToPro);

    createTrayIcon();

    fuseBackend = new FUSEBackend();

    //------------------------------------------------------------------------------------
    // For local testing
    //------------------------------------------------------------------------------------
    // Connection conn = {
    //     .machineId      = "test_machine_id",
    //     .machineName    = "test_machine_name",
    //     .machineAddress = "test_machine_address",
    //     .machinePort    = 0,
    //     .socket         = nullptr,
    // };
    // virtDisk = new VirtDisk(conn);
    // virtDisk->mount("M:\\");

    // return;
    //------------------------------------------------------------------------------------

    fuseHandlers.insert("readdir", std::bind(&MainWindow::readdirHandler, this, _1));
    fuseHandlers.insert("read", std::bind(&MainWindow::readHandler, this, _1));
    fuseHandlers.insert("write", std::bind(&MainWindow::writeHandler, this, _1));
    fuseHandlers.insert("readlink", std::bind(&MainWindow::readlinkHandler, this, _1));
    fuseHandlers.insert("statfs", std::bind(&MainWindow::statfsHandler, this, _1));
    fuseHandlers.insert("getattr", std::bind(&MainWindow::getattrHandler, this, _1));
    fuseHandlers.insert("create", std::bind(&MainWindow::createHandler, this, _1));

    connect(server, SIGNAL(newConnection()), this, SLOT(onConnection()));
    if (!server->listen(QHostAddress::Any, TCP_PORT))
    {
        qDebug() << "[Server] Unable to start: " << server->errorString();
    }
    else
    {
        qDebug() << "[Server] Started on port: " << server->serverPort();
    }

    broadcast();

    connect(broadcaster, SIGNAL(readyRead()), this, SLOT(onBroadcasting()));
    broadcaster->bind(UDP_PORT, QUdpSocket::ShareAddress);

    ui->statusbar->addWidget(ui->networkWidget);
}

MainWindow::~MainWindow()
{
    delete ui;
    delete server;
    delete broadcaster;
    delete virtDisk;
    delete fuseBackend;

    for (auto& conn : connections)
    {
        if (!conn.socket) continue;
        conn.socket->close();
        delete conn.socket;
    }
    connections.clear();
}

void MainWindow::broadcast()
{
    QUdpSocket broadcaster;
    QJsonObject root;
    QJsonObject machine;

    machine["id"]   = QSysInfo::machineUniqueId().constData();
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

void MainWindow::onBroadcasting()
{
    qDebug() << "[MainWindow::onBroadcasting] Connected";
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
            .socket         = nullptr,
        };

        if (connections.contains(newConn.machineId)) return;

        qDebug() << "[MainWindow::onBroadcasting] machine id: "     << newConn.machineId;
        qDebug() << "[MainWindow::onBroadcasting] machine name: "   << newConn.machineName;
        qDebug() << "[MainWindow::onBroadcasting] machine port: "   << newConn.machinePort;
        qDebug() << "[MainWindow::onBroadcasting] sender address: " << newConn.machineAddress;
        qDebug() << "[MainWindow::onBroadcasting] sender port: "    << netDG.senderPort();

        connections.insert(newConn.machineId, newConn);

        virtDisk = new VirtDisk(newConn);
        connect(virtDisk->client, SIGNAL(uploadedChanged(u64)), this, SLOT(onUploaded(u64)));
        connect(virtDisk->client, SIGNAL(downloadedChanged(u64)), this, SLOT(onDownloaded(u64)));
        virtDisk->mount("M:\\");
    }
}

void MainWindow::onConnection()
{
    qDebug() << "[MainWindow::onConnection] Connected";
    while (server->hasPendingConnections())
    {
        qDebug() << "[MainWindow::onConnection] Befor next pending connection";
        QTcpSocket *newConnection = server->nextPendingConnection();
        connect(newConnection, SIGNAL(readyRead()), this, SLOT(onSocketReadyRead()));
        connect(newConnection, SIGNAL(disconnected()), this, SLOT(onSocketDisconnected()));
    }
}

void MainWindow::onSocketReadyRead()
{
    QTcpSocket *newConnection = (QTcpSocket*)QObject::sender();

    SocketState &state = socketStates[newConnection];
    state.buffer.append(newConnection->readAll());
    DatagramHeader *header = &state.header;
    QByteArray &incoming = state.buffer;

    if (!state.headerParsed)
    {
        if ((u64)incoming.size() < sizeof(DatagramHeader))
            return;

        memcpy(&state.header, state.buffer.constData(), sizeof(DatagramHeader));
        // DatagramHeader::ReadFrom(&header, incoming.data());
        state.headerParsed = true;
    }

    assert(strcmp(header->messageType, "request") == 0);
    assert(header->protocolVersion == 1);

    if (incoming.size() < header->datagramSize)
        return;

    if (strcmp(header->virtDiskType, "fuse") != 0)
    {
        qDebug() << "[MainWindow::onSocketReadyRead] Error: invalid virt disk type:" << header->virtDiskType;
        return;
    }

    QString operationName(header->operationName);

    if (!fuseHandlers.contains(operationName))
    {
        qDebug() << "[MainWindow::onSocketReadyRead] Error: invalid operation name:" << header->operationName;
        return;
    }

    RequestHandler handler = fuseHandlers[operationName];
    QByteArray payload = incoming.sliced(sizeof(DatagramHeader));
    QByteArray response = handler(payload);

    newConnection->write(response);

    incoming.remove(0, header->datagramSize);
    state.headerParsed = false;
}

void MainWindow::onSocketDisconnected()
{
    QTcpSocket *socket = (QTcpSocket*)QObject::sender();
    if (!socket) return;

    qDebug() << "[onSocketDisconnected] disconnect socket:" << (u64)socket;

    socketStates.remove(socket);
    socket->deleteLater();
}

QByteArray MainWindow::readdirHandler(QByteArray payload)
{
    const char *path = payload.data();
    qDebug() << "[MainWindow::readdirHandler] fuse readdir path:" << path;
    Ref<ReaddirResult> result = fuseBackend->FD_readdir(path);
    qDebug() << "[MainWindow::readdirHandler] result status:" << result->status;

    DatagramHeader header("response", "fuse", "readdir");
    header.datagramSize += sizeof(ReaddirResult) + result->dataSize;

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(ReaddirResult));
    response.append((char *)result->findData, result->dataSize);

    return response;
}

QByteArray MainWindow::readHandler(QByteArray payload)
{
    u64 size = *(u64 *)(payload.data());
    i64 offset = *(i64 *)(payload.sliced(sizeof(u64)).data());
    QByteArray path = payload.sliced(sizeof(u64) + sizeof(i64));
    qDebug() << "[MainWindow::readHandler] incoming size:" << size;
    qDebug() << "[MainWindow::readHandler] incoming offset:" << offset;
    qDebug() << "[MainWindow::readHandler] incoming path:" << path.data();
    Ref<ReadResult> result = fuseBackend->FD_read(path.data(), size, offset);
    qDebug() << "[MainWindow::readHandler] result status:" << result->status;

    DatagramHeader header("response", "fuse", "read");
    header.datagramSize += sizeof(ReadResult) + result->size;

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(ReadResult));
    response.append((char *)result->data, result->size);

    return response;
}

QByteArray MainWindow::writeHandler(QByteArray payload)
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

    Ref<WriteResult> result = fuseBackend->FD_write(path.data(), buf.data(), buf.length() /* size */, offset);
    qDebug() << "[MainWindow::writeHandler] result status:" << result->status;

    DatagramHeader header("response", "fuse", "write");
    header.datagramSize += sizeof(WriteResult);

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(WriteResult));

    return response;
}

QByteArray MainWindow::readlinkHandler(QByteArray payload)
{
    u64 size = *(u64 *)(payload.data());
    QByteArray path = payload.sliced(sizeof(u64));
    qDebug() << "[MainWindow::readlinkHandler] incoming size:" << size;
    qDebug() << "[MainWindow::readlinkHandler] incoming path:" << path.data();
    Ref<ReadlinkResult> result = fuseBackend->FD_readlink(path.data(), size);
    qDebug() << "[MainWindow::readlinkHandler] result status:" << result->status;

    DatagramHeader header("response", "fuse", "readlink");
    header.datagramSize += sizeof(ReadlinkResult) + result->size;

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(ReadlinkResult));
    response.append((char *)result->data, result->size);

    return response;
}

QByteArray MainWindow::statfsHandler(QByteArray payload)
{
    const char *path = payload.data();
    qDebug() << "[MainWindow::statfsHandler] fuse statfs path:" << path;
    Ref<StatfsResult> result = fuseBackend->FD_statfs(path);
    qDebug() << "[MainWindow::statfsHandler] result status:" << result->status;

    DatagramHeader header("response", "fuse", "statfs");
    header.datagramSize += sizeof(StatfsResult);

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(StatfsResult));

    return response;
}

QByteArray MainWindow::getattrHandler(QByteArray payload)
{
    const char *path = payload.data();
    qDebug() << "[MainWindow::getattrHandler] fuse getattr path:" << path;
    Ref<GetattrResult> result = fuseBackend->FD_getattr(path);
    qDebug() << "[MainWindow::getattrHandler] result status:" << result->status;

    DatagramHeader header("response", "fuse", "getattr");
    header.datagramSize += sizeof(GetattrResult);

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(GetattrResult));

    return response;
}

QByteArray MainWindow::createHandler(QByteArray payload)
{
    u32 mode = *(u32 *)(payload.data());
    i32 flags = *(i32 *)(payload.sliced(sizeof(u32)).data());
    QByteArray path = payload.sliced(sizeof(u32) + sizeof(i32));
    qDebug() << "[MainWindow::createHandler] incoming mode:" << mode;
    qDebug() << "[MainWindow::createHandler] incoming flags:" << flags;
    qDebug() << "[MainWindow::createHandler] incoming path:" << path.data();
    Ref<CreateResult> result = fuseBackend->FD_create(path.data(), mode, flags);
    qDebug() << "[MainWindow::createHandler] result status:" << result->status;

    DatagramHeader header("response", "fuse", "create");
    header.datagramSize += sizeof(CreateResult);

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(CreateResult));

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
