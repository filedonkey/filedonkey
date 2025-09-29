#include "fuseclient.h"

#include <QDateTime>

struct CacheValue
{
    QDateTime expirationDate;
    QByteArray response;
};

static QHash<QString, CacheValue> netCache;

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
//    qDebug() << "[FUSEClient::FD_read] incoming result data:" << result->data;

    return result;
}

i32 FUSEClient::FD_write(const char *path, const char *buf, u64 size, i64 offset)
{
    QByteArray payload;
    payload.append((char *)(&size), sizeof(size));
    payload.append((char *)(&offset), sizeof(offset));
    payload.append((char *)buf, size);
    payload.append((char *)path, strlen(path));

    FetchResult incoming = Fetch("write", payload);

    i32 result = *incoming.payload.data();

    qDebug() << "[FUSEClient::FD_write] incoming result:" << result;

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
    //------------------------------------------------------------------------------------
    // Caching
    //------------------------------------------------------------------------------------
    QString cacheKey = QString("%1%2%3").arg(conn->machineId).arg(operationName).arg(payload);

    if (netCache.contains(cacheKey))
    {
        CacheValue value = netCache.value(cacheKey);
        if (value.expirationDate > QDateTime::currentDateTimeUtc())
        {
            QByteArray incoming = value.response;

            DatagramHeader *inHeader;
            DatagramHeader::ReadFrom(&inHeader, incoming.data());

            // qDebug() << "[FUSEClient::Fetch] cached message type:" << inHeader->messageType;
            // qDebug() << "[FUSEClient::Fetch] cached protocol version:" << inHeader->protocolVersion;
            // qDebug() << "[FUSEClient::Fetch] cached virt disk type:" << inHeader->virtDiskType;
            // qDebug() << "[FUSEClient::Fetch] cached operation name:" << inHeader->operationName;

            FetchResult result = {
                .header = *inHeader,
                .payload = incoming.sliced(sizeof(DatagramHeader))
            };

            return result;
        }
        else
        {
            netCache.remove(cacheKey);
        }
    }
    //------------------------------------------------------------------------------------

    qDebug() << "[FUSEClient::Fetch] machineId: " << conn->machineId;
    qDebug() << "[FUSEClient::Fetch] machineName: " << conn->machineName;

    QTcpSocket *socket = conn->socket;

    if (socket)
    {
        DatagramHeader header("request", "fuse", operationName);
        QByteArray request((char *)&header, sizeof(DatagramHeader));
        request.append(payload);

        socket->write(request);

        uploaded += request.size();
        emit uploadedChanged(uploaded);

        qDebug() << "[FUSEClient::Fetch] after write";

        socket->waitForReadyRead();

        qDebug() << "[FUSEClient::Fetch] socket bytesAvailable:" << socket->bytesAvailable();

        QByteArray incoming = socket->readAll();
        downloaded += incoming.size();
        emit downloadedChanged(downloaded);
        qsizetype datagramSize = *((qsizetype *)incoming.data());

        QLocale locale(QLocale::English, QLocale::UnitedStates);
        qDebug() << "[FUSEClient::Fetch] datagram size:"
                 << locale.formattedDataSize(datagramSize).toStdString().c_str();

        incoming.reserve(datagramSize);
        int count = 0;
        while (incoming.size() < datagramSize)
        {
            socket->waitForReadyRead();
            QByteArray data = socket->readAll();
            downloaded += data.size();
            emit downloadedChanged(downloaded);
            incoming.append(data);
            count++;
        }

        assert(incoming.size() == datagramSize);

        qDebug() << QString("[FUSEClient::Fetch] uploaded: %1    downloaded: %2")
                        .arg(locale.formattedDataSize(uploaded))
                        .arg(locale.formattedDataSize(downloaded))
                        .toStdString()
                        .c_str();

        //--------------------------------------------------------------------------------
        // Caching
        //--------------------------------------------------------------------------------
        CacheValue value = {
            .expirationDate = QDateTime::currentDateTimeUtc().addSecs(15),
            .response = incoming
        };
        netCache.insert(cacheKey, value);
        //--------------------------------------------------------------------------------

        qDebug() << "[FUSEClient::Fetch] count:" << count;
        qDebug() << "[FUSEClient::Fetch] incoming size:"
                 << locale.formattedDataSize(incoming.size()).toStdString().c_str();

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
