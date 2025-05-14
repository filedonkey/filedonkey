#ifndef FUSECLIENT_H
#define FUSECLIENT_H

#include "core.h"
#include "connection.h"
#include "fusebackend.h"

class FUSEClient
{
public:
    FUSEClient(Connection *conn) : conn(conn) {};

    Ref<ReaddirResult> FD_readdir(const char *path);

private:
    QByteArray Fetch(const char *operationName, const QByteArray &payload);

    Connection *conn;
};

#endif // FUSECLIENT_H
