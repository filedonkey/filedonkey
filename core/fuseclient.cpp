#include "fuseclient.h"

#include <algorithm>
#include <errno.h>

#include <QList>

#define SOCKET_WAIT_TIMEOUT  5000

static u64 savedBytes = 0;

Ref<ReaddirResult> FUSEClient::FD_readdir(const char *path)
{
    QByteArray payload((char *)path, strlen(path));
    FetchResult incoming = Fetch(OperationType::readdir, payload);

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

    FetchResult incoming = Fetch(OperationType::read, payload);

    Ref<ReadResult> result = MakeRef<ReadResult>(incoming.payload.data());

    qDebug() << "[FUSEClient::FD_read] incoming result status:" << result->status;
    qDebug() << "[FUSEClient::FD_read] incoming result size:" << result->size;

    return result;
}

Ref<StatusResult> FUSEClient::FD_write(const char *path, const char *buf, u64 size, i64 offset)
{
    const char *nullTerminal = "\0";
    size_t pathLength = strlen(path) + 1;
    QByteArray payload;
    payload.append((char *)(&size), sizeof(size));
    payload.append((char *)(&offset), sizeof(offset));
    payload.append((char *)(&pathLength), sizeof(pathLength));
    payload.append((char *)path, strlen(path));
    payload.append((char *)nullTerminal, 1);
    payload.append((char *)buf, size);

    qDebug() << "[FUSEClient::FD_write] size:" << size;
    qDebug() << "[FUSEClient::FD_write] offset:" << offset;
    qDebug() << "[FUSEClient::FD_write] path length:" << pathLength;
    qDebug() << "[FUSEClient::FD_write] path:" << path;
    qDebug() << "[FUSEClient::FD_write] payload length:" << payload.length();

    FetchResult incoming = Fetch(OperationType::write, payload);

    Ref<StatusResult> result = MakeRef<StatusResult>(incoming.payload.data());

    qDebug() << "[FUSEClient::FD_write] incoming result status:" << result->status;

    return result;
}

Ref<ReadlinkResult> FUSEClient::FD_readlink(const char *path, u64 size)
{
    QByteArray payload((char *)&size, sizeof(u64));
    payload.append((char *)path, strlen(path));

    FetchResult incoming = Fetch(OperationType::readlink, payload);

    Ref<ReadlinkResult> result = MakeRef<ReadlinkResult>(incoming.payload.data());

    qDebug() << "[FUSEClient::FD_readlink] incoming result status:" << result->status;
    qDebug() << "[FUSEClient::FD_readlink] incoming result size:" << result->size;

    return result;
}

Ref<StatfsResult> FUSEClient::FD_statfs(const char *path)
{
    QByteArray payload((char *)path, strlen(path));
    FetchResult incoming = Fetch(OperationType::statfs, payload);

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
    FetchResult incoming = Fetch(OperationType::getattr, payload);

    Ref<GetattrResult> result = MakeRef<GetattrResult>(incoming.payload.data());

    qDebug() << "[FUSEClient::FD_getattr] incoming result status:" << result->status;
    qDebug() << "[FUSEClient::FD_getattr] incoming result st_size:" << result->st_size;
    qDebug() << "[FUSEClient::FD_getattr] incoming result st_blksize:" << result->st_blksize;
    qDebug() << "[FUSEClient::FD_getattr] incoming result st_blocks:" << result->st_blocks;

    return result;
}

Ref<StatusResult> FUSEClient::FD_create(const char *path, u32 mode, i32 flags)
{
    QByteArray payload;
    payload.append((char *)(&mode), sizeof(mode));
    payload.append((char *)(&flags), sizeof(flags));
    payload.append((char *)path, strlen(path));

    FetchResult incoming = Fetch(OperationType::create, payload);

    Ref<StatusResult> result = MakeRef<StatusResult>(incoming.payload.data());

    qDebug() << "[FUSEClient::FD_create] incoming result status:" << result->status;

    netCache.clear();

    return result;
}

Ref<StatusResult> FUSEClient::FD_unlink(const char *path)
{
    QByteArray payload((char *)path, strlen(path));
    FetchResult incoming = Fetch(OperationType::unlink, payload);

    Ref<StatusResult> result = MakeRef<StatusResult>(incoming.payload.data());

    qDebug() << "[FUSEClient::FD_unlink] incoming result status:" << result->status;

    netCache.clear();

    return result;
}

