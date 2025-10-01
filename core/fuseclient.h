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

class FUSEClient : public QObject
{
    Q_OBJECT

public:
    FUSEClient(Connection *conn) : conn(conn), uploaded(0), downloaded(0) {};

    Ref<ReaddirResult>  FD_readdir(const char *path);
    Ref<ReadResult>     FD_read(const char *path, u64 size, i64 offset);
    i32                 FD_write(const char *path, const char *buf, u64 size, i64 offset);
    Ref<ReadlinkResult> FD_readlink(const char *path, u64 size);
    Ref<StatfsResult>   FD_statfs(const char *path);
    Ref<GetattrResult>  FD_getattr(const char *path);

signals:
    void uploadedChanged(u64 uploaded);
    void downloadedChanged(u64 downloaded);

private:
    FetchResult Fetch(const char *operationName, const QByteArray &payload);

    Connection *conn;
    u64 uploaded;
    u64 downloaded;
};

#endif // FUSECLIENT_H
