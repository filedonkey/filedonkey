#include "tcpkeepalive.h"

#include "tcpkeepalive.h"

#ifdef Q_OS_WIN

#include <winsock2.h>
#include <mstcpip.h>

#elif defined(Q_OS_LINUX) || defined(Q_OS_MACOS)

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>

#endif


bool setTcpKeepAlive(QTcpSocket* socket, int idleSeconds, int intervalSeconds, int count)
{
    if (!socket)
        return false;

    if (socket->state() != QAbstractSocket::ConnectedState)
        return false;

    if (idleSeconds <= 0 ||
        intervalSeconds <= 0 ||
        count <= 0)
    {
        return false;
    }

    const qintptr descriptor = socket->socketDescriptor();

    if (descriptor == -1)
        return false;


#ifdef Q_OS_WIN

    SOCKET fd = static_cast<SOCKET>(descriptor);

    tcp_keepalive keepalive;
    keepalive.onoff = 1;
    keepalive.keepalivetime =
        static_cast<ULONG>(idleSeconds * 1000);
    keepalive.keepaliveinterval =
        static_cast<ULONG>(intervalSeconds * 1000);

    DWORD bytesReturned = 0;

    if (WSAIoctl(
            fd,
            SIO_KEEPALIVE_VALS,
            &keepalive,
            sizeof(keepalive),
            nullptr,
            0,
            &bytesReturned,
            nullptr,
            nullptr) == SOCKET_ERROR)
    {
        return false;
    }

    // Windows Vista+ uses a fixed number of keepalive probes
    // (currently 10). The count parameter cannot be configured
    // per socket through SIO_KEEPALIVE_VALS.

    Q_UNUSED(count);

    return true;


#elif defined(Q_OS_LINUX)

    int enable = 1;

    if (setsockopt(
            static_cast<int>(descriptor),
            SOL_SOCKET,
            SO_KEEPALIVE,
            &enable,
            sizeof(enable)) != 0)
    {
        return false;
    }

    if (setsockopt(
            static_cast<int>(descriptor),
            IPPROTO_TCP,
            TCP_KEEPIDLE,
            &idleSeconds,
            sizeof(idleSeconds)) != 0)
    {
        return false;
    }

    if (setsockopt(
            static_cast<int>(descriptor),
            IPPROTO_TCP,
            TCP_KEEPINTVL,
            &intervalSeconds,
            sizeof(intervalSeconds)) != 0)
    {
        return false;
    }

    if (setsockopt(
            static_cast<int>(descriptor),
            IPPROTO_TCP,
            TCP_KEEPCNT,
            &count,
            sizeof(count)) != 0)
    {
        return false;
    }

    return true;


#elif defined(Q_OS_MACOS)

    int enable = 1;

    if (setsockopt(
            static_cast<int>(descriptor),
            SOL_SOCKET,
            SO_KEEPALIVE,
            &enable,
            sizeof(enable)) != 0)
    {
        return false;
    }

    // macOS calls the idle-time option TCP_KEEPALIVE
    // rather than Linux's TCP_KEEPIDLE.

    if (setsockopt(
            static_cast<int>(descriptor),
            IPPROTO_TCP,
            TCP_KEEPALIVE,
            &idleSeconds,
            sizeof(idleSeconds)) != 0)
    {
        return false;
    }

    if (setsockopt(
            static_cast<int>(descriptor),
            IPPROTO_TCP,
            TCP_KEEPINTVL,
            &intervalSeconds,
            sizeof(intervalSeconds)) != 0)
    {
        return false;
    }

    if (setsockopt(
            static_cast<int>(descriptor),
            IPPROTO_TCP,
            TCP_KEEPCNT,
            &count,
            sizeof(count)) != 0)
    {
        return false;
    }

    return true;


#else

    Q_UNUSED(socket);
    Q_UNUSED(idleSeconds);
    Q_UNUSED(intervalSeconds);
    Q_UNUSED(count);

    return false;

#endif
}
