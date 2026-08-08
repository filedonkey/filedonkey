#include "singleinstance.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QRegularExpression>

#define HANDSHAKE_TIMEOUT_MS 300

namespace {

// Per user, not per machine: two people logged into the same box each get their own FileDonkey,
// each serving their own home directory. The name becomes a named pipe on Windows and a socket
// file under /tmp elsewhere, so anything exotic in a username is stripped rather than trusted.
QString instanceName()
{
    QString user = qEnvironmentVariable("USERNAME");
    if (user.isEmpty()) user = qEnvironmentVariable("USER");
    if (user.isEmpty()) user = "default";

    user.replace(QRegularExpression("[^A-Za-z0-9_-]"), "_");

    return "filedonkey-" + user;
}

} // namespace

SingleInstance::SingleInstance(QObject *parent)
    : QObject(parent)
{
}

bool SingleInstance::claim()
{
    const QString name = instanceName();

    // Ask before taking. Anything that answers is a FileDonkey already up, and the only thing
    // worth doing with this process is having it tap the first on the shoulder.
    QLocalSocket probe;
    probe.connectToServer(name);

    if (probe.waitForConnected(HANDSHAKE_TIMEOUT_MS))
    {
        // The byte carries no meaning - the connection is the whole message. It is written so the
        // server side is certain to see the connection before this process goes away.
        probe.write("\x01", 1);
        probe.flush();
        probe.waitForBytesWritten(HANDSHAKE_TIMEOUT_MS);
        probe.disconnectFromServer();

        qDebug() << "[SingleInstance] already running, asked it to show itself";
        return false;
    }

    // Nothing answered. On unix the socket file outlives a process that was killed rather than
    // closed, and listen() fails on the leftover; removing it is safe precisely because the
    // connect above just proved nobody is serving it.
    QLocalServer::removeServer(name);

    server = new QLocalServer(this);
    server->setSocketOptions(QLocalServer::UserAccessOption);

    if (!server->listen(name))
    {
        // Losing the guard is worse than not having it, but not as bad as refusing to start: no
        // second instance can reach us now, which is where we were before any of this existed.
        qDebug() << "[SingleInstance] could not listen on" << name << ":" << server->errorString();
        return true;
    }

    connect(server, &QLocalServer::newConnection, this, [this]() {
        while (QLocalSocket *client = server->nextPendingConnection())
        {
            connect(client, &QLocalSocket::disconnected, client, &QLocalSocket::deleteLater);
            emit showRequested();
        }
    });

    return true;
}
