#ifndef FUSECLIENT_H
#define FUSECLIENT_H

#include "core.h"
#include "connection.h"
#include "fusebackend_types.h"

struct FetchResult
{
    DatagramHeader header;
    QByteArray payload;
};

class FUSEClient
{
public:
    FUSEClient(Connection *conn) : conn(conn) {};

    Ref<ReaddirResult> FD_readdir(const char *path);
    Ref<ReadResult>    FD_read(const char *path, u64 size, i64 offset);
    Ref<StatfsResult>  FD_statfs(const char *path);
    Ref<GetattrResult> FD_getattr(const char *path);

private:
    FetchResult Fetch(const char *operationName, const QByteArray &payload);

    Connection *conn;
};

#endif // FUSECLIENT_H
