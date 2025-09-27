#ifndef LSTAT_WIN32_H
#define LSTAT_WIN32_H

#if defined(_WIN32)

#include <fuse.h>

int lstat(const char* path, struct FUSE_STAT* buf);

#endif // _WIN32

#endif // LSTAT_WIN32_H
