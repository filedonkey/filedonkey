#ifndef VIRTDISK_H
#define VIRTDISK_H

#include "connection.h"

#include <QString>

class VirtDisk
{
public:
    VirtDisk(const Connection& conn);
    ~VirtDisk();

    void mount(const QString &mountPoint);

private:
    QString mountPoint;
    Connection conn;
};

#endif // VIRTDISK_H
