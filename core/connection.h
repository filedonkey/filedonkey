#ifndef CONNECTION_H
#define CONNECTION_H

#include "core.h"

#include <QString>

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

    // The one operation that is not a file system call. Two machines introduce themselves with it
    // over TCP, carrying in both directions exactly what a UDP announcement carries - see
    // LocalNode::machineDatagram() - so a network that will not pass a broadcast can still be told
    // by hand where the other machine is. Everything above it is answered under the shared root by
    // FUSEBackend; this one is answered by LocalNode itself, on the thread it runs on, because what
    // it does is take on a peer rather than read a file.
    //
    // Last in the list on purpose: the numbers go over the wire, so a build that predates this one
    // keeps the meaning of every value it already knew and simply refuses this as an operation it
    // has no handler for.
    hello    = 20,

    // The other operation that is not a file system call, and the only one that expects no answer.
    // A machine sends it on its client socket, just before closing it, to say that the socket is
    // going but the machine is not.
    //
    // Which is a thing the socket closing cannot say by itself, and the difference matters: both
    // machines mount each other, and each reads the other's client socket dropping as the other
    // going away - it is the one sign that arrives promptly, since our own mount's socket lives on
    // a thread with no event loop and notices nothing until a request times out. But that same
    // drop is also what a mount failing on the far side looks like, and treating it as a departure
    // took down a mount that was working perfectly, once for every failed attempt over there and
    // once more for every press of its Retry button.
    //
    // After hello, and last for the same reason: the numbers go over the wire, and a build that
    // predates this one refuses it as an operation it has no handler for, which leaves it behaving
    // exactly as it did before this existed.
    bye      = 21,
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
        case OperationType::hello:    return "hello";
        case OperationType::bye:      return "bye";
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
    i64 machinePort;

    // QSysInfo::productType() as the peer reported it - "windows", "macos", "android", or the
    // distribution's own name on Linux. Only the device list uses it, and only to badge the row,
    // so a peer built before the broadcast carried the field leaves it empty rather than breaking.
    QString machineOs;
};

#endif // CONNECTION_H
