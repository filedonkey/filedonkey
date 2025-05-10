#include "fuseclient.h"

ReaddirResult *FUSEClient::FD_readdir(const char *path)
{
    QTcpSocket *socket = conn->socket;

    if (socket)
    {
        qDebug() << "FUSEClient::FD_readdir machineId: " << conn->machineId;

        DatagramHeader header;
        InitDatagram(header, "request", "fuse", "readdir");
        QByteArray request((char *)&header, sizeof(DatagramHeader));
        request.append((char *)path, strlen(path));

        socket->write(request);

        qDebug() << "FUSEClient::FD_readdir after write";

        socket->waitForReadyRead();

        qDebug() << "FUSEClient::FD_readdir socket bytesAvailable:" << socket->bytesAvailable();
        qDebug() << "FUSEClient::FD_readdir before readAll";

        QByteArray incoming = socket->readAll();

        DatagramHeader *inHeader;
        ReadDatagramHeader(&inHeader, incoming.data());

        qDebug() << "FUSEClient::FD_readdir total size:" << inHeader->datagramSize;

        //            incoming.reserve(inHeader->totalSize);
        int count = 0;
        while (incoming.size() < inHeader->datagramSize)
        {
            socket->waitForReadyRead();
            incoming.append(socket->readAll());
            count++;
        }

        assert(incoming.size() == inHeader->datagramSize);

        qDebug() << "FUSEClient::FD_readdir count:" << count;

        qDebug() << "FUSEClient::FD_readdir incoming size:" << incoming.size();

        qDebug() << "FUSEClient::FD_readdir incoming message type:" << inHeader->messageType;
        qDebug() << "FUSEClient::FD_readdir incoming protocol version:" << inHeader->protocolVersion;
        qDebug() << "FUSEClient::FD_readdir incoming virt disk type:" << inHeader->virtDiskType;
        qDebug() << "FUSEClient::FD_readdir incoming operation name:" << inHeader->operationName;

        ReaddirResult *result;
        ReadResult(&result, incoming.sliced(sizeof(DatagramHeader)).data());

        qDebug() << "FUSEClient::FD_readdir incoming result status:" << result->status;
        qDebug() << "FUSEClient::FD_readdir incoming result dataSize:" << result->dataSize;
        qDebug() << "FUSEClient::FD_readdir incoming result count:" << result->count;

        return result;
    }
    else
    {
        qDebug() << "FUSEClient::FD_readdir connection is invalid";
    }
}
