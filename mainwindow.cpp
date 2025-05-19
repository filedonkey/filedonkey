#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "fusebackend.h"
// #include <fuse/fuse_win.h>

#include "assert.h"
#include "string.h"

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

#ifdef Q_OS_MACOS
#define LIGHT_MODE 236
#define DARK_MODE  50
#elif defined(Q_OS_WINDOWS)
#define LIGHT_MODE 243
#define DARK_MODE  30
#elif defined(Q_OS_LINUX)
#define LIGHT_MODE 236
#define DARK_MODE  50
#endif

#define MACHINE_NAME    "Leg3nd's Desktop"

#define UDP_PORT    45454

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
    // DatagramHeader header("request", "fuse", "readdir");

    // QByteArray datagram((char *)&header, sizeof(DatagramHeader));

    // ReaddirResult *result = FUSEBackend::FD_readdir("/");

    // struct FindData
    // {
    //     char name[1024];
    //     struct FUSE_STAT stat;
    // };

    // FindData *findData = (FindData *)result->findData;
    // qDebug() << "[ReaddirResult] findData.name" << findData->name << strlen(findData->name);

    // datagram.append((char *)result, sizeof(ReaddirResult));
    // datagram.append((char *)result->findData, result->dataSize);

    // for (unsigned int i = 0; i < (result->dataSize / sizeof(FindData)); ++i)
    // {
    //     FindData *findData = (FindData *)result->findData + i;
    //     qDebug() << "[ReaddirResult]" << i << "findData.name" << findData->name;
    // }

    // qDebug() << "[ReaddirResult] sizeof:" << sizeof(ReaddirResult);
    // qDebug() << "[ReaddirResult] status:" << result->status;
    // qDebug() << "[ReaddirResult] size:" << result->dataSize;
    // qDebug() << "[ReaddirResult] count:" << result->count;

    // delete result;

    // qDebug() << "[DatagramHeader] sizeof:" << sizeof(header);
    // qDebug() << "[Datagram] size:" << datagram.size();

    // qDebug() << "[DatagramHeader] messageType:" << header.messageType;
    // qDebug() << "[DatagramHeader] protocolVersion:" << header.protocolVersion;
    // qDebug() << "[DatagramHeader] virtDiskType:" << header.virtDiskType;
    // qDebug() << "[DatagramHeader] operationName:" << header.operationName;

    // DatagramHeader *header2;
    // DatagramHeader::ReadFrom(&header2, datagram.data());
    // qDebug() << "[DatagramHeader 2] messageType:" << header2->messageType;
    // qDebug() << "[DatagramHeader 2] protocolVersion:" << header2->protocolVersion;
    // qDebug() << "[DatagramHeader 2] virtDiskType:" << header2->virtDiskType;
    // qDebug() << "[DatagramHeader 2] operationName:" << header2->operationName;

    // ReaddirResult *result2 = new ReaddirResult(datagram.sliced(sizeof(DatagramHeader)).data());
    // qDebug() << "[ReaddirResult 2] status:" << result2->status;
    // qDebug() << "[ReaddirResult 2] size:" << result2->dataSize;
    // qDebug() << "[ReaddirResult 2] count:" << result2->count;

    // for (unsigned int i = 0; i < (result2->dataSize / sizeof(FindData)); ++i)
    // {
    //     FindData *findData = (FindData *)result2->findData + i;
    //     qDebug() << "[ReaddirResult 2]" << i << "findData.name" << findData->name;
    // }

    // delete result2;

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

    connect(server, SIGNAL(newConnection()), this, SLOT(onConnection()));
    if (!server->listen())
    {
        qDebug() << "[Server] Unable to start: " << server->errorString();
    }
    else
    {
        qDebug() << "[Server] Started on port: " << server->serverPort();
    }

    broadcast();

    connect(broadcaster, SIGNAL(readyRead()), this, SLOT(onBroadcasting()));
    broadcaster->bind(45454, QUdpSocket::ShareAddress);
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
    machine["name"] = MACHINE_NAME;
    machine["port"] = server->serverPort();

    root["machine"] = machine;

    QByteArray datagram = QJsonDocument(root).toJson(QJsonDocument::Compact);
    quint64 sentSize = broadcaster.writeDatagram(datagram, QHostAddress::Broadcast, UDP_PORT);
    qDebug() << "[Broadcas] Sent size: " << sentSize;
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

        //establishConnection(newConn);
    }
}

void MainWindow::establishConnection(const Connection& conn)
{
    Connection newConn = std::move(conn);
    newConn.socket = new QTcpSocket(this);
    qDebug() << "[establishConnection] try to connect";
    newConn.socket->connectToHost(QHostAddress(newConn.machineAddress), newConn.machinePort);
    if (newConn.socket->waitForConnected())
    {
        connections.insert(newConn.machineId, newConn);
        qDebug() << "[establishConnection] socket connected";
        sendInitialInfo(newConn);
    }
    else
    {
        qDebug() << "[establishConnection] NOT connected: " << newConn.socket->errorString();
    }
}

