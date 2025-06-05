#include "fuseclient.h"

Ref<ReaddirResult> FUSEClient::FD_readdir(const char *path)
{
    QByteArray payload((char *)path, strlen(path));
    FetchResult incoming = Fetch("readdir", payload);

    Ref<ReaddirResult> result = MakeRef<ReaddirResult>(incoming.payload.data());

    qDebug() << "[FUSEClient::FD_readdir] incoming result status:" << result->status;
    qDebug() << "[FUSEClient::FD_readdir] incoming result dataSize:" << result->dataSize;
    qDebug() << "[FUSEClient::FD_readdir] incoming result count:" << result->count;

    return result;
}

Ref<ReadResult> FUSEClient::FD_read(const char *path, u64 size, i64 offset)
{
    QByteArray payload;
    payload.append((char *)(&size), sizeof(size));
    payload.append((char *)(&offset), sizeof(offset));
    payload.append((char *)path, strlen(path));

    FetchResult incoming = Fetch("read", payload);

    Ref<ReadResult> result = MakeRef<ReadResult>(incoming.payload.data());

    qDebug() << "[FUSEClient::FD_read] incoming result status:" << result->status;
    qDebug() << "[FUSEClient::FD_read] incoming result size:" << result->size;

    return result;
}

Ref<ReadlinkResult> FUSEClient::FD_readlink(const char *path, u64 size)
{
    QByteArray payload((char *)&size, sizeof(u64));
    payload.append((char *)path, strlen(path));

    FetchResult incoming = Fetch("readlink", payload);

    Ref<ReadlinkResult> result = MakeRef<ReadlinkResult>(incoming.payload.data());

    qDebug() << "[FUSEClient::FD_readlink] incoming result status:" << result->status;
    qDebug() << "[FUSEClient::FD_readlink] incoming result size:" << result->size;

    return result;
}

Ref<StatfsResult> FUSEClient::FD_statfs(const char *path)
{
    QByteArray payload((char *)path, strlen(path));
    FetchResult incoming = Fetch("statfs", payload);

    Ref<StatfsResult> result = MakeRef<StatfsResult>(incoming.payload.data());

    qDebug() << "[FUSEClient::FD_statfs] incoming result status:" << result->status;
    qDebug() << "[FUSEClient::FD_statfs] incoming result f_bavail:" << result->f_bavail;
    qDebug() << "[FUSEClient::FD_statfs] incoming result f_bfree:" << result->f_bfree;
    qDebug() << "[FUSEClient::FD_statfs] incoming result f_bsize:" << result->f_bsize;

    return result;
}

Ref<GetattrResult> FUSEClient::FD_getattr(const char *path)
{
    QByteArray payload((char *)path, strlen(path));
    FetchResult incoming = Fetch("getattr", payload);

    Ref<GetattrResult> result = MakeRef<GetattrResult>(incoming.payload.data());

    qDebug() << "[FUSEClient::FD_getattr] incoming result status:" << result->status;
    qDebug() << "[FUSEClient::FD_getattr] incoming result st_size:" << result->st_size;
    qDebug() << "[FUSEClient::FD_getattr] incoming result st_blksize:" << result->st_blksize;
    qDebug() << "[FUSEClient::FD_getattr] incoming result st_blocks:" << result->st_blocks;

    return result;
}

FetchResult FUSEClient::Fetch(const char *operationName, const QByteArray &payload)
{
    qDebug() << "[FUSEClient::Fetch] machineId: " << conn->machineId;
    qDebug() << "[FUSEClient::Fetch] machineName: " << conn->machineName;

    QTcpSocket *socket = conn->socket;

    if (socket)
    {
        DatagramHeader header("request", "fuse", operationName);
        QByteArray request((char *)&header, sizeof(DatagramHeader));
        request.append(payload);

        socket->write(request);

        qDebug() << "[FUSEClient::Fetch] after write";

        socket->waitForReadyRead();

        qDebug() << "[FUSEClient::Fetch] socket bytesAvailable:" << socket->bytesAvailable();

        QByteArray incoming = socket->readAll();
        qsizetype datagramSize = *((qsizetype *)incoming.data());

        qDebug() << "[FUSEClient::Fetch] datagram size:" << datagramSize;

        incoming.reserve(datagramSize);
        int count = 0;
        while (incoming.size() < datagramSize)
        {
            socket->waitForReadyRead();
            incoming.append(socket->readAll());
            count++;
        }

        assert(incoming.size() == datagramSize);

        qDebug() << "[FUSEClient::Fetch] count:" << count;
        qDebug() << "[FUSEClient::Fetch] incoming size:" << incoming.size();

        DatagramHeader *inHeader;
        DatagramHeader::ReadFrom(&inHeader, incoming.data());

        qDebug() << "[FUSEClient::Fetch] incoming message type:" << inHeader->messageType;
        qDebug() << "[FUSEClient::Fetch] incoming protocol version:" << inHeader->protocolVersion;
        qDebug() << "[FUSEClient::Fetch] incoming virt disk type:" << inHeader->virtDiskType;
        qDebug() << "[FUSEClient::Fetch] incoming operation name:" << inHeader->operationName;

        FetchResult result = {
            .header = *inHeader,
            .payload = incoming.sliced(sizeof(DatagramHeader))
        };

        return result;
    }
    else
    {
        qDebug() << "[FUSEClient::Fetch] connection is invalid";
    }
}
