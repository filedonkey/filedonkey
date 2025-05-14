#include "fuseclient.h"

Ref<ReaddirResult> FUSEClient::FD_readdir(const char *path)
{
        QByteArray payload((char *)path, strlen(path));
        QByteArray incoming = Fetch("readdir", payload);

        DatagramHeader *header;
        DatagramHeader::ReadFrom(&header, incoming.data());

        qDebug() << "[FUSEClient::FD_readdir] incoming message type:" << header->messageType;
        qDebug() << "[FUSEClient::FD_readdir] incoming protocol version:" << header->protocolVersion;
        qDebug() << "[FUSEClient::FD_readdir] incoming virt disk type:" << header->virtDiskType;
        qDebug() << "[FUSEClient::FD_readdir] incoming operation name:" << header->operationName;

        Ref<ReaddirResult> result = MakeRef<ReaddirResult>(incoming.sliced(sizeof(DatagramHeader)).data());

        qDebug() << "[FUSEClient::FD_readdir] incoming result status:" << result->status;
        qDebug() << "[FUSEClient::FD_readdir] incoming result dataSize:" << result->dataSize;
        qDebug() << "[FUSEClient::FD_readdir] incoming result count:" << result->count;

        return result;
}

Ref<ReadResult> FUSEClient::FD_read(cstr path, u64 size, i64 offset)
{
    QByteArray payload((char *)&size, sizeof(u64));
    payload.append((char *)&offset, sizeof(i64));
    payload.append((char *)path, strlen(path));

    QByteArray incoming = Fetch("read", payload);

    DatagramHeader *header;
    DatagramHeader::ReadFrom(&header, incoming.data());

    qDebug() << "[FUSEClient::FD_read] incoming message type:" << header->messageType;
    qDebug() << "[FUSEClient::FD_read] incoming protocol version:" << header->protocolVersion;
    qDebug() << "[FUSEClient::FD_read] incoming virt disk type:" << header->virtDiskType;
    qDebug() << "[FUSEClient::FD_read] incoming operation name:" << header->operationName;

    Ref<ReadResult> result = MakeRef<ReadResult>(incoming.sliced(sizeof(DatagramHeader)).data());

    qDebug() << "[FUSEClient::FD_read] incoming result status:" << result->status;
    qDebug() << "[FUSEClient::FD_read] incoming result size:" << result->size;

    return result;
}

QByteArray FUSEClient::Fetch(const char *operationName, const QByteArray &payload)
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

        return incoming;
    }
    else
    {
        qDebug() << "[FUSEClient::Fetch] connection is invalid";
    }
}
