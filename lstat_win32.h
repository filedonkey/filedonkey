#ifndef LSTAT_WIN32_H
#define LSTAT_WIN32_H

#if defined(__WIN32)

#include <fuse.h>

int lstat(const char* path, struct FUSE_STAT* buf);

#endif // __WIN32

#endif // LSTAT_WIN32_H
