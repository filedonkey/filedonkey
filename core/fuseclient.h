#ifndef FUSECLIENT_H
#define FUSECLIENT_H

#include "core.h"
#include "connection.h"
#include "fusebackend_types.h"

#include <QDateTime>
#include <QTcpSocket>

struct FetchResult
{
    DatagramHeader header;
    QByteArray payload;
};

struct CacheValue
{
    QDateTime expirationDate;
    QByteArray response;
};

class FUSEClient : public QObject
{
    Q_OBJECT

public:
    FUSEClient(Connection *conn) : conn(conn), uploaded(0), downloaded(0), lastRequestId(0) {};

    // Set by VirtDisk once it has dialled the peer, and cleared before the socket is destroyed.
    // Only the fuse thread touches it: it is set there before fuse_loop starts and cleared there
    // after fuse_loop returns, so no request can be in flight either side of a change.
    void setSocket(QTcpSocket *socket) { this->socket = socket; }

    Ref<ReaddirResult>  FD_readdir(const char *path);
    Ref<ReadResult>     FD_read(const char *path, u64 size, i64 offset);
    Ref<StatusResult>   FD_write(const char *path, const char *buf, u64 size, i64 offset);
    Ref<ReadlinkResult> FD_readlink(const char *path, u64 size);
    Ref<StatfsResult>   FD_statfs(const char *path);
    Ref<GetattrResult>  FD_getattr(const char *path);
    Ref<StatusResult>   FD_create(const char *path, u32 mode, i32 flags);
    Ref<StatusResult>   FD_unlink(const char *path);
    Ref<StatusResult>   FD_rename(const char *from, const char *to);
    Ref<StatusResult>   FD_mkdir(const char *path, u32 mode);
    Ref<StatusResult>   FD_rmdir(const char *path);
    Ref<StatusResult>   FD_truncate(const char *path, i64 size);

signals:
    void uploadedChanged(u64 uploaded);
    void downloadedChanged(u64 downloaded);

private:
    FetchResult Fetch(OperationType operationType, const QByteArray &payload);
    FetchResult errorResponse(OperationType operationType, u64 requestId);

    Connection *conn;
    QTcpSocket *socket = nullptr;
    u64 uploaded;
    u64 downloaded;
    u64 lastRequestId;
    QHash<QString, CacheValue> netCache;
};

#endif // FUSECLIENT_H
