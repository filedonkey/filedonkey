#include "mainwindow.h"
#include "ui_mainwindow.h"

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

    createTrayIcon();
    trayIcon->setIcon(QIcon(":/assets/donkey-dark-icon.ico"));
    trayIcon->setToolTip("FileDonkey");
    trayIcon->show();

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
            .machineId = machine["id"].toString(),
            .machineName = machine["name"].toString(),
            .machineAddress = senderAddress.toString(),
            .machinePort = machine["port"].toInteger(),
            .socket = nullptr,
        };

        if (connections.contains(newConn.machineId)) return;

        qDebug() << "[IncomingBroadcasting] machine id: " << newConn.machineId;
        qDebug() << "[IncomingBroadcasting] machine name: " << newConn.machineName;
        qDebug() << "[IncomingBroadcasting] machine port: " << newConn.machinePort;
        qDebug() << "[IncomingBroadcasting] sender address: " << newConn.machineAddress;
        qDebug() << "[IncomingBroadcasting] sender port: " << netDG.senderPort();

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

    QByteArray buff = newConnection->readAll();
    QJsonDocument request = QJsonDocument::fromJson(buff);
    QString operationName = request["operationName"].toString();

    qDebug() << "[Server] operationName: " << operationName;

    qDebug() << "[Server] incoming operationName: " << operationName;

    QStorageInfo storage = QStorageInfo::root();
    storage.bytesTotal();

    qDebug() << "[Server] incoming freeBytesAvailable: " << storage.bytesAvailable();
    qDebug() << "[Server] incoming totalNumberOfBytes: " << storage.bytesTotal();
    qDebug() << "[Server] incoming totalNumberOfFreeBytes: " << storage.bytesFree();

    QJsonObject response;
    response["operationName"] = operationName;
    response["freeBytesAvailable"] = storage.bytesAvailable();
    response["totalNumberOfBytes"] = storage.bytesTotal();
    response["totalNumberOfFreeBytes"] = storage.bytesFree();

    QByteArray data = QJsonDocument(response).toJson(QJsonDocument::Compact);
    newConnection->write(data);
}

void MainWindow::createTrayIcon()
{
    trayIconMenu = new QMenu(this);
    // trayIconMenu->addAction(minimizeAction);
    // trayIconMenu->addAction(maximizeAction);
    trayIconMenu->addAction(restoreAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);

    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setContextMenu(trayIconMenu);
}