Ref<StatusResult> FUSEClient::FD_rename(const char *from, const char *to)
{
    QByteArray payload;
    payload.append((char *)from, strlen(from) + 1);
    payload.append((char *)to, strlen(to) + 1);
    FetchResult incoming = Fetch(OperationType::rename, payload);

    Ref<StatusResult> result = MakeRef<StatusResult>(incoming.payload.data());

    qDebug() << "[FUSEClient::FD_rename] incoming result status:" << result->status;

    netCache.clear();

    return result;
}

Ref<StatusResult> FUSEClient::FD_mkdir(const char *path, u32 mode)
{
    QByteArray payload;
    payload.append((char *)(&mode), sizeof(mode));
    payload.append((char *)path, strlen(path));

    FetchResult incoming = Fetch(OperationType::mkdir, payload);

    Ref<StatusResult> result = MakeRef<StatusResult>(incoming.payload.data());

    qDebug() << "[FUSEClient::FD_mkdir] incoming result status:" << result->status;

    netCache.clear();

    return result;
}

Ref<StatusResult> FUSEClient::FD_rmdir(const char *path)
{
    QByteArray payload((char *)path, strlen(path));
    FetchResult incoming = Fetch(OperationType::rmdir, payload);

    Ref<StatusResult> result = MakeRef<StatusResult>(incoming.payload.data());

    qDebug() << "[FUSEClient::FD_rmdir] incoming result status:" << result->status;

    netCache.clear();

    return result;
}

Ref<StatusResult> FUSEClient::FD_truncate(const char *path, i64 size)
{
    QByteArray payload;
    payload.append((char *)(&size), sizeof(size));
    payload.append((char *)path, strlen(path));

    FetchResult incoming = Fetch(OperationType::truncate, payload);

    Ref<StatusResult> result = MakeRef<StatusResult>(incoming.payload.data());

    qDebug() << "[FUSEClient::FD_truncate] incoming result status:" << result->status;

    netCache.clear();

    return result;
}

