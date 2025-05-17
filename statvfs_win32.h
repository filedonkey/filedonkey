#ifndef STATVFS_WIN32_H
#define STATVFS_WIN32_H

#include <fuse/fuse_win.h>

int statvfs(const char* path, struct statvfs* buf);

#endif // STATVFS_WIN32_H
