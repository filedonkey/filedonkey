#ifndef STATVFS_WIN32_H
#define STATVFS_WIN32_H

#if defined(_WIN32)

#include <fuse/fuse_win.h>

int statvfs(const char* path, struct statvfs* buf);

#endif // _WIN32

#endif // STATVFS_WIN32_H
