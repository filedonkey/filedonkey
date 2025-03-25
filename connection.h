#ifndef CONNECTION_H
#define CONNECTION_H

#include <QTcpSocket>

struct Connection
{
    QString machineId;
    QString machineName;
    QString machineAddress;
    qint64 machinePort;
    QTcpSocket *socket = nullptr;
};

#endif // CONNECTION_H
