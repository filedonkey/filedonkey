#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "fusebackend.h"

#include <assert.h>
#include <string.h>

#include <QStringList>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkDatagram>
#include <QHostAddress>
#include <QSysInfo>
#include <QStorageInfo>
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
    fuseHandlers.insert("statfs", std::bind(&MainWindow::statfsHandler, this, _1));
    fuseHandlers.insert("getattr", std::bind(&MainWindow::getattrHandler, this, _1));

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
}

MainWindow::~MainWindow()
{
    delete ui;
    delete server;
    delete broadcaster;
    delete virtDisk;

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
        // qDebug() << "[MainWindow::broadcast] Network interface:" << networkInterface.humanReadableName();
        for (auto addressEntry : networkInterface.addressEntries())
        {
            // qDebug() << "[MainWindow::broadcast] Address:" << addressEntry.broadcast().toString();
            bool isIPv4Protocol = addressEntry.ip().protocol() == QAbstractSocket::IPv4Protocol;
            bool isBroadcastNotEmpty = addressEntry.broadcast().toString() != "";
            if (isIPv4Protocol && isBroadcastNotEmpty)
            {
                QHostAddress host = addressEntry.broadcast();
                quint64 sentSize = broadcaster.writeDatagram(datagram, host, UDP_PORT);
                // qDebug() << "[MainWindow::broadcast] Sent size: " << sentSize;
            }
        }
    }
}

void MainWindow::onBroadcasting()
{
    qDebug() << "[IncomingBroadcasting] Connected";
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

        qDebug() << "[IncomingBroadcasting] machine id: "     << newConn.machineId;
        qDebug() << "[IncomingBroadcasting] machine name: "   << newConn.machineName;
        qDebug() << "[IncomingBroadcasting] machine port: "   << newConn.machinePort;
        qDebug() << "[IncomingBroadcasting] sender address: " << newConn.machineAddress;
        qDebug() << "[IncomingBroadcasting] sender port: "    << netDG.senderPort();

        connections.insert(newConn.machineId, newConn);

        virtDisk = new VirtDisk(newConn);
        virtDisk->mount("M:\\");
    }
}

void MainWindow::onConnection()
{
    qDebug() << "[Server] Connected";
    while (server->hasPendingConnections())
    {
        qDebug() << "[Server] Befor next pending connection";
        QTcpSocket *newConnection = server->nextPendingConnection();
        connect(newConnection, SIGNAL(readyRead()), this, SLOT(onSocketReadyRead()));
    }
}

void MainWindow::onSocketReadyRead()
{
    QTcpSocket *newConnection = (QTcpSocket*)QObject::sender();

    QByteArray incoming = newConnection->readAll();
    DatagramHeader *header;
    DatagramHeader::ReadFrom(&header, incoming.data());

    assert(strcmp(header->messageType, "request") == 0);
    assert(header->protocolVersion == 1);

    QByteArray response;

    if (strcmp(header->virtDiskType, "fuse") != 0)
    {
        qDebug() << "[onSocketReadyRead] Error: invalid virt disk type:" << header->virtDiskType;
        return;
    }

    QString operationName(header->operationName);

    if (!fuseHandlers.contains(operationName))
    {
        qDebug() << "[onSocketReadyRead] Error: invalid operation name:" << header->operationName;
        return;
    }

    RequestHandler handler = fuseHandlers[operationName];
    QByteArray payload = incoming.sliced(sizeof(DatagramHeader));
    response = handler(payload);

    // QStorageInfo storage = QStorageInfo::root();
    // storage.bytesTotal();
    // qDebug() << "[Server] incoming freeBytesAvailable: " << storage.bytesAvailable();
    // qDebug() << "[Server] incoming totalNumberOfBytes: " << storage.bytesTotal();
    // qDebug() << "[Server] incoming totalNumberOfFreeBytes: " << storage.bytesFree();

    newConnection->write(response);
}

QByteArray MainWindow::readdirHandler(QByteArray payload)
{
    const char *path = payload.data();
    qDebug() << "[onSocketReadyRead] fuse readdir path:" << path;
    Ref<ReaddirResult> result = FUSEBackend::FD_readdir(path);
    qDebug() << "[onSocketReadyRead] result status:" << result->status;

    DatagramHeader header("response", "fuse", "readdir");
    header.datagramSize += sizeof(ReaddirResult) + result->dataSize;

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(ReaddirResult));
    response.append((char *)result->findData, result->dataSize);

    return response;
}

QByteArray MainWindow::readHandler(QByteArray payload)
{
    u64 size = *(payload.data());
    i64 offset = *(payload.sliced(sizeof(u64)).data());
    QByteArray path = payload.sliced(sizeof(u64) + sizeof(i64));
    qDebug() << "[onSocketReadyRead] incoming size:" << size;
    qDebug() << "[onSocketReadyRead] incoming offset:" << offset;
    qDebug() << "[onSocketReadyRead] incoming path:" << path.data();
    Ref<ReadResult> result = FUSEBackend::FD_read(path.data(), size, offset);
    qDebug() << "[onSocketReadyRead] result status:" << result->status;

    DatagramHeader header("response", "fuse", "read");
    header.datagramSize += sizeof(ReadResult) + result->size;

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(ReadResult));
    response.append((char *)result->data, result->size);

    return response;
}

QByteArray MainWindow::statfsHandler(QByteArray payload)
{
    const char *path = payload.data();
    qDebug() << "[onSocketReadyRead] fuse statfs path:" << path;
    Ref<StatfsResult> result = FUSEBackend::FD_statfs(path);
    qDebug() << "[onSocketReadyRead] result status:" << result->status;

    DatagramHeader header("response", "fuse", "statfs");
    header.datagramSize += sizeof(StatfsResult);

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(StatfsResult));

    return response;
}

QByteArray MainWindow::getattrHandler(QByteArray payload)
{
    const char *path = payload.data();
    qDebug() << "[onSocketReadyRead] fuse getattr path:" << path;
    Ref<GetattrResult> result = FUSEBackend::FD_getattr(path);
    qDebug() << "[onSocketReadyRead] result status:" << result->status;

    DatagramHeader header("response", "fuse", "getattr");
    header.datagramSize += sizeof(GetattrResult);

    QByteArray response((char *)&header, sizeof(DatagramHeader));
    response.append((char *)result.get(), sizeof(GetattrResult));

    return response;
}

void MainWindow::onUpgradeToPro()
{
    QString link = "https://filedonkey.app";
    QDesktopServices::openUrl(QUrl(link));
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
    qDebug() << "[MainWindow::changeEvent] lightness:" << bg.lightness();

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
