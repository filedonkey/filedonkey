#ifndef CONNECTION_H
#define CONNECTION_H

#include "core.h"

#include <QTcpSocket>

enum class MessageType : u32
{
    Request  = 0,
    Response = 1,
};

enum class OperationType : u32
{
    readdir  = 0,
    read     = 1,
    write    = 2,
    readlink = 3,
    statfs   = 4,
    getattr  = 5,
    create   = 6,
    unlink   = 7,
    rename   = 8,
    mkdir    = 9,
    rmdir    = 10,
    truncate = 11,
};

inline const char *ToString(MessageType messageType)
{
    switch (messageType)
    {
        case MessageType::Request:  return "request";
        case MessageType::Response: return "response";
    }
    return "unknown";
}

inline const char *ToString(OperationType operationType)
{
    switch (operationType)
    {
        case OperationType::readdir:  return "readdir";
        case OperationType::read:     return "read";
        case OperationType::write:    return "write";
        case OperationType::readlink: return "readlink";
        case OperationType::statfs:   return "statfs";
        case OperationType::getattr:  return "getattr";
        case OperationType::create:   return "create";
        case OperationType::unlink:   return "unlink";
        case OperationType::rename:   return "rename";
        case OperationType::mkdir:    return "mkdir";
        case OperationType::rmdir:    return "rmdir";
        case OperationType::truncate: return "truncate";
    }
    return "unknown";
}

struct DatagramHeader
{
    u64 datagramSize;
    u64 requestId;
    u32 protocolVersion;
    MessageType messageType;
    OperationType operationType;

    DatagramHeader()
    {
        memset(this, 0, sizeof(DatagramHeader));
    }

    DatagramHeader(MessageType messageType,
                   OperationType operationType,
                   u64 requestId = 0,
                   u32 protocolVersion = 1)
    {
        memset(this, 0, sizeof(DatagramHeader));

        this->datagramSize    = sizeof(DatagramHeader);
        this->requestId       = requestId;
        this->protocolVersion = protocolVersion;
        this->messageType     = messageType;
        this->operationType   = operationType;
    }

    static void ReadFrom(DatagramHeader **header, const char *data)
    {
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
