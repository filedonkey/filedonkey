#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QStringList>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDatagram>
#include <QSysInfo>

#define MACHINE_NAME    "Leg3nd's Desktop"

#define TCP_PORT    5500
#define UDP_PORT    45454

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , server(new QTcpServer(this))
    , broadcaster(new QUdpSocket(this))
    , socket(new QTcpSocket(this))
{
    ui->setupUi(this);

    connect(server, SIGNAL(pendingConnectionAvailable()), this, SLOT(onConnection()));
    server->listen(QHostAddress::LocalHost, TCP_PORT);

    broadcast();

    connect(broadcaster, SIGNAL(readyRead()), this, SLOT(onBroadcasting()));
    broadcaster->bind(45454, QUdpSocket::ShareAddress);


    // qDebug() << "[Client] Befor socket connect";
    // socket->connectToHost(QHostAddress::LocalHost, TCP_PORT);
    // qDebug() << "[Client] After socket connect";
    // if (socket->waitForConnected())
    // {
    //     qDebug() << "[Client] Connected";
    //     QDir dir("D:\\Pictures");
    //     QStringList entries = dir.entryList();
    //     qDebug() << "[Client] Entries: " << entries.size();
    //     QJsonObject o;
    //     o["dirList"] = QJsonArray::fromStringList(entries);
    //     QByteArray d = QJsonDocument(o).toJson(QJsonDocument::Compact);
    //     socket->write(d);
    // }
}

MainWindow::~MainWindow()
{
    delete ui;
    delete server;
    delete socket;
}

void MainWindow::broadcast()
{
    QUdpSocket broadcaster;
    QJsonObject root;
    QJsonObject machine;

    machine["id"] = QSysInfo::machineUniqueId().constData();
    machine["name"] = MACHINE_NAME;
    machine["port"] = TCP_PORT;

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

        qDebug() << "[IncomingBroadcasting] machine id: " << machine["id"].toString();
        qDebug() << "[IncomingBroadcasting] machine name: " << machine["name"].toString();
        qDebug() << "[IncomingBroadcasting] machine port: " << machine["port"].toString();
    }
}

void MainWindow::onConnection()
{
    qDebug() << "[Server] Connected";
    while (server->hasPendingConnections())
    {
        qDebug() << "[Server] Befor next pending connection";
        QTcpSocket *newConnection = server->nextPendingConnection();
        qDebug() << "[Server] Befor wait for ready read";
        qDebug() << "[Server] Wait for ready read" << newConnection->waitForReadyRead(3000);

        QByteArray buff = newConnection->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(buff);
        QJsonArray dirList = doc["dirList"].toArray();
        qDebug() << "[Server] Recieved: " << dirList.toVariantList();
    }
}
