#ifndef DIR_WIN32_H
#define DIR_WIN32_H

#if defined(_WIN32)

#include <fuse/winfsp_fuse.h>

int mkdir(const char *path, fuse_mode_t mode);
int rmdir(const char *path);

#endif // _WIN32

#endif // DIR_WIN32_H
