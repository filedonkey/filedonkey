#ifndef TCPKEEPALIVE_H
#define TCPKEEPALIVE_H

#include <QTcpSocket>

bool setTcpKeepAlive(QTcpSocket* socket, int idleSeconds, int intervalSeconds, int count);

#endif // TCPKEEPALIVE_H
