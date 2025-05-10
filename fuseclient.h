#ifndef FUSECLIENT_H
#define FUSECLIENT_H

#include "connection.h"
#include "fusebackend.h"

class FUSEClient
{
public:
    FUSEClient(Connection *conn) : conn(conn) {};

    ReaddirResult *FD_readdir(const char *path);

private:
    Connection *conn;
};

#endif // FUSECLIENT_H
