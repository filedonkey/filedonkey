#ifndef CONNECTION_H
#define CONNECTION_H

#include <QTcpSocket>

struct DatagramHeader
{
    long long datagramSize;
    char messageType[32];
    unsigned int protocolVersion;
    char virtDiskType[32];
    char operationName[32];

    DatagramHeader(const char *messageType,
                   const char *virtDiskType,
                   const char *operationName,
                   unsigned int protocolVersion = 1)
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
