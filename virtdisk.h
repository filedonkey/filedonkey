#ifndef VIRTDISK_H
#define VIRTDISK_H

#include "connection.h"
#include "fuseclient.h"

#include <QString>

class VirtDisk
{
public:
    VirtDisk(const Connection& conn);
    ~VirtDisk();

    void mount(const QString &mountPoint);

    FUSEClient *client;
private:
    QString mountPoint;
    Connection conn;
};

#endif // VIRTDISK_H
