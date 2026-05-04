#ifndef STATVFS_WIN32_H
#define STATVFS_WIN32_H

#if defined(_WIN32)

#include <fuse/winfsp_fuse.h>

int statvfs(const char* path, struct fuse_statvfs* buf);

#endif // _WIN32

#endif // STATVFS_WIN32_H
