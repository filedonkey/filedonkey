#ifndef VIRTDISK_H
#define VIRTDISK_H

#include <QString>

class VirtDisk
{
public:
    VirtDisk();
    ~VirtDisk();

    void mount(const QString &mountPoint);
};

#endif // VIRTDISK_H