FetchResult FUSEClient::Fetch(OperationType operationType, const QByteArray &payload)
{
    //------------------------------------------------------------------------------------
    // Caching
    //------------------------------------------------------------------------------------
    static QList<OperationType> operationsAllowedToCache = {OperationType::getattr,
                                                            OperationType::statfs,
                                                            OperationType::readdir};
    bool shouldBeCached = operationsAllowedToCache.contains(operationType);

    if (shouldBeCached)
    {
        QString cacheKey = QString("%1%2%3").arg(conn->machineId).arg(ToString(operationType)).arg(payload);

        if (netCache.contains(cacheKey))
        {
            CacheValue value = netCache.value(cacheKey);
            if (value.expirationDate > QDateTime::currentDateTimeUtc())
            {
                QByteArray incoming = value.response;

                DatagramHeader *inHeader;
                DatagramHeader::ReadFrom(&inHeader, incoming.data());


                FetchResult result = {
                    .header = *inHeader,
                    .payload = incoming.sliced(sizeof(DatagramHeader))
                };

                savedBytes += incoming.size();

                QLocale locale(QLocale::English, QLocale::UnitedStates);
                qDebug() << "[FUSEClient::Fetch] cache bytes saved:" << locale.formattedDataSize(savedBytes).toStdString().c_str();

                return result;
            }
            else
            {
                netCache.remove(cacheKey);
            }
        }
    }
    //------------------------------------------------------------------------------------

    qDebug() << "[FUSEClient::Fetch] machineId: " << conn->machineId;
    qDebug() << "[FUSEClient::Fetch] machineName: " << conn->machineName;

    QTcpSocket *socket = conn->socket;

    if (socket)
    {
        u64 requestId = ++lastRequestId;

        DatagramHeader header(MessageType::Request, operationType, requestId);
        header.datagramSize += payload.size();
        QByteArray request((char *)&header, sizeof(DatagramHeader));
        request.append(payload);

        socket->write(request);
        socket->flush();

        uploaded += request.size();
        emit uploadedChanged(uploaded);

        qDebug() << "[FUSEClient::Fetch] after write";

        if (!socket->waitForReadyRead(SOCKET_WAIT_TIMEOUT))
        {
            qDebug() << "[FUSEClient::Fetch] Error: socket read waiting timeout";
            return errorResponse(operationType, requestId);
        }

        qDebug() << "[FUSEClient::Fetch] socket bytesAvailable:" << socket->bytesAvailable();

        QByteArray incoming = socket->readAll();
        downloaded += incoming.size();
        emit downloadedChanged(downloaded);

        if ((u64)incoming.size() < sizeof(DatagramHeader))
        {
            qDebug() << "[FUSEClient::Fetch] Error: incoming datagram size is not valid";
            return errorResponse(operationType, requestId);
        }

        u64 datagramSize = *((u64 *)incoming.data());

        QLocale locale(QLocale::English, QLocale::UnitedStates);
        qDebug() << "[FUSEClient::Fetch] datagram size:"
                 << locale.formattedDataSize(datagramSize).toStdString().c_str();

        incoming.reserve(datagramSize);
        int count = 0;
        while ((u64)incoming.size() < datagramSize)
        {
            if (!socket->waitForReadyRead(SOCKET_WAIT_TIMEOUT))
            {
                qDebug() << "[FUSEClient::Fetch] Error: socket read waiting timeout";
                return errorResponse(operationType, requestId);
            }
            QByteArray data = socket->readAll();
            downloaded += data.size();
            emit downloadedChanged(downloaded);
            incoming.append(data);
            count++;
        }

        assert((u64)incoming.size() == datagramSize);

        qDebug() << QString("[FUSEClient::Fetch] uploaded: %1    downloaded: %2")
                        .arg(locale.formattedDataSize(uploaded))
                        .arg(locale.formattedDataSize(downloaded))
                        .toStdString()
                        .c_str();

        //--------------------------------------------------------------------------------
        // Caching
        //--------------------------------------------------------------------------------
        if (shouldBeCached)
        {
            QString key = QString("%1%2%3").arg(conn->machineId).arg(ToString(operationType)).arg(payload);
            CacheValue value = {
                .expirationDate = QDateTime::currentDateTimeUtc().addSecs(15),
                .response = incoming
            };
            netCache.insert(key, value);
        }
        //--------------------------------------------------------------------------------

        qDebug() << "[FUSEClient::Fetch] count:" << count;
        qDebug() << "[FUSEClient::Fetch] incoming size:"
                 << locale.formattedDataSize(incoming.size()).toStdString().c_str();

        DatagramHeader *inHeader;
        DatagramHeader::ReadFrom(&inHeader, incoming.data());

        qDebug() << "[FUSEClient::Fetch] incoming message type:" << ToString(inHeader->messageType);
        qDebug() << "[FUSEClient::Fetch] incoming protocol version:" << inHeader->protocolVersion;
        qDebug() << "[FUSEClient::Fetch] incoming operation type:" << ToString(inHeader->operationType);
        qDebug() << "[FUSEClient::Fetch] incoming request id:" << inHeader->requestId;

        assert(inHeader->messageType == MessageType::Response);
        assert(inHeader->operationType == operationType);
        assert(inHeader->requestId == requestId);

        FetchResult result = {
            .header = *inHeader,
            .payload = incoming.sliced(sizeof(DatagramHeader))
        };

        return result;
    }
    else
    {
        qDebug() << "[FUSEClient::Fetch] connection is invalid";
        return errorResponse(operationType, 0);
    }
}

FetchResult FUSEClient::errorResponse(OperationType operationType, u64 requestId)
{
    constexpr qsizetype MaxResultSize = (qsizetype)std::max({sizeof(StatusResult),
                                                             sizeof(ReaddirResult),
                                                             sizeof(ReadResult),
                                                             sizeof(ReadlinkResult),
                                                             sizeof(StatfsResult),
                                                             sizeof(GetattrResult)});

    QByteArray payload(MaxResultSize, '\0');

    StatusResult fuseResult;
    fuseResult.status = -EIO;
    memcpy(payload.data(), &fuseResult, sizeof(StatusResult));

    DatagramHeader header(MessageType::Response, operationType, requestId);
    header.datagramSize += payload.size();

    return { .header = header, .payload = payload };
}
