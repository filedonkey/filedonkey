#ifndef CONNECTION_H
#define CONNECTION_H

#include <QTcpSocket>

struct DatagramHeader
{
    char messageType[32];
    unsigned int protocolVersion;
    char virtDiskType[32];
    char operationName[32];
};

static void InitDatagram(DatagramHeader &header,
                         const char *messageType,
                         const char *virtDiskType,
                         const char *operationName,
                         unsigned int protocolVersion = 1)
{
    memset(&header, 0, sizeof(DatagramHeader));

    memcpy(header.messageType, messageType, strlen(messageType));
    memcpy(header.virtDiskType, virtDiskType, strlen(virtDiskType));
    memcpy(header.operationName, operationName, strlen(operationName));

    header.protocolVersion = protocolVersion;
}

static void ReadDatagramHeader(DatagramHeader **header, const char *data)
{
    // memcpy(&header, data, sizeof(DatagramHeader));
    *header = (DatagramHeader *)data;
}

struct Connection
{
    QString machineId;
    QString machineName;
    QString machineAddress;
    qint64 machinePort;
    QTcpSocket *socket = nullptr;
};

#endif // CONNECTION_H