void MainWindow::sendInitialInfo(const Connection& conn)
{
    virtDisk = new VirtDisk(conn);
    virtDisk->mount("M:\\");

    // QDir dir("D:\\Pictures");
    // QStringList entries = dir.entryList();
    // qDebug() << "[Client] Entries: " << entries.size();
    // QJsonObject root;
    // root["dirList"] = QJsonArray::fromStringList(entries);
    // QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Compact);
    // conn.socket->write(data);
}

void MainWindow::onConnection()
{
    qDebug() << "[Server] Connected";
    while (server->hasPendingConnections())
    {
        qDebug() << "[Server] Befor next pending connection";
        QTcpSocket *newConnection = server->nextPendingConnection();
        connect(newConnection, SIGNAL(readyRead()), this, SLOT(onSocketReadyRead()));
        //qDebug() << "[Server] Befor wait for ready read";
        //qDebug() << "[Server] Wait for ready read" << newConnection->waitForReadyRead(3000);
        // QByteArray buff = newConnection->readAll();
        // QJsonDocument doc = QJsonDocument::fromJson(buff);
        // QJsonArray dirList = doc["dirList"].toArray();
        // qDebug() << "[Server] Recieved: " << dirList.toVariantList();
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

    if (strcmp(header->virtDiskType, "fuse") == 0)
    {
        if (strcmp(header->operationName, "readdir") == 0)
        {
            const char *path = incoming.sliced(sizeof(DatagramHeader)).data();
            qDebug() << "[onSocketReadyRead] fuse readdir path:" << path;
            Ref<ReaddirResult> result = FUSEBackend::FD_readdir(path);
            qDebug() << "[onSocketReadyRead] result status:" << result->status;

            DatagramHeader header("response", "fuse", "readdir");
            header.datagramSize += sizeof(ReaddirResult) + result->dataSize;

            response.append((char *)&header, sizeof(DatagramHeader));
            response.append((char *)result.get(), sizeof(ReaddirResult));
            response.append((char *)result->findData, result->dataSize);
        }
        else if (strcmp(header->operationName, "read") == 0)
        {
            u64 size = *(incoming.sliced(sizeof(DatagramHeader)).data());
            i64 offset = *(incoming.sliced(sizeof(DatagramHeader) + sizeof(u64)).data());
            const char *path = incoming.sliced(sizeof(DatagramHeader)  + sizeof(u64) + sizeof(i64)).data();
            qDebug() << "[onSocketReadyRead] incoming size:" << size;
            qDebug() << "[onSocketReadyRead] incoming offset:" << offset;
            qDebug() << "[onSocketReadyRead] incoming path:" << path;
            Ref<ReadResult> result = FUSEBackend::FD_read(path, size, offset);
            qDebug() << "[onSocketReadyRead] result status:" << result->status;

            DatagramHeader header("response", "fuse", "read");
            header.datagramSize += sizeof(ReaddirResult) + result->size;

            response.append((char *)&header, sizeof(DatagramHeader));
            response.append((char *)result.get(), sizeof(ReadResult));
            response.append((char *)result->data, result->size);
        }
        else
        {
            qDebug() << "[onSocketReadyRead] Error: invalid operation name:" << header->operationName;
            return;
        }
    }
    else
    {
        qDebug() << "[onSocketReadyRead] Error: invalid virt disk type:" << header->virtDiskType;
        return;
    }

    // QStorageInfo storage = QStorageInfo::root();
    // storage.bytesTotal();
    // qDebug() << "[Server] incoming freeBytesAvailable: " << storage.bytesAvailable();
    // qDebug() << "[Server] incoming totalNumberOfBytes: " << storage.bytesTotal();
    // qDebug() << "[Server] incoming totalNumberOfFreeBytes: " << storage.bytesFree();

    newConnection->write(response);
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

    auto bg = palette().color(QPalette::Active, QPalette::Window);
    if (bg.lightness() == LIGHT_MODE)
    {
        // QIcon::setThemeName(LIGHT_THEME);
        trayIcon->setIcon(QIcon(":/assets/filedonkey_tray_icon_dark.ico"));
    }
    else
    {
        // QIcon::setThemeName(DARK_THEME);
        trayIcon->setIcon(QIcon(":/assets/filedonkey_tray_icon_light.ico"));
    }

    trayIcon->setToolTip("FileDonkey");
    trayIcon->setContextMenu(trayIconMenu);
    trayIcon->show();
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange)
    {
        auto bg = palette().color(QPalette::Active, QPalette::Window);
        qDebug() << "[MainWindow::changeEvent] lightness:" << bg.lightness();

        if (bg.lightness() == LIGHT_MODE)
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
}
