#ifdef __APPLE__

// Example source:
// https://github.com/macfuse/demo/blob/master/LoopbackFS-C/loopback/loopback.c

// Docan FUSE operations mapping
// https://github.com/dokan-dev/dokany/wiki/FUSE

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
