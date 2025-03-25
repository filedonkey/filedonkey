#ifdef __APPLE__

#include "virtdisk.h"

VirtDisk::VirtDisk(const Connection& conn) : conn(conn)
{
}

VirtDisk::~VirtDisk()
{
}

void VirtDisk::mount(const QString &mountPoint)
{
}

#endif
