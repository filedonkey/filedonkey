#ifndef CONNECTION_H
#define CONNECTION_H

#include "core.h"

#include <QTcpSocket>

struct DatagramHeader
{
    i64 datagramSize;
    char messageType[32];
    u32 protocolVersion;
    char virtDiskType[32];
    char operationName[32];

    DatagramHeader(const char *messageType,
                   const char *virtDiskType,
                   const char *operationName,
                   u32 protocolVersion = 1)
    {
        memset(this, 0, sizeof(DatagramHeader));

        memcpy(this->messageType, messageType, strlen(messageType));
        memcpy(this->virtDiskType, virtDiskType, strlen(virtDiskType));
        memcpy(this->operationName, operationName, strlen(operationName));

        this->protocolVersion = protocolVersion;
        this->datagramSize = sizeof(DatagramHeader);
    }

    static void ReadFrom(DatagramHeader **header, const char *data)
    {
        // memcpy(&header, data, sizeof(DatagramHeader));
        *header = (DatagramHeader *)data;
    }
};

struct Connection
{
    QString machineId;
    QString machineName;
    QString machineAddress;
    qint64 machinePort;
    QTcpSocket *socket = nullptr;
};

#endif // CONNECTION_H
