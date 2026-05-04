#ifndef LSTAT_WIN32_H
#define LSTAT_WIN32_H

#if defined(_WIN32)

#include <fuse/fuse.h>

int lstat(const char* path, struct fuse_stat* buf);

#endif // _WIN32

#endif // LSTAT_WIN32_H
